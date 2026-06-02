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


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def load_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def int_field(row: dict[str, str], key: str, default: int = 0) -> int:
    value = row.get(key, "")
    if value == "":
        return int(default)
    return int(float(value))


def stress_margin(row: dict[str, str]) -> int:
    return int_field(row, "stress_margin", int_field(row, "primary_margin", 0))


def feature_dim(row: dict[str, str]) -> int:
    return max(1, int_field(row, "feature_dim", 1))


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


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def source_normal_scores(
    *,
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    score_mode: str,
) -> dict[str, np.ndarray]:
    scores: dict[str, np.ndarray] = {}
    for name in source_order:
        payload = payloads[name]
        margin = np.asarray(payload["int8_margin"], dtype=np.float64)
        dim = int(np.asarray(payload["embedding_int8"]).shape[1])
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)
        parent = np.asarray(payload["parent"], dtype=np.int64)
        score = transformed_margin(margin, dim, score_mode)
        # Normal replay failures are explicitly bad source evidence for routing.
        score = np.where(pred == parent, score, -1.0 - np.abs(score))
        scores[name] = score.astype(np.float64)
    return scores


def source_advantage_scores(
    *,
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    scores: dict[str, np.ndarray],
) -> dict[str, np.ndarray]:
    key_maps = {name: normal_key_map(payloads[name]) for name in source_order}
    common_keys = sorted(set.intersection(*(set(key_map) for key_map in key_maps.values())))
    by_source = {name: np.zeros_like(scores[name], dtype=np.float64) for name in source_order}
    for key in common_keys:
        values = {name: float(scores[name][key_maps[name][key]]) for name in source_order}
        for name in source_order:
            other = [value for other_name, value in values.items() if other_name != name]
            baseline = max(other) if other else 0.0
            by_source[name][key_maps[name][key]] = values[name] - baseline
    return by_source


def parse_feature(row: dict[str, str]) -> np.ndarray:
    if row.get("feature_json"):
        return np.asarray(json.loads(row["feature_json"]), dtype=np.int32)
    dim = feature_dim(row)
    return np.asarray([int_field(row, f"feature{i}", 0) for i in range(dim)], dtype=np.int32)


def event_matrix(rows: list[dict[str, str]]) -> np.ndarray:
    return np.stack([parse_feature(row) for row in rows], axis=0).astype(np.int32)


def local_knn_scores(
    *,
    query_x: np.ndarray,
    query_pred: np.ndarray,
    normal_x: np.ndarray,
    normal_pred: np.ndarray,
    normal_score: np.ndarray,
    k: int,
    neighbor_filter: str,
    aggregation: str,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray]:
    out = np.full(len(query_x), -1.0e18, dtype=np.float64)
    out_dist = np.full(len(query_x), -1, dtype=np.int64)
    if neighbor_filter == "all":
        groups = [(np.arange(len(query_x)), np.arange(len(normal_x)))]
    elif neighbor_filter == "same_pred":
        groups = []
        for parent in sorted(set(query_pred.tolist())):
            q_idx = np.where(query_pred == int(parent))[0]
            n_idx = np.where(normal_pred == int(parent))[0]
            if len(q_idx) and len(n_idx):
                groups.append((q_idx, n_idx))
    else:
        raise ValueError(f"unknown neighbor filter: {neighbor_filter}")

    for query_indexes, normal_indexes in groups:
        train_x = normal_x[normal_indexes].astype(np.int32)
        train_score = normal_score[normal_indexes].astype(np.float64)
        local_k = min(max(1, int(k)), len(train_x))
        for start in range(0, len(query_indexes), batch_size):
            q_idx = query_indexes[start : start + batch_size]
            q = query_x[q_idx].astype(np.int32)
            diff = q[:, None, :] - train_x[None, :, :]
            dist = np.sum(diff * diff, axis=2, dtype=np.int64)
            if local_k == len(train_x):
                nearest = np.tile(np.arange(len(train_x)), (len(q), 1))
            else:
                nearest = np.argpartition(dist, kth=local_k - 1, axis=1)[:, :local_k]
            nearest_dist = np.take_along_axis(dist, nearest, axis=1)
            nearest_score = train_score[nearest]
            if aggregation == "mean":
                score = np.mean(nearest_score, axis=1)
            elif aggregation == "invdist":
                weight = 1.0 / (nearest_dist.astype(np.float64) + 1.0)
                score = np.sum(nearest_score * weight, axis=1) / np.maximum(np.sum(weight, axis=1), 1.0e-12)
            elif aggregation == "best":
                best = np.argmin(nearest_dist, axis=1)
                score = nearest_score[np.arange(len(best)), best]
            else:
                raise ValueError(f"unknown aggregation: {aggregation}")
            out[q_idx] = score
            out_dist[q_idx] = np.min(nearest_dist, axis=1)
    return out, out_dist


