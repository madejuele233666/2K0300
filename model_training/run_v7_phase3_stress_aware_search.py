import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
from typing import Iterator

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from analyze_v6_parent100_error_attribution import DEFAULT_STRESS
from analyze_v7_expert_tflite_reverse import TfliteReverse
from run_v7_delta_merge_phase1 import PARENT_NAMES, counts_for, group_masks, pred_margin, true_margin
from run_v7_delta_merge_phase2 import (
    append_bias,
    build_target_delta,
    old_feature_matrix as phase2_old_feature_matrix,
    parse_floats,
    parse_ranks,
    sparsify_adapter,
    truncate_adapter_rank,
    weighted_ridge,
    zapply,
    zfit,
)
import train_tiny32_v5_visual_subclass_scan as train


ROT_MIRROR_VIEWS = [
    "rot90",
    "rot180",
    "rot270",
    "mirror_lr",
    "mirror_lr_rot90",
    "mirror_lr_rot180",
    "mirror_lr_rot270",
]
NOISE_VIEWS = ["noise_0p06", "noise_0p10"]
BLUR_NOISE_VIEWS = ["hblur5_noise_0p06", "diagblur5_noise_0p08", "vblur5", "diagblur5"]
CAMERA_VIEWS = [
    "cam_blur2a0",
    "cam_blur3a90",
    "cam_blur5a45",
    "cam_blur5a135",
    "cam_noise0p02",
    "cam_noise0p04",
    "cam_blur3a0_noise0p02",
    "cam_blur5a45_noise0p04",
]
WORST_PHASE2_VIEWS = [
    "diagblur5_noise_0p08",
    "mirror_lr_rot90",
    "cam_blur5a135",
    "rot90",
    "cam_blur5a45_noise0p04",
]

VIEW_PROFILES: dict[str, list[str]] = {
    "clean_rot90": ["clean", "rot90"],
    "rotmirror": ["clean"] + ROT_MIRROR_VIEWS,
    "rotmirror_noise": ["clean"] + ROT_MIRROR_VIEWS + NOISE_VIEWS,
    "rotmirror_blur_noise": ["clean"] + ROT_MIRROR_VIEWS + NOISE_VIEWS + BLUR_NOISE_VIEWS,
    "rotmirror_camera_light": ["clean"] + ROT_MIRROR_VIEWS + ["cam_blur2a0", "cam_noise0p02", "cam_blur3a0_noise0p02"],
    "rotmirror_camera_full": ["clean"] + ROT_MIRROR_VIEWS + CAMERA_VIEWS,
    "worst_phase2": ["clean"] + ROT_MIRROR_VIEWS + WORST_PHASE2_VIEWS,
    "all_stress": ["clean"] + ROT_MIRROR_VIEWS + NOISE_VIEWS + BLUR_NOISE_VIEWS + CAMERA_VIEWS,
}

POSITIVE_PROFILES = [
    "clean_rescue_all_views",
    "view_rescue_preserve_locked",
    "hybrid_clean_stress",
    "margin_improve_hard",
    "stress_union_conservative",
    "stress_recovery_allow_stable",
    "old_wrong_rotmirror",
    "old_wrong_rotmirror_plus_rescue",
    "old_wrong_all_views",
    "old_wrong_all_plus_rescue",
]
WEIGHT_PROFILES = [
    "balanced",
    "preserve_locked",
    "stable_locked",
    "rescue_heavy",
    "group_dro",
    "camera_guard",
    "strict_geo_lock",
    "rotmirror_dro",
]
TARGET_MODES = ["ctd_delta", "margin_target", "hybrid_ctd_margin"]
ADAPTER_FEATURES = [
    "old_gap",
    "old_gap_logits",
    "old_gap_poly",
    "old_gap_logits_poly",
    "old_gap_logits_interact",
]
GATE_FEATURES = ["old_gap", "old_gap_logits"]


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


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def csv_text(value: object) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, (list, tuple)):
        return ",".join(str(item) for item in value)
    return str(value)


def stable_hash(text: str) -> int:
    digest = hashlib.sha1(text.encode("utf-8")).hexdigest()
    return int(digest[:16], 16)


def domain_name(view: str) -> str:
    if view == "clean":
        return "clean"
    if view in ROT_MIRROR_VIEWS:
        return "rotmirror"
    if view in CAMERA_VIEWS or view.startswith("cam_"):
        return "camera"
    if "blur" in view:
        return "blur_noise"
    if view.startswith("noise"):
        return "noise"
    return "other"


