import argparse
import csv
import json
from pathlib import Path

import numpy as np

import train_tiny32_v5_visual_subclass_scan as train


def blend_with_hard(probs: np.ndarray, labels: np.ndarray, hard_alpha: float) -> np.ndarray:
    one_hot = np.eye(len(train.PARENT_NAMES), dtype=np.float32)[labels.astype(np.int64)]
    out = hard_alpha * one_hot + (1.0 - hard_alpha) * probs.astype(np.float32)
    out = np.clip(out, 1.0e-6, 1.0)
    out = out / np.sum(out, axis=1, keepdims=True)
    return out.astype(np.float32)


def hard_parent(labels: np.ndarray) -> np.ndarray:
    return np.eye(len(train.PARENT_NAMES), dtype=np.float32)[labels.astype(np.int64)]


def parent_soft_from_tflite(path: Path, x: np.ndarray, temperature: float) -> np.ndarray:
    outputs = train.tflite_outputs(path, x)
    return train.parent_soft_from_old_sixclass(outputs, temperature)


def group_masks(y_parent: np.ndarray, y_sub: np.ndarray, paths: list[str], old_pred: np.ndarray, rescue_pred: np.ndarray) -> dict[str, np.ndarray]:
    hard = np.asarray([Path(path).name in train.HARD_CLEAN_BASENAMES for path in paths], dtype=bool)
    c4 = y_sub == train.C4_SUBCLASS_INDEX
    old_correct = old_pred == y_parent
    rescue_correct = rescue_pred == y_parent
    return {
        "stable": old_correct & rescue_correct,
        "preserve": old_correct & (~rescue_correct),
        "rescue": (~old_correct) & rescue_correct,
        "both_wrong": (~old_correct) & (~rescue_correct),
        "hard": hard,
        "c4": c4,
        "c4_box": np.asarray([Path(path).name in train.C4_BOX_BASENAMES for path in paths], dtype=bool),
        "c4_circuit": np.asarray([Path(path).name in train.C4_CIRCUIT_BASENAMES for path in paths], dtype=bool),
    }