def local_knn_score_grid(
    *,
    query_x: np.ndarray,
    query_pred: np.ndarray,
    normal_x: np.ndarray,
    normal_pred: np.ndarray,
    normal_score: np.ndarray,
    k_values: list[int],
    neighbor_filter: str,
    aggregations: list[str],
    batch_size: int,
) -> tuple[dict[tuple[int, str], np.ndarray], dict[tuple[int, str], np.ndarray]]:
    scores = {
        (int(k), aggregation): np.full(len(query_x), -1.0e18, dtype=np.float64)
        for k in k_values
        for aggregation in aggregations
    }
    distances = {
        (int(k), aggregation): np.full(len(query_x), -1, dtype=np.int64)
        for k in k_values
        for aggregation in aggregations
    }
    if neighbor_filter == "all":
        groups = [(np.arange(len(query_x)), np.arange(len(normal_x)))]
    elif neighbor_filter == "same_pred":
        groups = []
        for parent in sorted(set(query_pred.tolist())):
            q_idx = np.where(query_pred == int(parent))[0]
            n_idx = np.where(normal_pred == int(parent))[0]
            if len(q_idx) and len(n_idx):
                groups.append((q_idx, n_idx))
    else:
        raise ValueError(f"unknown neighbor filter: {neighbor_filter}")

    max_requested_k = max(max(1, int(k)) for k in k_values)
    for query_indexes, normal_indexes in groups:
        train_x = normal_x[normal_indexes].astype(np.int32)
        train_score = normal_score[normal_indexes].astype(np.float64)
        max_k = min(max_requested_k, len(train_x))
        for start in range(0, len(query_indexes), batch_size):
            q_idx = query_indexes[start : start + batch_size]
            q = query_x[q_idx].astype(np.int32)
            diff = q[:, None, :] - train_x[None, :, :]
            dist = np.sum(diff * diff, axis=2, dtype=np.int64)
            if max_k == len(train_x):
                nearest = np.tile(np.arange(len(train_x)), (len(q), 1))
            else:
                nearest = np.argpartition(dist, kth=max_k - 1, axis=1)[:, :max_k]
            nearest_dist = np.take_along_axis(dist, nearest, axis=1)
            order = np.argsort(nearest_dist, axis=1)
            nearest_dist = np.take_along_axis(nearest_dist, order, axis=1)
            nearest_score = train_score[np.take_along_axis(nearest, order, axis=1)]
            for requested_k in k_values:
                local_k = min(max(1, int(requested_k)), nearest_score.shape[1])
                k_dist = nearest_dist[:, :local_k]
                k_score = nearest_score[:, :local_k]
                min_dist = np.min(k_dist, axis=1)
                for aggregation in aggregations:
                    if aggregation == "mean":
                        score = np.mean(k_score, axis=1)
                    elif aggregation == "invdist":
                        weight = 1.0 / (k_dist.astype(np.float64) + 1.0)
                        score = np.sum(k_score * weight, axis=1) / np.maximum(np.sum(weight, axis=1), 1.0e-12)
                    elif aggregation == "best":
                        score = k_score[:, 0]
                    else:
                        raise ValueError(f"unknown aggregation: {aggregation}")
                    scores[(int(requested_k), aggregation)][q_idx] = score
                    distances[(int(requested_k), aggregation)][q_idx] = min_dist
    return scores, distances


