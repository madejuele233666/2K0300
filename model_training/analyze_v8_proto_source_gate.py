import argparse
import csv
import json
import math
from collections import Counter, defaultdict
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


def parse_list(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


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


def transformed_margin(margin: np.ndarray, dim: int, mode: str) -> np.ndarray:
    values = np.maximum(margin.astype(np.float64), 0.0)
    if mode == "raw":
        return values
    if mode == "per_sqrt_dim":
        return values / math.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return values / float(max(dim, 1))
    if mode == "log":
        return np.log1p(values)
    raise ValueError(f"unknown score mode: {mode}")


def margin_bucket(value: int) -> str:
    for threshold in [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]:
        if int(value) <= threshold:
            return f"le{threshold}"
    return "gt512"


def parse_feature(row: dict[str, str]) -> np.ndarray:
    if row.get("feature_json"):
        return np.asarray(json.loads(row["feature_json"]), dtype=np.int8)
    dim = int(float(row.get("feature_dim", 4)))
    return np.asarray([int(float(row.get(f"feature{index}", 0))) for index in range(dim)], dtype=np.int8)


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    nearest_rows: list[np.ndarray] = []
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        nearest = np.full((len(x), 3), -1, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            local = dist[:, indexes]
            arg = np.argmin(local, axis=1)
            class_dist[:, parent] = local[np.arange(len(x)), arg]
            nearest[:, parent] = indexes[arg]
        order = np.argsort(class_dist, axis=1)
        rows = np.arange(len(x))
        pred = order[:, 0].astype(np.int64)
        pred_rows.append(pred)
        margin_rows.append((class_dist[rows, order[:, 1]] - class_dist[rows, order[:, 0]]).astype(np.int64))
        nearest_rows.append(nearest[rows, pred].astype(np.int64))
    return (
        np.concatenate(pred_rows).astype(np.int64),
        np.concatenate(margin_rows).astype(np.int64),
        np.concatenate(nearest_rows).astype(np.int64),
    )


def build_keys(
    *,
    mode: str,
    nearest_proto: np.ndarray,
    pred: np.ndarray,
    margin: np.ndarray,
    prototype_parent: np.ndarray,
) -> list[str]:
    out: list[str] = []
    for proto_index, pred_parent, margin_value in zip(nearest_proto.tolist(), pred.tolist(), margin.tolist(), strict=False):
        proto_parent = int(prototype_parent[int(proto_index)]) if int(proto_index) >= 0 else -1
        values = {
            "proto": f"proto={int(proto_index)}",
            "proto_parent": f"proto_parent={proto_parent}",
            "pred": f"pred={int(pred_parent)}",
            "margin_bucket": f"margin={margin_bucket(int(margin_value))}",
        }
        parts = [values[item] for item in mode.split("+") if item]
        out.append("|".join(parts) if parts else "global")
    return out


def source_scores(
    *,
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    score_mode: str,
) -> dict[str, np.ndarray]:
    out: dict[str, np.ndarray] = {}
    for name in source_order:
        payload = payloads[name]
        margin = np.asarray(payload["int8_margin"], dtype=np.float64)
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)
        parent = np.asarray(payload["parent"], dtype=np.int64)
        dim = int(np.asarray(payload["embedding_int8"]).shape[1])
        score = transformed_margin(margin, dim, score_mode)
        out[name] = np.where(pred == parent, score, -1.0 - np.abs(score)).astype(np.float64)
    return out


def learn_mapping(
    *,
    keys: list[str],
    base_payload: dict[str, np.ndarray],
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    score_mode: str,
    score_quantile: float,
    min_support: int,
) -> tuple[dict[str, str], str, list[dict[str, Any]]]:
    scores = source_scores(payloads=payloads, source_order=source_order, score_mode=score_mode)
    maps = {name: normal_key_map(payloads[name]) for name in source_order}
    base_sample = np.asarray(base_payload["sample_index"], dtype=np.int64)
    base_view = np.asarray(base_payload["view_labels"]).astype(str)
    groups: dict[str, list[int]] = defaultdict(list)
    for index, key in enumerate(keys):
        groups[key].append(index)

    global_scores: dict[str, tuple[Any, ...]] = {}
    for source in source_order:
        source_map = maps[source]
        values: list[float] = []
        correct_count = 0
        total = 0
        for sample, view in zip(base_sample.tolist(), base_view.tolist(), strict=False):
            source_index = source_map.get((int(sample), str(view)))
            if source_index is None:
                continue
            value = float(scores[source][source_index])
            values.append(value)
            correct_count += int(value >= 0.0)
            total += 1
        positive = np.asarray([value for value in values if value >= 0.0], dtype=np.float64)
        global_scores[source] = (
            correct_count / max(total, 1),
            float(np.percentile(positive, score_quantile)) if positive.size else -1.0,
            float(np.median(positive)) if positive.size else -1.0,
            -source_order.index(source),
        )
    fallback = max(source_order, key=lambda source: global_scores[source])

    mapping: dict[str, str] = {}
    rows: list[dict[str, Any]] = []
    for key, indexes in sorted(groups.items()):
        best_source = fallback
        best_tuple = None
        per_source: dict[str, dict[str, Any]] = {}
        for source in source_order:
            source_map = maps[source]
            values: list[float] = []
            for base_index in indexes:
                source_index = source_map.get((int(base_sample[base_index]), str(base_view[base_index])))
                if source_index is None:
                    continue
                values.append(float(scores[source][source_index]))
            if not values:
                continue
            arr = np.asarray(values, dtype=np.float64)
            positive = arr[arr >= 0.0]
            support = int(len(arr))
            accuracy = float(len(positive) / max(support, 1))
            q_score = float(np.percentile(positive, score_quantile)) if positive.size else -1.0
            med_score = float(np.median(positive)) if positive.size else -1.0
            score_tuple = (accuracy, q_score, med_score, support, -source_order.index(source))
            per_source[source] = {
                "support": support,
                "accuracy": accuracy,
                "score_q": q_score,
                "score_median": med_score,
            }
            if best_tuple is None or score_tuple > best_tuple:
                best_tuple = score_tuple
                best_source = source
        selected = best_source if len(indexes) >= int(min_support) else fallback
        mapping[key] = selected
        rows.append(
            {
                "key": key,
                "row_count": int(len(indexes)),
                "selected_source": selected,
                "raw_best_source": best_source,
                "fallback_used": bool(len(indexes) < int(min_support)),
                "per_source_json": json.dumps(per_source, ensure_ascii=False),
            }
        )
    return mapping, fallback, rows


def summarize_stress(
    *,
    base_rows: list[dict[str, str]],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    keys: list[str],
    mapping: dict[str, str],
    fallback: str,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    fallback_count = 0
    traces: list[dict[str, Any]] = []
    for row_index, row in enumerate(base_rows):
        key = event_key(row)
        gate_key = keys[row_index]
        source = mapping.get(gate_key, fallback)
        if key not in source_events[source]:
            source = fallback
            fallback_count += 1
        pred = int(source_events[source][key]["stress_pred"])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
        if len(traces) < 200:
            traces.append(
                {
                    "gate_key": gate_key,
                    "chosen_source": source,
                    "event_group": str(row["group"]),
                    "sample_index": int(row["sample_index"]),
                    "view_label": str(row["view_label"]),
                    "perturb": str(row["perturb"]),
                    "parent": parent,
                    "pred": pred,
                    "wrong": bool(wrong),
                }
            )
    wrong_events = int(sum(value[0] for value in grouped.values()))
    total_events = int(sum(value[1] for value in grouped.values()))
    summary = {
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
        "fallback_count": int(fallback_count),
        "chosen_counts": dict(chosen),
    }
    return summary, traces


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate normal-only source routing keyed by D4 nearest-prototype cells."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--key-modes",
        default="proto,proto+margin_bucket,proto+pred,proto+pred+margin_bucket,proto_parent+pred+margin_bucket",
    )
    parser.add_argument("--score-modes", default="raw,per_sqrt_dim,per_dim,log")
    parser.add_argument("--score-quantile", type=float, default=5.0)
    parser.add_argument("--min-supports", default="1,3,5,10")
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source order mismatch: {source_order} != {normal_order}")
    source_events = {name: read_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_keys:
        raise ValueError("source event files have no common high-pressure events")
    base_rows = [source_events[source_order[0]][key] for key in common_keys]
    normal_payloads = {name: load_npz(path) for name, path in normal_items}
    base_payload = load_npz(args.base_params_npz)
    prototypes = np.asarray(base_payload["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base_payload["prototype_parent"], dtype=np.int64)
    normal_features = np.asarray(base_payload["embedding_int8"], dtype=np.int8)
    normal_pred, normal_margin, normal_nearest = class_distances(
        normal_features,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )
    stress_features = np.stack([parse_feature(row) for row in base_rows], axis=0).astype(np.int8)
    stress_pred, stress_margin, stress_nearest = class_distances(
        stress_features,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    mapping_rows_all: list[dict[str, Any]] = []
    trace_rows: list[dict[str, Any]] = []
    for key_mode in parse_list(args.key_modes):
        normal_keys = build_keys(
            mode=key_mode,
            nearest_proto=normal_nearest,
            pred=normal_pred,
            margin=normal_margin,
            prototype_parent=prototype_parent,
        )
        stress_keys = build_keys(
            mode=key_mode,
            nearest_proto=stress_nearest,
            pred=stress_pred,
            margin=stress_margin,
            prototype_parent=prototype_parent,
        )
        for score_mode in parse_list(args.score_modes):
            for min_support in [int(value) for value in parse_list(args.min_supports)]:
                mapping, fallback, mapping_rows = learn_mapping(
                    keys=normal_keys,
                    base_payload=base_payload,
                    payloads=normal_payloads,
                    source_order=source_order,
                    score_mode=score_mode,
                    score_quantile=float(args.score_quantile),
                    min_support=int(min_support),
                )
                summary, traces = summarize_stress(
                    base_rows=base_rows,
                    source_events=source_events,
                    source_order=source_order,
                    keys=stress_keys,
                    mapping=mapping,
                    fallback=fallback,
                )
                policy_name = f"proto_gate:{key_mode}:{score_mode}:q{args.score_quantile:g}:min{min_support}"
                policy_rows.append(
                    {
                        "policy": policy_name,
                        "selection_label_usage": "none",
                        "key_mode": key_mode,
                        "score_mode": score_mode,
                        "min_support": int(min_support),
                        "fallback_source": fallback,
                        "mapping_size": int(len(mapping)),
                        **{key: value for key, value in summary.items() if key != "chosen_counts"},
                        "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
                    }
                )
                for row in mapping_rows:
                    mapping_rows_all.append(
                        {
                            "policy": policy_name,
                            "key_mode": key_mode,
                            "score_mode": score_mode,
                            "min_support": int(min_support),
                            **row,
                        }
                    )
                trace_rows.extend({"policy": policy_name, **row} for row in traces[:20])
    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "proto_gate_mapping.csv", mapping_rows_all)
    write_csv(args.output_dir / "stress_trace_sample.csv", trace_rows)
    summary = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "base_params_npz": str(args.base_params_npz),
        "source_order": source_order,
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "nearest-prototype source routing table only",
        "common_events": int(len(common_keys)),
        "top_policies": policy_rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
