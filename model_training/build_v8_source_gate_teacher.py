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


def softmax(values: np.ndarray, temperature: float) -> np.ndarray:
    temp = max(float(temperature), 1.0e-6)
    scaled = values.astype(np.float64) / temp
    scaled -= np.max(scaled, axis=1, keepdims=True)
    exp = np.exp(scaled)
    return (exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-12)).astype(np.float32)


def sharpen_probs(values: np.ndarray, temperature: float) -> np.ndarray:
    temp = max(float(temperature), 1.0e-6)
    clipped = np.maximum(values.astype(np.float64), 1.0e-12)
    sharpened = np.power(clipped, 1.0 / temp)
    return (sharpened / np.maximum(np.sum(sharpened, axis=1, keepdims=True), 1.0e-12)).astype(np.float32)


def view_family(view: str) -> str:
    if view == "clean":
        return "clean"
    if view.startswith("rot") or view.startswith("mirror"):
        return "d4"
    if "noise" in view and "blur" in view:
        return "blur_noise"
    if "blur" in view:
        return "blur"
    if "noise" in view:
        return "noise"
    if "bright" in view:
        return "brightness"
    if "contrast" in view:
        return "contrast"
    if "shift" in view:
        return "shift"
    return "other"


def build_labels(
    *,
    scores: np.ndarray,
    sample_index: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    label_mode: str,
) -> np.ndarray:
    if label_mode == "row_winner":
        return np.argmax(scores, axis=1).astype(np.int64)

    if label_mode.startswith("sample_family_"):
        group_keys = [
            (int(sample), int(parent_value), view_family(str(view)))
            for sample, parent_value, view in zip(
                sample_index.tolist(),
                parent.tolist(),
                view_labels.astype(str).tolist(),
                strict=False,
            )
        ]
        aggregate_mode = label_mode.removeprefix("sample_family_")
    elif label_mode.startswith("sample_"):
        group_keys = [
            (int(sample), int(parent_value))
            for sample, parent_value in zip(sample_index.tolist(), parent.tolist(), strict=False)
        ]
        aggregate_mode = label_mode.removeprefix("sample_")
    else:
        raise ValueError(f"unknown label mode: {label_mode}")

    grouped: dict[tuple[Any, ...], list[int]] = {}
    for row_index, key in enumerate(group_keys):
        grouped.setdefault(key, []).append(row_index)

    group_choice: dict[tuple[Any, ...], int] = {}
    for key, indexes in grouped.items():
        group_scores = scores[np.asarray(indexes, dtype=np.int64)]
        if aggregate_mode == "min":
            aggregate = np.min(group_scores, axis=0)
        elif aggregate_mode == "correct_count":
            correct = group_scores >= 0.0
            aggregate = np.sum(correct, axis=0).astype(np.float64) * 1.0e6
            aggregate += np.mean(np.maximum(group_scores, 0.0), axis=0)
        else:
            raise ValueError(f"unknown label aggregate mode: {aggregate_mode}")
        group_choice[key] = int(np.argmax(aggregate))

    return np.asarray([group_choice[key] for key in group_keys], dtype=np.int64)


