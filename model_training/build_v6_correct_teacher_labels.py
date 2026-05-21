import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

from train_tiny32_v5_visual_subclass_scan import (
    C4_SUBCLASS_INDEX,
    PARENT_NAMES,
    hard_indices,
    load_dataset_v5,
    parent_soft_from_old_sixclass,
    tflite_outputs,
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def one_hot(labels: np.ndarray, classes: int) -> np.ndarray:
    values = np.zeros((len(labels), classes), dtype=np.float32)
    values[np.arange(len(labels)), labels] = 1.0
    return values


def normalize_prob(values: np.ndarray) -> np.ndarray:
    probs = np.asarray(values, dtype=np.float32)
    if probs.shape[1] not in {3, 6}:
        raise ValueError(f"teacher output must have 3 or 6 columns, got {probs.shape}")
    return parent_soft_from_old_sixclass(probs, temperature=1.0)


def soften(values: np.ndarray, temperature: float) -> np.ndarray:
    probs = np.clip(np.asarray(values, dtype=np.float32), 1.0e-6, 1.0)
    logits = np.log(probs) / max(temperature, 1.0e-6)
    logits -= np.max(logits, axis=1, keepdims=True)
    exp = np.exp(logits)
    return (exp / np.sum(exp, axis=1, keepdims=True)).astype(np.float32)


def basename(path: str) -> str:
    return Path(path).name


def model_quality(row: dict[str, str]) -> float:
    all_acc = float(row.get("all_acc", 0.0))
    hard_acc = float(row.get("hard_acc", 0.0))
    stress = float(row.get("stress_worst_acc", 0.0))
    camera = float(row.get("camera_worst_acc", 0.0))
    return 0.45 * all_acc + 0.20 * hard_acc + 0.20 * stress + 0.15 * camera


def sample_tier(clean_wrong: int, is_hard: bool, is_c4: bool, stress_events: int) -> str:
    if clean_wrong >= 10:
        return "A"
    if clean_wrong >= 8:
        return "B"
    if is_c4 or is_hard:
        return "C"
    if clean_wrong > 0 or stress_events >= 80:
        return "D"
    return "anchor"


def tier_alpha(tier: str, correct_count: int, args: argparse.Namespace) -> float:
    alpha = {
        "A": args.alpha_core,
        "B": args.alpha_secondary,
        "C": args.alpha_hard,
        "D": args.alpha_soft,
        "anchor": args.alpha_anchor,
    }[tier]
    if correct_count <= 1:
        alpha = min(alpha, args.alpha_single_teacher_cap)
    elif correct_count <= 3:
        alpha = min(alpha, args.alpha_few_teacher_cap)
    return float(alpha)


def tier_parent_weight(tier: str, is_hard: bool, is_c4: bool, args: argparse.Namespace) -> float:
    base = {
        "A": args.parent_weight_core,
        "B": args.parent_weight_secondary,
        "C": args.parent_weight_hard,
        "D": args.parent_weight_soft,
        "anchor": 1.0,
    }[tier]
    if is_hard:
        base = max(base, args.parent_weight_hard)
    if is_c4:
        base = max(base, args.parent_weight_c4)
    return float(base)


def tier_teacher_weight(tier: str, args: argparse.Namespace) -> float:
    return float(
        {
            "A": args.teacher_weight_core,
            "B": args.teacher_weight_secondary,
            "C": args.teacher_weight_hard,
            "D": args.teacher_weight_soft,
            "anchor": args.teacher_weight_anchor,
        }[tier]
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build per-sample correct-teacher soft labels for V6 parent training.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--selected-models", type=Path, required=True)
    parser.add_argument("--model-summary", type=Path, required=True)
    parser.add_argument("--sample-summary", type=Path, required=True)
    parser.add_argument("--error-events", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary-csv", type=Path)
    parser.add_argument("--model-root", type=Path, default=Path("."))
    parser.add_argument("--temperature", type=float, default=2.0)
    parser.add_argument("--alpha-core", type=float, default=0.40)
    parser.add_argument("--alpha-secondary", type=float, default=0.32)
    parser.add_argument("--alpha-hard", type=float, default=0.25)
    parser.add_argument("--alpha-soft", type=float, default=0.14)
    parser.add_argument("--alpha-anchor", type=float, default=0.05)
    parser.add_argument("--alpha-single-teacher-cap", type=float, default=0.25)
    parser.add_argument("--alpha-few-teacher-cap", type=float, default=0.35)
    parser.add_argument("--parent-weight-core", type=float, default=8.0)
    parser.add_argument("--parent-weight-secondary", type=float, default=5.0)
    parser.add_argument("--parent-weight-hard", type=float, default=4.0)
    parser.add_argument("--parent-weight-c4", type=float, default=4.0)
    parser.add_argument("--parent-weight-soft", type=float, default=2.0)
    parser.add_argument("--teacher-weight-core", type=float, default=1.0)
    parser.add_argument("--teacher-weight-secondary", type=float, default=0.85)
    parser.add_argument("--teacher-weight-hard", type=float, default=0.70)
    parser.add_argument("--teacher-weight-soft", type=float, default=0.35)
    parser.add_argument("--teacher-weight-anchor", type=float, default=0.15)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _manifest = load_dataset_v5(args.dataset_dir)
    path_basenames = [basename(path) for path in paths]
    y_onehot = one_hot(y_parent, len(PARENT_NAMES))

    selected = read_csv(args.selected_models)
    model_summary = {row["model_id"]: row for row in read_csv(args.model_summary)}
    if not selected:
        raise RuntimeError("selected model list is empty")

    stress_wrong: dict[tuple[str, str], int] = defaultdict(int)
    for row in read_csv(args.error_events):
        if row.get("group") == "all" and row.get("stress") != "clean":
            stress_wrong[(row["basename"], row["model_id"])] += 1

    sample_info = {
        row["basename"]: {
            "clean_wrong": int(row["clean_wrong_model_count"]),
            "stress_events": int(row["stress_wrong_events"]),
            "camera_events": int(row["camera_wrong_events"]),
            "is_hard": row["is_hard"] == "1",
            "is_c4": row["is_c4"] == "1",
            "pred_parents": row["pred_parents"],
        }
        for row in read_csv(args.sample_summary)
    }
    hard_idx, _missing = hard_indices(paths)
    hard_basenames = {path_basenames[index] for index in hard_idx}

    model_probs: list[np.ndarray] = []
    model_soft: list[np.ndarray] = []
    qualities: list[float] = []
    model_ids: list[str] = []
    for row in selected:
        model_id = row["model_id"]
        model_path = args.model_root / row["model_path"]
        if not model_path.exists():
            raise FileNotFoundError(model_path)
        probs = normalize_prob(tflite_outputs(model_path, x))
        model_probs.append(probs)
        model_soft.append(soften(probs, args.temperature))
        qualities.append(model_quality(model_summary[model_id]))
        model_ids.append(model_id)

    qualities_np = np.asarray(qualities, dtype=np.float32)
    qualities_np = qualities_np / max(float(np.mean(qualities_np)), 1.0e-6)
    probs_stack = np.stack(model_probs, axis=0)
    soft_stack = np.stack(model_soft, axis=0)
    preds_stack = np.argmax(probs_stack, axis=2)

    teacher_parent_soft = np.zeros_like(y_onehot, dtype=np.float32)
    teacher_parent_weight = np.zeros(len(paths), dtype=np.float32)
    parent_sample_weight = np.ones(len(paths), dtype=np.float32)
    teacher_source_count = np.zeros(len(paths), dtype=np.int32)
    teacher_alpha = np.zeros(len(paths), dtype=np.float32)
    tier_codes: list[str] = []
    rows: list[dict[str, object]] = []

    for index, path in enumerate(paths):
        base = path_basenames[index]
        info = sample_info.get(
            base,
            {
                "clean_wrong": 0,
                "stress_events": 0,
                "camera_events": 0,
                "is_hard": base in hard_basenames,
                "is_c4": int(y_sub[index]) == C4_SUBCLASS_INDEX,
                "pred_parents": "",
            },
        )
        is_hard = bool(info["is_hard"]) or base in hard_basenames
        is_c4 = bool(info["is_c4"]) or int(y_sub[index]) == C4_SUBCLASS_INDEX
        clean_wrong = int(info["clean_wrong"])
        stress_events = int(info["stress_events"])
        tier = sample_tier(clean_wrong, is_hard, is_c4, stress_events)
        correct = preds_stack[:, index] == y_parent[index]
        correct_indexes = np.where(correct)[0]
        teacher_source_count[index] = int(len(correct_indexes))

        if len(correct_indexes) == 0:
            q_teacher = y_onehot[index]
            source_weight = 0.0
        else:
            weights = []
            for model_pos in correct_indexes:
                probs = probs_stack[model_pos, index]
                true_prob = float(probs[y_parent[index]])
                other_prob = float(np.max(np.delete(probs, y_parent[index])))
                margin = max(0.03, true_prob - other_prob)
                stress_count = stress_wrong[(base, model_ids[model_pos])]
                stress_score = 1.0 / (1.0 + 0.20 * stress_count)
                weights.append(float(qualities_np[model_pos]) * margin * stress_score)
            weight_np = np.asarray(weights, dtype=np.float32)
            if float(np.sum(weight_np)) <= 0.0:
                weight_np = np.ones_like(weight_np)
            weight_np = weight_np / np.sum(weight_np)
            q_teacher = np.sum(soft_stack[correct_indexes, index, :] * weight_np[:, None], axis=0)
            source_weight = float(np.clip(np.mean(weights) * 2.0, 0.05, 1.0))

        alpha = tier_alpha(tier, len(correct_indexes), args)
        q_final = (1.0 - alpha) * y_onehot[index] + alpha * q_teacher
        q_final = np.clip(q_final, 1.0e-6, 1.0)
        q_final = q_final / np.sum(q_final)

        teacher_parent_soft[index] = q_final.astype(np.float32)
        teacher_parent_weight[index] = tier_teacher_weight(tier, args) * source_weight
        parent_sample_weight[index] = tier_parent_weight(tier, is_hard, is_c4, args)
        teacher_alpha[index] = alpha
        tier_codes.append(tier)
        rows.append(
            {
                "path": path,
                "basename": base,
                "true_parent": PARENT_NAMES[int(y_parent[index])],
                "visual_index": int(y_sub[index]),
                "tier": tier,
                "is_hard": int(is_hard),
                "is_c4": int(is_c4),
                "clean_wrong_model_count": clean_wrong,
                "stress_events": stress_events,
                "teacher_source_count": int(len(correct_indexes)),
                "teacher_alpha": round(float(alpha), 6),
                "teacher_weight": round(float(teacher_parent_weight[index]), 6),
                "parent_sample_weight": round(float(parent_sample_weight[index]), 6),
                "teacher_top_parent": PARENT_NAMES[int(np.argmax(q_final))],
                "teacher_prob_true": round(float(q_final[y_parent[index]]), 6),
                "pred_parents": str(info["pred_parents"]),
            }
        )

    active_argmax = np.argmax(teacher_parent_soft, axis=1)
    bad_active = np.where((teacher_parent_weight > 0) & (active_argmax != y_parent))[0]
    if len(bad_active):
        raise RuntimeError(f"active teacher labels have wrong argmax for {len(bad_active)} samples")

    meta = {
        "builder": "build_v6_correct_teacher_labels.py",
        "temperature": args.temperature,
        "selected_models": len(model_ids),
        "model_ids": model_ids,
        "tier_counts": dict(Counter(tier_codes)),
        "teacher_source_count_distribution": dict(Counter(int(v) for v in teacher_source_count)),
        "parent_weight_mean": float(np.mean(parent_sample_weight)),
        "teacher_weight_mean": float(np.mean(teacher_parent_weight)),
        "active_teacher_count": int(np.sum(teacher_parent_weight > 0)),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        args.output,
        paths=np.asarray(paths, dtype=str),
        teacher_parent_soft=teacher_parent_soft.astype(np.float32),
        teacher_parent_weight=teacher_parent_weight.astype(np.float32),
        parent_sample_weight=parent_sample_weight.astype(np.float32),
        teacher_source_count=teacher_source_count,
        teacher_alpha=teacher_alpha,
        meta_json=np.asarray(json.dumps(meta, ensure_ascii=False)),
    )
    if args.summary_csv:
        args.summary_csv.parent.mkdir(parents=True, exist_ok=True)
        write_csv(args.summary_csv, rows)
    print(json.dumps(meta, indent=2, ensure_ascii=False))
    print(f"output={args.output}")
    if args.summary_csv:
        print(f"summary_csv={args.summary_csv}")


if __name__ == "__main__":
    main()
