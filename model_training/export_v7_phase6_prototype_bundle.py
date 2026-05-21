import argparse
import json
import math
from pathlib import Path

import numpy as np

from run_v7_delta_merge_phase1 import PARENT_NAMES
from run_v7_phase3_stress_aware_search import ROT_MIRROR_VIEWS, write_csv
from run_v7_phase6_prototype_rescue import as_str_list, flatten_cache, per_view_metrics


def c_float(value: float) -> str:
    if not math.isfinite(float(value)):
        raise ValueError(f"non-finite C float literal: {value}")
    text = f"{float(value):.9g}"
    if "e" not in text and "E" not in text and "." not in text:
        text += ".0"
    return f"{text}f"


def c_array(name: str, values: np.ndarray, c_type: str = "float", per_line: int = 12) -> str:
    flat = values.reshape(-1)
    if c_type == "float":
        rendered = [c_float(float(item)) for item in flat]
    else:
        rendered = [str(int(item)) for item in flat]
    chunks = [", ".join(rendered[index : index + per_line]) for index in range(0, len(rendered), per_line)]
    body = ",\n    ".join(chunks)
    return f"constexpr {c_type} {name}[{flat.size}] = {{\n    {body}\n}};"


def load_params(path: Path) -> dict[str, object]:
    with np.load(path, allow_pickle=True) as data:
        params: dict[str, object] = {key: data[key] for key in data.files}
    if "best_row_json" in params:
        params["best_config"] = json.loads(str(np.asarray(params["best_row_json"]).item()))
    else:
        params["best_config"] = {}
    return params


