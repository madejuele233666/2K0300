import argparse
import csv
import json
from pathlib import Path

import numpy as np

import train_tiny32_v5_visual_subclass_scan as train


def hard_parent(labels: np.ndarray) -> np.ndarray:
    return np.eye(len(train.PARENT_NAMES), dtype=np.float32)[labels.astype(np.int64)]


def blend_with_hard(probs: np.ndarray, labels: np.ndarray, hard_alpha: float) -> np.ndarray:
    one_hot = hard_parent(labels)
    out = hard_alpha * one_hot + (1.0 - hard_alpha) * probs.astype(np.float32)
    out = np.clip(out, 1.0e-6, 1.0)
    out = out / np.sum(out, axis=1, keepdims=True)
    return out.astype(np.float32)


def parent_soft_from_tflite(path: Path, x: np.ndarray, temperature: float) -> np.ndarray:
    outputs = train.tflite_outputs(path, x)
    return train.parent_soft_from_old_sixclass(outputs, temperature)


def group_masks(y_parent: np.ndarray, y_sub: np.ndarray, paths: list[str], old_pred: np.ndarray, rescue_pred: np.ndarray) -> dict[str, np.ndarray]:
    old_correct = old_pred == y_parent
    rescue_correct = rescue_pred == y_parent
    return {
        "stable": old_correct & rescue_correct,
        "preserve": old_correct & (~rescue_correct),
        "rescue": (~old_correct) & rescue_correct,
        "both_wrong": (~old_correct) & (~rescue_correct),
        "hard": np.asarray([Path(path).name in train.HARD_CLEAN_BASENAMES for path in paths], dtype=bool),
        "c4": y_sub == train.C4_SUBCLASS_INDEX,
        "c4_circuit": np.asarray([Path(path).name in train.C4_CIRCUIT_BASENAMES for path in paths], dtype=bool),
    }


def build_negative(
    y_parent: np.ndarray,
    source_pred: np.ndarray,
    active_mask: np.ndarray,
    weight: float,
) -> tuple[np.ndarray, np.ndarray]:
    negative_id = np.full(len(y_parent), -1, dtype=np.int64)
    negative_weight = np.zeros(len(y_parent), dtype=np.float32)
    valid = active_mask & (source_pred != y_parent)
    negative_id[valid] = source_pred[valid]
    negative_weight[valid] = float(weight)
    return negative_id, negative_weight


