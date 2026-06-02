import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np


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


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def key_map(sample_index: np.ndarray, view_labels: np.ndarray) -> dict[tuple[int, str], int]:
    return {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist(), strict=False))
    }


def append_base_rows(path: Path, rows: list[dict[str, Any]], weight_scale: float) -> int:
    if path is None:
        return 0
    base = load_npz(path)
    required = [
        "sample_index",
        "view_labels",
        "correct_proto_sample",
        "correct_proto_view",
        "wrong_proto_sample",
        "wrong_proto_view",
        "weight",
    ]
    missing = [key for key in required if key not in base]
    if missing:
        raise ValueError(f"{path} missing base dynamic pair arrays: {missing}")
    count = len(base["weight"])
    for index in range(count):
        rows.append(
            {
                "event_type": "base_dynamic_qpair",
                "sample_index": int(np.asarray(base["sample_index"], dtype=np.int64)[index]),
                "view_label": str(np.asarray(base["view_labels"]).astype(str)[index]),
                "correct_proto_sample": int(np.asarray(base["correct_proto_sample"], dtype=np.int64)[index]),
                "correct_proto_view": str(np.asarray(base["correct_proto_view"]).astype(str)[index]),
                "wrong_proto_sample": int(np.asarray(base["wrong_proto_sample"], dtype=np.int64)[index]),
                "wrong_proto_view": str(np.asarray(base["wrong_proto_view"]).astype(str)[index]),
                "weight": float(np.asarray(base["weight"], dtype=np.float32)[index]) * float(weight_scale),
            }
        )
    return count


def nearest_index(query: np.ndarray, candidates: np.ndarray, embeddings: np.ndarray) -> tuple[int, int]:
    if len(candidates) == 0:
        return -1, 0
    diff = embeddings[candidates].astype(np.int32) - query.astype(np.int32)[None, :]
    dist = np.sum(diff * diff, axis=1).astype(np.int64)
    local = int(np.argmin(dist))
    return int(candidates[local]), int(dist[local])


