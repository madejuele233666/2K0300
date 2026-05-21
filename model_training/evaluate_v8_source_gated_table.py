import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from estimate_v8_board_time import calibrated_conservative_us
from evaluate_v8_embedding_prototypes import metric_summary, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def unique_order(values: np.ndarray) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for value in values.astype(str).tolist():
        if value not in seen:
            out.append(str(value))
            seen.add(str(value))
    return out


def source_labels(features: np.ndarray, gate_start: int, source_count: int) -> tuple[np.ndarray, np.ndarray]:
    gate = features[:, gate_start : gate_start + source_count].astype(np.int32)
    if gate.shape[1] != source_count:
        raise ValueError(f"gate slice has shape {gate.shape}, expected {source_count} columns")
    order = np.argsort(gate, axis=1)
    labels = order[:, -1].astype(np.int64)
    gap = (gate[np.arange(len(gate)), order[:, -1]] - gate[np.arange(len(gate)), order[:, -2]]).astype(np.int64)
    return labels, gap


def init_source_tables(
    *,
    embeddings: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    labels: np.ndarray,
    source_count: int,
) -> tuple[list[list[np.ndarray]], list[list[int]], list[list[int]], list[list[str]]]:
    tables: list[list[np.ndarray]] = [[] for _ in range(source_count)]
    table_parent: list[list[int]] = [[] for _ in range(source_count)]
    table_sample: list[list[int]] = [[] for _ in range(source_count)]
    table_view: list[list[str]] = [[] for _ in range(source_count)]
    clean = view_labels.astype(str) == "clean"
    sample_index = np.arange(len(embeddings), dtype=np.int64)
    for source in range(source_count):
        for cls in range(3):
            candidates = np.where(clean & (labels == source) & (parent == cls))[0]
            if len(candidates) == 0:
                candidates = np.where(clean & (parent == cls))[0]
            if len(candidates) == 0:
                raise ValueError(f"no clean candidate for source={source} parent={cls}")
            # One deterministic seed prototype per parent/source keeps the table valid from the first pass.
            chosen = int(candidates[0])
            tables[source].append(embeddings[chosen].copy())
            table_parent[source].append(int(parent[chosen]))
            table_sample[source].append(int(sample_index[chosen]))
            table_view[source].append(str(view_labels[chosen]))
    return tables, table_parent, table_sample, table_view


def stack_tables(
    tables: list[list[np.ndarray]],
    parents: list[list[int]],
) -> tuple[list[np.ndarray], list[np.ndarray]]:
    proto_rows: list[np.ndarray] = []
    parent_rows: list[np.ndarray] = []
    for table, table_parent in zip(tables, parents, strict=False):
        if table:
            proto_rows.append(np.stack(table).astype(np.int8))
            parent_rows.append(np.asarray(table_parent, dtype=np.int64))
        else:
            proto_rows.append(np.zeros((0, 0), dtype=np.int8))
            parent_rows.append(np.zeros(0, dtype=np.int64))
    return proto_rows, parent_rows