def merge_negative(
    base_id: np.ndarray,
    base_weight: np.ndarray,
    extra_id: np.ndarray,
    extra_weight: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    take = extra_weight > base_weight
    out_id = base_id.copy()
    out_weight = base_weight.copy()
    out_id[take] = extra_id[take]
    out_weight[take] = extra_weight[take]
    return out_id, out_weight


def build_stable_bundle(
    y_parent: np.ndarray,
    old_soft: np.ndarray,
    rescue_pred: np.ndarray,
    masks: dict[str, np.ndarray],
    args: argparse.Namespace,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    soft = hard_parent(y_parent)
    old_zone = masks["stable"] | masks["preserve"]
    soft[old_zone] = blend_with_hard(old_soft[old_zone], y_parent[old_zone], args.stable_hard_alpha)
    soft[masks["hard"] & old_zone] = blend_with_hard(
        old_soft[masks["hard"] & old_zone],
        y_parent[masks["hard"] & old_zone],
        args.stable_hard_clean_alpha,
    )

    teacher_weight = np.zeros(len(y_parent), dtype=np.float32)
    teacher_weight[masks["stable"]] = args.stable_teacher_weight
    teacher_weight[masks["preserve"]] = args.preserve_teacher_weight
    teacher_weight[masks["hard"] & old_zone] = np.maximum(
        teacher_weight[masks["hard"] & old_zone],
        args.stable_hard_teacher_weight,
    )

    parent_weight = np.full(len(y_parent), args.stable_parent_weight, dtype=np.float32)
    parent_weight[masks["preserve"]] = args.preserve_parent_weight
    parent_weight[masks["rescue"]] = args.stable_rescue_parent_weight
    parent_weight[masks["hard"] & old_zone] = np.maximum(parent_weight[masks["hard"] & old_zone], args.stable_hard_parent_weight)
    parent_weight[masks["c4"] & old_zone] = np.maximum(parent_weight[masks["c4"] & old_zone], args.stable_c4_parent_weight)

    negative_id, negative_weight = build_negative(y_parent, rescue_pred, masks["preserve"], args.stable_preserve_negative_weight)
    hard_id, hard_weight = build_negative(y_parent, rescue_pred, masks["hard"] & masks["preserve"], args.stable_hard_negative_weight)
    negative_id, negative_weight = merge_negative(negative_id, negative_weight, hard_id, hard_weight)
    return soft, teacher_weight, parent_weight, negative_id, negative_weight


def build_rescue_bundle(
    y_parent: np.ndarray,
    old_soft: np.ndarray,
    rescue_soft: np.ndarray,
    old_pred: np.ndarray,
    rescue_pred: np.ndarray,
    masks: dict[str, np.ndarray],
    args: argparse.Namespace,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    soft = hard_parent(y_parent)
    soft[masks["rescue"]] = blend_with_hard(rescue_soft[masks["rescue"]], y_parent[masks["rescue"]], args.rescue_hard_alpha)
    soft[masks["stable"]] = blend_with_hard(old_soft[masks["stable"]], y_parent[masks["stable"]], args.rescue_stable_hard_alpha)
    soft[masks["preserve"]] = hard_parent(y_parent[masks["preserve"]])

    teacher_weight = np.zeros(len(y_parent), dtype=np.float32)
    teacher_weight[masks["rescue"]] = args.rescue_teacher_weight
    teacher_weight[masks["stable"]] = args.rescue_stable_teacher_weight
    teacher_weight[masks["hard"] & masks["rescue"]] = np.maximum(
        teacher_weight[masks["hard"] & masks["rescue"]],
        args.rescue_hard_teacher_weight,
    )
    teacher_weight[masks["c4"] & masks["rescue"]] = np.maximum(
        teacher_weight[masks["c4"] & masks["rescue"]],
        args.rescue_c4_teacher_weight,
    )

    parent_weight = np.full(len(y_parent), args.rescue_stable_parent_weight, dtype=np.float32)
    parent_weight[masks["preserve"]] = args.rescue_preserve_parent_weight
    parent_weight[masks["rescue"]] = args.rescue_parent_weight
    parent_weight[masks["hard"]] = np.maximum(parent_weight[masks["hard"]], args.rescue_hard_parent_weight)
    parent_weight[masks["c4"]] = np.maximum(parent_weight[masks["c4"]], args.rescue_c4_parent_weight)
    parent_weight[masks["c4_circuit"]] = np.maximum(parent_weight[masks["c4_circuit"]], args.rescue_circuit_parent_weight)

    negative_id, negative_weight = build_negative(y_parent, old_pred, masks["rescue"], args.rescue_negative_weight)
    preserve_id, preserve_weight = build_negative(y_parent, rescue_pred, masks["preserve"], args.rescue_preserve_negative_weight)
    negative_id, negative_weight = merge_negative(negative_id, negative_weight, preserve_id, preserve_weight)
    hard_id, hard_weight = build_negative(y_parent, old_pred, masks["hard"] & masks["rescue"], args.rescue_hard_negative_weight)
    negative_id, negative_weight = merge_negative(negative_id, negative_weight, hard_id, hard_weight)
    return soft, teacher_weight, parent_weight, negative_id, negative_weight


def write_bundle(
    path: Path,
    summary_csv: Path,
    paths: list[str],
    y_parent: np.ndarray,
    y_sub: np.ndarray,
    old_pred: np.ndarray,
    rescue_pred: np.ndarray,
    masks: dict[str, np.ndarray],
    teacher_soft: np.ndarray,
    teacher_weight: np.ndarray,
    parent_weight: np.ndarray,
    negative_id: np.ndarray,
    negative_weight: np.ndarray,
    meta: dict[str, object],
) -> None:
    teacher_pred = np.argmax(teacher_soft, axis=1).astype(np.int64)
    if np.any((teacher_weight > 0) & (teacher_pred != y_parent)):
        bad = int(np.sum((teacher_weight > 0) & (teacher_pred != y_parent)))
        raise ValueError(f"active teacher labels are wrong: {bad}")
    active_negative = negative_weight > 0
    if np.any(active_negative & ((negative_id < 0) | (negative_id >= len(train.PARENT_NAMES)) | (negative_id == y_parent))):
        bad = int(np.sum(active_negative & ((negative_id < 0) | (negative_id >= len(train.PARENT_NAMES)) | (negative_id == y_parent))))
        raise ValueError(f"active negative labels are invalid: {bad}")
    path.parent.mkdir(parents=True, exist_ok=True)
    summary_csv.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        path,
        paths=np.asarray(paths, dtype=str),
        teacher_parent_soft=teacher_soft.astype(np.float32),
        teacher_parent_weight=teacher_weight.astype(np.float32),
        parent_sample_weight=parent_weight.astype(np.float32),
        negative_parent_id=negative_id.astype(np.int64),
        negative_parent_weight=negative_weight.astype(np.float32),
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
            fieldnames=["path", "visual", "parent", "group", "old_pred", "rescue_pred", "teacher_weight", "parent_weight", "negative_parent", "negative_weight"],
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
            neg_name = train.PARENT_NAMES[int(negative_id[index])] if negative_weight[index] > 0 else ""
            writer.writerow(
                {
                    "path": sample_path,
                    "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "parent": train.PARENT_NAMES[int(y_parent[index])],
                    "group": group,
                    "old_pred": train.PARENT_NAMES[int(old_pred[index])],
                    "rescue_pred": train.PARENT_NAMES[int(rescue_pred[index])],
                    "teacher_weight": float(teacher_weight[index]),
                    "parent_weight": float(parent_weight[index]),
                    "negative_parent": neg_name,
                    "negative_weight": float(negative_weight[index]),
                }
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build V7 round3 expert teacher bundles with negative-margin labels.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--temperature", type=float, default=1.0)

    parser.add_argument("--stable-teacher-weight", type=float, default=0.85)
    parser.add_argument("--preserve-teacher-weight", type=float, default=1.20)
    parser.add_argument("--stable-hard-teacher-weight", type=float, default=1.35)
    parser.add_argument("--stable-parent-weight", type=float, default=0.55)
    parser.add_argument("--preserve-parent-weight", type=float, default=1.35)
    parser.add_argument("--stable-rescue-parent-weight", type=float, default=0.05)
    parser.add_argument("--stable-hard-parent-weight", type=float, default=1.10)
    parser.add_argument("--stable-c4-parent-weight", type=float, default=1.10)
    parser.add_argument("--stable-hard-alpha", type=float, default=0.10)
    parser.add_argument("--stable-hard-clean-alpha", type=float, default=0.20)
    parser.add_argument("--stable-preserve-negative-weight", type=float, default=3.0)
    parser.add_argument("--stable-hard-negative-weight", type=float, default=4.0)

    parser.add_argument("--rescue-teacher-weight", type=float, default=0.75)
    parser.add_argument("--rescue-stable-teacher-weight", type=float, default=0.10)
    parser.add_argument("--rescue-hard-teacher-weight", type=float, default=1.05)
    parser.add_argument("--rescue-c4-teacher-weight", type=float, default=1.05)
    parser.add_argument("--rescue-stable-parent-weight", type=float, default=0.55)
    parser.add_argument("--rescue-preserve-parent-weight", type=float, default=2.20)
    parser.add_argument("--rescue-parent-weight", type=float, default=4.00)
    parser.add_argument("--rescue-hard-parent-weight", type=float, default=3.00)
    parser.add_argument("--rescue-c4-parent-weight", type=float, default=3.20)
    parser.add_argument("--rescue-circuit-parent-weight", type=float, default=4.00)
    parser.add_argument("--rescue-hard-alpha", type=float, default=0.28)
    parser.add_argument("--rescue-stable-hard-alpha", type=float, default=0.18)
    parser.add_argument("--rescue-negative-weight", type=float, default=4.0)
    parser.add_argument("--rescue-preserve-negative-weight", type=float, default=5.0)
    parser.add_argument("--rescue-hard-negative-weight", type=float, default=5.0)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old_soft = parent_soft_from_tflite(args.old_tflite, x, args.temperature)
    rescue_soft = parent_soft_from_tflite(args.rescue_tflite, x, args.temperature)
    old_pred = np.argmax(old_soft, axis=1).astype(np.int64)
    rescue_pred = np.argmax(rescue_soft, axis=1).astype(np.int64)
    masks = group_masks(y_parent, y_sub, paths, old_pred, rescue_pred)
    counts = {name: int(np.sum(mask)) for name, mask in masks.items()}

    stable = build_stable_bundle(y_parent, old_soft, rescue_pred, masks, args)
    rescue = build_rescue_bundle(y_parent, old_soft, rescue_soft, old_pred, rescue_pred, masks, args)
    base_meta = {
        "builder": "build_v7_expert_teacher_round3_labels.py",
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "temperature": args.temperature,
        "counts": counts,
        "policy": "Expert-CTD v3: correct-teacher replay plus explicit negative parent margin on old/CTD disagreement zones",
    }
    write_bundle(
        args.output_dir / "stable_teacher_labels.npz",
        args.output_dir / "stable_teacher_summary.csv",
        paths,
        y_parent,
        y_sub,
        old_pred,
        rescue_pred,
        masks,
        *stable,
        {**base_meta, "role": "stable_negative_margin"},
    )
    write_bundle(
        args.output_dir / "rescue_teacher_labels.npz",
        args.output_dir / "rescue_teacher_summary.csv",
        paths,
        y_parent,
        y_sub,
        old_pred,
        rescue_pred,
        masks,
        *rescue,
        {**base_meta, "role": "rescue_negative_margin"},
    )
    print(
        json.dumps(
            {
                "counts": counts,
                "stable_teacher_active": int(np.sum(stable[1] > 0)),
                "stable_negative_active": int(np.sum(stable[4] > 0)),
                "rescue_teacher_active": int(np.sum(rescue[1] > 0)),
                "rescue_negative_active": int(np.sum(rescue[4] > 0)),
            },
            indent=2,
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
