import argparse
import csv
import json
from pathlib import Path

import numpy as np

import train_tiny32_v5_visual_subclass_scan as train


def sharpen(probs: np.ndarray, temperature: float) -> np.ndarray:
    probs = np.clip(probs.astype(np.float32), 1.0e-6, 1.0)
    logits = np.log(probs) / max(temperature, 1.0e-6)
    logits = logits - np.max(logits, axis=1, keepdims=True)
    out = np.exp(logits)
    return (out / np.sum(out, axis=1, keepdims=True)).astype(np.float32)


def blend_hard(probs: np.ndarray, labels: np.ndarray, alpha: float) -> np.ndarray:
    one_hot = np.eye(len(train.PARENT_NAMES), dtype=np.float32)[labels.astype(np.int64)]
    out = alpha * one_hot + (1.0 - alpha) * probs.astype(np.float32)
    out = np.clip(out, 1.0e-6, 1.0)
    out = out / np.sum(out, axis=1, keepdims=True)
    return out.astype(np.float32)


def parent_soft(path: Path, x: np.ndarray, temperature: float) -> np.ndarray:
    outputs = train.tflite_outputs(path, x)
    return sharpen(train.parent_soft_from_old_sixclass(outputs, 1.0), temperature)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary-csv", type=Path, required=True)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--stable-teacher-weight", type=float, default=0.03)
    parser.add_argument("--preserve-teacher-weight", type=float, default=0.45)
    parser.add_argument("--rescue-teacher-weight", type=float, default=0.70)
    parser.add_argument("--stable-parent-weight", type=float, default=1.0)
    parser.add_argument("--preserve-parent-weight", type=float, default=3.5)
    parser.add_argument("--rescue-parent-weight", type=float, default=6.0)
    parser.add_argument("--hard-parent-weight", type=float, default=2.5)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old_soft = parent_soft(args.old_tflite, x, args.temperature)
    rescue_soft = parent_soft(args.rescue_tflite, x, args.temperature)
    old_pred = np.argmax(old_soft, axis=1).astype(np.int64)
    rescue_pred = np.argmax(rescue_soft, axis=1).astype(np.int64)
    old_correct = old_pred == y_parent
    rescue_correct = rescue_pred == y_parent
    rescue_mask = (~old_correct) & rescue_correct
    preserve_mask = old_correct & (~rescue_correct)
    stable_mask = old_correct & rescue_correct
    both_wrong_mask = (~old_correct) & (~rescue_correct)
    hard_mask = np.asarray([Path(path).name in train.HARD_CLEAN_BASENAMES for path in paths], dtype=bool)

    teacher_parent_soft = blend_hard(old_soft, y_parent, 0.12)
    teacher_parent_soft[preserve_mask] = blend_hard(old_soft[preserve_mask], y_parent[preserve_mask], 0.35)
    teacher_parent_soft[rescue_mask] = blend_hard(rescue_soft[rescue_mask], y_parent[rescue_mask], 0.55)
    teacher_parent_soft[both_wrong_mask] = np.eye(len(train.PARENT_NAMES), dtype=np.float32)[y_parent[both_wrong_mask]]

    teacher_parent_weight = np.zeros(len(paths), dtype=np.float32)
    teacher_parent_weight[stable_mask] = args.stable_teacher_weight
    teacher_parent_weight[preserve_mask] = args.preserve_teacher_weight
    teacher_parent_weight[rescue_mask] = args.rescue_teacher_weight
    teacher_parent_weight[hard_mask & (teacher_parent_weight > 0)] = np.maximum(
        teacher_parent_weight[hard_mask & (teacher_parent_weight > 0)],
        args.preserve_teacher_weight,
    )

    parent_sample_weight = np.full(len(paths), args.stable_parent_weight, dtype=np.float32)
    parent_sample_weight[preserve_mask] = args.preserve_parent_weight
    parent_sample_weight[rescue_mask] = args.rescue_parent_weight
    parent_sample_weight[both_wrong_mask] = args.rescue_parent_weight
    parent_sample_weight[hard_mask] = np.maximum(parent_sample_weight[hard_mask], args.hard_parent_weight)

    teacher_pred = np.argmax(teacher_parent_soft, axis=1).astype(np.int64)
    if np.any((teacher_parent_weight > 0) & (teacher_pred != y_parent)):
        bad = int(np.sum((teacher_parent_weight > 0) & (teacher_pred != y_parent)))
        raise ValueError(f"active delta teacher labels not correct: {bad}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.summary_csv.parent.mkdir(parents=True, exist_ok=True)
    meta = {
        "builder": "build_v6_delta_teacher_labels.py",
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "temperature": args.temperature,
        "counts": {
            "total": int(len(paths)),
            "old_correct": int(np.sum(old_correct)),
            "rescue_correct": int(np.sum(rescue_correct)),
            "stable": int(np.sum(stable_mask)),
            "preserve_old_correct_rescue_wrong": int(np.sum(preserve_mask)),
            "rescue_old_wrong_rescue_correct": int(np.sum(rescue_mask)),
            "both_wrong": int(np.sum(both_wrong_mask)),
            "hard": int(np.sum(hard_mask)),
        },
        "weights": {
            "teacher_mean": float(np.mean(teacher_parent_weight)),
            "teacher_active": int(np.sum(teacher_parent_weight > 0)),
            "parent_mean": float(np.mean(parent_sample_weight)),
            "parent_max": float(np.max(parent_sample_weight)),
        },
    }
    np.savez_compressed(
        args.output,
        paths=np.asarray(paths, dtype=str),
        teacher_parent_soft=teacher_parent_soft.astype(np.float32),
        teacher_parent_weight=teacher_parent_weight,
        parent_sample_weight=parent_sample_weight,
        old_parent_soft=old_soft.astype(np.float32),
        rescue_parent_soft=rescue_soft.astype(np.float32),
        old_pred=old_pred,
        rescue_pred=rescue_pred,
        y_parent=y_parent.astype(np.int64),
        y_sub=y_sub.astype(np.int64),
        meta_json=json.dumps(meta, ensure_ascii=False),
    )

    with args.summary_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "path",
                "parent",
                "visual",
                "old_pred",
                "rescue_pred",
                "tier",
                "teacher_weight",
                "parent_weight",
            ],
        )
        writer.writeheader()
        for index, path in enumerate(paths):
            if rescue_mask[index]:
                tier = "rescue_old_wrong_rescue_correct"
            elif preserve_mask[index]:
                tier = "preserve_old_correct_rescue_wrong"
            elif both_wrong_mask[index]:
                tier = "both_wrong_hard_only"
            else:
                tier = "stable_both_correct"
            writer.writerow(
                {
                    "path": path,
                    "parent": train.PARENT_NAMES[int(y_parent[index])],
                    "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "old_pred": train.PARENT_NAMES[int(old_pred[index])],
                    "rescue_pred": train.PARENT_NAMES[int(rescue_pred[index])],
                    "tier": tier,
                    "teacher_weight": float(teacher_parent_weight[index]),
                    "parent_weight": float(parent_sample_weight[index]),
                }
            )
    print(json.dumps(meta, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
