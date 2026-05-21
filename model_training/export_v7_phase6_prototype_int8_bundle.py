import argparse
import json
import math
from pathlib import Path

import numpy as np

from run_v7_delta_merge_phase1 import PARENT_NAMES
from run_v7_phase3_stress_aware_search import ROT_MIRROR_VIEWS, write_csv
from run_v7_phase6_prototype_rescue import as_str_list, flatten_cache, per_view_metrics


def parse_floats(text: str) -> list[float]:
    return [float(item.strip()) for item in text.split(",") if item.strip()]


def c_float(value: float) -> str:
    if not math.isfinite(float(value)):
        raise ValueError(f"non-finite C float literal: {value}")
    text = f"{float(value):.9g}"
    if "e" not in text and "E" not in text and "." not in text:
        text += ".0"
    return f"{text}f"


def c_array(name: str, values: np.ndarray, c_type: str, per_line: int = 16) -> str:
    flat = values.reshape(-1)
    if c_type == "float":
        rendered = [c_float(float(item)) for item in flat]
    else:
        rendered = [str(int(item)) for item in flat]
    chunks = [", ".join(rendered[index : index + per_line]) for index in range(0, len(rendered), per_line)]
    body = ",\n    ".join(chunks)
    return f"constexpr {c_type} {name}[{flat.size}] = {{\n    {body}\n}};"


def quantize_features(values: np.ndarray, scale: float) -> np.ndarray:
    return np.clip(np.rint(values * scale), -128, 127).astype(np.int16)


