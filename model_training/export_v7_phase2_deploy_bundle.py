import argparse
import csv
import json
import math
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from analyze_v7_expert_tflite_reverse import TfliteReverse
from run_v7_delta_merge_phase1 import PARENT_NAMES, counts_for, group_masks, pred_margin
import train_tiny32_v5_visual_subclass_scan as train


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


def load_params(path: Path) -> dict[str, object]:
    with np.load(path, allow_pickle=False) as data:
        out: dict[str, object] = {key: data[key] for key in data.files}
    config_raw = out.get("best_config_json")
    if config_raw is None:
        raise ValueError(f"best_config_json not found in {path}")
    config_text = str(np.asarray(config_raw).item())
    out["best_config"] = json.loads(config_text)
    return out


def one_hot(indexes: np.ndarray, depth: int) -> np.ndarray:
    return np.eye(depth, dtype=np.float64)[indexes.astype(np.int64)]


def old_gap_logits_features(gap: np.ndarray, logits: np.ndarray) -> np.ndarray:
    pred = np.argmax(logits, axis=1).astype(np.int64)
    return np.concatenate(
        [
            gap.astype(np.float64),
            logits.astype(np.float64),
            pred_margin(logits).reshape(-1, 1).astype(np.float64),
            one_hot(pred, len(PARENT_NAMES)),
        ],
        axis=1,
    )


def append_bias(features: np.ndarray) -> np.ndarray:
    return np.concatenate([features, np.ones((features.shape[0], 1), dtype=features.dtype)], axis=1)


def parent_true_margin(logits: np.ndarray, y_parent: np.ndarray) -> np.ndarray:
    out = np.zeros(len(y_parent), dtype=np.float64)
    for index, true_id in enumerate(y_parent.astype(np.int64).tolist()):
        others = [item for item in range(len(PARENT_NAMES)) if item != true_id]
        out[index] = float(logits[index, true_id] - np.max(logits[index, others]))
    return out


def fixed_dot(features_with_bias: np.ndarray, coef: np.ndarray, frac_bits: int) -> tuple[np.ndarray, dict[str, object]]:
    scale = 1 << frac_bits
    feature_q = np.rint(features_with_bias * scale).astype(np.int64)
    coef_q = np.rint(coef * scale).astype(np.int64)
    accum = feature_q @ coef_q
    value = accum.astype(np.float64) / float(scale * scale)
    max_abs_feature_q = int(np.max(np.abs(feature_q))) if feature_q.size else 0
    max_abs_coef_q = int(np.max(np.abs(coef_q))) if coef_q.size else 0
    max_abs_accum = int(np.max(np.abs(accum))) if accum.size else 0
    return value, {
        "frac_bits": int(frac_bits),
        "scale": int(scale),
        "max_abs_feature_q": max_abs_feature_q,
        "max_abs_coef_q": max_abs_coef_q,
        "max_abs_accum": max_abs_accum,
        "int32_accumulator_safe": bool(max_abs_accum <= np.iinfo(np.int32).max),
    }


def params_with_thresholds(params: dict[str, object], main: float, tail_low: float, tail_margin: float) -> dict[str, object]:
    out = dict(params)
    out["gate_threshold"] = np.asarray(main, dtype=np.float32)
    out["gate_tail_low_threshold"] = np.asarray(tail_low, dtype=np.float32)
    out["gate_tail_margin_limit"] = np.asarray(tail_margin, dtype=np.float32)
    return out