def summarize_policy(
    *,
    name: str,
    label_usage: str,
    base_rows: list[dict[str, str]],
    source_order: list[str],
    pred_by_source: dict[str, np.ndarray],
    score_by_source: dict[str, np.ndarray],
    distance_by_source: dict[str, np.ndarray] | None = None,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    trace: list[dict[str, Any]] = []
    for index, row in enumerate(base_rows):
        source = max(
            source_order,
            key=lambda item: (float(score_by_source[item][index]), -source_order.index(item)),
        )
        pred = int(pred_by_source[source][index])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
        if len(trace) < 200:
            trace_row: dict[str, Any] = {
                "policy": name,
                "chosen_source": source,
                "event_group": str(row["group"]),
                "sample_index": int(row["sample_index"]),
                "view_label": str(row["view_label"]),
                "perturb": str(row["perturb"]),
                "parent": parent,
                "pred": pred,
                "wrong": bool(wrong),
            }
            for item in source_order:
                trace_row[f"{item}_score"] = float(score_by_source[item][index])
                trace_row[f"{item}_pred"] = int(pred_by_source[item][index])
                if distance_by_source is not None:
                    trace_row[f"{item}_nearest_dist"] = int(distance_by_source[item][index])
            trace.append(trace_row)
    wrong_events = int(sum(values[0] for values in grouped.values()))
    total_events = int(sum(values[1] for values in grouped.values()))
    summary = {
        "policy": name,
        "selection_label_usage": label_usage,
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 0])[0] / max(grouped.get("low", [0, 0])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 0])[0] / max(grouped.get("control", [0, 0])[1], 1)),
        "chosen_counts_json": json.dumps(dict(chosen), ensure_ascii=False),
    }
    return summary, trace


