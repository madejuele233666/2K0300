import argparse
import csv
import json
import math
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from analyze_v7_expert_tflite_reverse import TfliteReverse
import train_tiny32_v5_visual_subclass_scan as train


PARENT_NAMES = train.PARENT_NAMES


def softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - np.max(logits, axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / np.sum(exp, axis=1, keepdims=True)


def pred_margin(logits: np.ndarray) -> np.ndarray:
    sorted_logits = np.sort(logits, axis=1)
    return sorted_logits[:, -1] - sorted_logits[:, -2]


def true_margin(logits: np.ndarray, y_parent: np.ndarray) -> np.ndarray:
    out = np.zeros(len(y_parent), dtype=np.float32)
    for index, parent in enumerate(y_parent.astype(np.int64).tolist()):
        others = [item for item in range(len(PARENT_NAMES)) if item != parent]
        out[index] = float(logits[index, parent] - np.max(logits[index, others]))
    return out


def zscore(values: np.ndarray) -> np.ndarray:
    return (values - np.mean(values, axis=0, keepdims=True)) / (np.std(values, axis=0, keepdims=True) + 1.0e-6)


def normalized_columns(weights: np.ndarray) -> np.ndarray:
    cols = weights.T.astype(np.float64)
    return cols / (np.linalg.norm(cols, axis=1, keepdims=True) + 1.0e-9)


def greedy_match(score: np.ndarray) -> list[tuple[int, int, float]]:
    used_left: set[int] = set()
    used_right: set[int] = set()
    pairs: list[tuple[int, int, float]] = []
    order = sorted(
        ((left, right) for left in range(score.shape[0]) for right in range(score.shape[1])),
        key=lambda item: float(score[item[0], item[1]]),
        reverse=True,
    )
    for left, right in order:
        if left in used_left or right in used_right:
            continue
        used_left.add(left)
        used_right.add(right)
        pairs.append((left, right, float(score[left, right])))
    return pairs


def group_masks(paths: list[str], y_sub: np.ndarray, y_parent: np.ndarray, old_pred: np.ndarray, rescue_pred: np.ndarray) -> dict[str, np.ndarray]:
    old_correct = old_pred == y_parent
    rescue_correct = rescue_pred == y_parent
    return {
        "stable": old_correct & rescue_correct,
        "preserve": old_correct & (~rescue_correct),
        "rescue": (~old_correct) & rescue_correct,
        "both_wrong": (~old_correct) & (~rescue_correct),
        "hard": np.asarray([Path(path).name in train.HARD_CLEAN_BASENAMES for path in paths], dtype=bool),
        "c4": y_sub == train.C4_SUBCLASS_INDEX,
    }


def channel_sensitivity(gaps: np.ndarray, weights: np.ndarray, y_parent: np.ndarray, mask: np.ndarray) -> np.ndarray:
    if not np.any(mask):
        return np.zeros(gaps.shape[1], dtype=np.float64)
    values = np.zeros((int(np.sum(mask)), gaps.shape[1]), dtype=np.float64)
    selected = np.where(mask)[0]
    for row, sample_index in enumerate(selected.tolist()):
        true_id = int(y_parent[sample_index])
        others = [item for item in range(len(PARENT_NAMES)) if item != true_id]
        boundary = weights[true_id] - weights[others[np.argmax(np.max(weights[others], axis=1))]]
        values[row] = np.asarray(gaps[sample_index], dtype=np.float64) * np.asarray(boundary, dtype=np.float64)
    return np.mean(values * values, axis=0)


def build_alignment(
    old_gap: np.ndarray,
    rescue_gap: np.ndarray,
    old_weight: np.ndarray,
    rescue_weight: np.ndarray,
    groups: dict[str, np.ndarray],
    y_parent: np.ndarray,
) -> tuple[list[dict[str, object]], np.ndarray, np.ndarray]:
    gap_corr = zscore(old_gap).T @ zscore(rescue_gap) / len(old_gap)
    head_cos = normalized_columns(old_weight) @ normalized_columns(rescue_weight).T
    score = 0.70 * np.abs(gap_corr) + 0.30 * np.maximum(head_cos, 0.0)
    pairs = greedy_match(score)
    stable_mask = groups["stable"] | groups["preserve"]
    rescue_mask = groups["rescue"] | groups["hard"] | groups["c4"]
    stable_sens = channel_sensitivity(old_gap, old_weight, y_parent, stable_mask)
    rescue_sens = channel_sensitivity(old_gap, old_weight, y_parent, rescue_mask)
    rows: list[dict[str, object]] = []
    for rank, (old_ch, rescue_ch, match_score) in enumerate(pairs, start=1):
        old_values = old_gap[:, old_ch].astype(np.float64)
        rescue_values = rescue_gap[:, rescue_ch].astype(np.float64)
        variance = float(np.var(old_values))
        if variance <= 1.0e-12:
            scale = 0.0
        else:
            scale = float(np.mean((old_values - np.mean(old_values)) * (rescue_values - np.mean(rescue_values))) / variance)
        intercept = float(np.mean(rescue_values) - scale * np.mean(old_values))
        fisher_share = float(rescue_sens[old_ch] / (stable_sens[old_ch] + rescue_sens[old_ch] + 1.0e-9))
        rows.append(
            {
                "rank": rank,
                "old_channel": int(old_ch),
                "rescue_channel": int(rescue_ch),
                "score": float(match_score),
                "gap_corr": float(gap_corr[old_ch, rescue_ch]),
                "head_cos": float(head_cos[old_ch, rescue_ch]),
                "linear_scale": scale,
                "linear_intercept": intercept,
                "stable_sensitivity": float(stable_sens[old_ch]),
                "rescue_sensitivity": float(rescue_sens[old_ch]),
                "fisher_rescue_share": fisher_share,
            }
        )
    return rows, gap_corr, head_cos


def projected_rescue_head(
    alignment_rows: list[dict[str, object]],
    old_weight: np.ndarray,
    old_bias: np.ndarray,
    rescue_weight: np.ndarray,
    rescue_bias: np.ndarray,
    *,
    min_abs_corr: float,
    fisher_min_share: float,
) -> tuple[np.ndarray, np.ndarray, list[int]]:
    projected_weight = np.zeros_like(old_weight, dtype=np.float32)
    projected_bias = np.asarray(rescue_bias, dtype=np.float32).copy()
    active_channels: list[int] = []
    for row in alignment_rows:
        corr = abs(float(row["gap_corr"]))
        fisher_share = float(row["fisher_rescue_share"])
        if corr < min_abs_corr or fisher_share < fisher_min_share:
            continue
        old_ch = int(row["old_channel"])
        rescue_ch = int(row["rescue_channel"])
        scale = float(row["linear_scale"])
        intercept = float(row["linear_intercept"])
        projected_weight[:, old_ch] += rescue_weight[:, rescue_ch] * scale
        projected_bias += rescue_weight[:, rescue_ch] * intercept
        active_channels.append(old_ch)
    return projected_weight, projected_bias, active_channels


def apply_ties_mask(delta_weight: np.ndarray, percentile: float) -> np.ndarray:
    if percentile <= 0:
        return np.ones_like(delta_weight, dtype=np.float32)
    threshold = float(np.percentile(np.abs(delta_weight), percentile))
    large = np.abs(delta_weight) >= threshold
    row_sign = np.sign(np.sum(delta_weight, axis=1, keepdims=True))
    row_sign[row_sign == 0] = 1
    sign_ok = np.sign(delta_weight) == row_sign
    return (large & sign_ok).astype(np.float32)


def logits_from_head(gap: np.ndarray, weight: np.ndarray, bias: np.ndarray) -> np.ndarray:
    return gap @ weight.T + bias


def counts_for(pred: np.ndarray, y_parent: np.ndarray, groups: dict[str, np.ndarray]) -> dict[str, object]:
    out: dict[str, object] = {
        "all_correct": int(np.sum(pred == y_parent)),
        "all_total": int(len(y_parent)),
        "all_accuracy": float(np.mean(pred == y_parent)),
    }
    for name, mask in groups.items():
        out[f"{name}_correct"] = int(np.sum((pred == y_parent) & mask))
        out[f"{name}_total"] = int(np.sum(mask))
        out[f"{name}_accuracy"] = float(np.mean(pred[mask] == y_parent[mask])) if np.any(mask) else 0.0
    return out


def gate_mask_for(
    gate_type: str,
    old_logits: np.ndarray,
    rescue_logits: np.ndarray,
    threshold: float,
) -> np.ndarray:
    old_pred = np.argmax(old_logits, axis=1)
    rescue_pred = np.argmax(rescue_logits, axis=1)
    if gate_type == "always":
        return np.ones(len(old_logits), dtype=bool)
    if gate_type == "old_margin_lt":
        return pred_margin(old_logits) < threshold
    if gate_type == "disagree_rescue_margin_gt":
        return (old_pred != rescue_pred) & (pred_margin(rescue_logits) > threshold)
    if gate_type == "rescue_margin_gt":
        return pred_margin(rescue_logits) > threshold
    if gate_type == "delta_norm_gt":
        return np.linalg.norm(rescue_logits - old_logits, axis=1) > threshold
    raise ValueError(f"unknown gate type: {gate_type}")


def evaluate_candidate(
    name: str,
    old_logits: np.ndarray,
    delta_logits: np.ndarray,
    y_parent: np.ndarray,
    groups: dict[str, np.ndarray],
    *,
    alpha: float,
    gate_type: str,
    threshold: float,
    paths: list[str],
) -> tuple[dict[str, object], np.ndarray, np.ndarray]:
    rescue_like_logits = old_logits + alpha * delta_logits
    gate = gate_mask_for(gate_type, old_logits, rescue_like_logits, threshold)
    final_logits = old_logits + gate[:, None].astype(np.float32) * alpha * delta_logits
    pred = np.argmax(final_logits, axis=1).astype(np.int64)
    metrics = counts_for(pred, y_parent, groups)
    metrics.update(
        {
            "name": name,
            "alpha": float(alpha),
            "gate_type": gate_type,
            "threshold": float(threshold),
            "gate_count": int(np.sum(gate)),
            "preserve_false_trigger": int(np.sum(gate & groups["preserve"])),
            "stable_false_trigger": int(np.sum(gate & groups["stable"])),
            "rescue_trigger": int(np.sum(gate & groups["rescue"])),
            "wrong_files": [Path(paths[index]).name for index in np.where(pred != y_parent)[0].tolist()],
        }
    )
    return metrics, pred, gate


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def safe_score(row: dict[str, object]) -> tuple[object, ...]:
    preserve_ok = int(row["preserve_correct"]) == int(row["preserve_total"])
    return (
        preserve_ok,
        int(row["all_correct"]),
        int(row["hard_correct"]),
        int(row["c4_correct"]),
        -int(row["preserve_false_trigger"]),
        -int(row["gate_count"]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="V7 phase1 stable-anchored head/delta merge search.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--min-abs-corr", default="0.0,0.2,0.4,0.6,0.8")
    parser.add_argument("--fisher-min-share", default="0.0,0.15,0.30")
    parser.add_argument("--mask-percentile", default="0,70,80,90")
    parser.add_argument("--alpha", default="0.1,0.25,0.5,0.75,1.0")
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old = TfliteReverse(args.old_tflite)
    rescue = TfliteReverse(args.rescue_tflite)
    old_data = old.infer_dataset(x)
    rescue_data = rescue.infer_dataset(x)
    groups = group_masks(paths, y_sub, y_parent, old_data["pred"], rescue_data["pred"])

    alignment_rows, _gap_corr, _head_cos = build_alignment(
        old_data["gap"],
        rescue_data["gap"],
        old.parent_weight,
        rescue.parent_weight,
        groups,
        y_parent,
    )

    old_logits = old_data["logits"].astype(np.float32)
    baseline_pred = old_data["pred"].astype(np.int64)
    rescue_pred = rescue_data["pred"].astype(np.int64)
    baseline = counts_for(baseline_pred, y_parent, groups)
    baseline.update({"name": "old_stable_baseline", "gate_type": "none", "gate_count": 0, "preserve_false_trigger": 0})
    ctd = counts_for(rescue_pred, y_parent, groups)
    ctd.update({"name": "old_ctd_rescue_reference", "gate_type": "none", "gate_count": len(y_parent), "preserve_false_trigger": int(np.sum(groups["preserve"]))})

    min_corr_values = [float(item) for item in args.min_abs_corr.split(",") if item.strip()]
    fisher_values = [float(item) for item in args.fisher_min_share.split(",") if item.strip()]
    mask_values = [float(item) for item in args.mask_percentile.split(",") if item.strip()]
    alpha_values = [float(item) for item in args.alpha.split(",") if item.strip()]
    thresholds = {
        "always": [0.0],
        "old_margin_lt": [round(v, 3) for v in np.linspace(0.0, 2.0, 41)],
        "disagree_rescue_margin_gt": [round(v, 3) for v in np.linspace(0.0, 8.0, 41)],
        "rescue_margin_gt": [round(v, 3) for v in np.linspace(0.0, 8.0, 41)],
        "delta_norm_gt": [round(v, 3) for v in np.linspace(0.0, 12.0, 49)],
    }

    rows: list[dict[str, object]] = [baseline, ctd]
    head_rows: list[dict[str, object]] = []
    best_params: dict[str, object] | None = None
    best_pred: np.ndarray | None = None
    best_gate: np.ndarray | None = None
    best_delta_weight: np.ndarray | None = None
    best_delta_bias: np.ndarray | None = None
    for min_corr in min_corr_values:
        for fisher_min in fisher_values:
            projected_weight, projected_bias, active_channels = projected_rescue_head(
                alignment_rows,
                old.parent_weight,
                old.parent_bias,
                rescue.parent_weight,
                rescue.parent_bias,
                min_abs_corr=min_corr,
                fisher_min_share=fisher_min,
            )
            projected_logits = logits_from_head(old_data["gap"], projected_weight, projected_bias).astype(np.float32)
            projected_pred = np.argmax(projected_logits, axis=1).astype(np.int64)
            projected_oracle = np.sum((baseline_pred == y_parent) | (projected_pred == y_parent))
            head_rows.append(
                {
                    "min_abs_corr": min_corr,
                    "fisher_min_share": fisher_min,
                    "active_channels": len(active_channels),
                    "projected_rescue_correct": int(np.sum(projected_pred == y_parent)),
                    "old_plus_projected_oracle": int(projected_oracle),
                    "projected_hard_correct": int(np.sum((projected_pred == y_parent) & groups["hard"])),
                    "projected_c4_correct": int(np.sum((projected_pred == y_parent) & groups["c4"])),
                }
            )
            raw_delta_weight = projected_weight - old.parent_weight
            raw_delta_bias = projected_bias - old.parent_bias
            for mask_percentile in mask_values:
                mask = apply_ties_mask(raw_delta_weight, mask_percentile)
                delta_weight = raw_delta_weight * mask
                # Bias delta is kept, but only under gate. This is safer than unconditional bias soup.
                delta_bias = raw_delta_bias
                delta_logits = logits_from_head(old_data["gap"], delta_weight, delta_bias).astype(np.float32)
                for alpha in alpha_values:
                    for gate_type, gate_thresholds in thresholds.items():
                        for threshold in gate_thresholds:
                            row, pred, gate = evaluate_candidate(
                                "phase1_head_delta_gate",
                                old_logits,
                                delta_logits,
                                y_parent,
                                groups,
                                alpha=alpha,
                                gate_type=gate_type,
                                threshold=threshold,
                                paths=paths,
                            )
                            row.update(
                                {
                                    "min_abs_corr": min_corr,
                                    "fisher_min_share": fisher_min,
                                    "mask_percentile": mask_percentile,
                                    "active_channels": len(active_channels),
                                    "mask_density": float(np.mean(mask)),
                                }
                            )
                            rows.append(row)
                            if best_params is None or safe_score(row) > safe_score(best_params):
                                best_params = dict(row)
                                best_pred = pred.copy()
                                best_gate = gate.copy()
                                best_delta_weight = delta_weight.copy()
                                best_delta_bias = delta_bias.copy()

    rows_sorted = sorted(rows, key=safe_score, reverse=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "alignment_channels.csv", alignment_rows)
    write_csv(args.output_dir / "projected_head_summary.csv", head_rows)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    summary = {
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "group_counts": {name: int(np.sum(mask)) for name, mask in groups.items()},
        "baseline": baseline,
        "ctd_reference": ctd,
        "best": best_params,
        "top10": rows_sorted[:10],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    if best_params is not None and best_pred is not None and best_gate is not None:
        sample_rows = []
        for index, path in enumerate(paths):
            sample_rows.append(
                {
                    "index": index,
                    "path": path,
                    "file": Path(path).name,
                    "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "parent": PARENT_NAMES[int(y_parent[index])],
                    "old_pred": PARENT_NAMES[int(baseline_pred[index])],
                    "ctd_pred": PARENT_NAMES[int(rescue_pred[index])],
                    "phase1_pred": PARENT_NAMES[int(best_pred[index])],
                    "phase1_correct": bool(best_pred[index] == y_parent[index]),
                    "gate": bool(best_gate[index]),
                    "old_correct": bool(baseline_pred[index] == y_parent[index]),
                    "ctd_correct": bool(rescue_pred[index] == y_parent[index]),
                    "stable": bool(groups["stable"][index]),
                    "preserve": bool(groups["preserve"][index]),
                    "rescue": bool(groups["rescue"][index]),
                    "hard": bool(groups["hard"][index]),
                    "c4": bool(groups["c4"][index]),
                }
            )
        write_csv(args.output_dir / "best_sample_decisions.csv", sample_rows)
        np.savez_compressed(
            args.output_dir / "best_phase1_head_delta_params.npz",
            old_parent_weight=old.parent_weight.astype(np.float32),
            old_parent_bias=old.parent_bias.astype(np.float32),
            delta_weight=best_delta_weight.astype(np.float32),  # type: ignore[union-attr]
            delta_bias=best_delta_bias.astype(np.float32),  # type: ignore[union-attr]
            best_config_json=json.dumps(best_params, ensure_ascii=False),
        )
    print(json.dumps(summary["best"], indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
