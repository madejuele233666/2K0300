import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_paths(text: str) -> list[Path]:
    return [Path(item.strip()) for item in text.split(",") if item.strip()]


def nearest_by_parent(
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    parent_count = 3
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    dist_rows: list[np.ndarray] = []
    nearest_rows: list[np.ndarray] = []
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(parent_count)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), parent_count), np.iinfo(np.int64).max, dtype=np.int64)
        nearest = np.full((len(x), parent_count), -1, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            local = dist[:, indexes]
            arg = np.argmin(local, axis=1)
            class_dist[:, parent] = local[np.arange(len(x)), arg]
            nearest[:, parent] = indexes[arg]
        pred = np.argmin(class_dist, axis=1).astype(np.int64)
        sorted_dist = np.sort(class_dist, axis=1)
        pred_rows.append(pred)
        margin_rows.append((sorted_dist[:, 1] - sorted_dist[:, 0]).astype(np.int64))
        dist_rows.append(class_dist)
        nearest_rows.append(nearest)
    return (
        np.concatenate(pred_rows).astype(np.int64),
        np.concatenate(margin_rows).astype(np.int64),
        np.concatenate(dist_rows).astype(np.int64),
        np.concatenate(nearest_rows).astype(np.int64),
    )


def nearest_wrong_parent(class_dist: np.ndarray, parent: np.ndarray) -> np.ndarray:
    masked = class_dist.copy()
    masked[np.arange(len(parent)), parent.astype(np.int64)] = np.iinfo(np.int64).max
    return np.argmin(masked, axis=1).astype(np.int64)


def payload_key_map(sample_index: np.ndarray, view_labels: np.ndarray) -> dict[tuple[int, str], int]:
    return {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist(), strict=False))
    }