def build_stable_bundle(
    y_parent: np.ndarray,
    old_soft: np.ndarray,
    rescue_soft: np.ndarray,
    masks: dict[str, np.ndarray],
    args: argparse.Namespace,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    soft = hard_parent(y_parent)
    soft[masks["stable"]] = blend_with_hard(old_soft[masks["stable"]], y_parent[masks["stable"]], args.stable_hard_alpha)
    soft[masks["preserve"]] = blend_with_hard(old_soft[masks["preserve"]], y_parent[masks["preserve"]], args.preserve_hard_alpha)
    soft[masks["rescue"]] = hard_parent(y_parent[masks["rescue"]])
    soft[masks["both_wrong"]] = hard_parent(y_parent[masks["both_wrong"]])

    teacher_weight = np.zeros(len(y_parent), dtype=np.float32)
    teacher_weight[masks["stable"]] = args.stable_teacher_weight
    teacher_weight[masks["preserve"]] = args.preserve_teacher_weight
    teacher_weight[masks["hard"] & (teacher_weight > 0)] = np.maximum(
        teacher_weight[masks["hard"] & (teacher_weight > 0)],
        args.stable_hard_teacher_weight,
    )

    parent_weight = np.full(len(y_parent), args.stable_parent_weight, dtype=np.float32)
    parent_weight[masks["preserve"]] = args.preserve_parent_weight
    parent_weight[masks["rescue"]] = args.stable_rescue_parent_weight
    parent_weight[masks["hard"]] = np.maximum(parent_weight[masks["hard"]], args.stable_hard_parent_weight)
    parent_weight[masks["c4"]] = np.maximum(parent_weight[masks["c4"]], args.stable_c4_parent_weight)
    return soft, teacher_weight, parent_weight


def build_rescue_bundle(
    y_parent: np.ndarray,
    old_soft: np.ndarray,
    rescue_soft: np.ndarray,
    masks: dict[str, np.ndarray],
    args: argparse.Namespace,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    soft = hard_parent(y_parent)
    soft[masks["stable"]] = blend_with_hard(old_soft[masks["stable"]], y_parent[masks["stable"]], args.rescue_stable_hard_alpha)
    soft[masks["preserve"]] = blend_with_hard(old_soft[masks["preserve"]], y_parent[masks["preserve"]], args.rescue_preserve_hard_alpha)
    soft[masks["rescue"]] = blend_with_hard(rescue_soft[masks["rescue"]], y_parent[masks["rescue"]], args.rescue_hard_alpha)

    rescue_or_c4 = masks["rescue"] | masks["c4"] | masks["hard"]
    rescue_pred = np.argmax(rescue_soft, axis=1).astype(np.int64)
    rescue_correct = rescue_pred == y_parent
    use_rescue = rescue_or_c4 & rescue_correct
    soft[use_rescue] = blend_with_hard(rescue_soft[use_rescue], y_parent[use_rescue], args.rescue_aux_hard_alpha)
    soft[masks["both_wrong"]] = hard_parent(y_parent[masks["both_wrong"]])

    teacher_weight = np.zeros(len(y_parent), dtype=np.float32)
    teacher_weight[masks["stable"]] = args.rescue_stable_teacher_weight
    teacher_weight[masks["preserve"]] = args.rescue_preserve_teacher_weight
    teacher_weight[masks["rescue"]] = args.rescue_teacher_weight
    teacher_weight[use_rescue & masks["c4"]] = np.maximum(teacher_weight[use_rescue & masks["c4"]], args.rescue_c4_teacher_weight)
    teacher_weight[use_rescue & masks["hard"]] = np.maximum(teacher_weight[use_rescue & masks["hard"]], args.rescue_hard_teacher_weight)

    parent_weight = np.full(len(y_parent), args.rescue_stable_parent_weight, dtype=np.float32)
    parent_weight[masks["preserve"]] = args.rescue_preserve_parent_weight
    parent_weight[masks["rescue"]] = args.rescue_parent_weight
    parent_weight[masks["hard"]] = np.maximum(parent_weight[masks["hard"]], args.rescue_hard_parent_weight)
    parent_weight[masks["c4"]] = np.maximum(parent_weight[masks["c4"]], args.rescue_c4_parent_weight)
    parent_weight[masks["c4_circuit"]] = np.maximum(parent_weight[masks["c4_circuit"]], args.rescue_circuit_parent_weight)
    return soft, teacher_weight, parent_weight


def write_bundle(
    path: Path,
    summary_csv: Path,
    paths: list[str],
    y_parent: np.ndarray,
    y_sub: np.ndarray,
    old_soft: np.ndarray,
    rescue_soft: np.ndarray,
    old_pred: np.ndarray,
    rescue_pred: np.ndarray,
    masks: dict[str, np.ndarray],
    teacher_soft: np.ndarray,
    teacher_weight: np.ndarray,
    parent_weight: np.ndarray,
    meta: dict[str, object],
) -> None:
    teacher_pred = np.argmax(teacher_soft, axis=1).astype(np.int64)
    if np.any((teacher_weight > 0) & (teacher_pred != y_parent)):
        bad = int(np.sum((teacher_weight > 0) & (teacher_pred != y_parent)))
        raise ValueError(f"active teacher labels are wrong: {bad}")
    path.parent.mkdir(parents=True, exist_ok=True)
    summary_csv.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        path,
        paths=np.asarray(paths, dtype=str),
        teacher_parent_soft=teacher_soft.astype(np.float32),
        teacher_parent_weight=teacher_weight.astype(np.float32),
        parent_sample_weight=parent_weight.astype(np.float32),
        old_parent_soft=old_soft.astype(np.float32),
        rescue_parent_soft=rescue_soft.astype(np.float32),
        old_pred=old_pred.astype(np.int64),
        rescue_pred=rescue_pred.astype(np.int64),
        y_parent=y_parent.astype(np.int64),
        y_sub=y_sub.astype(np.int64),
        group_stable=masks["stable"],
        group_preserve=masks["preserve"],
        group_rescue=masks["rescue"],
        group_hard=masks["hard"],
        group_c4=masks["c4"],
        meta_json=json.dumps(meta, ensure_ascii=False),
    )

    with summary_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "path",
                "visual",
                "parent",
                "group",
                "old_pred",
                "rescue_pred",
                "teacher_pred",
                "teacher_weight",
                "parent_weight",
            ],
        )
        writer.writeheader()
        for index, sample_path in enumerate(paths):
            if masks["rescue"][index]:
                group = "rescue_old_wrong_rescue_correct"
            elif masks["preserve"][index]:
                group = "preserve_old_correct_rescue_wrong"
            elif masks["stable"][index]:
                group = "stable_both_correct"
            else:
                group = "both_wrong"
            writer.writerow(
                {
                    "path": sample_path,
                    "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "parent": train.PARENT_NAMES[int(y_parent[index])],
                    "group": group,
                    "old_pred": train.PARENT_NAMES[int(old_pred[index])],
                    "rescue_pred": train.PARENT_NAMES[int(rescue_pred[index])],
                    "teacher_pred": train.PARENT_NAMES[int(teacher_pred[index])],
                    "teacher_weight": float(teacher_weight[index]),
                    "parent_weight": float(parent_weight[index]),
                }
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--temperature", type=float, default=1.0)

    parser.add_argument("--stable-teacher-weight", type=float, default=0.08)
    parser.add_argument("--preserve-teacher-weight", type=float, default=0.70)
    parser.add_argument("--stable-hard-teacher-weight", type=float, default=0.45)
    parser.add_argument("--stable-parent-weight", type=float, default=1.0)
    parser.add_argument("--preserve-parent-weight", type=float, default=5.0)
    parser.add_argument("--stable-rescue-parent-weight", type=float, default=0.75)
    parser.add_argument("--stable-hard-parent-weight", type=float, default=3.0)
    parser.add_argument("--stable-c4-parent-weight", type=float, default=1.5)
    parser.add_argument("--stable-hard-alpha", type=float, default=0.18)
    parser.add_argument("--preserve-hard-alpha", type=float, default=0.40)

    parser.add_argument("--rescue-stable-teacher-weight", type=float, default=0.02)
    parser.add_argument("--rescue-preserve-teacher-weight", type=float, default=0.35)
    parser.add_argument("--rescue-teacher-weight", type=float, default=0.85)
    parser.add_argument("--rescue-c4-teacher-weight", type=float, default=0.65)
    parser.add_argument("--rescue-hard-teacher-weight", type=float, default=0.65)
    parser.add_argument("--rescue-stable-parent-weight", type=float, default=0.80)
    parser.add_argument("--rescue-preserve-parent-weight", type=float, default=3.0)
    parser.add_argument("--rescue-parent-weight", type=float, default=8.0)
    parser.add_argument("--rescue-hard-parent-weight", type=float, default=5.0)
    parser.add_argument("--rescue-c4-parent-weight", type=float, default=6.0)
    parser.add_argument("--rescue-circuit-parent-weight", type=float, default=8.0)
    parser.add_argument("--rescue-stable-hard-alpha", type=float, default=0.10)
    parser.add_argument("--rescue-preserve-hard-alpha", type=float, default=0.40)
    parser.add_argument("--rescue-hard-alpha", type=float, default=0.60)
    parser.add_argument("--rescue-aux-hard-alpha", type=float, default=0.55)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old_soft = parent_soft_from_tflite(args.old_tflite, x, args.temperature)
    rescue_soft = parent_soft_from_tflite(args.rescue_tflite, x, args.temperature)
    old_pred = np.argmax(old_soft, axis=1).astype(np.int64)
    rescue_pred = np.argmax(rescue_soft, axis=1).astype(np.int64)
    masks = group_masks(y_parent, y_sub, paths, old_pred, rescue_pred)
    counts = {name: int(np.sum(mask)) for name, mask in masks.items()}

    stable_soft, stable_teacher_weight, stable_parent_weight = build_stable_bundle(
        y_parent, old_soft, rescue_soft, masks, args
    )
    rescue_soft_out, rescue_teacher_weight, rescue_parent_weight = build_rescue_bundle(
        y_parent, old_soft, rescue_soft, masks, args
    )

    base_meta = {
        "builder": "build_v7_expert_teacher_labels.py",
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "temperature": args.temperature,
        "counts": counts,
    }
    stable_meta = {
        **base_meta,
        "role": "stable_teacher",
        "weights": {
            "teacher_mean": float(np.mean(stable_teacher_weight)),
            "teacher_active": int(np.sum(stable_teacher_weight > 0)),
            "parent_mean": float(np.mean(stable_parent_weight)),
            "parent_max": float(np.max(stable_parent_weight)),
        },
    }
    rescue_meta = {
        **base_meta,
        "role": "rescue_teacher",
        "weights": {
            "teacher_mean": float(np.mean(rescue_teacher_weight)),
            "teacher_active": int(np.sum(rescue_teacher_weight > 0)),
            "parent_mean": float(np.mean(rescue_parent_weight)),
            "parent_max": float(np.max(rescue_parent_weight)),
        },
    }
    write_bundle(
        args.output_dir / "stable_teacher_labels.npz",
        args.output_dir / "stable_teacher_summary.csv",
        paths,
        y_parent,
        y_sub,
        old_soft,
        rescue_soft,
        old_pred,
        rescue_pred,
        masks,
        stable_soft,
        stable_teacher_weight,
        stable_parent_weight,
        stable_meta,
    )
    write_bundle(
        args.output_dir / "rescue_teacher_labels.npz",
        args.output_dir / "rescue_teacher_summary.csv",
        paths,
        y_parent,
        y_sub,
        old_soft,
        rescue_soft,
        old_pred,
        rescue_pred,
        masks,
        rescue_soft_out,
        rescue_teacher_weight,
        rescue_parent_weight,
        rescue_meta,
    )
    print(json.dumps({"stable": stable_meta, "rescue": rescue_meta}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
