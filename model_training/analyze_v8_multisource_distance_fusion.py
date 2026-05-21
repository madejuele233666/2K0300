import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path
from typing import Any

import numpy as np


EventKey = tuple[str, int, str, int]
NormalKey = tuple[int, str]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def parse_feature(row: dict[str, str]) -> np.ndarray:
    if row.get("feature_json"):
        return np.asarray(json.loads(row["feature_json"]), dtype=np.int8)
    dim = int(float(row.get("feature_dim", 4)))
    return np.asarray([int(float(row.get(f"feature{index}", 0))) for index in range(dim)], dtype=np.int8)


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def read_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    rows: list[np.ndarray] = []
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        cls = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes):
                cls[:, parent] = np.min(dist[:, indexes], axis=1)
        rows.append(cls)
    return np.concatenate(rows).astype(np.float64)


def margin_matrix(class_dist: np.ndarray) -> np.ndarray:
    margins = np.zeros_like(class_dist, dtype=np.float64)
    for parent in range(3):
        other = [item for item in range(3) if item != parent]
        margins[:, parent] = np.min(class_dist[:, other], axis=1) - class_dist[:, parent]
    return margins


def rank_matrix(class_dist: np.ndarray) -> np.ndarray:
    order = np.argsort(class_dist, axis=1)
    rank = np.zeros_like(class_dist, dtype=np.float64)
    for row_index in range(len(class_dist)):
        for rank_value, parent in enumerate(order[row_index].tolist()):
            rank[row_index, int(parent)] = float(rank_value)
    return rank


def source_scale_stats(class_dist: np.ndarray, parent: np.ndarray) -> dict[str, float]:
    pred = np.argmin(class_dist, axis=1)
    margins = margin_matrix(class_dist)
    true_margin = margins[np.arange(len(parent)), parent.astype(np.int64)]
    true_dist = class_dist[np.arange(len(parent)), parent.astype(np.int64)]
    correct_margin = true_margin[pred == parent]
    return {
        "margin_p50": float(np.percentile(np.maximum(correct_margin, 1.0), 50)) if len(correct_margin) else 1.0,
        "margin_p90": float(np.percentile(np.maximum(correct_margin, 1.0), 90)) if len(correct_margin) else 1.0,
        "true_dist_p50": float(np.percentile(np.maximum(true_dist, 1.0), 50)),
        "true_dist_p90": float(np.percentile(np.maximum(true_dist, 1.0), 90)),
        "normal_acc": float(np.mean(pred == parent)),
    }