def dedup_quantized_prototypes(proto_q: np.ndarray, labels: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    keep: list[int] = []
    seen: set[tuple[int, bytes]] = set()
    for index, (vector, label) in enumerate(zip(proto_q, labels)):
        key = (int(label), bytes(vector.astype(np.int8)))
        if key in seen:
            continue
        seen.add(key)
        keep.append(index)
    return proto_q[np.asarray(keep, dtype=np.int64)], labels[np.asarray(keep, dtype=np.int64)]


def nearest_int8(
    z_q: np.ndarray,
    proto_q: np.ndarray,
    labels: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    best_dist = np.full((len(z_q), len(PARENT_NAMES)), np.iinfo(np.int32).max, dtype=np.int32)
    z_i32 = z_q.astype(np.int32)
    for proto_index, label in enumerate(labels.tolist()):
        delta = z_i32 - proto_q[proto_index].astype(np.int32)
        dist = np.sum(delta * delta, axis=1).astype(np.int32)
        best_dist[:, int(label)] = np.minimum(best_dist[:, int(label)], dist)
    proto_pred = np.argmin(best_dist, axis=1).astype(np.int64)
    sorted_dist = np.sort(best_dist, axis=1)
    nearest = sorted_dist[:, 0].astype(np.int32)
    confidence = (sorted_dist[:, 1] - sorted_dist[:, 0]).astype(np.int32)
    return proto_pred, nearest, confidence


def evaluate_quantized(
    *,
    flat: dict[str, object],
    params: dict[str, object],
    scale: float,
    threshold_grid: int,
) -> tuple[dict[str, object], dict[str, np.ndarray]]:
    feature_name = str(np.asarray(params["feature_name"]).item())
    if feature_name != "old_gap":
        raise ValueError(f"int8 prototype export currently supports old_gap only, got {feature_name}")
    gap = np.asarray(flat["old_gap"], dtype=np.float64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    view_order = list(flat["views"])  # type: ignore[arg-type]
    mean = np.asarray(params["feature_mean"], dtype=np.float64)
    std = np.asarray(params["feature_std"], dtype=np.float64)
    prototypes = np.asarray(params["prototype_features"], dtype=np.float64)
    labels = np.asarray(params["prototype_labels"], dtype=np.int64)
    z = (gap - mean) / std
    z_q = quantize_features(z, scale)
    raw_proto_q = quantize_features(prototypes, scale)
    proto_q, labels = dedup_quantized_prototypes(raw_proto_q, labels)
    proto_pred, nearest, confidence = nearest_int8(z_q, proto_q, labels)
    conf_values = np.unique(np.quantile(confidence, np.linspace(0.0, 1.0, threshold_grid)).astype(np.int32))
    dist_values = np.unique(np.quantile(nearest, np.linspace(0.0, 1.0, threshold_grid)).astype(np.int32))
    best_row: dict[str, object] | None = None
    best_arrays: dict[str, np.ndarray] = {}
    rot_views = [view for view in ROT_MIRROR_VIEWS if view in set(view_order)]
    for conf_threshold in conf_values:
        for dist_threshold in dist_values:
            gate = (proto_pred != old_pred) & (confidence >= conf_threshold) & (nearest <= dist_threshold)
            pred = np.where(gate, proto_pred, old_pred)
            per_view = per_view_metrics(
                view_order=view_order,
                view_labels=view_labels,
                y_parent=y_parent,
                pred=pred,
                gate=gate,
            )
            by_name = {str(item["stress"]): item for item in per_view}
            stress_rows = [item for item in per_view if item["stress"] != "clean"]
            rot_rows = [by_name[view] for view in rot_views]
            row = {
                "name": "phase6_prototype_rescue_int8",
                "feature_name": feature_name,
                "feature_dim": int(proto_q.shape[1]),
                "prototype_count": int(len(proto_q)),
                "quant_scale": float(scale),
                "conf_threshold": int(conf_threshold),
                "dist_threshold": int(dist_threshold),
                "clean_correct": int(by_name["clean"]["correct"]),
                "clean_total": int(by_name["clean"]["correct"]) + int(by_name["clean"]["wrong"]),
                "clean_accuracy": float(by_name["clean"]["accuracy"]),
                "clean_all_correct": int(by_name["clean"]["wrong"]) == 0,
                "rotmirror_min_accuracy": float(min(float(item["accuracy"]) for item in rot_rows)),
                "rotmirror_all_correct": all(int(item["wrong"]) == 0 for item in rot_rows),
                "stress_min_accuracy": float(min(float(item["accuracy"]) for item in stress_rows)),
                "stress_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in stress_rows])),
                "stress_all_correct": all(int(item["wrong"]) == 0 for item in stress_rows),
                "gate_count": int(np.sum(gate)),
                "estimated_distance_macs": int(len(proto_q) * proto_q.shape[1]),
                "estimated_int8_table_bytes": int(len(proto_q) * proto_q.shape[1]),
                "per_view_json": json.dumps(per_view, ensure_ascii=False),
            }
            score = (
                bool(row["clean_all_correct"]),
                bool(row["rotmirror_all_correct"]),
                bool(row["stress_all_correct"]),
                int(row["clean_correct"]),
                float(row["rotmirror_min_accuracy"]),
                float(row["stress_min_accuracy"]),
                float(row["stress_mean_accuracy"]),
                -int(row["gate_count"]),
                -int(row["dist_threshold"]),
                -int(row["conf_threshold"]),
            )
            best_score = None
            if best_row is not None:
                best_score = (
                    bool(best_row["clean_all_correct"]),
                    bool(best_row["rotmirror_all_correct"]),
                    bool(best_row["stress_all_correct"]),
                    int(best_row["clean_correct"]),
                    float(best_row["rotmirror_min_accuracy"]),
                    float(best_row["stress_min_accuracy"]),
                    float(best_row["stress_mean_accuracy"]),
                    -int(best_row["gate_count"]),
                    -int(best_row["dist_threshold"]),
                    -int(best_row["conf_threshold"]),
                )
            if best_score is None or score > best_score:
                best_row = row
                best_arrays = {
                    "z_q": z_q.astype(np.int8),
                    "prototype_q": proto_q.astype(np.int8),
                    "prototype_labels": labels.astype(np.uint8),
                    "proto_pred": proto_pred.astype(np.int64),
                    "nearest": nearest.astype(np.int32),
                    "confidence": confidence.astype(np.int32),
                    "gate": gate.astype(bool),
                    "pred": pred.astype(np.int64),
                }
    assert best_row is not None
    return best_row, best_arrays


def write_header(path: Path, params: dict[str, object], best: dict[str, object], arrays: dict[str, np.ndarray]) -> None:
    mean = np.asarray(params["feature_mean"], dtype=np.float32)
    std = np.asarray(params["feature_std"], dtype=np.float32)
    proto_q = np.asarray(arrays["prototype_q"], dtype=np.int8)
    labels = np.asarray(arrays["prototype_labels"], dtype=np.uint8)
    gap_dim = int(proto_q.shape[1])
    lines = [
        "#pragma once",
        "",
        "#include <cmath>",
        "#include <cstdint>",
        "#include <limits>",
        "",
        "namespace v7_phase6_int8 {",
        "",
        f"constexpr int kGapDim = {gap_dim};",
        "constexpr int kParentCount = 3;",
        f"constexpr int kPrototypeCount = {int(proto_q.shape[0])};",
        f"constexpr float kQuantScale = {c_float(float(best['quant_scale']))};",
        f"constexpr std::int32_t kProtoConfThreshold = {int(best['conf_threshold'])};",
        f"constexpr std::int32_t kProtoDistThreshold = {int(best['dist_threshold'])};",
        "",
        c_array("kFeatureMean", mean, "float", per_line=12),
        c_array("kFeatureStd", std, "float", per_line=12),
        c_array("kPrototypeFeaturesQ", proto_q, "std::int8_t", per_line=24),
        c_array("kPrototypeLabels", labels, "std::uint8_t", per_line=24),
        "",
        "struct Phase6Int8Result {",
        "    int parent = 0;",
        "    bool gate = false;",
        "    int prototype_parent = 0;",
        "    std::int32_t nearest_distance = 0;",
        "    std::int32_t prototype_confidence = 0;",
        "};",
        "",
        "inline int ArgMax3(const float values[kParentCount]) {",
        "    int best = 0;",
        "    if (values[1] > values[best]) best = 1;",
        "    if (values[2] > values[best]) best = 2;",
        "    return best;",
        "}",
        "",
        "inline std::int8_t QuantizeFeature(float value) {",
        "    const long rounded = std::lround(value * kQuantScale);",
        "    if (rounded < -128L) return static_cast<std::int8_t>(-128);",
        "    if (rounded > 127L) return static_cast<std::int8_t>(127);",
        "    return static_cast<std::int8_t>(rounded);",
        "}",
        "",
        "inline Phase6Int8Result ApplyPrototypeRescueInt8(const float gap[kGapDim], const float old_logits[kParentCount]) {",
        "    std::int8_t z[kGapDim] = {};",
        "    for (int i = 0; i < kGapDim; ++i) {",
        "        z[i] = QuantizeFeature((gap[i] - kFeatureMean[i]) / kFeatureStd[i]);",
        "    }",
        "    std::int32_t best_dist[kParentCount] = {",
        "        std::numeric_limits<std::int32_t>::max(),",
        "        std::numeric_limits<std::int32_t>::max(),",
        "        std::numeric_limits<std::int32_t>::max()",
        "    };",
        "    for (int p = 0; p < kPrototypeCount; ++p) {",
        "        std::int32_t dist = 0;",
        "        const int base = p * kGapDim;",
        "        for (int i = 0; i < kGapDim; ++i) {",
        "            const std::int32_t diff = static_cast<std::int32_t>(z[i]) -",
        "                                      static_cast<std::int32_t>(kPrototypeFeaturesQ[base + i]);",
        "            dist += diff * diff;",
        "        }",
        "        const int label = static_cast<int>(kPrototypeLabels[p]);",
        "        if (dist < best_dist[label]) best_dist[label] = dist;",
        "    }",
        "    int proto_parent = 0;",
        "    if (best_dist[1] < best_dist[proto_parent]) proto_parent = 1;",
        "    if (best_dist[2] < best_dist[proto_parent]) proto_parent = 2;",
        "    const std::int32_t nearest = best_dist[proto_parent];",
        "    std::int32_t second = std::numeric_limits<std::int32_t>::max();",
        "    for (int i = 0; i < kParentCount; ++i) {",
        "        if (i != proto_parent && best_dist[i] < second) second = best_dist[i];",
        "    }",
        "    const std::int32_t confidence = second - nearest;",
        "    const int old_parent = ArgMax3(old_logits);",
        "    Phase6Int8Result result{};",
        "    result.prototype_parent = proto_parent;",
        "    result.nearest_distance = nearest;",
        "    result.prototype_confidence = confidence;",
        "    result.gate = (proto_parent != old_parent) &&",
        "                  (confidence >= kProtoConfThreshold) &&",
        "                  (nearest <= kProtoDistThreshold);",
        "    result.parent = result.gate ? proto_parent : old_parent;",
        "    return result;",
        "}",
        "",
        "}  // namespace v7_phase6_int8",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def load_params(path: Path) -> dict[str, object]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def main() -> None:
    parser = argparse.ArgumentParser(description="Export and verify V7 phase6 int8 prototype rescue bundle.")
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--params", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--threshold-grid", type=int, default=121)
    args = parser.parse_args()

    params = load_params(args.params)
    with np.load(args.feature_cache, allow_pickle=True) as data:
        cache = {key: data[key] for key in data.files}
    flat = flatten_cache(cache, as_str_list(cache["view_names"]))
    rows: list[dict[str, object]] = []
    payloads: dict[int, dict[str, np.ndarray]] = {}
    for scale in parse_floats(args.quant_scales):
        row, arrays = evaluate_quantized(flat=flat, params=params, scale=scale, threshold_grid=args.threshold_grid)
        rows.append(row)
        payloads[len(rows) - 1] = arrays
    rows_sorted = sorted(
        enumerate(rows),
        key=lambda item: (
            bool(item[1]["clean_all_correct"]),
            bool(item[1]["rotmirror_all_correct"]),
            bool(item[1]["stress_all_correct"]),
            int(item[1]["clean_correct"]),
            float(item[1]["rotmirror_min_accuracy"]),
            float(item[1]["stress_min_accuracy"]),
            float(item[1]["stress_mean_accuracy"]),
            -int(item[1]["gate_count"]),
            -int(item[1]["estimated_int8_table_bytes"]),
        ),
        reverse=True,
    )
    best_index, best = rows_sorted[0]
    arrays = payloads[best_index]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "int8_quantization_candidates.csv", [row for _idx, row in rows_sorted])
    per_view = json.loads(str(best["per_view_json"]))
    write_csv(args.output_dir / "deploy_stress_summary.csv", per_view)
    write_header(args.output_dir / "v7_phase6_prototype_int8_params.hpp", params, best, arrays)
    summary = {
        "feature_cache": str(args.feature_cache),
        "params": str(args.params),
        "parent_names": PARENT_NAMES,
        "selected": best,
        "candidates": [row for _idx, row in rows_sorted],
        "deploy_files": {
            "header": str(args.output_dir / "v7_phase6_prototype_int8_params.hpp"),
            "stress_summary": str(args.output_dir / "deploy_stress_summary.csv"),
            "quantization_candidates": str(args.output_dir / "int8_quantization_candidates.csv"),
        },
        "notes": [
            "old stable/fast TFLite remains the only neural model.",
            "prototype table is int8 z-scored GAP; distance and thresholds are int32.",
            "This bundle is selected only if clean, rot_mirror, and stress replay remain exact.",
        ],
    }
    (args.output_dir / "deploy_bundle_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"selected": best}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