def classify_source_gated(
    *,
    features: np.ndarray,
    true_parent: np.ndarray,
    labels: np.ndarray,
    tables: list[list[np.ndarray]],
    table_parent: list[list[int]],
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    proto_rows, parent_rows = stack_tables(tables, table_parent)
    pred = np.zeros(len(features), dtype=np.int64)
    margin = np.zeros(len(features), dtype=np.int64)
    class_dist_all = np.full((len(features), 3), np.iinfo(np.int64).max, dtype=np.int64)
    for row_index, feature in enumerate(features.astype(np.int32)):
        source = int(labels[row_index])
        proto = proto_rows[source].astype(np.int32)
        proto_parent = parent_rows[source]
        if len(proto) == 0:
            raise ValueError(f"empty source table: {source}")
        dist = np.sum((proto - feature[None, :]) ** 2, axis=1).astype(np.int64)
        class_dist = np.full(3, np.iinfo(np.int64).max, dtype=np.int64)
        for cls in range(3):
            mask = proto_parent == cls
            if np.any(mask):
                class_dist[cls] = int(np.min(dist[mask]))
        order = np.argsort(class_dist)
        pred[row_index] = int(order[0])
        margin[row_index] = int(class_dist[order[1]] - class_dist[order[0]])
        class_dist_all[row_index] = class_dist
    return pred, margin, class_dist_all


def compile_source_tables(
    *,
    embeddings: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    labels: np.ndarray,
    source_count: int,
    target_margin: int,
    max_iterations: int,
) -> tuple[list[list[np.ndarray]], list[list[int]], list[list[int]], list[list[str]], dict[str, Any]]:
    tables, table_parent, table_sample, table_view = init_source_tables(
        embeddings=embeddings,
        parent=parent,
        view_labels=view_labels,
        labels=labels,
        source_count=source_count,
    )
    trace: list[dict[str, Any]] = []
    for iteration in range(1, max_iterations + 1):
        pred, margin, _class_dist = classify_source_gated(
            features=embeddings,
            true_parent=parent,
            labels=labels,
            tables=tables,
            table_parent=table_parent,
        )
        risk = np.where((pred != parent) | (margin <= target_margin))[0]
        if len(risk) == 0:
            break
        added = 0
        for row_index in risk.tolist():
            source = int(labels[row_index])
            tables[source].append(embeddings[row_index].copy())
            table_parent[source].append(int(parent[row_index]))
            table_sample[source].append(int(row_index))
            table_view[source].append(str(view_labels[row_index]))
            added += 1
        trace.append(
            {
                "iteration": int(iteration),
                "risk_before": int(len(risk)),
                "wrong_before": int(np.sum(pred != parent)),
                "low_margin_before": int(np.sum(margin <= target_margin)),
                "added": int(added),
                "table_sizes": [len(table) for table in tables],
            }
        )
    pred, margin, _class_dist = classify_source_gated(
        features=embeddings,
        true_parent=parent,
        labels=labels,
        tables=tables,
        table_parent=table_parent,
    )
    summary = {
        "iterations": int(len(trace)),
        "trace": trace,
        "all_correct": bool(np.all(pred == parent)),
        "margin_min": int(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "table_sizes": [int(len(table)) for table in tables],
        "total_prototypes": int(sum(len(table) for table in tables)),
        "max_source_prototypes": int(max(len(table) for table in tables)),
    }
    return tables, table_parent, table_sample, table_view, summary


def summarize_group(rows: list[dict[str, Any]], keys: list[str]) -> list[dict[str, Any]]:
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for row in rows:
        groups.setdefault(tuple(row.get(key) for key in keys), []).append(row)
    out: list[dict[str, Any]] = []
    for values, items in groups.items():
        total = len(items)
        wrong = sum(1 for item in items if bool(item["wrong"]))
        margins = np.asarray([int(item["margin"]) for item in items], dtype=np.int64)
        out.append(
            {
                **{key: value for key, value in zip(keys, values, strict=False)},
                "total": int(total),
                "wrong": int(wrong),
                "accuracy": float((total - wrong) / max(total, 1)),
                "wrong_rate": float(wrong / max(total, 1)),
                "margin_min": int(np.min(margins)),
                "margin_p05": float(np.percentile(margins, 5)),
                "margin_median": float(np.median(margins)),
            }
        )
    return sorted(out, key=lambda row: (row["accuracy"], -row["total"], str(row.get(keys[0], ""))))


def evaluate_stress_rows(
    *,
    stress_rows: list[dict[str, str]],
    features: np.ndarray,
    labels: np.ndarray,
    tables: list[list[np.ndarray]],
    table_parent: list[list[int]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    parent = np.asarray([int(row["parent"]) for row in stress_rows], dtype=np.int64)
    pred, margin, _class_dist = classify_source_gated(
        features=features,
        true_parent=parent,
        labels=labels,
        tables=tables,
        table_parent=table_parent,
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


def save_payload(
    *,
    path: Path,
    base: dict[str, np.ndarray],
    tables: list[list[np.ndarray]],
    table_parent: list[list[int]],
    table_sample: list[list[int]],
    table_view: list[list[str]],
    labels: np.ndarray,
    margin: np.ndarray,
    pred: np.ndarray,
    gate_start: int,
    source_count: int,
) -> None:
    payload = {key: value for key, value in base.items() if not key.startswith("prototype_") and key not in {"prototypes", "prototypes_int8"}}
    prototypes = np.concatenate([np.stack(table) for table in tables], axis=0).astype(np.int8)
    parents = np.concatenate([np.asarray(row, dtype=np.int64) for row in table_parent], axis=0)
    source = np.concatenate([
        np.full(len(table), source_index, dtype=np.int64)
        for source_index, table in enumerate(tables)
    ])
    sample = np.concatenate([np.asarray(row, dtype=np.int64) for row in table_sample], axis=0)
    view = np.concatenate([np.asarray(row).astype(str) for row in table_view], axis=0)
    payload.update(
        {
            "pred": pred.astype(np.int64),
            "int8_pred": pred.astype(np.int64),
            "margin": margin.astype(np.float32),
            "int8_margin": margin.astype(np.int64),
            "source_gate_label": labels.astype(np.int64),
            "source_gate_start": np.asarray(gate_start, dtype=np.int64),
            "source_count": np.asarray(source_count, dtype=np.int64),
            "prototypes_int8": prototypes,
            "prototypes": prototypes.astype(np.float32),
            "prototype_parent": parents,
            "prototype_source_gate": source,
            "prototype_sample_index": sample,
            "prototype_view_label": view,
            "prototype_source_kind": np.asarray(["source_gated_table"] * len(prototypes)).astype(str),
            "prototype_cluster": np.arange(len(prototypes), dtype=np.int64),
        }
    )
    np.savez_compressed(path, **payload)


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate a normal-only source-gated V8 prototype table.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--train-config", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--gate-start", type=int, default=3)
    parser.add_argument("--source-count", type=int, default=4)
    parser.add_argument("--target-margin", type=int, default=0)
    parser.add_argument("--max-iterations", type=int, default=12)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base = load_npz(args.params_npz)
    config = json.loads(args.train_config.read_text(encoding="utf-8"))
    shutil.copy2(args.train_config, args.output_dir / "train_config.json")

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    labels, gate_gap = source_labels(embeddings, args.gate_start, args.source_count)
    tables, table_parent, table_sample, table_view, compile_summary = compile_source_tables(
        embeddings=embeddings,
        parent=parent,
        view_labels=view_labels,
        labels=labels,
        source_count=args.source_count,
        target_margin=args.target_margin,
        max_iterations=args.max_iterations,
    )
    pred, margin, _class_dist = classify_source_gated(
        features=embeddings,
        true_parent=parent,
        labels=labels,
        tables=tables,
        table_parent=table_parent,
    )
    view_order = unique_order(view_labels)
    normal_row: dict[str, Any] = {
        "stage": "v8_source_gated_table",
        "name": f"source_gated_t{args.target_margin}",
        "prototype_source": "source_gated_residual",
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(sum(len(table) for table in tables)),
        "max_source_prototypes": int(max(len(table) for table in tables)),
        "estimated_distance_macs": int(max(len(table) for table in tables) * embeddings.shape[1]),
        "int8_margin_min": int(np.min(margin)),
        "int8_margin_mean": float(np.mean(margin)),
        "source_table_sizes_json": json.dumps([len(table) for table in tables]),
        "gate_gap_min": int(np.min(gate_gap)),
        "gate_gap_p05": float(np.percentile(gate_gap, 5)),
        "gate_gap_median": float(np.median(gate_gap)),
    }
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=pred))
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=pred, prefix="int8_"))

    stress_rows = read_csv_rows(args.stress_events_csv)
    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_labels, stress_gate_gap = source_labels(stress_features, args.gate_start, args.source_count)
    stress_summary, stress_out = evaluate_stress_rows(
        stress_rows=stress_rows,
        features=stress_features,
        labels=stress_labels,
        tables=tables,
        table_parent=table_parent,
    )

    backbone_conservative = calibrated_conservative_us(config)
    table_us = max(20.0, float(normal_row["estimated_distance_macs"]) * 0.02)
    board_total_conservative = backbone_conservative + table_us
    normal_row.update(
        {
            "board_backbone_conservative_us": int(round(backbone_conservative)),
            "board_table_worst_us": int(round(table_us)),
            "board_total_conservative_us": int(round(board_total_conservative)),
            "under_2ms_conservative": bool(board_total_conservative <= 2000.0),
        }
    )
    write_csv(args.output_dir / "candidate_results.csv", [normal_row])
    write_csv(args.output_dir / "stress_events.csv", stress_out)
    save_payload(
        path=args.output_dir / "source_gated_parent_logits_params.npz",
        base=base,
        tables=tables,
        table_parent=table_parent,
        table_sample=table_sample,
        table_view=table_view,
        labels=labels,
        margin=margin,
        pred=pred,
        gate_start=args.gate_start,
        source_count=args.source_count,
    )
    write_json(
        args.output_dir / "summary.json",
        {
            "params_npz": str(args.params_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_gate_output_only",
            "gate_start": int(args.gate_start),
            "source_count": int(args.source_count),
            "target_margin": int(args.target_margin),
            "compile": compile_summary,
            "normal": normal_row,
            "stress": {
                **stress_summary,
                "gate_gap_min": int(np.min(stress_gate_gap)),
                "gate_gap_p05": float(np.percentile(stress_gate_gap, 5)),
                "gate_gap_median": float(np.median(stress_gate_gap)),
            },
        },
    )
    print(json.dumps({"normal": normal_row, "stress": stress_summary}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