def calibrate_two_band_thresholds(
    params: dict[str, object],
    old_pred: np.ndarray,
    arrays: dict[str, np.ndarray],
) -> tuple[dict[str, object], dict[str, object]]:
    score = arrays["gate_score"].astype(np.float64)
    gate = arrays["gate"].astype(bool)
    adapter_pred = arrays["adapter_pred"].astype(np.int64)
    adapter_margin = arrays["adapter_margin"].astype(np.float64)
    disagree = old_pred.astype(np.int64) != adapter_pred
    main = float(np.asarray(params["gate_threshold"]).item())
    tail_low = float(np.asarray(params["gate_tail_low_threshold"]).item())
    tail_margin = float(np.asarray(params["gate_tail_margin_limit"]).item())

    high_gate = (score > main) & disagree
    high_pos = high_gate & gate
    high_neg = disagree & (~gate)
    calibrated_main = main
    main_gap = 0.0
    if np.any(high_pos):
        min_pos = float(np.min(score[high_pos]))
        max_neg = float(np.max(score[high_neg])) if np.any(high_neg) else float(np.min(score) - 1.0)
        main_gap = min_pos - max_neg
        if max_neg < min_pos:
            calibrated_main = (max_neg + min_pos) / 2.0

    tail_gate = (score > tail_low) & disagree & (adapter_margin < tail_margin)
    tail_pos = tail_gate & gate & (~high_gate)
    tail_neg = disagree & (~gate) & (adapter_margin < tail_margin)
    calibrated_tail_low = tail_low
    tail_score_gap = 0.0
    if np.any(tail_pos):
        min_pos = float(np.min(score[tail_pos]))
        max_neg = float(np.max(score[tail_neg])) if np.any(tail_neg) else float(np.min(score) - 1.0)
        tail_score_gap = min_pos - max_neg
        if max_neg < min_pos:
            calibrated_tail_low = (max_neg + min_pos) / 2.0

    margin_neg = disagree & (~gate) & (score > calibrated_tail_low)
    calibrated_tail_margin = tail_margin
    tail_margin_gap = 0.0
    if np.any(tail_pos) and np.any(margin_neg):
        max_pos = float(np.max(adapter_margin[tail_pos]))
        min_neg = float(np.min(adapter_margin[margin_neg]))
        tail_margin_gap = min_neg - max_pos
        if max_pos < min_neg:
            calibrated_tail_margin = (max_pos + min_neg) / 2.0

    diagnostics = {
        "original_gate_threshold": main,
        "original_tail_low_threshold": tail_low,
        "original_tail_margin_limit": tail_margin,
        "calibrated_gate_threshold": calibrated_main,
        "calibrated_tail_low_threshold": calibrated_tail_low,
        "calibrated_tail_margin_limit": calibrated_tail_margin,
        "main_score_gap": main_gap,
        "tail_score_gap": tail_score_gap,
        "tail_margin_gap": tail_margin_gap,
    }
    return params_with_thresholds(params, calibrated_main, calibrated_tail_low, calibrated_tail_margin), diagnostics


def evaluate_outputs(
    *,
    old_logits: np.ndarray,
    old_gap: np.ndarray,
    y_parent: np.ndarray,
    groups: dict[str, np.ndarray],
    paths: list[str],
    params: dict[str, object],
    quant_frac_bits: int | None,
) -> tuple[dict[str, object], dict[str, np.ndarray], dict[str, object]]:
    config = params["best_config"]
    assert isinstance(config, dict)
    alpha = float(config.get("alpha", 1.0))

    adapter_coef = np.asarray(params["adapter_coef"], dtype=np.float64)
    feature_mean = np.asarray(params["feature_mean"], dtype=np.float64)
    feature_std = np.asarray(params["feature_std"], dtype=np.float64)
    gap_z = (old_gap.astype(np.float64) - feature_mean) / feature_std
    adapter_features = append_bias(gap_z)

    quant_meta: dict[str, object] = {"mode": "float32"}
    if quant_frac_bits is None:
        delta_logits = adapter_features @ adapter_coef
    else:
        delta_logits, adapter_quant = fixed_dot(adapter_features, adapter_coef, quant_frac_bits)
        quant_meta = {"mode": f"q{quant_frac_bits}", "adapter": adapter_quant}

    adapter_logits = old_logits + alpha * delta_logits
    old_pred = np.argmax(old_logits, axis=1).astype(np.int64)
    adapter_pred = np.argmax(adapter_logits, axis=1).astype(np.int64)
    adapter_margin = pred_margin(adapter_logits)

    gate_feature_name = str(np.asarray(params.get("gate_feature_name", "old_gap_logits")).item())
    if gate_feature_name != "old_gap_logits":
        raise ValueError(f"unsupported gate_feature_name: {gate_feature_name}")
    raw_gate_features = old_gap_logits_features(old_gap, old_logits)
    gate_feature_mean = np.asarray(params["gate_feature_mean"], dtype=np.float64)
    gate_feature_std = np.asarray(params["gate_feature_std"], dtype=np.float64)
    gate_features = append_bias((raw_gate_features - gate_feature_mean) / gate_feature_std)
    gate_coef = np.asarray(params["gate_coef"], dtype=np.float64)
    if quant_frac_bits is None:
        gate_score = gate_features @ gate_coef
    else:
        gate_score, gate_quant = fixed_dot(gate_features, gate_coef.reshape(-1, 1), quant_frac_bits)
        gate_score = gate_score.reshape(-1)
        quant_meta["gate"] = gate_quant

    main_threshold = float(np.asarray(params["gate_threshold"]).item())
    tail_low_threshold = float(np.asarray(params["gate_tail_low_threshold"]).item())
    tail_margin_limit = float(np.asarray(params["gate_tail_margin_limit"]).item())
    disagree = old_pred != adapter_pred
    gate = ((gate_score > main_threshold) & disagree) | (
        (gate_score > tail_low_threshold) & disagree & (adapter_margin < tail_margin_limit)
    )
    final_logits = old_logits + gate[:, None].astype(np.float64) * alpha * delta_logits
    pred = np.argmax(final_logits, axis=1).astype(np.int64)
    metrics = counts_for(pred, y_parent, groups)
    metrics.update(
        {
            "mode": quant_meta["mode"],
            "alpha": alpha,
            "gate_count": int(np.sum(gate)),
            "preserve_false_trigger": int(np.sum(gate & groups["preserve"])),
            "stable_false_trigger": int(np.sum(gate & groups["stable"])),
            "rescue_trigger": int(np.sum(gate & groups["rescue"])),
            "wrong_files": [Path(paths[index]).name for index in np.where(pred != y_parent)[0].tolist()],
            "gate_threshold": main_threshold,
            "gate_tail_low_threshold": tail_low_threshold,
            "gate_tail_margin_limit": tail_margin_limit,
            "min_gate_distance_to_main": float(np.min(np.abs(gate_score - main_threshold))),
            "min_gate_distance_to_tail_low": float(np.min(np.abs(gate_score - tail_low_threshold))),
            "min_adapter_margin_distance_to_tail": float(np.min(np.abs(adapter_margin - tail_margin_limit))),
            "min_final_true_margin": float(np.min(parent_true_margin(final_logits, y_parent))),
        }
    )
    arrays = {
        "delta_logits": delta_logits,
        "adapter_logits": adapter_logits,
        "adapter_pred": adapter_pred,
        "adapter_margin": adapter_margin,
        "gate_score": gate_score,
        "gate": gate,
        "final_logits": final_logits,
        "pred": pred,
    }
    return metrics, arrays, quant_meta


