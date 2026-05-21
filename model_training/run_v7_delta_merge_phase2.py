import argparse
import json
import os
import re
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from analyze_v7_expert_tflite_reverse import TfliteReverse
from run_v7_delta_merge_phase1 import (
    PARENT_NAMES,
    counts_for,
    group_masks,
    pred_margin,
    write_csv,
)
import train_tiny32_v5_visual_subclass_scan as train


def parse_floats(text: str) -> list[float]:
    return [float(item) for item in text.split(",") if item.strip()]


def parse_ranks(text: str) -> list[int | None]:
    ranks: list[int | None] = []
    for item in text.split(","):
        value = item.strip().lower()
        if not value:
            continue
        if value in {"full", "none", "0"}:
            ranks.append(None)
        else:
            ranks.append(int(value))
    return ranks


def zfit(values: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = np.mean(values, axis=0)
    std = np.std(values, axis=0) + 1.0e-6
    return (values - mean) / std, mean, std


def zapply(values: np.ndarray, mean: np.ndarray, std: np.ndarray) -> np.ndarray:
    return (values - mean) / std


def append_bias(features: np.ndarray) -> np.ndarray:
    return np.concatenate([features, np.ones((features.shape[0], 1), dtype=features.dtype)], axis=1)


def force_true_margin_delta(old_logits: np.ndarray, y_parent: np.ndarray, mask: np.ndarray, margin: float) -> np.ndarray:
    target = old_logits.copy()
    for index in np.where(mask)[0].tolist():
        true_id = int(y_parent[index])
        others = [item for item in range(len(PARENT_NAMES)) if item != true_id]
        target[index, true_id] = max(target[index, true_id], float(np.max(target[index, others]) + margin))
    return target - old_logits


def build_target_delta(
    mode: str,
    old_logits: np.ndarray,
    rescue_logits: np.ndarray,
    y_parent: np.ndarray,
    positive_mask: np.ndarray,
    margin: float,
    blend: float,
) -> np.ndarray:
    ctd_delta = rescue_logits - old_logits
    margin_delta = force_true_margin_delta(old_logits, y_parent, positive_mask, margin)
    target = np.zeros_like(old_logits, dtype=np.float64)
    if mode == "ctd_delta":
        target[positive_mask] = ctd_delta[positive_mask]
    elif mode == "margin_target":
        target[positive_mask] = margin_delta[positive_mask]
    elif mode == "hybrid_ctd_margin":
        target[positive_mask] = blend * ctd_delta[positive_mask] + (1.0 - blend) * margin_delta[positive_mask]
    else:
        raise ValueError(f"unknown target mode: {mode}")
    return target


def sample_weights(profile: str, groups: dict[str, np.ndarray], positive_mask: np.ndarray) -> np.ndarray:
    weights = np.ones(len(positive_mask), dtype=np.float64)
    if profile == "balanced":
        weights[:] = 0.25
        weights[groups["stable"]] = 0.5
        weights[groups["preserve"]] = 80.0
        weights[positive_mask] = 80.0
    elif profile == "preserve_locked":
        weights[:] = 0.25
        weights[groups["stable"]] = 0.75
        weights[groups["preserve"]] = 160.0
        weights[positive_mask] = 120.0
    elif profile == "rescue_heavy":
        weights[:] = 0.15
        weights[groups["stable"]] = 0.35
        weights[groups["preserve"]] = 80.0
        weights[positive_mask] = 180.0
    else:
        raise ValueError(f"unknown weight profile: {profile}")
    return weights


def weighted_ridge(features_with_bias: np.ndarray, target: np.ndarray, weights: np.ndarray, l2: float) -> np.ndarray:
    sqrt_w = np.sqrt(weights)[:, None]
    xw = features_with_bias * sqrt_w
    yw = target * sqrt_w
    reg = np.eye(features_with_bias.shape[1], dtype=np.float64) * l2
    reg[-1, -1] = l2 * 0.01
    return np.linalg.solve(xw.T @ xw + reg, xw.T @ yw)


def truncate_adapter_rank(coef: np.ndarray, rank: int | None) -> np.ndarray:
    if rank is None:
        return coef.copy()
    weight = coef[:-1]
    bias = coef[-1:].copy()
    u, s, vt = np.linalg.svd(weight, full_matrices=False)
    keep = min(rank, len(s))
    low_rank = (u[:, :keep] * s[:keep]) @ vt[:keep]
    return np.concatenate([low_rank, bias], axis=0)


def sparsify_adapter(coef: np.ndarray, percentile: float) -> tuple[np.ndarray, float]:
    if percentile <= 0:
        return coef.copy(), 1.0
    out = coef.copy()
    weight = out[:-1]
    threshold = float(np.percentile(np.abs(weight), percentile))
    mask = np.abs(weight) >= threshold
    weight *= mask
    return out, float(np.mean(mask))


def old_feature_matrix(old_gap: np.ndarray, old_logits: np.ndarray) -> dict[str, np.ndarray]:
    old_pred = np.argmax(old_logits, axis=1)
    pred_one_hot = np.eye(len(PARENT_NAMES), dtype=np.float64)[old_pred]
    return {
        "old_gap": old_gap.astype(np.float64),
        "old_gap_logits": np.concatenate(
            [
                old_gap.astype(np.float64),
                old_logits.astype(np.float64),
                pred_margin(old_logits).reshape(-1, 1).astype(np.float64),
                pred_one_hot,
            ],
            axis=1,
        ),
    }


def learned_gate_bank(
    old_gap: np.ndarray,
    old_logits: np.ndarray,
    groups: dict[str, np.ndarray],
    gate_l2_values: list[float],
) -> dict[str, dict[str, object]]:
    labels = np.where(groups["rescue"], 1.0, -1.0).astype(np.float64)
    weights = np.ones(len(labels), dtype=np.float64) * 0.25
    weights[groups["stable"]] = 0.50
    weights[groups["preserve"]] = 140.0
    weights[groups["rescue"]] = 180.0
    bank: dict[str, dict[str, object]] = {}
    for feature_name, raw_features in old_feature_matrix(old_gap, old_logits).items():
        z, mean, std = zfit(raw_features)
        xb = append_bias(z)
        for gate_l2 in gate_l2_values:
            coef = weighted_ridge(xb, labels.reshape(-1, 1), weights, gate_l2).reshape(-1)
            score = xb @ coef
            key = f"learned_{feature_name}_l2_{gate_l2:g}"
            sorted_score = np.sort(score)[::-1]
            thresholds = [float(np.min(score) - 1.0), float(np.max(score) + 1.0)]
            for gate_count in [3, 5, 7, 9, 12, 20, 35, 50, 80, 120]:
                if gate_count < len(sorted_score):
                    thresholds.append(float((sorted_score[gate_count - 1] + sorted_score[gate_count]) / 2.0))
            if np.any(groups["preserve"]):
                thresholds.append(float(np.max(score[groups["preserve"]]) + 1.0e-6))
            if np.any(groups["rescue"]):
                rescue_scores = np.sort(score[groups["rescue"]])
                thresholds.extend(float(item - 1.0e-6) for item in rescue_scores)
            thresholds = sorted(set(round(item, 8) for item in thresholds))
            bank[key] = {
                "feature_name": feature_name,
                "l2": gate_l2,
                "feature_mean": mean,
                "feature_std": std,
                "coef": coef,
                "score": score,
                "thresholds": thresholds,
            }
    return bank


def analytic_gate_specs(
    old_logits: np.ndarray,
    adapter_logits: np.ndarray,
    delta_logits: np.ndarray,
) -> list[tuple[str, float, np.ndarray]]:
    old_pred = np.argmax(old_logits, axis=1)
    adapter_pred = np.argmax(adapter_logits, axis=1)
    old_m = pred_margin(old_logits)
    adapter_m = pred_margin(adapter_logits)
    delta_norm = np.linalg.norm(delta_logits, axis=1)
    specs: list[tuple[str, float, np.ndarray]] = [("always", 0.0, np.ones(len(old_logits), dtype=bool))]
    for threshold in np.linspace(0.0, 2.0, 41):
        specs.append(("old_margin_lt", float(threshold), old_m < threshold))
    for threshold in np.linspace(0.0, 8.0, 41):
        specs.append(("adapter_margin_gt", float(threshold), adapter_m > threshold))
        specs.append(
            (
                "disagree_adapter_margin_gt",
                float(threshold),
                (old_pred != adapter_pred) & (adapter_m > threshold),
            )
        )
    max_delta = max(1.0, float(np.max(delta_norm)))
    for threshold in np.linspace(0.0, max_delta, 49):
        specs.append(("delta_norm_gt", float(threshold), delta_norm > threshold))
    return specs


def evaluate_with_gate(
    name: str,
    old_logits: np.ndarray,
    delta_logits: np.ndarray,
    y_parent: np.ndarray,
    groups: dict[str, np.ndarray],
    paths: list[str],
    alpha: float,
    gate_name: str,
    threshold: float,
    gate: np.ndarray,
    deployable: bool,
) -> tuple[dict[str, object], np.ndarray]:
    final_logits = old_logits + gate[:, None].astype(np.float64) * alpha * delta_logits
    pred = np.argmax(final_logits, axis=1).astype(np.int64)
    metrics = counts_for(pred, y_parent, groups)
    metrics.update(
        {
            "name": name,
            "deployable": bool(deployable),
            "alpha": float(alpha),
            "gate_type": gate_name,
            "threshold": float(threshold),
            "gate_count": int(np.sum(gate)),
            "preserve_false_trigger": int(np.sum(gate & groups["preserve"])),
            "stable_false_trigger": int(np.sum(gate & groups["stable"])),
            "rescue_trigger": int(np.sum(gate & groups["rescue"])),
            "wrong_files": [Path(paths[index]).name for index in np.where(pred != y_parent)[0].tolist()],
        }
    )
    return metrics, pred


def row_score(row: dict[str, object]) -> tuple[object, ...]:
    preserve_locked = int(row["preserve_correct"]) == int(row["preserve_total"]) and int(row["preserve_false_trigger"]) == 0
    stable_correct = int(row["stable_correct"]) == int(row["stable_total"])
    return (
        bool(row.get("deployable", False)),
        preserve_locked,
        stable_correct,
        int(row["all_correct"]),
        int(row["rescue_correct"]),
        int(row["hard_correct"]),
        int(row["c4_correct"]),
        -int(row["preserve_false_trigger"]),
        -int(row["stable_false_trigger"]),
        -int(row["gate_count"]),
    )


def should_keep_row(row: dict[str, object], keep_min_correct: int) -> bool:
    if row["name"] in {"old_stable_baseline", "old_ctd_rescue_reference"}:
        return True
    if int(row["all_correct"]) >= keep_min_correct:
        return True
    preserve_locked = int(row["preserve_correct"]) == int(row["preserve_total"]) and int(row["preserve_false_trigger"]) == 0
    return preserve_locked and int(row["rescue_correct"]) >= 3


def trim_kept_rows(rows: list[dict[str, object]], max_rows: int) -> list[dict[str, object]]:
    if len(rows) <= max_rows:
        return rows
    required = [row for row in rows if row["name"] in {"old_stable_baseline", "old_ctd_rescue_reference"}]
    optional = [row for row in rows if row["name"] not in {"old_stable_baseline", "old_ctd_rescue_reference"}]
    optional = sorted(optional, key=row_score, reverse=True)
    return required + optional[: max(0, max_rows - len(required))]


def main() -> None:
    parser = argparse.ArgumentParser(description="V7 phase2 low-rank rescue adapter and distilled router search.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--l2", default="0.001,0.01,0.1,1,10,100")
    parser.add_argument("--gate-l2", default="0.001,0.01,0.1")
    parser.add_argument("--rank", default="full,1,2,3")
    parser.add_argument("--mask-percentile", default="0,70,80,90,95")
    parser.add_argument("--alpha", default="0.5,0.75,1.0,1.5,2.0,3.0")
    parser.add_argument("--margin", default="2,4,6")
    parser.add_argument("--blend", type=float, default=0.5)
    parser.add_argument("--keep-min-correct", type=int, default=298)
    parser.add_argument("--max-kept-rows", type=int, default=50000)
    parser.add_argument("--learned-router-top-adapters", type=int, default=300)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old = TfliteReverse(args.old_tflite)
    rescue = TfliteReverse(args.rescue_tflite)
    old_data = old.infer_dataset(x)
    rescue_data = rescue.infer_dataset(x)

    old_logits = old_data["logits"].astype(np.float64)
    rescue_logits = rescue_data["logits"].astype(np.float64)
    old_pred = old_data["pred"].astype(np.int64)
    rescue_pred = rescue_data["pred"].astype(np.int64)
    groups = group_masks(paths, y_sub, y_parent, old_pred, rescue_pred)
    positive_mask = groups["rescue"]

    old_gap = old_data["gap"].astype(np.float64)
    gap_z, feature_mean, feature_std = zfit(old_gap)
    features = append_bias(gap_z)

    baseline = counts_for(old_pred, y_parent, groups)
    baseline.update(
        {
            "name": "old_stable_baseline",
            "deployable": True,
            "gate_type": "none",
            "gate_count": 0,
            "preserve_false_trigger": 0,
            "stable_false_trigger": 0,
            "rescue_trigger": 0,
            "wrong_files": [Path(paths[index]).name for index in np.where(old_pred != y_parent)[0].tolist()],
        }
    )
    ctd = counts_for(rescue_pred, y_parent, groups)
    ctd.update(
        {
            "name": "old_ctd_rescue_reference",
            "deployable": False,
            "gate_type": "none",
            "gate_count": len(y_parent),
            "preserve_false_trigger": int(np.sum(groups["preserve"])),
            "stable_false_trigger": int(np.sum(groups["stable"])),
            "rescue_trigger": int(np.sum(groups["rescue"])),
            "wrong_files": [Path(paths[index]).name for index in np.where(rescue_pred != y_parent)[0].tolist()],
        }
    )

    l2_values = parse_floats(args.l2)
    gate_l2_values = parse_floats(args.gate_l2)
    rank_values = parse_ranks(args.rank)
    mask_values = parse_floats(args.mask_percentile)
    alpha_values = parse_floats(args.alpha)
    margin_values = parse_floats(args.margin)
    target_modes = ["ctd_delta", "margin_target", "hybrid_ctd_margin"]
    weight_profiles = ["balanced", "preserve_locked", "rescue_heavy"]
    gate_bank = learned_gate_bank(old_gap, old_logits, groups, gate_l2_values)

    rows: list[dict[str, object]] = [baseline, ctd]
    best_deployable: dict[str, object] | None = None
    best_deployable_pred: np.ndarray | None = None
    best_deployable_gate: np.ndarray | None = None
    best_deployable_coef: np.ndarray | None = None
    best_gate_record: dict[str, object] | None = None
    best_oracle: dict[str, object] | None = None
    adapter_records: list[dict[str, object]] = []

    for target_mode in target_modes:
        mode_margins = [0.0] if target_mode == "ctd_delta" else margin_values
        for margin in mode_margins:
            target_delta = build_target_delta(
                target_mode,
                old_logits,
                rescue_logits,
                y_parent,
                positive_mask,
                margin=margin,
                blend=args.blend,
            )
            for profile in weight_profiles:
                weights = sample_weights(profile, groups, positive_mask)
                for l2 in l2_values:
                    raw_coef = weighted_ridge(features, target_delta, weights, l2)
                    for rank in rank_values:
                        ranked_coef = truncate_adapter_rank(raw_coef, rank)
                        for mask_percentile in mask_values:
                            sparse_coef, mask_density = sparsify_adapter(ranked_coef, mask_percentile)
                            raw_delta_logits = features @ sparse_coef
                            for alpha in alpha_values:
                                adapter_delta_logits = raw_delta_logits
                                adapter_logits = old_logits + alpha * adapter_delta_logits
                                adapter_pred = np.argmax(adapter_logits, axis=1).astype(np.int64)

                                oracle_gate = (old_pred != y_parent) & (adapter_pred == y_parent)
                                oracle_row, _oracle_pred = evaluate_with_gate(
                                    "phase2_lowrank_adapter_oracle",
                                    old_logits,
                                    adapter_delta_logits,
                                    y_parent,
                                    groups,
                                    paths,
                                    alpha,
                                    "label_oracle_old_wrong_adapter_correct",
                                    0.0,
                                    oracle_gate,
                                    False,
                                )
                                oracle_row.update(
                                    {
                                        "target_mode": target_mode,
                                        "weight_profile": profile,
                                        "l2": float(l2),
                                        "rank": "full" if rank is None else int(rank),
                                        "mask_percentile": float(mask_percentile),
                                        "mask_density": mask_density,
                                        "margin": float(margin),
                                    }
                                )
                                rows.append(oracle_row)
                                if not should_keep_row(oracle_row, args.keep_min_correct):
                                    rows.pop()
                                if best_oracle is None or row_score(oracle_row) > row_score(best_oracle):
                                    best_oracle = dict(oracle_row)
                                adapter_records.append(
                                    {
                                        "oracle_row": dict(oracle_row),
                                        "delta_logits": adapter_delta_logits.copy(),
                                        "coef": sparse_coef.copy(),
                                        "alpha": float(alpha),
                                        "target_mode": target_mode,
                                        "weight_profile": profile,
                                        "l2": float(l2),
                                        "rank": "full" if rank is None else int(rank),
                                        "mask_percentile": float(mask_percentile),
                                        "mask_density": mask_density,
                                        "margin": float(margin),
                                    }
                                )
                                if len(adapter_records) > args.learned_router_top_adapters * 2:
                                    adapter_records = sorted(
                                        adapter_records,
                                        key=lambda item: row_score(item["oracle_row"]),  # type: ignore[arg-type]
                                        reverse=True,
                                    )[: args.learned_router_top_adapters]

                                gate_specs = analytic_gate_specs(old_logits, adapter_logits, adapter_delta_logits)
                                for gate_name, threshold, gate in gate_specs:
                                    row, pred = evaluate_with_gate(
                                        "phase2_lowrank_adapter_gate",
                                        old_logits,
                                        adapter_delta_logits,
                                        y_parent,
                                        groups,
                                        paths,
                                        alpha,
                                        gate_name,
                                        threshold,
                                        gate,
                                        True,
                                    )
                                    row.update(
                                        {
                                            "target_mode": target_mode,
                                            "weight_profile": profile,
                                            "l2": float(l2),
                                            "rank": "full" if rank is None else int(rank),
                                            "mask_percentile": float(mask_percentile),
                                            "mask_density": mask_density,
                                            "margin": float(margin),
                                            "router": "analytic",
                                            "gate_l2": "",
                                            "gate_feature_name": "",
                                        }
                                    )
                                    if should_keep_row(row, args.keep_min_correct):
                                        rows.append(row)
                                        rows = trim_kept_rows(rows, args.max_kept_rows)
                                    if best_deployable is None or row_score(row) > row_score(best_deployable):
                                        best_deployable = dict(row)
                                        best_deployable_pred = pred.copy()
                                        best_deployable_gate = gate.copy()
                                        best_deployable_coef = sparse_coef.copy()
                                        best_gate_record = {"router": "analytic", "gate_name": gate_name}

    adapter_records = sorted(
        adapter_records,
        key=lambda item: row_score(item["oracle_row"]),  # type: ignore[arg-type]
        reverse=True,
    )[: args.learned_router_top_adapters]
    for adapter_record in adapter_records:
        alpha = float(adapter_record["alpha"])
        adapter_delta_logits = adapter_record["delta_logits"]
        sparse_coef = adapter_record["coef"]
        assert isinstance(adapter_delta_logits, np.ndarray)
        assert isinstance(sparse_coef, np.ndarray)
        adapter_logits = old_logits + alpha * adapter_delta_logits
        adapter_pred = np.argmax(adapter_logits, axis=1).astype(np.int64)
        old_adapter_disagree = old_pred != adapter_pred
        adapter_margin = pred_margin(adapter_logits)
        for gate_key, gate_record in gate_bank.items():
            score = gate_record["score"]  # type: ignore[index]
            assert isinstance(score, np.ndarray)
            thresholds = gate_record["thresholds"]  # type: ignore[index]
            assert isinstance(thresholds, list)
            for threshold in thresholds:
                base_gate = score > float(threshold)
                learned_variants = [
                    (f"{gate_key}", base_gate),
                    (f"{gate_key}_disagree", base_gate & old_adapter_disagree),
                    (
                        f"{gate_key}_disagree_margin_gt_1",
                        base_gate & old_adapter_disagree & (adapter_margin > 1.0),
                    ),
                ]
                for low_threshold in thresholds:
                    low_threshold = float(low_threshold)
                    if low_threshold >= float(threshold):
                        continue
                    for margin_limit in [0.05, 0.10, 0.20, 0.50]:
                        tail_gate = (score > low_threshold) & old_adapter_disagree & (adapter_margin < margin_limit)
                        learned_variants.append(
                            (
                                f"{gate_key}_two_band_disagree_tail_margin_lt_{margin_limit:g}_low_{low_threshold:g}",
                                (base_gate & old_adapter_disagree) | tail_gate,
                            )
                        )
                for gate_name, gate in learned_variants:
                    row, pred = evaluate_with_gate(
                        "phase2_lowrank_adapter_learned_router",
                        old_logits,
                        adapter_delta_logits,
                        y_parent,
                        groups,
                        paths,
                        alpha,
                        gate_name,
                        float(threshold),
                        gate,
                        True,
                    )
                    row.update(
                        {
                            "target_mode": str(adapter_record["target_mode"]),
                            "weight_profile": str(adapter_record["weight_profile"]),
                            "l2": float(adapter_record["l2"]),
                            "rank": adapter_record["rank"],
                            "mask_percentile": float(adapter_record["mask_percentile"]),
                            "mask_density": float(adapter_record["mask_density"]),
                            "margin": float(adapter_record["margin"]),
                            "router": "learned_old_stable",
                            "gate_l2": float(gate_record["l2"]),  # type: ignore[index]
                            "gate_feature_name": str(gate_record["feature_name"]),  # type: ignore[index]
                        }
                    )
                    if should_keep_row(row, args.keep_min_correct):
                        rows.append(row)
                        rows = trim_kept_rows(rows, args.max_kept_rows)
                    if best_deployable is None or row_score(row) > row_score(best_deployable):
                        best_deployable = dict(row)
                        best_deployable_pred = pred.copy()
                        best_deployable_gate = gate.copy()
                        best_deployable_coef = sparse_coef.copy()
                        best_gate_record = dict(gate_record)
                        best_gate_record["router"] = "learned_old_stable"
                        best_gate_record["gate_name"] = gate_name

    rows_sorted = sorted(rows, key=row_score, reverse=True)
    deployable_rows = [row for row in rows_sorted if bool(row.get("deployable", False)) and row["name"] != "old_stable_baseline"]
    oracle_rows = [row for row in rows_sorted if row["name"] == "phase2_lowrank_adapter_oracle"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)

    summary = {
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "group_counts": {name: int(np.sum(mask)) for name, mask in groups.items()},
        "baseline": baseline,
        "ctd_reference": ctd,
        "best_deployable": best_deployable,
        "best_oracle_upper_bound": best_oracle,
        "top10_deployable": deployable_rows[:10],
        "top10_oracle": oracle_rows[:10],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    if best_deployable is not None and best_deployable_pred is not None and best_deployable_gate is not None:
        sample_rows = []
        for index, path in enumerate(paths):
            sample_rows.append(
                {
                    "index": index,
                    "path": path,
                    "file": Path(path).name,
                    "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "parent": PARENT_NAMES[int(y_parent[index])],
                    "old_pred": PARENT_NAMES[int(old_pred[index])],
                    "ctd_pred": PARENT_NAMES[int(rescue_pred[index])],
                    "phase2_pred": PARENT_NAMES[int(best_deployable_pred[index])],
                    "phase2_correct": bool(best_deployable_pred[index] == y_parent[index]),
                    "gate": bool(best_deployable_gate[index]),
                    "old_correct": bool(old_pred[index] == y_parent[index]),
                    "ctd_correct": bool(rescue_pred[index] == y_parent[index]),
                    "stable": bool(groups["stable"][index]),
                    "preserve": bool(groups["preserve"][index]),
                    "rescue": bool(groups["rescue"][index]),
                    "hard": bool(groups["hard"][index]),
                    "c4": bool(groups["c4"][index]),
                }
            )
        write_csv(args.output_dir / "best_sample_decisions.csv", sample_rows)
        save_kwargs: dict[str, object] = {
            "old_parent_weight": old.parent_weight.astype(np.float32),
            "old_parent_bias": old.parent_bias.astype(np.float32),
            "adapter_coef": best_deployable_coef.astype(np.float32),  # type: ignore[union-attr]
            "feature_mean": feature_mean.astype(np.float32),
            "feature_std": feature_std.astype(np.float32),
            "best_config_json": json.dumps(best_deployable, ensure_ascii=False),
        }
        if best_gate_record is not None and best_gate_record.get("router") == "learned_old_stable":
            gate_type = str(best_deployable["gate_type"])
            tail_match = re.search(r"tail_margin_lt_([0-9.]+)_low_([0-9.]+)", gate_type)
            tail_margin_limit = float(tail_match.group(1)) if tail_match else np.nan
            tail_low_threshold = float(tail_match.group(2)) if tail_match else np.nan
            save_kwargs.update(
                {
                    "gate_feature_name": np.asarray(str(best_gate_record["feature_name"])),
                    "gate_feature_mean": best_gate_record["feature_mean"],  # type: ignore[index]
                    "gate_feature_std": best_gate_record["feature_std"],  # type: ignore[index]
                    "gate_coef": best_gate_record["coef"],  # type: ignore[index]
                    "gate_threshold": np.asarray(float(best_deployable["threshold"])),
                    "gate_tail_low_threshold": np.asarray(tail_low_threshold),
                    "gate_tail_margin_limit": np.asarray(tail_margin_limit),
                }
            )
        np.savez_compressed(args.output_dir / "best_phase2_adapter_params.npz", **save_kwargs)

    print(json.dumps(summary["best_deployable"], indent=2, ensure_ascii=False))
    print(json.dumps({"best_oracle_upper_bound": summary["best_oracle_upper_bound"]}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