def _softmax_features(logits: np.ndarray) -> np.ndarray:
    values = logits.astype(np.float64)
    shifted = values - np.max(values, axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / np.sum(exp, axis=1, keepdims=True)


def _expanded_old_features(old_gap: np.ndarray, old_logits: np.ndarray) -> dict[str, np.ndarray]:
    gap = old_gap.astype(np.float64)
    logits = old_logits.astype(np.float64)
    centered_logits = logits - np.mean(logits, axis=1, keepdims=True)
    old_pred = np.argmax(logits, axis=1)
    pred_one_hot = np.eye(len(PARENT_NAMES), dtype=np.float64)[old_pred]
    margin = pred_margin(logits).reshape(-1, 1).astype(np.float64)
    probs = _softmax_features(logits)
    gap_sq = gap * gap
    gap_abs = np.abs(gap)
    logits_sq = logits * logits
    gap_logit_interactions = np.concatenate(
        [gap * centered_logits[:, class_id : class_id + 1] for class_id in range(logits.shape[1])],
        axis=1,
    )
    base = phase2_old_feature_matrix(gap, logits)
    gap_poly = np.concatenate([gap, gap_sq, gap_abs], axis=1)
    logits_poly = np.concatenate(
        [
            gap,
            gap_sq,
            gap_abs,
            logits,
            centered_logits,
            logits_sq,
            margin,
            probs,
            pred_one_hot,
        ],
        axis=1,
    )
    return {
        **base,
        "old_gap_poly": gap_poly,
        "old_gap_logits_poly": logits_poly,
        "old_gap_logits_interact": np.concatenate([logits_poly, gap_logit_interactions], axis=1),
    }


def raw_adapter_features(feature_name: str, old_gap: np.ndarray, old_logits: np.ndarray) -> np.ndarray:
    features = _expanded_old_features(old_gap, old_logits)
    if feature_name not in features:
        raise ValueError(f"unknown adapter feature: {feature_name}")
    return features[feature_name]


def raw_gate_features(feature_name: str, old_gap: np.ndarray, old_logits: np.ndarray) -> np.ndarray:
    features = _expanded_old_features(old_gap, old_logits)
    if feature_name not in features:
        raise ValueError(f"unknown gate feature: {feature_name}")
    return features[feature_name]


def as_str_list(array: np.ndarray) -> list[str]:
    return [str(item) for item in array.tolist()]


def load_or_build_cache(
    *,
    cache_path: Path,
    dataset_dir: Path,
    old_tflite: Path,
    rescue_tflite: Path,
    stress_names: list[str],
    rebuild: bool,
) -> dict[str, object]:
    if cache_path.exists() and not rebuild:
        with np.load(cache_path, allow_pickle=True) as data:
            return {key: data[key] for key in data.files}

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(dataset_dir)
    old = TfliteReverse(old_tflite)
    rescue = TfliteReverse(rescue_tflite)
    view_names = ["clean"] + stress_names
    old_logits_rows: list[np.ndarray] = []
    old_gap_rows: list[np.ndarray] = []
    old_pred_rows: list[np.ndarray] = []
    rescue_logits_rows: list[np.ndarray] = []
    rescue_pred_rows: list[np.ndarray] = []
    for view in view_names:
        xs = x if view == "clean" else train.stress_batch_any(view, x)
        old_data = old.infer_dataset(xs)
        rescue_data = rescue.infer_dataset(xs)
        old_logits_rows.append(old_data["logits"].astype(np.float32))
        old_gap_rows.append(old_data["gap"].astype(np.float32))
        old_pred_rows.append(old_data["pred"].astype(np.int64))
        rescue_logits_rows.append(rescue_data["logits"].astype(np.float32))
        rescue_pred_rows.append(rescue_data["pred"].astype(np.int64))
        print(
            json.dumps(
                {
                    "cache_view_done": view,
                    "old_correct": int(np.sum(old_data["pred"].astype(np.int64) == y_parent)),
                    "rescue_correct": int(np.sum(rescue_data["pred"].astype(np.int64) == y_parent)),
                },
                ensure_ascii=False,
            ),
            flush=True,
        )

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cache_path,
        view_names=np.asarray(view_names),
        paths=np.asarray(paths),
        y_sub=y_sub.astype(np.int64),
        y_parent=y_parent.astype(np.int64),
        old_logits=np.stack(old_logits_rows).astype(np.float32),
        old_gap=np.stack(old_gap_rows).astype(np.float32),
        old_pred=np.stack(old_pred_rows).astype(np.int64),
        rescue_logits=np.stack(rescue_logits_rows).astype(np.float32),
        rescue_pred=np.stack(rescue_pred_rows).astype(np.int64),
        old_tflite=np.asarray(str(old_tflite)),
        rescue_tflite=np.asarray(str(rescue_tflite)),
    )
    with np.load(cache_path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def clean_groups(cache: dict[str, object]) -> dict[str, np.ndarray]:
    view_names = as_str_list(np.asarray(cache["view_names"]))
    clean_index = view_names.index("clean")
    return group_masks(
        as_str_list(np.asarray(cache["paths"])),
        np.asarray(cache["y_sub"], dtype=np.int64),
        np.asarray(cache["y_parent"], dtype=np.int64),
        np.asarray(cache["old_pred"], dtype=np.int64)[clean_index],
        np.asarray(cache["rescue_pred"], dtype=np.int64)[clean_index],
    )


def flatten_views(cache: dict[str, object], selected_views: list[str]) -> dict[str, object]:
    all_views = as_str_list(np.asarray(cache["view_names"]))
    indexes = [all_views.index(view) for view in selected_views]
    y_sub = np.asarray(cache["y_sub"], dtype=np.int64)
    y_parent = np.asarray(cache["y_parent"], dtype=np.int64)
    n = len(y_parent)
    old_logits = np.asarray(cache["old_logits"], dtype=np.float64)[indexes]
    old_gap = np.asarray(cache["old_gap"], dtype=np.float64)[indexes]
    rescue_logits = np.asarray(cache["rescue_logits"], dtype=np.float64)[indexes]
    old_pred = np.asarray(cache["old_pred"], dtype=np.int64)[indexes]
    rescue_pred = np.asarray(cache["rescue_pred"], dtype=np.int64)[indexes]
    view_labels = np.asarray([selected_views[row] for row in range(len(indexes)) for _ in range(n)])
    sample_index = np.tile(np.arange(n, dtype=np.int64), len(indexes))
    y_parent_rep = np.tile(y_parent, len(indexes))
    y_sub_rep = np.tile(y_sub, len(indexes))
    groups = clean_groups(cache)
    group_rep = {name: np.tile(mask, len(indexes)) for name, mask in groups.items()}
    return {
        "view_names": selected_views,
        "view_labels": view_labels,
        "sample_index": sample_index,
        "old_logits": old_logits.reshape(len(indexes) * n, old_logits.shape[-1]),
        "old_gap": old_gap.reshape(len(indexes) * n, old_gap.shape[-1]),
        "rescue_logits": rescue_logits.reshape(len(indexes) * n, rescue_logits.shape[-1]),
        "old_pred": old_pred.reshape(len(indexes) * n),
        "rescue_pred": rescue_pred.reshape(len(indexes) * n),
        "y_parent": y_parent_rep,
        "y_sub": y_sub_rep,
        "groups": group_rep,
        "is_clean_view": view_labels == "clean",
        "domain": np.asarray([domain_name(view) for view in view_labels.tolist()]),
    }


def positive_mask_for(profile: str, fixture: dict[str, object]) -> np.ndarray:
    groups = fixture["groups"]
    assert isinstance(groups, dict)
    old_pred = np.asarray(fixture["old_pred"], dtype=np.int64)
    rescue_pred = np.asarray(fixture["rescue_pred"], dtype=np.int64)
    y_parent = np.asarray(fixture["y_parent"], dtype=np.int64)
    old_logits = np.asarray(fixture["old_logits"], dtype=np.float64)
    rescue_logits = np.asarray(fixture["rescue_logits"], dtype=np.float64)
    domain = np.asarray(fixture["domain"])
    old_correct = old_pred == y_parent
    rescue_correct = rescue_pred == y_parent
    old_wrong = ~old_correct
    view_rescue = (~old_correct) & rescue_correct
    sample_rescue = np.asarray(groups["rescue"], dtype=bool)
    preserve = np.asarray(groups["preserve"], dtype=bool)
    stable = np.asarray(groups["stable"], dtype=bool)
    hard_or_c4 = np.asarray(groups["hard"], dtype=bool) | np.asarray(groups["c4"], dtype=bool)
    old_tm = true_margin(old_logits, y_parent)
    rescue_tm = true_margin(rescue_logits, y_parent)
    improve = rescue_tm > (old_tm + 0.20)
    stress_view = domain != "clean"
    clean_or_rotmirror = (domain == "clean") | (domain == "rotmirror")

    if profile == "clean_rescue_all_views":
        mask = sample_rescue
    elif profile == "view_rescue_preserve_locked":
        mask = view_rescue & (~preserve)
    elif profile == "hybrid_clean_stress":
        mask = sample_rescue | (view_rescue & (~preserve))
    elif profile == "margin_improve_hard":
        mask = sample_rescue | (hard_or_c4 & improve & (~preserve))
    elif profile == "stress_union_conservative":
        mask = sample_rescue | (stress_view & view_rescue & (~preserve) & (~stable))
    elif profile == "stress_recovery_allow_stable":
        mask = sample_rescue | (stress_view & view_rescue & (~preserve))
    elif profile == "old_wrong_rotmirror":
        mask = old_wrong & clean_or_rotmirror
    elif profile == "old_wrong_rotmirror_plus_rescue":
        mask = sample_rescue | (old_wrong & clean_or_rotmirror)
    elif profile == "old_wrong_all_views":
        mask = old_wrong
    elif profile == "old_wrong_all_plus_rescue":
        mask = sample_rescue | old_wrong
    else:
        raise ValueError(f"unknown positive profile: {profile}")
    return mask.astype(bool)


def sample_weights(profile: str, fixture: dict[str, object], positive: np.ndarray) -> np.ndarray:
    groups = fixture["groups"]
    assert isinstance(groups, dict)
    domain = np.asarray(fixture["domain"])
    y_parent = np.asarray(fixture["y_parent"], dtype=np.int64)
    old_logits = np.asarray(fixture["old_logits"], dtype=np.float64)
    weights = np.ones(len(positive), dtype=np.float64) * 0.08
    stable = np.asarray(groups["stable"], dtype=bool)
    preserve = np.asarray(groups["preserve"], dtype=bool)
    hard = np.asarray(groups["hard"], dtype=bool)
    c4 = np.asarray(groups["c4"], dtype=bool)
    rotmirror = domain == "rotmirror"
    camera = domain == "camera"
    stress = domain != "clean"
    clean = domain == "clean"
    margin_boost = np.clip(1.5 - np.abs(true_margin(old_logits, y_parent)), 0.0, 1.5)

    if profile == "balanced":
        weights[stable] = 0.50
        weights[preserve] = 110.0
        weights[positive] = 120.0
    elif profile == "preserve_locked":
        weights[stable] = 0.55
        weights[preserve] = 240.0
        weights[positive] = 140.0
    elif profile == "stable_locked":
        weights[stable] = 2.50
        weights[preserve] = 220.0
        weights[positive] = 120.0
    elif profile == "rescue_heavy":
        weights[stable] = 0.30
        weights[preserve] = 110.0
        weights[positive] = 260.0
    elif profile == "group_dro":
        weights[stable] = 0.80
        weights[preserve] = 160.0
        weights[positive] = 180.0
        for domain_value in sorted(set(domain.tolist())):
            idx = domain == domain_value
            weights[idx] *= len(weights) / max(1, int(np.sum(idx)))
        weights *= 1.0 / max(1.0, float(np.mean(weights)))
        weights[preserve] *= 4.0
        weights[positive] *= 4.0
    elif profile == "camera_guard":
        weights[stable] = 0.65
        weights[preserve] = 180.0
        weights[positive] = 150.0
        weights[camera & (stable | preserve)] *= 3.0
        weights[camera & positive] *= 1.5
    elif profile == "strict_geo_lock":
        weights[:] = 0.04
        weights[stable] = 0.70
        weights[preserve] = 260.0
        weights[positive] = 240.0
        weights[clean & (stable | preserve)] *= 4.0
        weights[rotmirror & (stable | preserve) & (~positive)] *= 2.5
    elif profile == "rotmirror_dro":
        weights[:] = 0.05
        weights[stable] = 0.80
        weights[preserve] = 210.0
        weights[positive] = 220.0
        for domain_value in ["clean", "rotmirror", "noise", "blur_noise", "camera"]:
            idx = domain == domain_value
            if np.any(idx):
                weights[idx] *= len(weights) / max(1, int(np.sum(idx)))
        weights *= 1.0 / max(1.0, float(np.mean(weights)))
        weights[clean & (stable | preserve)] *= 6.0
        weights[rotmirror & positive] *= 3.0
        weights[rotmirror & (stable | preserve) & (~positive)] *= 3.0
    else:
        raise ValueError(f"unknown weight profile: {profile}")

    weights[rotmirror & (stable | preserve)] *= 1.8
    weights[stress & preserve] *= 1.4
    weights[hard | c4] *= 1.25
    weights *= 1.0 + 0.20 * margin_boost
    return np.clip(weights, 1.0e-4, 1.0e6)


def router_weights(profile: str, fixture: dict[str, object], positive: np.ndarray) -> np.ndarray:
    weights = sample_weights(profile, fixture, positive)
    groups = fixture["groups"]
    assert isinstance(groups, dict)
    weights[positive] *= 1.4
    weights[np.asarray(groups["preserve"], dtype=bool)] *= 1.6
    return weights


def gate_thresholds(score: np.ndarray, positive: np.ndarray, max_thresholds: int) -> list[float]:
    thresholds: set[float] = {float(np.min(score) - 1.0), float(np.max(score) + 1.0)}
    sorted_score = np.sort(score)[::-1]
    for count in [1, 3, 5, 7, 9, 12, 16, 20, 28, 35, 50, 80, 120, 180, 260, 400, 650]:
        if count < len(sorted_score):
            thresholds.add(float((sorted_score[count - 1] + sorted_score[count]) / 2.0))
    for q in np.linspace(0.55, 0.995, 24):
        thresholds.add(float(np.quantile(score, q)))
    if np.any(positive):
        pos = np.sort(score[positive])
        thresholds.add(float(np.min(pos) - 1.0e-6))
        thresholds.add(float(np.median(pos)))
        thresholds.add(float(np.max(pos) + 1.0e-6))
    values = sorted({round(item, 8) for item in thresholds})
    if len(values) <= max_thresholds:
        return values
    stride = max(1, len(values) // max_thresholds)
    kept = values[::stride][:max_thresholds]
    if values[-1] not in kept:
        kept[-1] = values[-1]
    return kept


def fit_gate_bank(
    fixture: dict[str, object],
    positive: np.ndarray,
    weight_profile: str,
    gate_l2_values: list[float],
    gate_feature_names: list[str],
    max_thresholds: int,
) -> list[dict[str, object]]:
    old_gap = np.asarray(fixture["old_gap"], dtype=np.float64)
    old_logits = np.asarray(fixture["old_logits"], dtype=np.float64)
    labels = np.where(positive, 1.0, -1.0).astype(np.float64)
    weights = router_weights(weight_profile, fixture, positive)
    bank: list[dict[str, object]] = []
    for feature_name in gate_feature_names:
        raw = raw_gate_features(feature_name, old_gap, old_logits)
        z, mean, std = zfit(raw)
        xb = append_bias(z)
        for gate_l2 in gate_l2_values:
            coef = weighted_ridge(xb, labels.reshape(-1, 1), weights, gate_l2).reshape(-1)
            score = xb @ coef
            bank.append(
                {
                    "feature_name": feature_name,
                    "l2": float(gate_l2),
                    "feature_mean": mean,
                    "feature_std": std,
                    "coef": coef,
                    "score": score,
                    "thresholds": gate_thresholds(score, positive, max_thresholds),
                }
            )
    return bank


def evaluate_flat(
    *,
    flat_eval: dict[str, object],
    adapter_feature_name: str,
    feature_mean: np.ndarray,
    feature_std: np.ndarray,
    adapter_coef: np.ndarray,
    alpha: float,
    gate: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    old_gap = np.asarray(flat_eval["old_gap"], dtype=np.float64)
    old_logits = np.asarray(flat_eval["old_logits"], dtype=np.float64)
    raw_features = raw_adapter_features(adapter_feature_name, old_gap, old_logits)
    features = append_bias(zapply(raw_features, feature_mean, feature_std))
    delta_logits = features @ adapter_coef
    final_logits = old_logits + gate[:, None].astype(np.float64) * alpha * delta_logits
    pred = np.argmax(final_logits, axis=1).astype(np.int64)
    adapter_logits = old_logits + alpha * delta_logits
    adapter_pred = np.argmax(adapter_logits, axis=1).astype(np.int64)
    adapter_margin = pred_margin(adapter_logits)
    return pred, adapter_pred, adapter_margin, delta_logits


def metrics_across_views(
    *,
    row_base: dict[str, object],
    flat_eval: dict[str, object],
    groups: dict[str, np.ndarray],
    paths: list[str],
    pred: np.ndarray,
    gate: np.ndarray,
    baseline: dict[str, object],
) -> dict[str, object]:
    y_parent = np.asarray(flat_eval["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat_eval["view_labels"])
    clean_mask = view_labels == "clean"
    stress_mask = view_labels != "clean"
    out = dict(row_base)
    clean_groups = {name: mask[clean_mask] for name, mask in groups.items()}
    clean_counts = counts_for(pred[clean_mask], y_parent[clean_mask], clean_groups)
    out.update({f"clean_{key}": value for key, value in clean_counts.items()})
    out["clean_gate_count"] = int(np.sum(gate[clean_mask]))
    out["clean_preserve_false_trigger"] = int(np.sum(gate[clean_mask] & clean_groups["preserve"]))
    out["clean_stable_false_trigger"] = int(np.sum(gate[clean_mask] & clean_groups["stable"]))
    out["clean_wrong_files"] = [
        Path(paths[index]).name for index in np.where(pred[clean_mask] != y_parent[clean_mask])[0].tolist()
    ]
    out["stress_gate_count"] = int(np.sum(gate[stress_mask]))
    out["stress_preserve_false_trigger"] = int(np.sum(gate[stress_mask] & groups["preserve"][stress_mask]))
    out["stress_stable_false_trigger"] = int(np.sum(gate[stress_mask] & groups["stable"][stress_mask]))
    per_view: list[dict[str, object]] = []
    for view in sorted(set(view_labels.tolist()), key=lambda item: (item != "clean", item)):
        idx = view_labels == view
        acc = float(np.mean(pred[idx] == y_parent[idx]))
        per_view.append(
            {
                "stress": str(view),
                "accuracy": acc,
                "correct": int(np.sum(pred[idx] == y_parent[idx])),
                "wrong": int(np.sum(pred[idx] != y_parent[idx])),
                "gate_count": int(np.sum(gate[idx])),
                "preserve_false_trigger": int(np.sum(gate[idx] & groups["preserve"][idx])),
                "stable_false_trigger": int(np.sum(gate[idx] & groups["stable"][idx])),
            }
        )
    stress_rows = [item for item in per_view if item["stress"] != "clean"]
    rot_rows = [item for item in stress_rows if str(item["stress"]) in ROT_MIRROR_VIEWS]
    camera_rows = [item for item in stress_rows if str(item["stress"]).startswith("cam_")]
    blur_rows = [item for item in stress_rows if domain_name(str(item["stress"])) in {"blur_noise", "noise"}]
    stress_acc = [float(item["accuracy"]) for item in stress_rows]
    rot_acc = [float(item["accuracy"]) for item in rot_rows]
    camera_acc = [float(item["accuracy"]) for item in camera_rows]
    blur_acc = [float(item["accuracy"]) for item in blur_rows]
    rot_mask = np.asarray([str(view) in ROT_MIRROR_VIEWS for view in view_labels.tolist()], dtype=bool)
    worst = min(stress_rows, key=lambda item: float(item["accuracy"])) if stress_rows else per_view[0]
    out.update(
        {
            "stress_mean_accuracy": float(np.mean(stress_acc)) if stress_acc else float(clean_counts["all_accuracy"]),
            "stress_min_accuracy": float(np.min(stress_acc)) if stress_acc else float(clean_counts["all_accuracy"]),
            "rotmirror_mean_accuracy": float(np.mean(rot_acc)) if rot_acc else 0.0,
            "rotmirror_min_accuracy": float(np.min(rot_acc)) if rot_acc else 0.0,
            "rotmirror_all_correct": int(np.sum(pred[rot_mask] == y_parent[rot_mask])) if np.any(rot_mask) else 0,
            "rotmirror_all_total": int(np.sum(rot_mask)),
            "rotmirror_gate_count": int(np.sum(gate[rot_mask])) if np.any(rot_mask) else 0,
            "rotmirror_preserve_false_trigger": int(np.sum(gate[rot_mask] & groups["preserve"][rot_mask]))
            if np.any(rot_mask)
            else 0,
            "rotmirror_stable_false_trigger": int(np.sum(gate[rot_mask] & groups["stable"][rot_mask]))
            if np.any(rot_mask)
            else 0,
            "camera_min_accuracy": float(np.min(camera_acc)) if camera_acc else 0.0,
            "blur_noise_min_accuracy": float(np.min(blur_acc)) if blur_acc else 0.0,
            "worst_stress": str(worst["stress"]),
            "worst_stress_accuracy": float(worst["accuracy"]),
            "beats_old_stress_mean": bool((np.mean(stress_acc) if stress_acc else 0.0) >= float(baseline["old_stress_mean_accuracy"])),
            "beats_old_stress_min": bool((np.min(stress_acc) if stress_acc else 0.0) >= float(baseline["old_stress_min_accuracy"])),
            "beats_old_camera_min": bool(
                camera_acc and float(np.min(camera_acc)) >= float(baseline["old_camera_min_accuracy"])
            ),
            "per_view_json": json.dumps(per_view, ensure_ascii=False),
        }
    )
    return out


def row_score(row: dict[str, object]) -> tuple[object, ...]:
    clean_full = int(row.get("clean_all_correct", 0)) == int(row.get("clean_all_total", 1))
    rotmirror_full = float(row.get("rotmirror_min_accuracy", 0.0)) >= 0.999999
    clean_false = int(row.get("clean_preserve_false_trigger", 0)) + int(row.get("clean_stable_false_trigger", 0))
    rotmirror_false = int(row.get("rotmirror_preserve_false_trigger", 0)) + int(
        row.get("rotmirror_stable_false_trigger", 0)
    )
    return (
        clean_full,
        rotmirror_full,
        -clean_false,
        -rotmirror_false,
        int(row.get("clean_all_correct", 0)),
        float(row.get("rotmirror_min_accuracy", 0.0)),
        bool(row.get("beats_old_stress_mean", False)),
        bool(row.get("beats_old_stress_min", False)),
        float(row.get("stress_mean_accuracy", 0.0)),
        float(row.get("stress_min_accuracy", 0.0)),
        float(row.get("rotmirror_min_accuracy", 0.0)),
        float(row.get("camera_min_accuracy", 0.0)),
        -int(row.get("stress_preserve_false_trigger", 0)),
        -int(row.get("stress_stable_false_trigger", 0)),
        -int(row.get("stress_gate_count", 0)),
    )


def compact_score(row: dict[str, object]) -> tuple[object, ...]:
    clean_full = int(row.get("oracle_clean_correct", 0)) == 304
    rotmirror_full = float(row.get("oracle_rotmirror_min_accuracy", 0.0)) >= 0.999999
    return (
        clean_full,
        rotmirror_full,
        int(row.get("oracle_clean_correct", 0)),
        float(row.get("oracle_rotmirror_min_accuracy", 0.0)),
        float(row.get("oracle_stress_mean_accuracy", 0.0)),
        float(row.get("oracle_stress_min_accuracy", 0.0)),
        -int(row.get("oracle_clean_gate_count", 999999)),
    )


def trim_rows(rows: list[dict[str, object]], max_rows: int) -> list[dict[str, object]]:
    if len(rows) <= max_rows:
        return rows
    return sorted(rows, key=row_score, reverse=True)[:max_rows]


def baseline_report(flat_eval: dict[str, object], groups: dict[str, np.ndarray], paths: list[str]) -> dict[str, object]:
    old_pred = np.asarray(flat_eval["old_pred"], dtype=np.int64)
    y_parent = np.asarray(flat_eval["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat_eval["view_labels"])
    per_view = []
    for view in sorted(set(view_labels.tolist()), key=lambda item: (item != "clean", item)):
        idx = view_labels == view
        per_view.append(
            {
                "stress": str(view),
                "accuracy": float(np.mean(old_pred[idx] == y_parent[idx])),
                "correct": int(np.sum(old_pred[idx] == y_parent[idx])),
                "wrong": int(np.sum(old_pred[idx] != y_parent[idx])),
            }
        )
    stress_rows = [item for item in per_view if item["stress"] != "clean"]
    camera_rows = [item for item in stress_rows if str(item["stress"]).startswith("cam_")]
    rot_rows = [item for item in stress_rows if str(item["stress"]) in ROT_MIRROR_VIEWS]
    clean_mask = view_labels == "clean"
    clean_counts = counts_for(
        old_pred[clean_mask],
        y_parent[clean_mask],
        {name: mask[clean_mask] for name, mask in groups.items()},
    )
    return {
        "old_clean": clean_counts,
        "old_stress_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in stress_rows])),
        "old_stress_min_accuracy": float(np.min([float(item["accuracy"]) for item in stress_rows])),
        "old_camera_min_accuracy": float(np.min([float(item["accuracy"]) for item in camera_rows])) if camera_rows else 0.0,
        "old_rotmirror_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in rot_rows])) if rot_rows else 0.0,
        "old_rotmirror_min_accuracy": float(np.min([float(item["accuracy"]) for item in rot_rows])) if rot_rows else 0.0,
        "old_per_view": per_view,
        "old_wrong_files_clean": [
            Path(paths[index]).name for index in np.where(old_pred[clean_mask] != y_parent[clean_mask])[0].tolist()
        ],
    }


def iter_adapter_grid(args: argparse.Namespace) -> Iterator[dict[str, object]]:
    view_profiles = parse_csv(args.view_profiles)
    positive_profiles = parse_csv(args.positive_profiles)
    weight_profiles = parse_csv(args.weight_profiles)
    target_modes = parse_csv(args.target_modes)
    adapter_features = parse_csv(args.adapter_features)
    l2_values = parse_floats(args.l2)
    rank_values = parse_ranks(args.rank)
    mask_values = parse_floats(args.mask_percentile)
    margin_values = parse_floats(args.margin)
    alpha_values = parse_floats(args.alpha)
    blend_values = parse_floats(args.blend)
    for view_profile in view_profiles:
        for positive_profile in positive_profiles:
            for weight_profile in weight_profiles:
                for adapter_feature in adapter_features:
                    for target_mode in target_modes:
                        margins = [0.0] if target_mode == "ctd_delta" else margin_values
                        blends = [1.0] if target_mode != "hybrid_ctd_margin" else blend_values
                        for margin in margins:
                            for blend in blends:
                                for l2 in l2_values:
                                    for rank in rank_values:
                                        for mask_percentile in mask_values:
                                            for alpha in alpha_values:
                                                yield {
                                                    "view_profile": view_profile,
                                                    "positive_profile": positive_profile,
                                                    "weight_profile": weight_profile,
                                                    "adapter_feature": adapter_feature,
                                                    "target_mode": target_mode,
                                                    "margin": float(margin),
                                                    "blend": float(blend),
                                                    "l2": float(l2),
                                                    "rank": "full" if rank is None else int(rank),
                                                    "rank_value": rank,
                                                    "mask_percentile": float(mask_percentile),
                                                    "alpha": float(alpha),
                                                }


def random_adapter_combo(args: argparse.Namespace, rng: np.random.Generator) -> dict[str, object]:
    view_profile = str(rng.choice(parse_csv(args.view_profiles)))
    positive_profile = str(rng.choice(parse_csv(args.positive_profiles)))
    weight_profile = str(rng.choice(parse_csv(args.weight_profiles)))
    adapter_feature = str(rng.choice(parse_csv(args.adapter_features)))
    target_mode = str(rng.choice(parse_csv(args.target_modes)))
    l2 = float(rng.choice(parse_floats(args.l2)))
    rank_values = parse_ranks(args.rank)
    rank_value = rank_values[int(rng.integers(0, len(rank_values)))]
    mask_percentile = float(rng.choice(parse_floats(args.mask_percentile)))
    alpha = float(rng.choice(parse_floats(args.alpha)))
    if target_mode == "ctd_delta":
        margin = 0.0
        blend = 1.0
    elif target_mode == "margin_target":
        margin = float(rng.choice(parse_floats(args.margin)))
        blend = 1.0
    else:
        margin = float(rng.choice(parse_floats(args.margin)))
        blend = float(rng.choice(parse_floats(args.blend)))
    return {
        "view_profile": view_profile,
        "positive_profile": positive_profile,
        "weight_profile": weight_profile,
        "adapter_feature": adapter_feature,
        "target_mode": target_mode,
        "margin": margin,
        "blend": blend,
        "l2": l2,
        "rank": "full" if rank_value is None else int(rank_value),
        "rank_value": rank_value,
        "mask_percentile": mask_percentile,
        "alpha": alpha,
    }


def select_shard_combos(args: argparse.Namespace) -> list[dict[str, object]]:
    if args.max_adapter_combos > 0:
        rng = np.random.default_rng(args.seed + 1009 * args.shard_index)
        selected: list[dict[str, object]] = []
        seen: set[str] = set()
        max_attempts = max(args.max_adapter_combos * 200, 10000)
        for _attempt in range(max_attempts):
            combo = random_adapter_combo(args, rng)
            key = json.dumps({k: v for k, v in combo.items() if k != "rank_value"}, sort_keys=True)
            if key in seen:
                continue
            seen.add(key)
            selected.append(combo)
            if len(selected) >= args.max_adapter_combos:
                break
        return selected

    selected: list[dict[str, object]] = []
    for combo in iter_adapter_grid(args):
        key = json.dumps({k: v for k, v in combo.items() if k != "rank_value"}, sort_keys=True)
        if stable_hash(key) % args.shard_count == args.shard_index:
            selected.append(combo)
    rng = np.random.default_rng(args.seed + args.shard_index)
    rng.shuffle(selected)
    if args.max_adapter_combos > 0:
        selected = selected[: args.max_adapter_combos]
    return selected


def oracle_summary(
    *,
    flat_eval: dict[str, object],
    adapter_feature_name: str,
    feature_mean: np.ndarray,
    feature_std: np.ndarray,
    adapter_coef: np.ndarray,
    alpha: float,
) -> dict[str, object]:
    old_pred = np.asarray(flat_eval["old_pred"], dtype=np.int64)
    y_parent = np.asarray(flat_eval["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat_eval["view_labels"])
    old_gap = np.asarray(flat_eval["old_gap"], dtype=np.float64)
    old_logits = np.asarray(flat_eval["old_logits"], dtype=np.float64)
    raw_features = raw_adapter_features(adapter_feature_name, old_gap, old_logits)
    delta = append_bias(zapply(raw_features, feature_mean, feature_std)) @ adapter_coef
    adapter_pred = np.argmax(old_logits + alpha * delta, axis=1).astype(np.int64)
    gate = (old_pred != y_parent) & (adapter_pred == y_parent)
    pred = np.where(gate, adapter_pred, old_pred)
    clean = view_labels == "clean"
    stress_rows = []
    for view in sorted(set(view_labels.tolist())):
        if view == "clean":
            continue
        idx = view_labels == view
        stress_rows.append(
            {
                "stress": str(view),
                "accuracy": float(np.mean(pred[idx] == y_parent[idx])),
            }
        )
    rot_acc = [float(item["accuracy"]) for item in stress_rows if item["stress"] in ROT_MIRROR_VIEWS]
    return {
        "oracle_clean_correct": int(np.sum(pred[clean] == y_parent[clean])),
        "oracle_clean_gate_count": int(np.sum(gate[clean])),
        "oracle_stress_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in stress_rows])),
        "oracle_stress_min_accuracy": float(np.min([float(item["accuracy"]) for item in stress_rows])),
        "oracle_rotmirror_min_accuracy": float(np.min(rot_acc)) if rot_acc else 0.0,
    }


def make_gate_variants(
    *,
    flat_eval: dict[str, object],
    adapter_pred: np.ndarray,
    adapter_margin: np.ndarray,
    delta_logits: np.ndarray,
    gate_score: np.ndarray | None,
    thresholds: list[float],
    prefix: str,
    max_gates: int,
) -> list[tuple[str, float, np.ndarray]]:
    old_pred = np.asarray(flat_eval["old_pred"], dtype=np.int64)
    old_logits = np.asarray(flat_eval["old_logits"], dtype=np.float64)
    disagree = old_pred != adapter_pred
    variants: list[tuple[str, float, np.ndarray]] = []
    if gate_score is not None:
        lows = thresholds[:: max(1, len(thresholds) // 8)]
        for threshold in thresholds:
            base = gate_score > threshold
            variants.append((f"{prefix}_score_disagree", float(threshold), base & disagree))
            variants.append((f"{prefix}_score_disagree_margin_gt_0p5", float(threshold), base & disagree & (adapter_margin > 0.5)))
            for low in lows:
                if low >= threshold:
                    continue
                for margin_limit in [0.05, 0.10, 0.25, 0.50, 1.00]:
                    gate = (base & disagree) | ((gate_score > low) & disagree & (adapter_margin < margin_limit))
                    variants.append(
                        (
                            f"{prefix}_two_band_tail_margin_lt_{margin_limit:g}_low_{low:g}",
                            float(threshold),
                            gate,
                        )
                    )
            if len(variants) >= max_gates:
                break
    old_margin = pred_margin(old_logits)
    delta_norm = np.linalg.norm(delta_logits, axis=1)
    for threshold in np.linspace(0.0, 2.5, 18):
        variants.append((f"{prefix}_analytic_old_margin_lt", float(threshold), (old_margin < threshold) & disagree))
    for threshold in np.linspace(0.0, 6.0, 18):
        variants.append(
            (
                f"{prefix}_analytic_disagree_adapter_margin_gt",
                float(threshold),
                disagree & (adapter_margin > threshold),
            )
        )
    max_delta = max(1.0, float(np.max(delta_norm)))
    for threshold in np.linspace(0.0, max_delta, 18):
        variants.append((f"{prefix}_analytic_delta_norm_gt_disagree", float(threshold), (delta_norm > threshold) & disagree))
    return variants[:max_gates]


def main() -> None:
    parser = argparse.ArgumentParser(description="V7 phase3 stress-aware adapter/router long search.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--stress", default=csv_text(DEFAULT_STRESS))
    parser.add_argument("--rebuild-cache", action="store_true")
    parser.add_argument("--prepare-cache-only", action="store_true")
    parser.add_argument("--shard-count", type=int, default=16)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260519)
    parser.add_argument("--view-profiles", default=",".join(VIEW_PROFILES.keys()))
    parser.add_argument("--positive-profiles", default=",".join(POSITIVE_PROFILES))
    parser.add_argument("--weight-profiles", default=",".join(WEIGHT_PROFILES))
    parser.add_argument("--adapter-features", default="old_gap")
    parser.add_argument("--gate-features", default=",".join(GATE_FEATURES))
    parser.add_argument("--target-modes", default=",".join(TARGET_MODES))
    parser.add_argument("--l2", default="0.0001,0.001,0.01,0.1,1,10")
    parser.add_argument("--gate-l2", default="0.0001,0.001,0.01,0.1")
    parser.add_argument("--rank", default="full,1,2,3,4,6")
    parser.add_argument("--mask-percentile", default="0,60,75,85,92,96")
    parser.add_argument("--alpha", default="0.5,0.75,1.0,1.25,1.5,2.0")
    parser.add_argument("--margin", default="1,2,4,6,8")
    parser.add_argument("--blend", default="0.25,0.5,0.75")
    parser.add_argument("--max-adapter-combos", type=int, default=1200)
    parser.add_argument("--router-top-adapters", type=int, default=96)
    parser.add_argument("--max-thresholds", type=int, default=36)
    parser.add_argument("--max-gates-per-adapter", type=int, default=360)
    parser.add_argument("--max-kept-rows", type=int, default=20000)
    args = parser.parse_args()

    stress_names = parse_csv(args.stress)
    cache = load_or_build_cache(
        cache_path=args.feature_cache,
        dataset_dir=args.dataset_dir,
        old_tflite=args.old_tflite,
        rescue_tflite=args.rescue_tflite,
        stress_names=stress_names,
        rebuild=args.rebuild_cache,
    )
    if args.prepare_cache_only:
        print(json.dumps({"prepared_feature_cache": str(args.feature_cache)}, ensure_ascii=False))
        return

    paths = as_str_list(np.asarray(cache["paths"]))
    groups_clean = clean_groups(cache)
    flat_eval = flatten_views(cache, ["clean"] + stress_names)
    eval_groups = flat_eval["groups"]
    assert isinstance(eval_groups, dict)
    baseline = baseline_report(flat_eval, eval_groups, paths)
    gate_feature_names = parse_csv(args.gate_features)
    combos = select_shard_combos(args)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "selected_adapter_combo_count.txt").write_text(f"{len(combos)}\n", encoding="utf-8")

    adapter_records: list[dict[str, object]] = []
    adapter_rows: list[dict[str, object]] = []
    fixture_cache: dict[str, dict[str, object]] = {}
    for combo_index, combo in enumerate(combos):
        view_profile = str(combo["view_profile"])
        if view_profile not in VIEW_PROFILES:
            raise ValueError(f"unknown view profile: {view_profile}")
        if view_profile not in fixture_cache:
            fixture_cache[view_profile] = flatten_views(cache, VIEW_PROFILES[view_profile])
        fixture = fixture_cache[view_profile]
        positive = positive_mask_for(str(combo["positive_profile"]), fixture)
        if int(np.sum(positive)) == 0:
            continue
        old_gap = np.asarray(fixture["old_gap"], dtype=np.float64)
        old_logits = np.asarray(fixture["old_logits"], dtype=np.float64)
        rescue_logits = np.asarray(fixture["rescue_logits"], dtype=np.float64)
        y_parent = np.asarray(fixture["y_parent"], dtype=np.int64)
        adapter_feature_name = str(combo["adapter_feature"])
        raw_features = raw_adapter_features(adapter_feature_name, old_gap, old_logits)
        feature_z, feature_mean, feature_std = zfit(raw_features)
        features = append_bias(feature_z)
        weights = sample_weights(str(combo["weight_profile"]), fixture, positive)
        target_delta = build_target_delta(
            str(combo["target_mode"]),
            old_logits,
            rescue_logits,
            y_parent,
            positive,
            margin=float(combo["margin"]),
            blend=float(combo["blend"]),
        )
        raw_coef = weighted_ridge(features, target_delta, weights, float(combo["l2"]))
        ranked = truncate_adapter_rank(raw_coef, combo["rank_value"])  # type: ignore[arg-type]
        coef, mask_density = sparsify_adapter(ranked, float(combo["mask_percentile"]))
        oracle = oracle_summary(
            flat_eval=flat_eval,
            adapter_feature_name=adapter_feature_name,
            feature_mean=feature_mean,
            feature_std=feature_std,
            adapter_coef=coef,
            alpha=float(combo["alpha"]),
        )
        adapter_row = {
            "combo_index": combo_index,
            "positive_count": int(np.sum(positive)),
            "mask_density": float(mask_density),
            **{k: v for k, v in combo.items() if k != "rank_value"},
            **oracle,
        }
        adapter_rows.append(adapter_row)
        adapter_records.append(
            {
                "row": adapter_row,
                "combo": combo,
                "feature_mean": feature_mean,
                "feature_std": feature_std,
                "coef": coef,
                "positive": positive,
                "fixture": fixture,
            }
        )
        if (combo_index + 1) % 100 == 0:
            print(
                json.dumps(
                    {
                        "shard": args.shard_index,
                        "adapter_combos_done": combo_index + 1,
                        "kept_for_router": min(len(adapter_records), args.router_top_adapters),
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

    adapter_records = sorted(adapter_records, key=lambda item: compact_score(item["row"]), reverse=True)[
        : args.router_top_adapters
    ]
    write_csv(args.output_dir / "adapter_oracle_candidates.csv", sorted(adapter_rows, key=compact_score, reverse=True))

    rows: list[dict[str, object]] = []
    best_row: dict[str, object] | None = None
    best_payload: dict[str, object] | None = None
    gate_l2_values = parse_floats(args.gate_l2)
    flat_old_gap = np.asarray(flat_eval["old_gap"], dtype=np.float64)
    flat_old_logits = np.asarray(flat_eval["old_logits"], dtype=np.float64)

    for adapter_rank, record in enumerate(adapter_records, start=1):
        row_info = record["row"]
        combo = record["combo"]
        fixture = record["fixture"]
        positive = np.asarray(record["positive"], dtype=bool)
        feature_mean = np.asarray(record["feature_mean"], dtype=np.float64)
        feature_std = np.asarray(record["feature_std"], dtype=np.float64)
        coef = np.asarray(record["coef"], dtype=np.float64)
        alpha = float(combo["alpha"])
        adapter_feature_name = str(combo["adapter_feature"])
        pred_base_gate = np.zeros(len(np.asarray(flat_eval["y_parent"])), dtype=bool)
        _pred, adapter_pred, adapter_margin, delta_logits = evaluate_flat(
            flat_eval=flat_eval,
            adapter_feature_name=adapter_feature_name,
            feature_mean=feature_mean,
            feature_std=feature_std,
            adapter_coef=coef,
            alpha=alpha,
            gate=pred_base_gate,
        )
        gate_bank = fit_gate_bank(
            fixture,
            positive,
            str(combo["weight_profile"]),
            gate_l2_values,
            gate_feature_names,
            args.max_thresholds,
        )
        variants: list[tuple[str, str, float, np.ndarray, dict[str, object] | None]] = []
        for gate_record in gate_bank:
            feature_name = str(gate_record["feature_name"])
            raw = raw_gate_features(feature_name, flat_old_gap, flat_old_logits)
            gate_features = append_bias(
                zapply(raw, np.asarray(gate_record["feature_mean"], dtype=np.float64), np.asarray(gate_record["feature_std"], dtype=np.float64))
            )
            gate_score = gate_features @ np.asarray(gate_record["coef"], dtype=np.float64)
            gate_variants = make_gate_variants(
                flat_eval=flat_eval,
                adapter_pred=adapter_pred,
                adapter_margin=adapter_margin,
                delta_logits=delta_logits,
                gate_score=gate_score,
                thresholds=list(gate_record["thresholds"]),
                prefix=f"learned_{feature_name}_l2_{float(gate_record['l2']):g}",
                max_gates=args.max_gates_per_adapter,
            )
            variants.extend((feature_name, name, threshold, gate, gate_record) for name, threshold, gate in gate_variants)
        variants.extend(
            ("analytic", name, threshold, gate, None)
            for name, threshold, gate in make_gate_variants(
                flat_eval=flat_eval,
                adapter_pred=adapter_pred,
                adapter_margin=adapter_margin,
                delta_logits=delta_logits,
                gate_score=None,
                thresholds=[],
                prefix="analytic",
                max_gates=80,
            )
        )

        for gate_feature, gate_name, threshold, gate, gate_record in variants:
            pred, _adapter_pred, _adapter_margin, _delta_logits = evaluate_flat(
                flat_eval=flat_eval,
                adapter_feature_name=adapter_feature_name,
                feature_mean=feature_mean,
                feature_std=feature_std,
                adapter_coef=coef,
                alpha=alpha,
                gate=gate,
            )
            row = metrics_across_views(
                row_base={
                    "name": "phase3_stress_aware_adapter_router",
                    "adapter_rank": adapter_rank,
                    "gate_feature_name": gate_feature,
                    "gate_type": gate_name,
                    "gate_threshold": float(threshold),
                    "gate_l2": "" if gate_record is None else float(gate_record["l2"]),
                    "mask_density": float(row_info["mask_density"]),
                    "positive_count": int(row_info["positive_count"]),
                    **{k: v for k, v in combo.items() if k != "rank_value"},
                },
                flat_eval=flat_eval,
                groups=eval_groups,
                paths=paths,
                pred=pred,
                gate=gate,
                baseline=baseline,
            )
            rows.append(row)
            if best_row is None or row_score(row) > row_score(best_row):
                best_row = dict(row)
                best_payload = {
                    "feature_mean": feature_mean.copy(),
                    "feature_std": feature_std.copy(),
                    "adapter_coef": coef.copy(),
                    "adapter_feature_name": adapter_feature_name,
                    "gate": gate.copy(),
                    "pred": pred.copy(),
                    "gate_record": None if gate_record is None else dict(gate_record),
                    "gate_type": gate_name,
                    "gate_threshold": float(threshold),
                    "combo": dict(combo),
                }
            rows = trim_rows(rows, args.max_kept_rows)

        print(
            json.dumps(
                {
                    "shard": args.shard_index,
                    "router_adapter_done": adapter_rank,
                    "current_best_clean": None if best_row is None else best_row["clean_all_correct"],
                    "current_best_stress_mean": None if best_row is None else best_row["stress_mean_accuracy"],
                    "current_best_stress_min": None if best_row is None else best_row["stress_min_accuracy"],
                },
                ensure_ascii=False,
            ),
            flush=True,
        )

    rows_sorted = sorted(rows, key=row_score, reverse=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    summary = {
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "feature_cache": str(args.feature_cache),
        "shard_count": args.shard_count,
        "shard_index": args.shard_index,
        "selected_adapter_combos": len(combos),
        "routed_adapter_count": len(adapter_records),
        "group_counts": {name: int(np.sum(mask)) for name, mask in groups_clean.items()},
        "baseline": baseline,
        "best": best_row,
        "top20": rows_sorted[:20],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    if best_row is not None and best_payload is not None:
        per_view = json.loads(str(best_row["per_view_json"]))
        write_csv(args.output_dir / "best_stress_summary.csv", per_view)
        gate = np.asarray(best_payload["gate"], dtype=bool)
        pred = np.asarray(best_payload["pred"], dtype=np.int64)
        y_parent = np.asarray(flat_eval["y_parent"], dtype=np.int64)
        view_labels = np.asarray(flat_eval["view_labels"])
        sample_index = np.asarray(flat_eval["sample_index"], dtype=np.int64)
        sample_rows = []
        for index in np.where(gate | (pred != y_parent))[0].tolist():
            sample_rows.append(
                {
                    "stress": str(view_labels[index]),
                    "sample_index": int(sample_index[index]),
                    "file": Path(paths[int(sample_index[index])]).name,
                    "parent": PARENT_NAMES[int(y_parent[index])],
                    "pred": PARENT_NAMES[int(pred[index])],
                    "correct": bool(pred[index] == y_parent[index]),
                    "gate": bool(gate[index]),
                    "stable": bool(eval_groups["stable"][index]),
                    "preserve": bool(eval_groups["preserve"][index]),
                    "rescue": bool(eval_groups["rescue"][index]),
                    "hard": bool(eval_groups["hard"][index]),
                    "c4": bool(eval_groups["c4"][index]),
                }
            )
        write_csv(args.output_dir / "best_sample_events.csv", sample_rows)
        save_kwargs: dict[str, object] = {
            "adapter_coef": np.asarray(best_payload["adapter_coef"], dtype=np.float32),
            "adapter_feature_name": np.asarray(str(best_payload.get("adapter_feature_name", "old_gap"))),
            "feature_mean": np.asarray(best_payload["feature_mean"], dtype=np.float32),
            "feature_std": np.asarray(best_payload["feature_std"], dtype=np.float32),
            "best_config_json": json.dumps(best_row, ensure_ascii=False),
        }
        gate_record = best_payload["gate_record"]
        if isinstance(gate_record, dict):
            save_kwargs.update(
                {
                    "gate_feature_name": np.asarray(str(gate_record["feature_name"])),
                    "gate_feature_mean": np.asarray(gate_record["feature_mean"], dtype=np.float64),
                    "gate_feature_std": np.asarray(gate_record["feature_std"], dtype=np.float64),
                    "gate_coef": np.asarray(gate_record["coef"], dtype=np.float64),
                    "gate_threshold": np.asarray(float(best_payload["gate_threshold"]), dtype=np.float64),
                }
            )
        np.savez_compressed(args.output_dir / "best_phase3_stress_aware_params.npz", **save_kwargs)

    print(json.dumps({"best": best_row, "baseline": baseline}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