def oracle_any(
    *,
    base_rows: list[dict[str, str]],
    source_order: list[str],
    pred_by_source: dict[str, np.ndarray],
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    for index, row in enumerate(base_rows):
        parent = int(row["parent"])
        source = source_order[0]
        pred = int(pred_by_source[source][index])
        for candidate in source_order:
            candidate_pred = int(pred_by_source[candidate][index])
            if candidate_pred == parent:
                source = candidate
                pred = candidate_pred
                break
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
    wrong_events = int(sum(values[0] for values in grouped.values()))
    total_events = int(sum(values[1] for values in grouped.values()))
    return {
        "policy": "oracle_any",
        "selection_label_usage": "true_parent",
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 0])[0] / max(grouped.get("low", [0, 0])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 0])[0] / max(grouped.get("control", [0, 0])[1], 1)),
        "chosen_counts_json": json.dumps(dict(chosen), ensure_ascii=False),
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate normal-only local-neighborhood source gates on aligned V8 high-pressure events."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--score-modes", default="per_sqrt_dim,per_dim,log")
    parser.add_argument("--k-values", default="1,3,5,9,17")
    parser.add_argument("--neighbor-filters", default="all,same_pred")
    parser.add_argument("--aggregations", default="mean,invdist,best")
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source and normal names/order must match: {source_order} != {normal_order}")

    sources = {name: load_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in sources.values())))
    if not common_keys:
        raise ValueError("sources have no common high-pressure events")
    base_name = source_order[0]
    base_rows = [sources[base_name][key] for key in common_keys]
    source_rows = {name: [sources[name][key] for key in common_keys] for name in source_order}
    pred_by_source = {
        name: np.asarray([int(row["stress_pred"]) for row in rows], dtype=np.int64)
        for name, rows in source_rows.items()
    }
    stress_margin_score = {
        name: {
            mode: transformed_margin(
                np.asarray([stress_margin(row) for row in rows], dtype=np.float64),
                max(1, int(np.median([feature_dim(row) for row in rows]))),
                mode,
            )
            for mode in [item.strip() for item in args.score_modes.split(",") if item.strip()]
        }
        for name, rows in source_rows.items()
    }
    payloads = {name: load_npz(path) for name, path in normal_items}
    normal_x = {
        name: np.asarray(payloads[name]["embedding_int8"], dtype=np.int32)
        for name in source_order
    }
    normal_pred = {
        name: np.asarray(payloads[name]["int8_pred"], dtype=np.int64)
        for name in source_order
    }
    stress_x = {name: event_matrix(rows) for name, rows in source_rows.items()}

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    trace_rows: list[dict[str, Any]] = []
    score_modes = [item.strip() for item in args.score_modes.split(",") if item.strip()]
    k_values = [int(item.strip()) for item in args.k_values.split(",") if item.strip()]
    neighbor_filters = [item.strip() for item in args.neighbor_filters.split(",") if item.strip()]
    aggregations = [item.strip() for item in args.aggregations.split(",") if item.strip()]

    for score_mode in score_modes:
        margin_scores = source_normal_scores(payloads=payloads, source_order=source_order, score_mode=score_mode)
        advantage_scores = source_advantage_scores(payloads=payloads, source_order=source_order, scores=margin_scores)
        score_sets = {
            "local_margin": margin_scores,
            "local_advantage": advantage_scores,
        }
        stress_scores = {name: stress_margin_score[name][score_mode] for name in source_order}
        summary, trace = summarize_policy(
            name=f"stress_margin:{score_mode}",
            label_usage="none",
            base_rows=base_rows,
            source_order=source_order,
            pred_by_source=pred_by_source,
            score_by_source=stress_scores,
        )
        policy_rows.append(summary)
        trace_rows.extend(trace[:25])
        for score_name, normal_score_by_source in score_sets.items():
            for neighbor_filter in neighbor_filters:
                source_grids: dict[str, dict[tuple[int, str], np.ndarray]] = {}
                source_distance_grids: dict[str, dict[tuple[int, str], np.ndarray]] = {}
                for source in source_order:
                    score_grid, distance_grid = local_knn_score_grid(
                        query_x=stress_x[source],
                        query_pred=pred_by_source[source],
                        normal_x=normal_x[source],
                        normal_pred=normal_pred[source],
                        normal_score=normal_score_by_source[source],
                        k_values=k_values,
                        neighbor_filter=neighbor_filter,
                        aggregations=aggregations,
                        batch_size=int(args.batch_size),
                    )
                    source_grids[source] = score_grid
                    source_distance_grids[source] = distance_grid
                for aggregation in aggregations:
                    for k in k_values:
                        local_scores = {
                            source: source_grids[source][(int(k), aggregation)]
                            for source in source_order
                        }
                        local_distances = {
                            source: source_distance_grids[source][(int(k), aggregation)]
                            for source in source_order
                        }
                        for policy_name, score_by_source in [
                            (f"{score_name}:{score_mode}:{neighbor_filter}:k{k}:{aggregation}", local_scores),
                            (
                                f"{score_name}_plus_stress:{score_mode}:{neighbor_filter}:k{k}:{aggregation}",
                                {
                                    source: local_scores[source] + stress_scores[source]
                                    for source in source_order
                                },
                            ),
                        ]:
                            summary, trace = summarize_policy(
                                name=policy_name,
                                label_usage="none",
                                base_rows=base_rows,
                                source_order=source_order,
                                pred_by_source=pred_by_source,
                                score_by_source=score_by_source,
                                distance_by_source=local_distances,
                            )
                            policy_rows.append(summary)
                            trace_rows.extend(trace[:25])

    policy_rows.append(oracle_any(base_rows=base_rows, source_order=source_order, pred_by_source=pred_by_source))
    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            str(row["selection_label_usage"]) != "none",
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "trace_sample.csv", trace_rows)
    summary = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "source_order": source_order,
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "local normal-neighborhood reliability only",
        "selection_label_usage": "none except oracle_any",
        "common_events": int(len(common_keys)),
        "dropped_events": {name: int(len(rows) - len(common_keys)) for name, rows in sources.items()},
        "top_policies": policy_rows[:15],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