def build_source_choice_pair_teacher(
    *,
    reference_params_npz: Path,
    source_gate_teacher_npz: Path,
    output_dir: Path,
    base_dynamic_qpair_npz: Path | None,
    base_weight_scale: float,
    source_weight: float,
    max_source_weight: float,
    max_events: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    reference = load_npz(reference_params_npz)
    teacher = load_npz(source_gate_teacher_npz)
    ref_sample = np.asarray(reference["sample_index"], dtype=np.int64)
    ref_view = np.asarray(reference["view_labels"]).astype(str)
    ref_parent = np.asarray(reference["parent"], dtype=np.int64)
    ref_embedding = np.asarray(reference["embedding_int8"], dtype=np.int8)
    ref_map = key_map(ref_sample, ref_view)

    teacher_sample = np.asarray(teacher["sample_index"], dtype=np.int64)
    teacher_view = np.asarray(teacher["view_labels"]).astype(str)
    teacher_parent = np.asarray(teacher["parent"], dtype=np.int64)
    source_label = np.asarray(teacher["source_label"], dtype=np.int64)
    weight_key = "weights" if "weights" in teacher else "weight"
    teacher_weight = np.asarray(teacher[weight_key], dtype=np.float32)
    source_names = np.asarray(teacher.get("source_names", np.asarray([]))).astype(str)

    row_to_teacher: dict[int, int] = {}
    missing = 0
    parent_mismatch = 0
    for teacher_index, key in enumerate(zip(teacher_sample.tolist(), teacher_view.tolist(), strict=False)):
        ref_index = ref_map.get((int(key[0]), str(key[1])))
        if ref_index is None:
            missing += 1
            continue
        if int(ref_parent[ref_index]) != int(teacher_parent[teacher_index]):
            parent_mismatch += 1
            continue
        row_to_teacher[int(ref_index)] = int(teacher_index)
    if parent_mismatch:
        raise ValueError(f"parent mismatch between reference and source teacher: {parent_mismatch}")
    if not row_to_teacher:
        raise ValueError("no aligned source-teacher rows")

    by_parent_source: dict[tuple[int, int], list[int]] = defaultdict(list)
    by_parent: dict[int, list[int]] = defaultdict(list)
    for ref_index in row_to_teacher:
        teacher_index = row_to_teacher[ref_index]
        if float(teacher_weight[teacher_index]) <= 0.0:
            continue
        parent = int(ref_parent[ref_index])
        source = int(source_label[teacher_index])
        by_parent_source[(parent, source)].append(ref_index)
        by_parent[parent].append(ref_index)

    rows: list[dict[str, Any]] = []
    base_count = append_base_rows(base_dynamic_qpair_npz, rows, base_weight_scale) if base_dynamic_qpair_npz else 0
    skipped_no_positive = 0
    skipped_no_negative = 0
    source_rows = 0
    source_counts = Counter()
    parent_counts = Counter()
    for ref_index in sorted(row_to_teacher):
        teacher_index = row_to_teacher[ref_index]
        weight = float(teacher_weight[teacher_index])
        if weight <= 0.0:
            continue
        parent = int(ref_parent[ref_index])
        source = int(source_label[teacher_index])
        same = np.asarray(by_parent_source.get((parent, source), []), dtype=np.int64)
        same = same[same != ref_index]
        different = np.asarray(
            [candidate for candidate in by_parent.get(parent, []) if int(source_label[row_to_teacher[candidate]]) != source],
            dtype=np.int64,
        )
        if len(same) == 0:
            skipped_no_positive += 1
            continue
        if len(different) == 0:
            skipped_no_negative += 1
            continue
        query = ref_embedding[ref_index]
        positive_index, positive_dist = nearest_index(query, same, ref_embedding)
        negative_index, negative_dist = nearest_index(query, different, ref_embedding)
        if positive_index < 0 or negative_index < 0:
            continue
        scaled_weight = float(weight) * float(source_weight)
        if max_source_weight > 0:
            scaled_weight = min(scaled_weight, float(max_source_weight))
        rows.append(
            {
                "event_type": "source_choice_pair",
                "sample_index": int(ref_sample[ref_index]),
                "view_label": str(ref_view[ref_index]),
                "parent": parent,
                "source_label": source,
                "source_name": str(source_names[source]) if len(source_names) > source else str(source),
                "positive_dist": int(positive_dist),
                "negative_dist": int(negative_dist),
                "correct_proto_sample": int(ref_sample[positive_index]),
                "correct_proto_view": str(ref_view[positive_index]),
                "wrong_proto_sample": int(ref_sample[negative_index]),
                "wrong_proto_view": str(ref_view[negative_index]),
                "weight": float(scaled_weight),
            }
        )
        source_rows += 1
        source_counts[str(source)] += 1
        parent_counts[str(parent)] += 1

    if max_events > 0 and len(rows) > max_events:
        base_rows = [row for row in rows if row["event_type"] == "base_dynamic_qpair"]
        source_pair_rows = [row for row in rows if row["event_type"] == "source_choice_pair"]
        source_pair_rows.sort(
            key=lambda row: (
                int(row.get("negative_dist", 0)) - int(row.get("positive_dist", 0)),
                -float(row["weight"]),
                int(row["sample_index"]),
                str(row["view_label"]),
            )
        )
        rows = base_rows + source_pair_rows[: max(0, int(max_events) - len(base_rows))]

    write_csv(output_dir / "source_choice_pair_events.csv", rows)
    np.savez_compressed(
        output_dir / "source_choice_pair_teacher.npz",
        sample_index=np.asarray([row["sample_index"] for row in rows], dtype=np.int64),
        view_labels=np.asarray([row["view_label"] for row in rows]).astype(str),
        correct_proto_sample=np.asarray([row["correct_proto_sample"] for row in rows], dtype=np.int64),
        correct_proto_view=np.asarray([row["correct_proto_view"] for row in rows]).astype(str),
        wrong_proto_sample=np.asarray([row["wrong_proto_sample"] for row in rows], dtype=np.int64),
        wrong_proto_view=np.asarray([row["wrong_proto_view"] for row in rows]).astype(str),
        weight=np.asarray([row["weight"] for row in rows], dtype=np.float32),
        event_type=np.asarray([row["event_type"] for row in rows]).astype(str),
        high_pressure_usage=np.asarray("none"),
        reference_params_npz=np.asarray(str(reference_params_npz)),
        source_gate_teacher_npz=np.asarray(str(source_gate_teacher_npz)),
        base_dynamic_qpair_npz=np.asarray(str(base_dynamic_qpair_npz) if base_dynamic_qpair_npz else ""),
    )
    weights = np.asarray([row["weight"] for row in rows], dtype=np.float32)
    write_json(
        output_dir / "summary.json",
        {
            "reference_params_npz": str(reference_params_npz),
            "source_gate_teacher_npz": str(source_gate_teacher_npz),
            "base_dynamic_qpair_npz": str(base_dynamic_qpair_npz) if base_dynamic_qpair_npz else None,
            "output": str(output_dir / "source_choice_pair_teacher.npz"),
            "high_pressure_usage": "none",
            "aligned_rows": int(len(row_to_teacher)),
            "missing_teacher_rows": int(missing),
            "base_dynamic_rows": int(base_count),
            "source_choice_rows_before_cap": int(source_rows),
            "total_rows": int(len(rows)),
            "skipped_no_positive": int(skipped_no_positive),
            "skipped_no_negative": int(skipped_no_negative),
            "source_counts": dict(source_counts),
            "parent_counts": dict(parent_counts),
            "weight_min": float(np.min(weights)) if len(weights) else 0.0,
            "weight_mean": float(np.mean(weights)) if len(weights) else 0.0,
            "weight_max": float(np.max(weights)) if len(weights) else 0.0,
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build normal-only source-choice dynamic pair constraints.")
    parser.add_argument("--reference-params-npz", type=Path, required=True)
    parser.add_argument("--source-gate-teacher-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--base-dynamic-qpair-npz", type=Path, default=None)
    parser.add_argument("--base-weight-scale", type=float, default=1.0)
    parser.add_argument("--source-weight", type=float, default=1.0)
    parser.add_argument("--max-source-weight", type=float, default=4.0)
    parser.add_argument("--max-events", type=int, default=0)
    args = parser.parse_args()
    build_source_choice_pair_teacher(
        reference_params_npz=args.reference_params_npz,
        source_gate_teacher_npz=args.source_gate_teacher_npz,
        output_dir=args.output_dir,
        base_dynamic_qpair_npz=args.base_dynamic_qpair_npz,
        base_weight_scale=args.base_weight_scale,
        source_weight=args.source_weight,
        max_source_weight=args.max_source_weight,
        max_events=args.max_events,
    )


if __name__ == "__main__":
    main()
