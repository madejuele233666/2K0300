import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from estimate_v8_board_time import calibrated_conservative_us
from evaluate_v8_embedding_prototypes import ROT_MIRROR_VIEWS, metric_summary, write_csv
from evaluate_v8_source_gated_table import (
    load_npz,
    read_csv_rows,
    source_labels,
    summarize_group,
    unique_order,
)
from analyze_v8_synthetic_source_event_gate import event_key, parse_named_path, read_events


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def subset_mask(view_labels: np.ndarray, subset: str) -> np.ndarray:
    if subset == "clean":
        return view_labels.astype(str) == "clean"
    if subset == "clean_rotmirror":
        return (view_labels.astype(str) == "clean") | np.isin(view_labels.astype(str), ROT_MIRROR_VIEWS)
    raise ValueError(f"unknown base subset: {subset}")


def stack_residual_tables(
    tables: list[list[np.ndarray]],
    parents: list[list[int]],
    feature_dim: int,
) -> tuple[list[np.ndarray], list[np.ndarray]]:
    proto_rows: list[np.ndarray] = []
    parent_rows: list[np.ndarray] = []
    for table, table_parent in zip(tables, parents, strict=False):
        if table:
            proto_rows.append(np.stack(table).astype(np.int8))
            parent_rows.append(np.asarray(table_parent, dtype=np.int64))
        else:
            proto_rows.append(np.zeros((0, feature_dim), dtype=np.int8))
            parent_rows.append(np.zeros(0, dtype=np.int64))
    return proto_rows, parent_rows


def classify_global_plus_source_residual(
    *,
    features: np.ndarray,
    labels: np.ndarray,
    base_prototypes: np.ndarray,
    base_parent: np.ndarray,
    residual_tables: list[list[np.ndarray]],
    residual_parent: list[list[int]],
) -> tuple[np.ndarray, np.ndarray]:
    feature_dim = int(features.shape[1])
    residual_proto, residual_parent_rows = stack_residual_tables(
        residual_tables,
        residual_parent,
        feature_dim,
    )
    base_x = base_prototypes.astype(np.int32)
    base_parent = base_parent.astype(np.int64)
    pred = np.zeros(len(features), dtype=np.int64)
    margin = np.zeros(len(features), dtype=np.int64)
    for row_index, feature in enumerate(features.astype(np.int32)):
        source = int(labels[row_index])
        proto_parts = [base_x]
        parent_parts = [base_parent]
        if len(residual_proto[source]):
            proto_parts.append(residual_proto[source].astype(np.int32))
            parent_parts.append(residual_parent_rows[source].astype(np.int64))
        proto = np.concatenate(proto_parts, axis=0)
        proto_parent = np.concatenate(parent_parts, axis=0)
        dist = np.sum((proto - feature[None, :]) ** 2, axis=1).astype(np.int64)
        class_dist = np.full(3, np.iinfo(np.int64).max, dtype=np.int64)
        for cls in range(3):
            mask = proto_parent == cls
            if np.any(mask):
                class_dist[cls] = int(np.min(dist[mask]))
        order = np.argsort(class_dist)
        pred[row_index] = int(order[0])
        margin[row_index] = int(class_dist[order[1]] - class_dist[order[0]])
    return pred, margin