def c_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError(f"non-finite C float literal: {value}")
    text = f"{float(value):.9g}"
    if "e" not in text and "E" not in text and "." not in text:
        text += ".0"
    return f"{text}f"


def c_array(name: str, values: np.ndarray, c_type: str = "float") -> str:
    flat = values.reshape(-1)
    if c_type == "float":
        rendered = ", ".join(c_float(float(item)) for item in flat)
    else:
        rendered = ", ".join(str(int(item)) for item in flat)
    return f"constexpr {c_type} {name}[{flat.size}] = {{{rendered}}};"


def write_header(path: Path, params: dict[str, object], best_quant_bits: int) -> None:
    adapter_coef = np.asarray(params["adapter_coef"], dtype=np.float32)
    feature_mean = np.asarray(params["feature_mean"], dtype=np.float32)
    feature_std = np.asarray(params["feature_std"], dtype=np.float32)
    gate_feature_mean = np.asarray(params["gate_feature_mean"], dtype=np.float32)
    gate_feature_std = np.asarray(params["gate_feature_std"], dtype=np.float32)
    gate_coef = np.asarray(params["gate_coef"], dtype=np.float32)
    gate_threshold = float(np.asarray(params["gate_threshold"]).item())
    gate_tail_low = float(np.asarray(params["gate_tail_low_threshold"]).item())
    gate_tail_margin = float(np.asarray(params["gate_tail_margin_limit"]).item())
    config = params["best_config"]
    assert isinstance(config, dict)
    alpha = float(config.get("alpha", 1.0))
    qscale = 1 << best_quant_bits
    adapter_coef_q = np.rint(adapter_coef * qscale).astype(np.int32)
    gate_coef_q = np.rint(gate_coef * qscale).astype(np.int32)
    lines = [
        "#pragma once",
        "",
        "#include <cmath>",
        "#include <cstdint>",
        "",
        "namespace v7_phase2 {",
        "",
        "constexpr int kGapDim = 36;",
        "constexpr int kParentCount = 3;",
        "constexpr int kAdapterFeatureDim = 37;",
        "constexpr int kGateRawFeatureDim = 43;",
        "constexpr int kGateFeatureDim = 44;",
        f"constexpr float kAdapterAlpha = {c_float(alpha)};",
        f"constexpr float kGateThreshold = {c_float(gate_threshold)};",
        f"constexpr float kGateTailLowThreshold = {c_float(gate_tail_low)};",
        f"constexpr float kGateTailMarginLimit = {c_float(gate_tail_margin)};",
        f"constexpr int kFixedPointFracBits = {best_quant_bits};",
        "",
        c_array("kGapMean", feature_mean),
        c_array("kGapStd", feature_std),
        c_array("kAdapterCoef", adapter_coef),
        c_array("kGateFeatureMean", gate_feature_mean),
        c_array("kGateFeatureStd", gate_feature_std),
        c_array("kGateCoef", gate_coef),
        c_array("kAdapterCoefQ", adapter_coef_q, "std::int32_t"),
        c_array("kGateCoefQ", gate_coef_q, "std::int32_t"),
        "",
        "struct Phase2Result {",
        "    int parent = 0;",
        "    bool gate = false;",
        "    float logits[kParentCount] = {};",
        "    float adapter_logits[kParentCount] = {};",
        "    float gate_score = 0.0f;",
        "    float adapter_margin = 0.0f;",
        "};",
        "",
        "inline int ArgMax3(const float values[kParentCount]) {",
        "    int best = 0;",
        "    if (values[1] > values[best]) best = 1;",
        "    if (values[2] > values[best]) best = 2;",
        "    return best;",
        "}",
        "",
        "inline float Margin3(const float values[kParentCount]) {",
        "    float first = values[0];",
        "    float second = values[1];",
        "    if (second > first) {",
        "        const float tmp = first;",
        "        first = second;",
        "        second = tmp;",
        "    }",
        "    if (values[2] > first) {",
        "        second = first;",
        "        first = values[2];",
        "    } else if (values[2] > second) {",
        "        second = values[2];",
        "    }",
        "    return first - second;",
        "}",
        "",
        "inline std::int32_t QuantizeQ(float value) {",
        "    return static_cast<std::int32_t>(std::lround(value * static_cast<float>(1 << kFixedPointFracBits)));",
        "}",
        "",
        "inline float DotQ(const float* features, const std::int32_t* coef, int count, int stride, int column) {",
        "    std::int64_t acc = 0;",
        "    for (int i = 0; i < count; ++i) {",
        "        acc += static_cast<std::int64_t>(QuantizeQ(features[i])) * coef[i * stride + column];",
        "    }",
        "    const float denom = static_cast<float>((1 << kFixedPointFracBits) * (1 << kFixedPointFracBits));",
        "    return static_cast<float>(acc) / denom;",
        "}",
        "",
        "inline float DotFloat(const float* features, const float* coef, int count, int stride, int column) {",
        "    float acc = 0.0f;",
        "    for (int i = 0; i < count; ++i) {",
        "        acc += features[i] * coef[i * stride + column];",
        "    }",
        "    return acc;",
        "}",
        "",
        "inline void BuildAdapterFeatures(const float gap[kGapDim], float features[kAdapterFeatureDim]) {",
        "    for (int i = 0; i < kGapDim; ++i) {",
        "        features[i] = (gap[i] - kGapMean[i]) / kGapStd[i];",
        "    }",
        "    features[kGapDim] = 1.0f;",
        "}",
        "",
        "inline void BuildGateFeatures(const float gap[kGapDim], const float old_logits[kParentCount], float features[kGateFeatureDim]) {",
        "    float raw[kGateRawFeatureDim] = {};",
        "    for (int i = 0; i < kGapDim; ++i) raw[i] = gap[i];",
        "    for (int i = 0; i < kParentCount; ++i) raw[kGapDim + i] = old_logits[i];",
        "    raw[kGapDim + kParentCount] = Margin3(old_logits);",
        "    const int old_pred = ArgMax3(old_logits);",
        "    raw[kGapDim + kParentCount + 1 + old_pred] = 1.0f;",
        "    for (int i = 0; i < kGateRawFeatureDim; ++i) {",
        "        features[i] = (raw[i] - kGateFeatureMean[i]) / kGateFeatureStd[i];",
        "    }",
        "    features[kGateRawFeatureDim] = 1.0f;",
        "}",
        "",
        "inline Phase2Result ApplyPhase2Float(const float gap[kGapDim], const float old_logits[kParentCount]) {",
        "    Phase2Result result{};",
        "    float adapter_features[kAdapterFeatureDim] = {};",
        "    BuildAdapterFeatures(gap, adapter_features);",
        "    float delta[kParentCount] = {};",
        "    for (int j = 0; j < kParentCount; ++j) {",
        "        delta[j] = DotFloat(adapter_features, kAdapterCoef, kAdapterFeatureDim, kParentCount, j);",
        "        result.adapter_logits[j] = old_logits[j] + kAdapterAlpha * delta[j];",
        "    }",
        "    float gate_features[kGateFeatureDim] = {};",
        "    BuildGateFeatures(gap, old_logits, gate_features);",
        "    result.gate_score = DotFloat(gate_features, kGateCoef, kGateFeatureDim, 1, 0);",
        "    result.adapter_margin = Margin3(result.adapter_logits);",
        "    const bool disagree = ArgMax3(old_logits) != ArgMax3(result.adapter_logits);",
        "    result.gate = ((result.gate_score > kGateThreshold) && disagree) ||",
        "                  ((result.gate_score > kGateTailLowThreshold) && disagree &&",
        "                   (result.adapter_margin < kGateTailMarginLimit));",
        "    for (int j = 0; j < kParentCount; ++j) {",
        "        result.logits[j] = old_logits[j] + (result.gate ? kAdapterAlpha * delta[j] : 0.0f);",
        "    }",
        "    result.parent = ArgMax3(result.logits);",
        "    return result;",
        "}",
        "",
        "inline Phase2Result ApplyPhase2Q(const float gap[kGapDim], const float old_logits[kParentCount]) {",
        "    Phase2Result result{};",
        "    float adapter_features[kAdapterFeatureDim] = {};",
        "    BuildAdapterFeatures(gap, adapter_features);",
        "    float delta[kParentCount] = {};",
        "    for (int j = 0; j < kParentCount; ++j) {",
        "        delta[j] = DotQ(adapter_features, kAdapterCoefQ, kAdapterFeatureDim, kParentCount, j);",
        "        result.adapter_logits[j] = old_logits[j] + kAdapterAlpha * delta[j];",
        "    }",
        "    float gate_features[kGateFeatureDim] = {};",
        "    BuildGateFeatures(gap, old_logits, gate_features);",
        "    result.gate_score = DotQ(gate_features, kGateCoefQ, kGateFeatureDim, 1, 0);",
        "    result.adapter_margin = Margin3(result.adapter_logits);",
        "    const bool disagree = ArgMax3(old_logits) != ArgMax3(result.adapter_logits);",
        "    result.gate = ((result.gate_score > kGateThreshold) && disagree) ||",
        "                  ((result.gate_score > kGateTailLowThreshold) && disagree &&",
        "                   (result.adapter_margin < kGateTailMarginLimit));",
        "    for (int j = 0; j < kParentCount; ++j) {",
        "        result.logits[j] = old_logits[j] + (result.gate ? kAdapterAlpha * delta[j] : 0.0f);",
        "    }",
        "    result.parent = ArgMax3(result.logits);",
        "    return result;",
        "}",
        "",
        "}  // namespace v7_phase2",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export and verify V7 phase2 adapter/router deploy bundle.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path)
    parser.add_argument("--params", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--quant-frac-bits", default="8,10,12,14,15")
    args = parser.parse_args()

    params = load_params(args.params)
    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old = TfliteReverse(args.old_tflite)
    old_data = old.infer_dataset(x)
    old_pred = old_data["pred"].astype(np.int64)

    if args.rescue_tflite:
        rescue = TfliteReverse(args.rescue_tflite)
        rescue_data = rescue.infer_dataset(x)
        rescue_pred = rescue_data["pred"].astype(np.int64)
    else:
        rescue_pred = old_pred.copy()
    groups = group_masks(paths, y_sub, y_parent, old_pred, rescue_pred)

    old_logits = old_data["logits"].astype(np.float64)
    old_gap = old_data["gap"].astype(np.float64)
    original_float_metrics, original_float_arrays, _float_meta = evaluate_outputs(
        old_logits=old_logits,
        old_gap=old_gap,
        y_parent=y_parent,
        groups=groups,
        paths=paths,
        params=params,
        quant_frac_bits=None,
    )
    calibrated_params, threshold_calibration = calibrate_two_band_thresholds(params, old_pred, original_float_arrays)
    float_metrics, float_arrays, _float_meta = evaluate_outputs(
        old_logits=old_logits,
        old_gap=old_gap,
        y_parent=y_parent,
        groups=groups,
        paths=paths,
        params=calibrated_params,
        quant_frac_bits=None,
    )
    if not (
        np.array_equal(original_float_arrays["gate"], float_arrays["gate"])
        and np.array_equal(original_float_arrays["pred"], float_arrays["pred"])
    ):
        calibrated_params = params
        float_metrics = original_float_metrics
        float_arrays = original_float_arrays
        threshold_calibration["fallback_to_original"] = True
    else:
        threshold_calibration["fallback_to_original"] = False

    quant_rows: list[dict[str, object]] = []
    quant_metas: dict[str, object] = {}
    quant_arrays: dict[str, dict[str, np.ndarray]] = {}
    for item in args.quant_frac_bits.split(","):
        if not item.strip():
            continue
        bits = int(item)
        metrics, arrays, meta = evaluate_outputs(
            old_logits=old_logits,
            old_gap=old_gap,
            y_parent=y_parent,
            groups=groups,
            paths=paths,
            params=calibrated_params,
            quant_frac_bits=bits,
        )
        quant_rows.append(metrics)
        quant_metas[str(bits)] = meta
        quant_arrays[str(bits)] = arrays

    valid_quant_rows = [
        row
        for row in quant_rows
        if int(row["all_correct"]) == len(y_parent)
        and int(row["gate_count"]) == int(float_metrics["gate_count"])
        and int(row["preserve_false_trigger"]) == 0
        and int(row["stable_false_trigger"]) == 0
        and bool(quant_metas[str(row["mode"])[1:]]["adapter"]["int32_accumulator_safe"])  # type: ignore[index]
        and bool(quant_metas[str(row["mode"])[1:]]["gate"]["int32_accumulator_safe"])  # type: ignore[index]
    ]
    if valid_quant_rows:
        best_quant = sorted(valid_quant_rows, key=lambda row: int(str(row["mode"])[1:]))[0]
    else:
        best_quant = sorted(quant_rows, key=lambda row: (int(row["all_correct"]), -int(str(row["mode"])[1:])), reverse=True)[0]
    best_quant_bits = int(str(best_quant["mode"])[1:])

    sample_rows: list[dict[str, object]] = []
    best_quant_arrays = quant_arrays[str(best_quant_bits)]
    for index, path in enumerate(paths):
        sample_rows.append(
            {
                "index": index,
                "path": path,
                "file": Path(path).name,
                "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                "parent": PARENT_NAMES[int(y_parent[index])],
                "old_pred": PARENT_NAMES[int(old_pred[index])],
                "float_pred": PARENT_NAMES[int(float_arrays["pred"][index])],
                "quant_pred": PARENT_NAMES[int(best_quant_arrays["pred"][index])],
                "float_gate": bool(float_arrays["gate"][index]),
                "quant_gate": bool(best_quant_arrays["gate"][index]),
                "float_gate_score": float(float_arrays["gate_score"][index]),
                "quant_gate_score": float(best_quant_arrays["gate_score"][index]),
                "float_adapter_margin": float(float_arrays["adapter_margin"][index]),
                "quant_adapter_margin": float(best_quant_arrays["adapter_margin"][index]),
                "stable": bool(groups["stable"][index]),
                "preserve": bool(groups["preserve"][index]),
                "rescue": bool(groups["rescue"][index]),
                "hard": bool(groups["hard"][index]),
                "c4": bool(groups["c4"][index]),
            }
        )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "deploy_sample_decisions.csv", sample_rows)
    write_csv(args.output_dir / "quantization_sweep.csv", [float_metrics] + quant_rows)
    write_header(args.output_dir / "v7_phase2_adapter_params.hpp", calibrated_params, best_quant_bits)

    bundle = {
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite) if args.rescue_tflite else "",
        "params": str(args.params),
        "parent_names": PARENT_NAMES,
        "group_counts": {name: int(np.sum(mask)) for name, mask in groups.items()},
        "float_metrics": float_metrics,
        "threshold_calibration": threshold_calibration,
        "quantization": {
            "rows": quant_rows,
            "metas": quant_metas,
            "selected_mode": best_quant["mode"],
            "selected_metrics": best_quant,
        },
        "deploy_files": {
            "header": str(args.output_dir / "v7_phase2_adapter_params.hpp"),
            "sample_decisions": str(args.output_dir / "deploy_sample_decisions.csv"),
            "quantization_sweep": str(args.output_dir / "quantization_sweep.csv"),
        },
        "notes": [
            "old stable TFLite remains the only neural TFLite model.",
            "phase2 adapter/router consumes dequantized old GAP and parent logits.",
            "fixed-point sweep quantizes z-scored features and coefficients for adapter/router dot products.",
        ],
    }
    (args.output_dir / "deploy_bundle_summary.json").write_text(json.dumps(bundle, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"float_metrics": float_metrics, "selected_quant": best_quant}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