def summarize_predictions(
    *,
    pred: np.ndarray,
    parent: np.ndarray,
    groups: np.ndarray,
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for pred_value, parent_value, group in zip(pred.tolist(), parent.tolist(), groups.tolist(), strict=False):
        wrong = int(int(pred_value) != int(parent_value))
        grouped[str(group)][0] += wrong
        grouped[str(group)][1] += 1
    wrong_events = int(np.sum(pred.astype(np.int64) != parent.astype(np.int64)))
    return {
        "wrong_events": wrong_events,
        "total_events": int(len(parent)),
        "wrong_rate": float(wrong_events / max(len(parent), 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
    }


def choose_from_scores(scores: np.ndarray, mode: str) -> tuple[np.ndarray, np.ndarray]:
    if mode == "min":
        order = np.argsort(scores, axis=1)
        pred = order[:, 0].astype(np.int64)
        margin = (scores[np.arange(len(scores)), order[:, 1]] - scores[np.arange(len(scores)), order[:, 0]]).astype(np.float64)
        return pred, margin
    if mode == "max":
        order = np.argsort(-scores, axis=1)
        pred = order[:, 0].astype(np.int64)
        margin = (scores[np.arange(len(scores)), order[:, 0]] - scores[np.arange(len(scores)), order[:, 1]]).astype(np.float64)
        return pred, margin
    raise ValueError(f"unknown choose mode: {mode}")


def softmax_neg_dist(class_dist: np.ndarray, temperature: float) -> np.ndarray:
    tau = max(float(temperature), 1.0e-6)
    logits = -class_dist.astype(np.float64) / tau
    logits -= np.max(logits, axis=1, keepdims=True)
    exp = np.exp(logits)
    return exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-12)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate label-free class-distance fusion over multiple V8 source hypotheses."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--min-subset-size", type=int, default=2)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source order mismatch: {source_order} != {normal_order}")

    source_events = {name: read_events(path) for name, path in source_items}
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_event_keys:
        raise ValueError("source event files have no common events")
    base_rows = [source_events[source_order[0]][key] for key in common_event_keys]
    stress_parent = np.asarray([int(row["parent"]) for row in base_rows], dtype=np.int64)
    stress_group = np.asarray([str(row["group"]) for row in base_rows]).astype(str)

    payloads = {name: load_npz(path) for name, path in normal_items}
    key_maps = {name: normal_key_map(payloads[name]) for name in source_order}
    normal_keys = sorted(set.intersection(*(set(mapping) for mapping in key_maps.values())))
    if not normal_keys:
        raise ValueError("normal params have no common rows")
    base_payload = payloads[source_order[0]]
    base_map = key_maps[source_order[0]]
    base_indexes = np.asarray([base_map[key] for key in normal_keys], dtype=np.int64)
    normal_parent = np.asarray(base_payload["parent"], dtype=np.int64)[base_indexes]
    normal_group = np.asarray(["normal"] * len(normal_parent)).astype(str)

    normal_class: dict[str, np.ndarray] = {}
    stress_class: dict[str, np.ndarray] = {}
    stats: dict[str, dict[str, float]] = {}
    for name in source_order:
        payload = payloads[name]
        proto = np.asarray(payload["prototypes_int8"], dtype=np.int8)
        proto_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
        normal_indexes = np.asarray([key_maps[name][key] for key in normal_keys], dtype=np.int64)
        normal_x = np.asarray(payload["embedding_int8"], dtype=np.int8)[normal_indexes]
        stress_x = np.stack([parse_feature(source_events[name][key]) for key in common_event_keys], axis=0).astype(np.int8)
        normal_cd = class_distances(normal_x, proto, proto_parent, batch_size=args.batch_size)
        stress_cd = class_distances(stress_x, proto, proto_parent, batch_size=args.batch_size)
        normal_class[name] = normal_cd
        stress_class[name] = stress_cd
        stats[name] = source_scale_stats(normal_cd, normal_parent)

    policies: list[dict[str, Any]] = []
    trace_rows: list[dict[str, Any]] = []

    def add_policy(name: str, normal_scores: np.ndarray, stress_scores: np.ndarray, choose_mode: str) -> None:
        normal_pred, normal_margin = choose_from_scores(normal_scores, choose_mode)
        stress_pred, stress_margin = choose_from_scores(stress_scores, choose_mode)
        normal_summary = summarize_predictions(pred=normal_pred, parent=normal_parent, groups=normal_group)
        stress_summary = summarize_predictions(pred=stress_pred, parent=stress_parent, groups=stress_group)
        policies.append(
            {
                "policy": name,
                "selection_label_usage": "none",
                "normal_replay_100": bool(normal_summary["wrong_events"] == 0),
                "normal_wrong_events": int(normal_summary["wrong_events"]),
                "normal_margin_min": float(np.min(normal_margin)),
                "normal_margin_p01": float(np.percentile(normal_margin, 1)),
                "normal_margin_mean": float(np.mean(normal_margin)),
                "high_wrong_events": int(stress_summary["wrong_events"]),
                "high_total_events": int(stress_summary["total_events"]),
                "high_wrong_rate": float(stress_summary["wrong_rate"]),
                "high_low_wrong_rate": float(stress_summary["low_wrong_rate"]),
                "high_control_wrong_rate": float(stress_summary["control_wrong_rate"]),
            }
        )
        if len(trace_rows) < 500:
            counts = Counter(stress_pred.tolist())
            trace_rows.append(
                {
                    "policy": name,
                    "pred_counts_json": json.dumps({str(k): int(v) for k, v in sorted(counts.items())}),
                }
            )

    for name in source_order:
        add_policy(
            f"single_distance:{name}",
            normal_class[name],
            stress_class[name],
            "min",
        )

    min_subset_size = max(1, int(args.min_subset_size))
    subsets: list[tuple[str, ...]] = []
    for size in range(min_subset_size, len(source_order) + 1):
        subsets.extend(tuple(items) for items in combinations(source_order, size))

    for subset in subsets:
        subset_tag = "+".join(subset)
        for scale_mode in ["none", "margin_p50", "margin_p90", "true_dist_p50", "true_dist_p90", "sqrt_dim"]:
            norm_dist = np.zeros((len(normal_parent), 3), dtype=np.float64)
            stress_dist = np.zeros((len(stress_parent), 3), dtype=np.float64)
            norm_margin_sum = np.zeros((len(normal_parent), 3), dtype=np.float64)
            stress_margin_sum = np.zeros((len(stress_parent), 3), dtype=np.float64)
            norm_pos_margin_sum = np.zeros((len(normal_parent), 3), dtype=np.float64)
            stress_pos_margin_sum = np.zeros((len(stress_parent), 3), dtype=np.float64)
            norm_rank = np.zeros((len(normal_parent), 3), dtype=np.float64)
            stress_rank = np.zeros((len(stress_parent), 3), dtype=np.float64)
            norm_min_dist = np.full((len(normal_parent), 3), np.inf, dtype=np.float64)
            stress_min_dist = np.full((len(stress_parent), 3), np.inf, dtype=np.float64)
            for name in subset:
                dim = int(np.asarray(payloads[name]["embedding_int8"]).shape[1])
                if scale_mode == "none":
                    scale = 1.0
                elif scale_mode == "sqrt_dim":
                    scale = math.sqrt(float(max(dim, 1)))
                else:
                    scale = max(float(stats[name][scale_mode]), 1.0)
                n_cd = normal_class[name] / scale
                s_cd = stress_class[name] / scale
                n_margin = margin_matrix(normal_class[name]) / scale
                s_margin = margin_matrix(stress_class[name]) / scale
                norm_dist += n_cd
                stress_dist += s_cd
                norm_min_dist = np.minimum(norm_min_dist, n_cd)
                stress_min_dist = np.minimum(stress_min_dist, s_cd)
                norm_margin_sum += n_margin
                stress_margin_sum += s_margin
                norm_pos_margin_sum += np.maximum(n_margin, 0.0)
                stress_pos_margin_sum += np.maximum(s_margin, 0.0)
                norm_rank += rank_matrix(normal_class[name])
                stress_rank += rank_matrix(stress_class[name])
            add_policy(f"sum_distance:{scale_mode}:{subset_tag}", norm_dist, stress_dist, "min")
            add_policy(f"min_distance:{scale_mode}:{subset_tag}", norm_min_dist, stress_min_dist, "min")
            add_policy(f"sum_class_margin:{scale_mode}:{subset_tag}", norm_margin_sum, stress_margin_sum, "max")
            add_policy(
                f"sum_positive_class_margin:{scale_mode}:{subset_tag}",
                norm_pos_margin_sum,
                stress_pos_margin_sum,
                "max",
            )
            add_policy(f"rank_sum:{scale_mode}:{subset_tag}", norm_rank, stress_rank, "min")

        for temp_scale in [0.25, 0.5, 1.0, 2.0, 4.0]:
            norm_prob = np.zeros((len(normal_parent), 3), dtype=np.float64)
            stress_prob = np.zeros((len(stress_parent), 3), dtype=np.float64)
            for name in subset:
                tau = max(float(stats[name]["margin_p50"]) * float(temp_scale), 1.0)
                norm_prob += softmax_neg_dist(normal_class[name], tau)
                stress_prob += softmax_neg_dist(stress_class[name], tau)
            add_policy(
                f"softmax_distance_sum:margin_p50_x{temp_scale:g}:{subset_tag}",
                norm_prob,
                stress_prob,
                "max",
            )

    policies = sorted(
        policies,
        key=lambda row: (
            not bool(row["normal_replay_100"]),
            float(row["high_low_wrong_rate"]),
            float(row["high_control_wrong_rate"]),
            float(row["high_wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policies)
    write_csv(args.output_dir / "policy_trace.csv", trace_rows)
    summary = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "source_order": source_order,
        "common_normal_rows": int(len(normal_keys)),
        "common_high_pressure_events": int(len(common_event_keys)),
        "high_pressure_usage": "evaluation_only",
        "selection_label_usage": "none",
        "runtime_feature_usage": "all_source_class_distances_diagnostic_not_deployable",
        "min_subset_size": int(min_subset_size),
        "source_scale_stats": stats,
        "top_policies": policies[:15],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
