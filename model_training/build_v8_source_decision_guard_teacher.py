import argparse
import csv
import json
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


def row_key_map(payload: dict[str, np.ndarray]) -> dict[tuple[int, str], int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    views = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_index), str(view)): int(index)
        for index, (sample_index, view) in enumerate(zip(sample.tolist(), views.tolist(), strict=False))
    }


def build_guard_teacher(
    *,
    base_npz: Path,
    source_decision_npz: Path,
    output_dir: Path,
    max_rows: int,
    max_base_margin: int,
    min_support: int,
    min_weight: float,
) -> None:
    base = load_npz(base_npz)
    teacher = load_npz(source_decision_npz)
    base_by_key = row_key_map(base)
    base_parent = np.asarray(base["parent"], dtype=np.int64)
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)
    teacher_sample = np.asarray(teacher["sample_index"], dtype=np.int64)
    teacher_view = np.asarray(teacher["view_labels"]).astype(str)
    teacher_parent = np.asarray(teacher["parent"], dtype=np.int64)
    teacher_wrong = np.asarray(teacher["wrong_parent"], dtype=np.int64)
    support = np.asarray(teacher["support"], dtype=np.int64)
    normalized_margin = np.asarray(teacher["normalized_aggregate_margin"], dtype=np.float32)
    target_margin = np.asarray(teacher["target_margin"], dtype=np.float32)
    weight = np.asarray(teacher["weight"], dtype=np.float32)

    rows: list[dict[str, Any]] = []
    skipped = {
        "missing_base_row": 0,
        "parent_mismatch": 0,
        "base_margin_gt_max": 0,
        "support_lt_min": 0,
        "weight_lt_min": 0,
    }
    for teacher_index, (sample_index, view_label) in enumerate(zip(teacher_sample.tolist(), teacher_view.tolist(), strict=False)):
        base_index = base_by_key.get((int(sample_index), str(view_label)))
        if base_index is None:
            skipped["missing_base_row"] += 1
            continue
        if int(base_parent[base_index]) != int(teacher_parent[teacher_index]):
            skipped["parent_mismatch"] += 1
            continue
        if max_base_margin >= 0 and int(base_margin[base_index]) > int(max_base_margin):
            skipped["base_margin_gt_max"] += 1
            continue
        if int(support[teacher_index]) < int(min_support):
            skipped["support_lt_min"] += 1
            continue
        if float(weight[teacher_index]) < float(min_weight):
            skipped["weight_lt_min"] += 1
            continue
        # Lower base margin and lower aggregate source margin are the rows most
        # likely to need a local guard, while high support keeps the source signal
        # from being a single-source artifact.
        order_key = (
            int(base_margin[base_index]),
            float(normalized_margin[teacher_index]),
            -int(support[teacher_index]),
            -float(weight[teacher_index]),
            int(base_index),
        )
        rows.append(
            {
                "order_key": order_key,
                "query_index": int(base_index),
                "sample_index": int(sample_index),
                "view_label": str(view_label),
                "parent": int(teacher_parent[teacher_index]),
                "teacher_wrong_parent": int(teacher_wrong[teacher_index]),
                "teacher_vote_count": int(support[teacher_index]),
                "teacher_margin_mean": float(normalized_margin[teacher_index] * float(target_margin[teacher_index])),
                "student_int8_margin": int(base_margin[base_index]),
                "weight": float(weight[teacher_index]),
                "normalized_aggregate_margin": float(normalized_margin[teacher_index]),
                "target_margin": float(target_margin[teacher_index]),
            }
        )
    rows.sort(key=lambda row: row["order_key"])
    if max_rows > 0:
        rows = rows[: int(max_rows)]
    if not rows:
        raise ValueError("no guard rows left after filtering")

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "source_decision_guard_teacher.npz",
        query_index=np.asarray([row["query_index"] for row in rows], dtype=np.int64),
        sample_index=np.asarray([row["sample_index"] for row in rows], dtype=np.int64),
        view_labels=np.asarray([row["view_label"] for row in rows]).astype(str),
        parent=np.asarray([row["parent"] for row in rows], dtype=np.int64),
        teacher_wrong_parent=np.asarray([row["teacher_wrong_parent"] for row in rows], dtype=np.int64),
        teacher_vote_count=np.asarray([row["teacher_vote_count"] for row in rows], dtype=np.int64),
        teacher_margin_mean=np.asarray([row["teacher_margin_mean"] for row in rows], dtype=np.float32),
        student_int8_margin=np.asarray([row["student_int8_margin"] for row in rows], dtype=np.int64),
        weight=np.asarray([row["weight"] for row in rows], dtype=np.float32),
        normalized_aggregate_margin=np.asarray([row["normalized_aggregate_margin"] for row in rows], dtype=np.float32),
        target_margin=np.asarray([row["target_margin"] for row in rows], dtype=np.float32),
        source_decision_npz=np.asarray(str(source_decision_npz)),
        base_npz=np.asarray(str(base_npz)),
        high_pressure_usage=np.asarray("none"),
    )
    csv_rows = [{key: value for key, value in row.items() if key != "order_key"} for row in rows]
    write_csv(output_dir / "source_decision_guard_rows.csv", csv_rows)
    summary = {
        "output": str(output_dir / "source_decision_guard_teacher.npz"),
        "base_npz": str(base_npz),
        "source_decision_npz": str(source_decision_npz),
        "high_pressure_usage": "none",
        "row_count": int(len(rows)),
        "input_row_count": int(len(teacher_sample)),
        "max_rows": int(max_rows),
        "max_base_margin": int(max_base_margin),
        "min_support": int(min_support),
        "min_weight": float(min_weight),
        "skipped": skipped,
        "student_int8_margin_min": int(min(row["student_int8_margin"] for row in rows)),
        "student_int8_margin_p50": float(np.percentile([row["student_int8_margin"] for row in rows], 50)),
        "student_int8_margin_max": int(max(row["student_int8_margin"] for row in rows)),
        "support_counts": {
            str(value): int(sum(1 for row in rows if row["teacher_vote_count"] == value))
            for value in sorted({int(row["teacher_vote_count"]) for row in rows})
        },
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a normal-only source-decision teacher into region-guard teacher rows."
    )
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--source-decision-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--max-rows", type=int, default=0)
    parser.add_argument("--max-base-margin", type=int, default=256)
    parser.add_argument("--min-support", type=int, default=3)
    parser.add_argument("--min-weight", type=float, default=0.0)
    args = parser.parse_args()
    build_guard_teacher(
        base_npz=args.base_npz,
        source_decision_npz=args.source_decision_npz,
        output_dir=args.output_dir,
        max_rows=args.max_rows,
        max_base_margin=args.max_base_margin,
        min_support=args.min_support,
        min_weight=args.min_weight,
    )


if __name__ == "__main__":
    main()