def compile_residual_tables(
    *,
    embeddings: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    labels: np.ndarray,
    source_count: int,
    base_subset: str,
    target_margin: int,
    max_iterations: int,
) -> tuple[np.ndarray, np.ndarray, list[list[np.ndarray]], list[list[int]], list[list[int]], list[list[str]], dict[str, Any]]:
    base_mask = subset_mask(view_labels, base_subset)
    base_prototypes = embeddings[base_mask].astype(np.int8)
    base_parent = parent[base_mask].astype(np.int64)
    residual_tables: list[list[np.ndarray]] = [[] for _ in range(source_count)]
    residual_parent: list[list[int]] = [[] for _ in range(source_count)]
    residual_sample: list[list[int]] = [[] for _ in range(source_count)]
    residual_view: list[list[str]] = [[] for _ in range(source_count)]
    added_rows: set[int] = set()
    trace: list[dict[str, Any]] = []
    for iteration in range(1, max_iterations + 1):
        pred, margin = classify_global_plus_source_residual(
            features=embeddings,
            labels=labels,
            base_prototypes=base_prototypes,
            base_parent=base_parent,
            residual_tables=residual_tables,
            residual_parent=residual_parent,
        )
        risk = np.where((pred != parent) | (margin <= target_margin))[0]
        if len(risk) == 0:
            break
        added = 0
        skipped_existing = 0
        for row_index in risk.tolist():
            if int(row_index) in added_rows:
                skipped_existing += 1
                continue
            source = int(labels[row_index])
            residual_tables[source].append(embeddings[row_index].copy())
            residual_parent[source].append(int(parent[row_index]))
            residual_sample[source].append(int(row_index))
            residual_view[source].append(str(view_labels[row_index]))
            added_rows.add(int(row_index))
            added += 1
        trace.append(
            {
                "iteration": int(iteration),
                "risk_before": int(len(risk)),
                "wrong_before": int(np.sum(pred != parent)),
                "low_margin_before": int(np.sum(margin <= target_margin)),
                "added": int(added),
                "skipped_existing": int(skipped_existing),
                "residual_table_sizes": [len(table) for table in residual_tables],
            }
        )
        if added == 0:
            break
    pred, margin = classify_global_plus_source_residual(
        features=embeddings,
        labels=labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )
    summary = {
        "iterations": int(len(trace)),
        "trace": trace,
        "all_correct": bool(np.all(pred == parent)),
        "margin_min": int(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "base_prototypes": int(len(base_prototypes)),
        "residual_table_sizes": [int(len(table)) for table in residual_tables],
        "max_source_residual": int(max(len(table) for table in residual_tables)),
        "total_stored_prototypes": int(len(base_prototypes) + sum(len(table) for table in residual_tables)),
        "runtime_effective_prototypes": int(len(base_prototypes) + max(len(table) for table in residual_tables)),
    }
    return base_prototypes, base_parent, residual_tables, residual_parent, residual_sample, residual_view, summary


def evaluate_stress_rows(
    *,
    stress_rows: list[dict[str, str]],
    features: np.ndarray,
    labels: np.ndarray,
    base_prototypes: np.ndarray,
    base_parent: np.ndarray,
    residual_tables: list[list[np.ndarray]],
    residual_parent: list[list[int]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    parent = np.asarray([int(row["parent"]) for row in stress_rows], dtype=np.int64)
    pred, margin = classify_global_plus_source_residual(
        features=features,
        labels=labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )
    out_rows: list[dict[str, Any]] = []
    for row, source, pred_value, margin_value in zip(stress_rows, labels.tolist(), pred.tolist(), margin.tolist(), strict=False):
        parent_value = int(row["parent"])
        out_rows.append(
            {
                "group": row["group"],
                "base_query_index": int(row["base_query_index"]),
                "sample_index": int(row["sample_index"]),
                "view_label": row["view_label"],
                "parent": parent_value,
                "perturb": row["perturb"],
                "perturb_family": row["perturb_family"],
                "source_label": int(source),
                "pred": int(pred_value),
                "wrong": bool(int(pred_value) != parent_value),
                "margin": int(margin_value),
            }
        )
    per_group = summarize_group(out_rows, ["group"])
    wrong_events = int(sum(1 for row in out_rows if bool(row["wrong"])))
    return (
        {
            "wrong_events": wrong_events,
            "total_events": int(len(out_rows)),
            "wrong_base_count": int(len({(row["group"], int(row["base_query_index"])) for row in out_rows if bool(row["wrong"])})),
            "high_pressure_low_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "low"), None),
            "high_pressure_control_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "control"), None),
            "per_group": per_group,
            "per_view_top20": summarize_group(out_rows, ["group", "view_label"])[:20],
            "per_perturb_top20": summarize_group(out_rows, ["group", "perturb_family", "perturb"])[:20],
            "source_counts": {str(source): int(np.sum(labels == source)) for source in sorted(set(labels.tolist()))},
        },
        out_rows,
    )


def oracle_stress_source_labels(
    *,
    stress_rows: list[dict[str, str]],
    source_events: list[tuple[str, Path]],
) -> tuple[np.ndarray, dict[str, Any]]:
    event_maps = {name: read_events(path) for name, path in source_events}
    source_order = [name for name, _path in source_events]
    labels: list[int] = []
    missing: list[tuple[str, int, str, int, str]] = []
    for row in stress_rows:
        key = event_key(row)
        parent = int(row["parent"])
        scores: list[float] = []
        for name in source_order:
            source_row = event_maps[name].get(key)
            if source_row is None:
                missing.append((key[0], key[1], key[2], key[3], name))
                scores.append(-1.0e9)
                continue
            pred = int(source_row["stress_pred"])
            margin = max(int(source_row["stress_margin"]), 0)
            score = float(np.log1p(margin)) if pred == parent else float(-1.0 - np.log1p(margin))
            scores.append(score)
        labels.append(int(np.argmax(np.asarray(scores, dtype=np.float32))))
    if missing:
        preview = ", ".join(f"{group}:{base}:{perturb}:{event}:{name}" for group, base, perturb, event, name in missing[:10])
        raise ValueError(f"missing source stress rows: {len(missing)}, first {preview}")
    labels_array = np.asarray(labels, dtype=np.int64)
    return labels_array, {
        "stress_source_label_mode": "highpressure_oracle_evaluation_only",
        "source_order": source_order,
        "source_counts": {name: int(np.sum(labels_array == index)) for index, name in enumerate(source_order)},
    }


def normal_teacher_source_labels(
    *,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    parent: np.ndarray,
    teacher_npz: Path,
    fallback_labels: np.ndarray | None,
    allow_missing: bool,
    allow_evaluation_only: bool,
) -> tuple[np.ndarray, dict[str, Any]]:
    with np.load(teacher_npz, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent", "source_label", "source_names", "high_pressure_usage"]
        missing_keys = [key for key in required if key not in data.files]
        if missing_keys:
            raise ValueError(f"{teacher_npz} missing source teacher arrays: {missing_keys}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_parent = np.asarray(data["parent"], dtype=np.int64)
        teacher_label = np.asarray(data["source_label"], dtype=np.int64)
        source_names = np.asarray(data["source_names"]).astype(str)
        high_pressure_usage = str(np.asarray(data["high_pressure_usage"]).item())
    if high_pressure_usage != "none" and not (allow_evaluation_only and "evaluation_only" in high_pressure_usage):
        raise ValueError(f"source labels must be normal-only/evaluation-only, got {high_pressure_usage}")
    by_key = {
        (int(sample), str(view)): (int(label), int(row_parent))
        for sample, view, label, row_parent in zip(
            teacher_sample.tolist(),
            teacher_view.tolist(),
            teacher_label.tolist(),
            teacher_parent.tolist(),
            strict=False,
        )
    }
    labels = np.full(len(sample_index), -1, dtype=np.int64)
    missing: list[tuple[int, str]] = []
    parent_mismatch: list[tuple[int, str, int, int]] = []
    for index, (sample, view, row_parent) in enumerate(
        zip(sample_index.tolist(), view_labels.tolist(), parent.tolist(), strict=False)
    ):
        found = by_key.get((int(sample), str(view)))
        if found is None:
            missing.append((int(sample), str(view)))
            continue
        label, teacher_parent_value = found
        if int(teacher_parent_value) != int(row_parent):
            parent_mismatch.append((int(sample), str(view), int(row_parent), int(teacher_parent_value)))
            continue
        labels[index] = int(label)
    if parent_mismatch:
        preview = ", ".join(
            f"{sample}:{view}:{row_parent}!={teacher_parent_value}"
            for sample, view, row_parent, teacher_parent_value in parent_mismatch[:10]
        )
        raise ValueError(f"normal source teacher parent mismatch: {len(parent_mismatch)}, first {preview}")
    if missing and fallback_labels is None and not allow_missing:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing[:10])
        raise ValueError(f"normal source teacher missing rows: {len(missing)}, first {preview}")
    if fallback_labels is not None:
        labels[labels < 0] = fallback_labels[labels < 0]
    if np.any(labels < 0):
        preview_indexes = np.flatnonzero(labels < 0)[:10].tolist()
        raise ValueError(f"normal source labels still missing: {int(np.sum(labels < 0))}, first indexes {preview_indexes}")
    return labels.astype(np.int64), {
        "normal_source_label_mode": "teacher_npz",
        "normal_source_teacher_npz": str(teacher_npz),
        "normal_source_teacher_high_pressure_usage": high_pressure_usage,
        "normal_source_teacher_allow_missing": bool(allow_missing),
        "normal_source_teacher_fallback_gate": fallback_labels is not None,
        "normal_source_teacher_missing_rows": int(len(missing)),
        "normal_source_counts": {
            str(source_names[int(label)]): int(np.sum(labels == label))
            for label in sorted(set(labels.tolist()))
            if int(label) >= 0 and int(label) < len(source_names)
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate a global V8 prototype table plus source-gated residual tables."
    )
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--train-config", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--gate-start", type=int, default=3)
    parser.add_argument("--source-count", type=int, default=5)
    parser.add_argument("--base-subset", choices=["clean", "clean_rotmirror"], default="clean")
    parser.add_argument("--target-margin", type=int, default=0)
    parser.add_argument("--max-iterations", type=int, default=12)
    parser.add_argument("--normal-source-gate-teacher-npz", type=Path, default=None)
    parser.add_argument("--allow-missing-normal-source-labels", action="store_true")
    parser.add_argument("--normal-source-label-fallback-gate", action="store_true")
    parser.add_argument("--allow-evaluation-only-normal-teacher", action="store_true")
    parser.add_argument(
        "--stress-source-events",
        action="append",
        default=[],
        help="Optional evaluation-only source stress rows, name=stress_events.csv. If provided, high-pressure labels use oracle source selection.",
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base = load_npz(args.params_npz)
    config = json.loads(args.train_config.read_text(encoding="utf-8"))
    shutil.copy2(args.train_config, args.output_dir / "train_config.json")

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    gate_labels, gate_gap = source_labels(embeddings, args.gate_start, args.source_count)
    normal_label_info: dict[str, Any] = {
        "normal_source_label_mode": "gate_output",
        "normal_source_counts": {str(source): int(np.sum(gate_labels == source)) for source in sorted(set(gate_labels.tolist()))},
    }
    if args.normal_source_gate_teacher_npz is not None:
        labels, normal_label_info = normal_teacher_source_labels(
            sample_index=sample_index,
            view_labels=view_labels,
            parent=parent,
            teacher_npz=args.normal_source_gate_teacher_npz,
            fallback_labels=gate_labels if args.normal_source_label_fallback_gate else None,
            allow_missing=bool(args.allow_missing_normal_source_labels),
            allow_evaluation_only=bool(args.allow_evaluation_only_normal_teacher),
        )
    else:
        labels = gate_labels
    (
        base_prototypes,
        base_parent,
        residual_tables,
        residual_parent,
        _residual_sample,
        _residual_view,
        compile_summary,
    ) = compile_residual_tables(
        embeddings=embeddings,
        parent=parent,
        view_labels=view_labels,
        labels=labels,
        source_count=args.source_count,
        base_subset=args.base_subset,
        target_margin=args.target_margin,
        max_iterations=args.max_iterations,
    )
    pred, margin = classify_global_plus_source_residual(
        features=embeddings,
        labels=labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )
    runtime_effective_prototypes = int(compile_summary["runtime_effective_prototypes"])
    feature_dim = int(embeddings.shape[1])
    estimated_distance_macs = int(runtime_effective_prototypes * feature_dim)
    backbone_conservative = calibrated_conservative_us(config)
    table_us = max(20.0, float(estimated_distance_macs) * 0.02)
    board_total_conservative = backbone_conservative + table_us
    view_order = unique_order(view_labels)
    normal_row: dict[str, Any] = {
        "stage": "v8_source_gated_residual_table",
        "name": f"source_gated_residual_{args.base_subset}_t{args.target_margin}",
        "prototype_source": "global_base_plus_source_residual",
        "feature_dim": feature_dim,
        "base_prototypes": int(len(base_prototypes)),
        "max_source_residual": int(compile_summary["max_source_residual"]),
        "runtime_effective_prototypes": runtime_effective_prototypes,
        "total_stored_prototypes": int(compile_summary["total_stored_prototypes"]),
        "prototype_count": runtime_effective_prototypes,
        "estimated_distance_macs": estimated_distance_macs,
        "int8_margin_min": int(np.min(margin)),
        "int8_margin_mean": float(np.mean(margin)),
        "residual_table_sizes_json": json.dumps(compile_summary["residual_table_sizes"]),
        "gate_gap_min": int(np.min(gate_gap)),
        "gate_gap_p05": float(np.percentile(gate_gap, 5)),
        "gate_gap_median": float(np.median(gate_gap)),
        "board_backbone_conservative_us": int(round(backbone_conservative)),
        "board_table_worst_us": int(round(table_us)),
        "board_total_conservative_us": int(round(board_total_conservative)),
        "under_2ms_conservative": bool(board_total_conservative <= 2000.0),
    }
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=pred))
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=pred, prefix="int8_"))

    stress_rows = read_csv_rows(args.stress_events_csv)
    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_label_info: dict[str, Any]
    if args.stress_source_events:
        stress_labels, stress_label_info = oracle_stress_source_labels(
            stress_rows=stress_rows,
            source_events=[parse_named_path(item) for item in args.stress_source_events],
        )
        stress_gate_gap = np.zeros(len(stress_labels), dtype=np.int64)
    else:
        stress_labels, stress_gate_gap = source_labels(stress_features, args.gate_start, args.source_count)
        stress_label_info = {
            "stress_source_label_mode": "normal_gate_output",
            "source_counts": {str(source): int(np.sum(stress_labels == source)) for source in sorted(set(stress_labels.tolist()))},
        }
    stress_summary, stress_out = evaluate_stress_rows(
        stress_rows=stress_rows,
        features=stress_features,
        labels=stress_labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )

    write_csv(args.output_dir / "candidate_results.csv", [normal_row])
    write_csv(args.output_dir / "stress_events.csv", stress_out)
    write_json(
        args.output_dir / "summary.json",
        {
            "params_npz": str(args.params_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_gate_output_only"
            if not args.stress_source_events
            else "normal_compile_gate_output_plus_highpressure_oracle_label_diagnostic",
            "gate_start": int(args.gate_start),
            "source_count": int(args.source_count),
            "base_subset": args.base_subset,
            "target_margin": int(args.target_margin),
            "normal_source_labels": normal_label_info,
            "compile": compile_summary,
            "normal": normal_row,
            "stress": {
                **stress_summary,
                **stress_label_info,
                "gate_gap_min": int(np.min(stress_gate_gap)),
                "gate_gap_p05": float(np.percentile(stress_gate_gap, 5)),
                "gate_gap_median": float(np.median(stress_gate_gap)),
            },
        },
    )
    print(json.dumps({"normal": normal_row, "stress": stress_summary}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