def build_source_gate_teacher(
    *,
    source_confidence_npz: Path,
    output_dir: Path,
    score_key: str,
    temperature: float,
    min_top_gap: float,
    gap_weight: float,
    base_weight_key: str,
    hard_label: bool,
    label_smoothing: float,
    class_balance: str,
    max_class_balance_weight: float,
    label_mode: str,
) -> None:
    with np.load(source_confidence_npz, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{source_confidence_npz} is missing arrays: {missing}")
        if score_key not in data.files:
            raise ValueError(f"{source_confidence_npz} is missing score key: {score_key}")
        sample_index = np.asarray(data["sample_index"], dtype=np.int64)
        view_labels = np.asarray(data["view_labels"]).astype(str)
        parent = np.asarray(data["parent"], dtype=np.int64)
        scores = np.asarray(data[score_key], dtype=np.float32)
        source_names = (
            np.asarray(data["source_names"]).astype(str)
            if "source_names" in data.files
            else np.asarray([f"source{index}" for index in range(scores.shape[1])]).astype(str)
        )
        base_weight = (
            np.asarray(data[base_weight_key], dtype=np.float32)
            if base_weight_key and base_weight_key in data.files
            else np.ones(len(sample_index), dtype=np.float32)
        )
    if scores.ndim != 2 or scores.shape[0] != len(sample_index):
        raise ValueError(f"score matrix shape {scores.shape} does not match row count {len(sample_index)}")
    if len(source_names) != scores.shape[1]:
        raise ValueError(f"source_names length {len(source_names)} does not match source count {scores.shape[1]}")
    if len(base_weight) != len(sample_index):
        raise ValueError(f"base weight length {len(base_weight)} does not match row count {len(sample_index)}")

    if score_key == "source_confidence":
        target = sharpen_probs(scores, temperature)
    else:
        target = softmax(scores, temperature)
    order = np.argsort(target, axis=1)
    top1 = target[np.arange(len(target)), order[:, -1]]
    top2 = target[np.arange(len(target)), order[:, -2]] if target.shape[1] > 1 else np.zeros(len(target), dtype=np.float32)
    top_gap = (top1 - top2).astype(np.float32)
    keep = top_gap >= float(min_top_gap)
    if not np.any(keep):
        raise ValueError("no rows left after min_top_gap filter")
    label = build_labels(
        scores=scores,
        sample_index=sample_index,
        parent=parent,
        view_labels=view_labels,
        label_mode=label_mode,
    )
    if hard_label or label_mode != "row_winner":
        smooth = float(label_smoothing)
        hard = np.full_like(target, smooth / max(target.shape[1] - 1, 1), dtype=np.float32)
        hard[np.arange(len(target)), label] = 1.0 - smooth
        target = hard.astype(np.float32)
    weight = base_weight.astype(np.float32) * (1.0 + float(gap_weight) * top_gap.astype(np.float32))
    class_weight = np.ones(target.shape[1], dtype=np.float32)
    if class_balance == "inverse_label":
        kept_labels = label[keep]
        counts = np.bincount(kept_labels, minlength=target.shape[1]).astype(np.float32)
        nonzero = counts > 0
        mean_count = float(np.mean(counts[nonzero])) if np.any(nonzero) else 1.0
        class_weight[nonzero] = mean_count / np.maximum(counts[nonzero], 1.0)
        class_weight = np.minimum(class_weight, float(max_class_balance_weight)).astype(np.float32)
        weight = weight * class_weight[label]
    elif class_balance != "none":
        raise ValueError(f"unknown class_balance: {class_balance}")

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "source_gate_teacher.npz",
        sample_index=sample_index[keep].astype(np.int64),
        view_labels=view_labels[keep].astype(str),
        parent=parent[keep].astype(np.int64),
        target_probs=target[keep].astype(np.float32),
        source_label=label[keep].astype(np.int64),
        weight=weight[keep].astype(np.float32),
        top_gap=top_gap[keep].astype(np.float32),
        source_names=source_names.astype(str),
        source_confidence_npz=np.asarray(str(source_confidence_npz)),
        score_key=np.asarray(str(score_key)),
        label_mode=np.asarray(str(label_mode)),
        high_pressure_usage=np.asarray("none"),
    )

    rows: list[dict[str, Any]] = []
    for local_index, row_index in enumerate(np.where(keep)[0].tolist()):
        probs = target[row_index]
        rows.append(
            {
                "row_index": int(row_index),
                "sample_index": int(sample_index[row_index]),
                "view_label": str(view_labels[row_index]),
                "parent": int(parent[row_index]),
                "source_label": int(label[row_index]),
                "source_name": str(source_names[int(label[row_index])]),
                "top_gap": float(top_gap[row_index]),
                "weight": float(weight[row_index]),
                "class_weight": float(class_weight[int(label[row_index])]),
                "target_probs_json": json.dumps([float(value) for value in probs.tolist()]),
                "local_index": int(local_index),
            }
        )
    write_csv(output_dir / "source_gate_teacher_rows.csv", rows)
    label_counts = {
        str(source_names[index]): int(np.sum(label[keep] == index))
        for index in range(len(source_names))
    }
    summary = {
        "output": str(output_dir / "source_gate_teacher.npz"),
        "source_confidence_npz": str(source_confidence_npz),
        "high_pressure_usage": "none",
        "score_key": str(score_key),
        "label_mode": str(label_mode),
        "temperature": float(temperature),
        "hard_label": bool(hard_label),
        "label_smoothing": float(label_smoothing),
        "class_balance": str(class_balance),
        "max_class_balance_weight": float(max_class_balance_weight),
        "row_count": int(np.sum(keep)),
        "input_row_count": int(len(sample_index)),
        "source_count": int(scores.shape[1]),
        "source_names": [str(name) for name in source_names.tolist()],
        "label_counts": label_counts,
        "class_weights": {
            str(source_names[index]): float(class_weight[index])
            for index in range(len(source_names))
        },
        "min_top_gap": float(min_top_gap),
        "top_gap_min": float(np.min(top_gap[keep])),
        "top_gap_mean": float(np.mean(top_gap[keep])),
        "top_gap_p50": float(np.percentile(top_gap[keep], 50)),
        "top_gap_p90": float(np.percentile(top_gap[keep], 90)),
        "weight_min": float(np.min(weight[keep])),
        "weight_mean": float(np.mean(weight[keep])),
        "weight_max": float(np.max(weight[keep])),
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a normal-only source-gate teacher from retained-source confidence.")
    parser.add_argument("--source-confidence-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--score-key", default="source_margin_scores")
    parser.add_argument("--temperature", type=float, default=0.1)
    parser.add_argument("--min-top-gap", type=float, default=0.0)
    parser.add_argument("--gap-weight", type=float, default=4.0)
    parser.add_argument("--base-weight-key", default="qanchor_weight")
    parser.add_argument("--hard-label", action="store_true")
    parser.add_argument("--label-smoothing", type=float, default=0.05)
    parser.add_argument(
        "--label-mode",
        default="row_winner",
        help="Normal-only source label mode: row_winner,sample_min,sample_correct_count,sample_family_min,sample_family_correct_count.",
    )
    parser.add_argument("--class-balance", choices=["none", "inverse_label"], default="none")
    parser.add_argument("--max-class-balance-weight", type=float, default=3.0)
    args = parser.parse_args()
    build_source_gate_teacher(
        source_confidence_npz=args.source_confidence_npz,
        output_dir=args.output_dir,
        score_key=args.score_key,
        temperature=args.temperature,
        min_top_gap=args.min_top_gap,
        gap_weight=args.gap_weight,
        base_weight_key=args.base_weight_key,
        hard_label=args.hard_label,
        label_smoothing=args.label_smoothing,
        class_balance=args.class_balance,
        max_class_balance_weight=args.max_class_balance_weight,
        label_mode=args.label_mode,
    )


if __name__ == "__main__":
    main()