def evaluate_params(flat: dict[str, object], params: dict[str, object]) -> tuple[dict[str, object], dict[str, np.ndarray]]:
    feature_name = str(np.asarray(params["feature_name"]).item())
    if feature_name != "old_gap":
        raise ValueError(f"phase6 header export currently supports old_gap only, got {feature_name}")
    gap = np.asarray(flat["old_gap"], dtype=np.float64)
    old_logits = np.asarray(flat["old_logits"], dtype=np.float64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    view_order = list(flat["views"])  # type: ignore[arg-type]
    mean = np.asarray(params["feature_mean"], dtype=np.float64)
    std = np.asarray(params["feature_std"], dtype=np.float64)
    prototypes = np.asarray(params["prototype_features"], dtype=np.float64)
    labels = np.asarray(params["prototype_labels"], dtype=np.int64)
    conf_threshold = float(np.asarray(params["conf_threshold"]).item())
    dist_threshold = float(np.asarray(params["dist_threshold"]).item())
    z = (gap - mean) / std
    best_dist = np.full((len(z), len(PARENT_NAMES)), np.inf, dtype=np.float64)
    for proto_index, label in enumerate(labels.tolist()):
        delta = z - prototypes[proto_index]
        dist = np.sum(delta * delta, axis=1)
        best_dist[:, int(label)] = np.minimum(best_dist[:, int(label)], dist)
    proto_pred = np.argmin(best_dist, axis=1).astype(np.int64)
    sorted_dist = np.sort(best_dist, axis=1)
    nearest_dist = sorted_dist[:, 0]
    proto_conf = sorted_dist[:, 1] - sorted_dist[:, 0]
    gate = (proto_pred != old_pred) & (proto_conf >= conf_threshold) & (nearest_dist <= dist_threshold)
    pred = np.where(gate, proto_pred, old_pred)
    per_view = per_view_metrics(
        view_order=view_order,
        view_labels=view_labels,
        y_parent=y_parent,
        pred=pred,
        gate=gate,
    )
    per_view_by_name = {str(item["stress"]): item for item in per_view}
    stress_rows = [item for item in per_view if item["stress"] != "clean"]
    rot_rows = [per_view_by_name[view] for view in ROT_MIRROR_VIEWS]
    metrics = {
        "clean_correct": int(per_view_by_name["clean"]["correct"]),
        "clean_total": int(per_view_by_name["clean"]["correct"]) + int(per_view_by_name["clean"]["wrong"]),
        "clean_accuracy": float(per_view_by_name["clean"]["accuracy"]),
        "clean_all_correct": int(per_view_by_name["clean"]["wrong"]) == 0,
        "rotmirror_min_accuracy": float(min(float(item["accuracy"]) for item in rot_rows)),
        "rotmirror_all_correct": all(int(item["wrong"]) == 0 for item in rot_rows),
        "stress_min_accuracy": float(min(float(item["accuracy"]) for item in stress_rows)),
        "stress_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in stress_rows])),
        "stress_all_correct": all(int(item["wrong"]) == 0 for item in stress_rows),
        "gate_count": int(np.sum(gate)),
        "prototype_count": int(len(prototypes)),
        "feature_dim": int(prototypes.shape[1]),
        "estimated_distance_macs": int(prototypes.shape[0] * prototypes.shape[1]),
        "estimated_float_table_bytes": int(prototypes.shape[0] * prototypes.shape[1] * 4),
        "estimated_int8_table_bytes": int(prototypes.shape[0] * prototypes.shape[1]),
    }
    arrays = {
        "pred": pred,
        "gate": gate,
        "proto_pred": proto_pred,
        "proto_conf": proto_conf,
        "nearest_dist": nearest_dist,
        "per_view": np.asarray(per_view, dtype=object),
    }
    return metrics, arrays


def write_header(path: Path, params: dict[str, object]) -> None:
    feature_name = str(np.asarray(params["feature_name"]).item())
    if feature_name != "old_gap":
        raise ValueError(f"phase6 header export currently supports old_gap only, got {feature_name}")
    mean = np.asarray(params["feature_mean"], dtype=np.float32)
    std = np.asarray(params["feature_std"], dtype=np.float32)
    prototypes = np.asarray(params["prototype_features"], dtype=np.float32)
    labels = np.asarray(params["prototype_labels"], dtype=np.int64).astype(np.uint8)
    gap_dim = int(prototypes.shape[1])
    conf_threshold = float(np.asarray(params["conf_threshold"]).item())
    dist_threshold = float(np.asarray(params["dist_threshold"]).item())
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <limits>",
        "",
        "namespace v7_phase6 {",
        "",
        f"constexpr int kGapDim = {gap_dim};",
        "constexpr int kParentCount = 3;",
        f"constexpr int kPrototypeCount = {int(prototypes.shape[0])};",
        f"constexpr float kProtoConfThreshold = {c_float(conf_threshold)};",
        f"constexpr float kProtoDistThreshold = {c_float(dist_threshold)};",
        "",
        c_array("kFeatureMean", mean),
        c_array("kFeatureStd", std),
        c_array("kPrototypeFeatures", prototypes),
        c_array("kPrototypeLabels", labels, "std::uint8_t", per_line=24),
        "",
        "struct Phase6Result {",
        "    int parent = 0;",
        "    bool gate = false;",
        "    int prototype_parent = 0;",
        "    float nearest_distance = 0.0f;",
        "    float prototype_confidence = 0.0f;",
        "};",
        "",
        "inline int ArgMax3(const float values[kParentCount]) {",
        "    int best = 0;",
        "    if (values[1] > values[best]) best = 1;",
        "    if (values[2] > values[best]) best = 2;",
        "    return best;",
        "}",
        "",
        "inline Phase6Result ApplyPrototypeRescueFloat(const float gap[kGapDim], const float old_logits[kParentCount]) {",
        "    float z[kGapDim] = {};",
        "    for (int i = 0; i < kGapDim; ++i) {",
        "        z[i] = (gap[i] - kFeatureMean[i]) / kFeatureStd[i];",
        "    }",
        "    float best_dist[kParentCount] = {",
        "        std::numeric_limits<float>::infinity(),",
        "        std::numeric_limits<float>::infinity(),",
        "        std::numeric_limits<float>::infinity()",
        "    };",
        "    for (int p = 0; p < kPrototypeCount; ++p) {",
        "        float dist = 0.0f;",
        "        const int base = p * kGapDim;",
        "        for (int i = 0; i < kGapDim; ++i) {",
        "            const float diff = z[i] - kPrototypeFeatures[base + i];",
        "            dist += diff * diff;",
        "        }",
        "        const int label = static_cast<int>(kPrototypeLabels[p]);",
        "        if (dist < best_dist[label]) best_dist[label] = dist;",
        "    }",
        "    int proto_parent = 0;",
        "    if (best_dist[1] < best_dist[proto_parent]) proto_parent = 1;",
        "    if (best_dist[2] < best_dist[proto_parent]) proto_parent = 2;",
        "    float nearest = best_dist[proto_parent];",
        "    float second = std::numeric_limits<float>::infinity();",
        "    for (int i = 0; i < kParentCount; ++i) {",
        "        if (i != proto_parent && best_dist[i] < second) second = best_dist[i];",
        "    }",
        "    const float confidence = second - nearest;",
        "    const int old_parent = ArgMax3(old_logits);",
        "    Phase6Result result{};",
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
        "}  // namespace v7_phase6",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export and verify V7 phase6 prototype rescue deploy bundle.")
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--params", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    params = load_params(args.params)
    with np.load(args.feature_cache, allow_pickle=True) as data:
        cache = {key: data[key] for key in data.files}
    flat = flatten_cache(cache, as_str_list(cache["view_names"]))
    metrics, arrays = evaluate_params(flat, params)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_header(args.output_dir / "v7_phase6_prototype_params.hpp", params)

    view_order = list(flat["views"])  # type: ignore[arg-type]
    per_view = per_view_metrics(
        view_order=view_order,
        view_labels=np.asarray(flat["view_labels"]),
        y_parent=np.asarray(flat["y_parent"], dtype=np.int64),
        pred=np.asarray(arrays["pred"], dtype=np.int64),
        gate=np.asarray(arrays["gate"], dtype=bool),
    )
    write_csv(args.output_dir / "deploy_stress_summary.csv", per_view)
    sample_rows = []
    paths = list(flat["paths"])  # type: ignore[arg-type]
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    pred = np.asarray(arrays["pred"], dtype=np.int64)
    gate = np.asarray(arrays["gate"], dtype=bool)
    for index in np.where(gate | (pred != y_parent))[0].tolist():
        sample_rows.append(
            {
                "stress": str(view_labels[index]),
                "sample_index": int(sample_index[index]),
                "file": Path(str(paths[int(sample_index[index])])).name,
                "parent": PARENT_NAMES[int(y_parent[index])],
                "old_pred": PARENT_NAMES[int(old_pred[index])],
                "prototype_pred": PARENT_NAMES[int(arrays["proto_pred"][index])],
                "final_pred": PARENT_NAMES[int(pred[index])],
                "correct": bool(pred[index] == y_parent[index]),
                "gate": bool(gate[index]),
                "nearest_distance": float(arrays["nearest_dist"][index]),
                "prototype_confidence": float(arrays["proto_conf"][index]),
            }
        )
    write_csv(args.output_dir / "deploy_sample_decisions.csv", sample_rows)
    summary = {
        "feature_cache": str(args.feature_cache),
        "params": str(args.params),
        "parent_names": PARENT_NAMES,
        "metrics": metrics,
        "deploy_files": {
            "header": str(args.output_dir / "v7_phase6_prototype_params.hpp"),
            "stress_summary": str(args.output_dir / "deploy_stress_summary.csv"),
            "sample_decisions": str(args.output_dir / "deploy_sample_decisions.csv"),
        },
        "notes": [
            "old stable TFLite remains the only neural model.",
            "phase6 prototype rescue consumes dequantized old GAP and parent logits.",
            "prototype table is z-scored old_gap space; gate overrides only when nearest class is close and separated.",
        ],
    }
    (args.output_dir / "deploy_bundle_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"metrics": metrics}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
