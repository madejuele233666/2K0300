import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from augment_v8_conflict_family_guards import summarize_group, write_csv, write_json
from evaluate_v8_embedding_prototypes import metric_summary


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def build_conflict_map(
    teacher: dict[str, np.ndarray],
    *,
    min_support: int,
) -> dict[int, dict[int, dict[str, float]]]:
    wrong_proto_index = np.asarray(teacher["wrong_proto_index"], dtype=np.int64)
    parent = np.asarray(teacher["parent"], dtype=np.int64)
    weight = np.asarray(teacher["weight"], dtype=np.float32)
    teacher_vote_count = np.asarray(teacher["teacher_vote_count"], dtype=np.int64)
    teacher_margin_mean = np.asarray(teacher["teacher_margin_mean"], dtype=np.float32)

    buckets: dict[int, dict[int, dict[str, float]]] = defaultdict(dict)
    raw: dict[tuple[int, int], dict[str, float]] = defaultdict(
        lambda: {"support": 0.0, "weight_sum": 0.0, "vote_sum": 0.0, "teacher_margin_sum": 0.0}
    )
    for proto_index, candidate_parent, event_weight, vote_count, margin_mean in zip(
        wrong_proto_index.tolist(),
        parent.tolist(),
        weight.tolist(),
        teacher_vote_count.tolist(),
        teacher_margin_mean.tolist(),
    ):
        key = (int(proto_index), int(candidate_parent))
        raw[key]["support"] += 1.0
        raw[key]["weight_sum"] += float(event_weight)
        raw[key]["vote_sum"] += float(vote_count)
        raw[key]["teacher_margin_sum"] += float(margin_mean)

    for (proto_index, candidate_parent), stats in raw.items():
        support = int(stats["support"])
        if support < min_support:
            continue
        buckets[int(proto_index)][int(candidate_parent)] = {
            "support": float(support),
            "weight_sum": float(stats["weight_sum"]),
            "vote_mean": float(stats["vote_sum"] / max(support, 1)),
            "teacher_margin_mean": float(stats["teacher_margin_sum"] / max(support, 1)),
        }
    return dict(buckets)


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_count = 3
    class_dist = np.full((len(x_all), parent_count), np.iinfo(np.int64).max, dtype=np.int64)
    nearest_proto = np.full((len(x_all), parent_count), -1, dtype=np.int64)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(parent_count)]
    for start in range(0, len(x_all), 512):
        x = x_all[start : start + 512]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            local = dist[:, indexes]
            arg = np.argmin(local, axis=1)
            class_dist[start : start + len(x), parent] = local[np.arange(len(x)), arg]
            nearest_proto[start : start + len(x), parent] = indexes[arg]
    order = np.argsort(class_dist, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (
        class_dist[np.arange(len(x_all)), order[:, 1]]
        - class_dist[np.arange(len(x_all)), order[:, 0]]
    ).astype(np.int64)
    return pred, margin, class_dist, nearest_proto


def apply_conflict_cascade(
    *,
    y_parent: np.ndarray,
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    nearest_proto: np.ndarray,
    conflict_map: dict[int, dict[int, dict[str, float]]],
    gate_gap: int,
    bonus_per_support: int,
) -> tuple[np.ndarray, list[dict[str, Any]]]:
    out = pred.copy()
    traces: list[dict[str, Any]] = []
    for index, primary_parent in enumerate(pred.tolist()):
        primary_parent = int(primary_parent)
        primary_proto = int(nearest_proto[index, primary_parent])
        candidate_stats = conflict_map.get(primary_proto)
        if not candidate_stats:
            continue
        primary_dist = int(class_dist[index, primary_parent])
        best_parent = primary_parent
        best_adjusted = float(primary_dist)
        best_gap = 0
        best_support = 0.0
        for candidate_parent, stats in candidate_stats.items():
            candidate_parent = int(candidate_parent)
            if candidate_parent == primary_parent:
                continue
            candidate_dist = int(class_dist[index, candidate_parent])
            gap = candidate_dist - primary_dist
            adjusted = float(candidate_dist) - float(bonus_per_support) * float(stats["support"])
            if gap <= gate_gap and adjusted < best_adjusted:
                best_parent = candidate_parent
                best_adjusted = adjusted
                best_gap = int(gap)
                best_support = float(stats["support"])
        if best_parent == primary_parent:
            continue
        out[index] = best_parent
        traces.append(
            {
                "row_index": int(index),
                "true_parent": int(y_parent[index]),
                "primary_pred": primary_parent,
                "cascade_pred": int(best_parent),
                "primary_margin": int(margin[index]),
                "primary_proto": primary_proto,
                "candidate_gap": int(best_gap),
                "candidate_support": float(best_support),
                "fixed": bool(primary_parent != int(y_parent[index]) and best_parent == int(y_parent[index])),
                "broken": bool(primary_parent == int(y_parent[index]) and best_parent != int(y_parent[index])),
            }
        )
    return out, traces


def evaluate_setting(
    *,
    name: str,
    y_parent: np.ndarray,
    view_labels: np.ndarray,
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    nearest_proto: np.ndarray,
    conflict_map: dict[int, dict[int, dict[str, float]]],
    gate_gap: int,
    bonus_per_support: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    cascade_pred, traces = apply_conflict_cascade(
        y_parent=y_parent,
        pred=pred,
        margin=margin,
        class_dist=class_dist,
        nearest_proto=nearest_proto,
        conflict_map=conflict_map,
        gate_gap=gate_gap,
        bonus_per_support=bonus_per_support,
    )
    view_order = list(dict.fromkeys(view_labels.astype(str).tolist()))
    fixed = sum(1 for row in traces if row["fixed"])
    broken = sum(1 for row in traces if row["broken"])
    row: dict[str, Any] = {
        "name": name,
        "gate_gap": int(gate_gap),
        "bonus_per_support": int(bonus_per_support),
        "cascade_used": int(len(traces)),
        "cascade_fixed": int(fixed),
        "cascade_broken": int(broken),
        "all_correct": bool(np.all(cascade_pred == y_parent)),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=cascade_pred))
    return row, traces


def evaluate_stress(
    *,
    stress_rows: list[dict[str, str]],
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    nearest_proto: np.ndarray,
    conflict_map: dict[int, dict[int, dict[str, float]]],
    gate_gap: int,
    bonus_per_support: int,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    y_parent = np.asarray([int(row["parent"]) for row in stress_rows], dtype=np.int64)
    view_labels = np.asarray([row["view_label"] for row in stress_rows]).astype(str)
    cascade_pred, traces = apply_conflict_cascade(
        y_parent=y_parent,
        pred=pred,
        margin=margin,
        class_dist=class_dist,
        nearest_proto=nearest_proto,
        conflict_map=conflict_map,
        gate_gap=gate_gap,
        bonus_per_support=bonus_per_support,
    )
    out_rows: list[dict[str, Any]] = []
    for row, primary_pred, cascade_value, margin_value in zip(
        stress_rows,
        pred.tolist(),
        cascade_pred.tolist(),
        margin.tolist(),
    ):
        parent = int(row["parent"])
        out_rows.append(
            {
                "group": row["group"],
                "base_query_index": int(row["base_query_index"]),
                "sample_index": int(row["sample_index"]),
                "view_label": row["view_label"],
                "parent": parent,
                "perturb": row["perturb"],
                "perturb_family": row["perturb_family"],
                "primary_pred": int(primary_pred),
                "cascade_pred": int(cascade_value),
                "wrong": bool(int(cascade_value) != parent),
                "primary_wrong": bool(int(primary_pred) != parent),
                "primary_margin": int(margin_value),
            }
        )
    per_group = summarize_group(
        [
            {**row, "stress_margin": int(row["primary_margin"])}
            for row in out_rows
        ],
        ["group"],
    )
    wrong_events = sum(1 for row in out_rows if bool(row["wrong"]))
    fixed = sum(1 for row in out_rows if bool(row["primary_wrong"]) and not bool(row["wrong"]))
    broken = sum(1 for row in out_rows if not bool(row["primary_wrong"]) and bool(row["wrong"]))
    summary = {
        "gate_gap": int(gate_gap),
        "bonus_per_support": int(bonus_per_support),
        "cascade_used": int(len(traces)),
        "cascade_fixed": int(fixed),
        "cascade_broken": int(broken),
        "total_events": int(len(out_rows)),
        "wrong_events": int(wrong_events),
        "wrong_base_count": int(len({(row["group"], int(row["base_query_index"])) for row in out_rows if bool(row["wrong"])})),
        "high_pressure_low_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "low"), None),
        "high_pressure_control_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "control"), None),
        "per_group": per_group,
    }
    return summary, out_rows, traces


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate a normal-teacher-cache gated conflict cascade without using high-pressure rows for training."
    )
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--teacher-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--min-supports", default="1,2,3,5")
    parser.add_argument("--gate-gaps", default="0,16,32,64,128,256,512,1024,2048,4096,8192")
    parser.add_argument("--bonus-per-supports", default="0,16,32,64,128")
    args = parser.parse_args()

    base = load_npz(args.base_params_npz)
    teacher = load_npz(args.teacher_npz)
    stress_rows = read_csv_rows(args.stress_events_csv)
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    normal_pred, normal_margin, normal_class_dist, normal_nearest_proto = class_distances(
        embeddings,
        prototypes,
        prototype_parent,
    )
    if not np.all(normal_pred == parent):
        raise ValueError("base normal int8 replay is not 100%; refuse to evaluate cascade")

    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_pred, stress_margin, stress_class_dist, stress_nearest_proto = class_distances(
        stress_features,
        prototypes,
        prototype_parent,
    )

    rows: list[dict[str, Any]] = []
    best_valid: dict[str, Any] | None = None
    for min_support in parse_ints(args.min_supports):
        conflict_map = build_conflict_map(teacher, min_support=min_support)
        proto_count = len(conflict_map)
        parent_edges = sum(len(value) for value in conflict_map.values())
        support_counter = Counter()
        for per_parent in conflict_map.values():
            for stats in per_parent.values():
                support_counter[int(stats["support"])] += 1
        for gate_gap in parse_ints(args.gate_gaps):
            for bonus_per_support in parse_ints(args.bonus_per_supports):
                setting_name = f"support{min_support}_gap{gate_gap}_bonus{bonus_per_support}"
                normal_row, normal_traces = evaluate_setting(
                    name=setting_name,
                    y_parent=parent,
                    view_labels=view_labels,
                    pred=normal_pred,
                    margin=normal_margin,
                    class_dist=normal_class_dist,
                    nearest_proto=normal_nearest_proto,
                    conflict_map=conflict_map,
                    gate_gap=gate_gap,
                    bonus_per_support=bonus_per_support,
                )
                stress_summary, stress_out, stress_traces = evaluate_stress(
                    stress_rows=stress_rows,
                    pred=stress_pred,
                    margin=stress_margin,
                    class_dist=stress_class_dist,
                    nearest_proto=stress_nearest_proto,
                    conflict_map=conflict_map,
                    gate_gap=gate_gap,
                    bonus_per_support=bonus_per_support,
                )
                row = {
                    **normal_row,
                    "min_support": int(min_support),
                    "conflict_proto_count": int(proto_count),
                    "conflict_parent_edges": int(parent_edges),
                    "normal_cascade_used": int(len(normal_traces)),
                    "stress_cascade_used": int(stress_summary["cascade_used"]),
                    "stress_cascade_fixed": int(stress_summary["cascade_fixed"]),
                    "stress_cascade_broken": int(stress_summary["cascade_broken"]),
                    "high_pressure_wrong_events": int(stress_summary["wrong_events"]),
                    "high_pressure_low_wrong_rate": stress_summary["high_pressure_low_wrong_rate"],
                    "high_pressure_control_wrong_rate": stress_summary["high_pressure_control_wrong_rate"],
                    "high_pressure_per_group_json": json.dumps(stress_summary["per_group"], ensure_ascii=False),
                }
                rows.append(row)
                if bool(row["all_correct"]):
                    if best_valid is None or (
                        float(row["high_pressure_low_wrong_rate"]),
                        float(row["high_pressure_control_wrong_rate"]),
                        int(row["high_pressure_wrong_events"]),
                    ) < (
                        float(best_valid["high_pressure_low_wrong_rate"]),
                        float(best_valid["high_pressure_control_wrong_rate"]),
                        int(best_valid["high_pressure_wrong_events"]),
                    ):
                        best_valid = row
                setting_dir = output_dir / setting_name
                write_json(
                    setting_dir / "summary.json",
                    {
                        "normal": normal_row,
                        "stress": stress_summary,
                        "min_support": int(min_support),
                        "conflict_proto_count": int(proto_count),
                        "conflict_parent_edges": int(parent_edges),
                        "support_histogram": dict(sorted(support_counter.items())),
                        "high_pressure_usage": "evaluation_only",
                        "selection_usage": "normal_teacher_cache_only",
                    },
                )
                write_csv(setting_dir / "stress_events.csv", stress_out)
                write_csv(setting_dir / "normal_cascade_traces.csv", normal_traces)
                write_csv(setting_dir / "stress_cascade_traces.csv", stress_traces)

    write_csv(output_dir / "candidate_results.csv", rows)
    write_json(
        output_dir / "summary.json",
        {
            "base_params_npz": str(args.base_params_npz),
            "teacher_npz": str(args.teacher_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_teacher_cache_only",
            "settings": int(len(rows)),
            "best_valid": best_valid,
        },
    )
    print(json.dumps({"output_dir": str(output_dir), "settings": len(rows), "best_valid": best_valid}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