def load_payload(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def build_multiteacher_pair_teacher(
    *,
    student_params_npz: Path,
    teacher_params_npz: list[Path],
    output_dir: Path,
    min_votes: int,
    teacher_margin_min: int,
    student_margin_max: int,
    student_margin_target: int,
    vote_weight: float,
    vulnerability_weight: float,
    teacher_strength_weight: float,
    teacher_margin_scale: float,
    max_weight: float,
    max_events: int,
    batch_size: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    student = load_payload(student_params_npz)
    y_parent = np.asarray(student["parent"], dtype=np.int64)
    y_sub = np.asarray(student["subclass"], dtype=np.int64)
    sample_index = np.asarray(student["sample_index"], dtype=np.int64)
    view_labels = np.asarray(student["view_labels"]).astype(str)
    paths = np.asarray(student.get("paths", np.asarray([]))).astype(str)
    student_embeddings = np.asarray(student["embedding_int8"], dtype=np.int8)
    student_prototypes = np.asarray(student["prototypes_int8"], dtype=np.int8)
    student_proto_parent = np.asarray(student["prototype_parent"], dtype=np.int64)
    student_proto_sample = np.asarray(student["prototype_sample_index"], dtype=np.int64)
    student_proto_view = np.asarray(student["prototype_view_label"]).astype(str)

    student_pred, student_margin, student_class_dist, student_nearest = nearest_by_parent(
        student_embeddings,
        student_prototypes,
        student_proto_parent,
        batch_size=batch_size,
    )
    student_wrong_parent = nearest_wrong_parent(student_class_dist, y_parent)
    student_keys = [(int(sample), str(view)) for sample, view in zip(sample_index.tolist(), view_labels.tolist(), strict=False)]

    votes: dict[tuple[int, int], list[tuple[str, int]]] = {}
    teacher_summaries: list[dict[str, Any]] = []
    for teacher_path in teacher_params_npz:
        teacher = load_payload(teacher_path)
        teacher_name = teacher_path.parent.name
        teacher_sample = np.asarray(teacher["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(teacher["view_labels"]).astype(str)
        teacher_key_to_row = payload_key_map(teacher_sample, teacher_view)
        teacher_embeddings = np.asarray(teacher["embedding_int8"], dtype=np.int8)
        teacher_prototypes = np.asarray(teacher["prototypes_int8"], dtype=np.int8)
        teacher_proto_parent = np.asarray(teacher["prototype_parent"], dtype=np.int64)
        teacher_parent = np.asarray(teacher["parent"], dtype=np.int64)
        teacher_pred, teacher_margin, teacher_class_dist, _teacher_nearest = nearest_by_parent(
            teacher_embeddings,
            teacher_prototypes,
            teacher_proto_parent,
            batch_size=batch_size,
        )
        teacher_wrong_parent = nearest_wrong_parent(teacher_class_dist, teacher_parent)
        aligned = 0
        eligible = 0
        for student_row, key in enumerate(student_keys):
            teacher_row = teacher_key_to_row.get(key)
            if teacher_row is None:
                continue
            aligned += 1
            if int(teacher_parent[teacher_row]) != int(y_parent[student_row]):
                continue
            if int(teacher_pred[teacher_row]) != int(y_parent[student_row]):
                continue
            if int(teacher_margin[teacher_row]) < int(teacher_margin_min):
                continue
            wrong_parent = int(teacher_wrong_parent[teacher_row])
            if wrong_parent == int(y_parent[student_row]):
                continue
            votes.setdefault((int(student_row), wrong_parent), []).append((teacher_name, int(teacher_margin[teacher_row])))
            eligible += 1
        teacher_summaries.append(
            {
                "teacher": str(teacher_path),
                "name": teacher_name,
                "aligned_rows": int(aligned),
                "eligible_votes": int(eligible),
                "teacher_margin_min_filter": int(teacher_margin_min),
            }
        )

    rows: list[dict[str, Any]] = []
    for (query_index, wrong_parent), items in votes.items():
        vote_count = len(items)
        if vote_count < int(min_votes):
            continue
        if student_margin_max > 0 and int(student_margin[query_index]) > int(student_margin_max):
            continue
        correct_proto_index = int(student_nearest[query_index, int(y_parent[query_index])])
        wrong_proto_index = int(student_nearest[query_index, int(wrong_parent)])
        if correct_proto_index < 0 or wrong_proto_index < 0:
            continue
        margins = np.asarray([margin for _name, margin in items], dtype=np.float32)
        teacher_names = [name for name, _margin in items]
        vulnerability = 0.0
        if student_margin_target > 0:
            vulnerability = float(
                np.clip((float(student_margin_target) - float(student_margin[query_index])) / float(student_margin_target), 0.0, 1.0)
            )
        teacher_strength = float(np.clip(float(np.mean(margins)) / max(float(teacher_margin_scale), 1.0), 0.0, 1.0))
        weight = (
            1.0
            + max(0, vote_count - 1) * float(vote_weight)
            + vulnerability * float(vulnerability_weight)
            + teacher_strength * float(teacher_strength_weight)
        )
        if max_weight > 0:
            weight = min(weight, float(max_weight))
        rows.append(
            {
                "query_index": int(query_index),
                "sample_index": int(sample_index[query_index]),
                "path": str(paths[int(sample_index[query_index])]) if len(paths) > int(sample_index[query_index]) else "",
                "view_label": str(view_labels[query_index]),
                "parent": int(y_parent[query_index]),
                "subclass": int(y_sub[query_index]),
                "student_pred": int(student_pred[query_index]),
                "student_margin": int(student_margin[query_index]),
                "student_wrong_parent": int(student_wrong_parent[query_index]),
                "teacher_wrong_parent": int(wrong_parent),
                "teacher_vote_count": int(vote_count),
                "teacher_margin_mean": float(np.mean(margins)),
                "teacher_margin_min": int(np.min(margins)),
                "teacher_names": ";".join(teacher_names),
                "correct_proto_index": correct_proto_index,
                "correct_proto_sample": int(student_proto_sample[correct_proto_index]),
                "correct_proto_view": str(student_proto_view[correct_proto_index]),
                "wrong_proto_index": wrong_proto_index,
                "wrong_proto_sample": int(student_proto_sample[wrong_proto_index]),
                "wrong_proto_view": str(student_proto_view[wrong_proto_index]),
                "weight": float(weight),
            }
        )

    rows.sort(
        key=lambda row: (
            int(row["student_margin"]),
            -int(row["teacher_vote_count"]),
            -float(row["teacher_margin_mean"]),
            int(row["query_index"]),
            int(row["teacher_wrong_parent"]),
        )
    )
    if max_events > 0:
        rows = rows[:max_events]

    write_csv(output_dir / "multiteacher_pair_events.csv", rows)
    query_indexes = np.asarray([row["query_index"] for row in rows], dtype=np.int64)
    correct_proto_index = np.asarray([row["correct_proto_index"] for row in rows], dtype=np.int64)
    wrong_proto_index = np.asarray([row["wrong_proto_index"] for row in rows], dtype=np.int64)
    weights = np.asarray([row["weight"] for row in rows], dtype=np.float32)
    np.savez_compressed(
        output_dir / "pair_margin_teacher.npz",
        sample_index=sample_index[query_indexes].astype(np.int64),
        view_labels=view_labels[query_indexes].astype(str),
        query_index=query_indexes.astype(np.int64),
        parent=y_parent[query_indexes].astype(np.int64),
        student_int8_margin=student_margin[query_indexes].astype(np.int64),
        student_wrong_parent=np.asarray([row["student_wrong_parent"] for row in rows], dtype=np.int64),
        teacher_wrong_parent=np.asarray([row["teacher_wrong_parent"] for row in rows], dtype=np.int64),
        teacher_vote_count=np.asarray([row["teacher_vote_count"] for row in rows], dtype=np.int64),
        teacher_margin_mean=np.asarray([row["teacher_margin_mean"] for row in rows], dtype=np.float32),
        teacher_margin_min=np.asarray([row["teacher_margin_min"] for row in rows], dtype=np.int64),
        correct_proto_index=correct_proto_index.astype(np.int64),
        correct_proto_sample=student_proto_sample[correct_proto_index].astype(np.int64),
        correct_proto_view=student_proto_view[correct_proto_index].astype(str),
        wrong_proto_index=wrong_proto_index.astype(np.int64),
        wrong_proto_sample=student_proto_sample[wrong_proto_index].astype(np.int64),
        wrong_proto_view=student_proto_view[wrong_proto_index].astype(str),
        weight=weights,
        source_student_params_npz=np.asarray(str(student_params_npz)),
        source_teacher_params_npz=np.asarray([str(path) for path in teacher_params_npz]),
        high_pressure_usage=np.asarray("none"),
        min_votes=np.asarray(int(min_votes), dtype=np.int64),
        teacher_margin_min_filter=np.asarray(int(teacher_margin_min), dtype=np.int64),
        student_margin_max=np.asarray(int(student_margin_max), dtype=np.int64),
    )

    by_view: dict[str, int] = {}
    by_wrong_parent: dict[str, int] = {}
    for row in rows:
        by_view[str(row["view_label"])] = by_view.get(str(row["view_label"]), 0) + 1
        by_wrong_parent[str(row["teacher_wrong_parent"])] = by_wrong_parent.get(str(row["teacher_wrong_parent"]), 0) + 1
    write_json(
        output_dir / "summary.json",
        {
            "student_params_npz": str(student_params_npz),
            "teacher_params_npz": [str(path) for path in teacher_params_npz],
            "high_pressure_usage": "none",
            "min_votes": int(min_votes),
            "teacher_margin_min": int(teacher_margin_min),
            "student_margin_max": int(student_margin_max),
            "student_margin_target": int(student_margin_target),
            "event_count": int(len(rows)),
            "unique_query_count": int(len(set(int(row["query_index"]) for row in rows))),
            "student_margin_min": int(min((int(row["student_margin"]) for row in rows), default=0)),
            "student_margin_max_selected": int(max((int(row["student_margin"]) for row in rows), default=0)),
            "teacher_vote_count_min": int(min((int(row["teacher_vote_count"]) for row in rows), default=0)),
            "teacher_vote_count_max": int(max((int(row["teacher_vote_count"]) for row in rows), default=0)),
            "weight_min": float(np.min(weights)) if len(weights) else 0.0,
            "weight_max": float(np.max(weights)) if len(weights) else 0.0,
            "weight_mean": float(np.mean(weights)) if len(weights) else 0.0,
            "teacher_summaries": teacher_summaries,
            "by_view_top20": dict(sorted(by_view.items(), key=lambda item: (-item[1], item[0]))[:20]),
            "by_teacher_wrong_parent": by_wrong_parent,
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build normal-only multi-teacher dynamic pair constraints for V8.")
    parser.add_argument("--student-params-npz", type=Path, required=True)
    parser.add_argument("--teacher-params-npz", required=True, help="Comma-separated retained teacher params npz paths.")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--min-votes", type=int, default=2)
    parser.add_argument("--teacher-margin-min", type=int, default=64)
    parser.add_argument("--student-margin-max", type=int, default=512)
    parser.add_argument("--student-margin-target", type=int, default=512)
    parser.add_argument("--vote-weight", type=float, default=0.5)
    parser.add_argument("--vulnerability-weight", type=float, default=1.0)
    parser.add_argument("--teacher-strength-weight", type=float, default=0.5)
    parser.add_argument("--teacher-margin-scale", type=float, default=512.0)
    parser.add_argument("--max-weight", type=float, default=4.0)
    parser.add_argument("--max-events", type=int, default=0)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()
    build_multiteacher_pair_teacher(
        student_params_npz=args.student_params_npz,
        teacher_params_npz=parse_paths(args.teacher_params_npz),
        output_dir=args.output_dir,
        min_votes=args.min_votes,
        teacher_margin_min=args.teacher_margin_min,
        student_margin_max=args.student_margin_max,
        student_margin_target=args.student_margin_target,
        vote_weight=args.vote_weight,
        vulnerability_weight=args.vulnerability_weight,
        teacher_strength_weight=args.teacher_strength_weight,
        teacher_margin_scale=args.teacher_margin_scale,
        max_weight=args.max_weight,
        max_events=args.max_events,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
