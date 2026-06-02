import argparse
import csv
import json
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import tensorflow as tf


PARENT_NAMES = ["supplies", "vehicle", "weapon"]
LAYER_SPECS = {
    "block_1_conv": {"weight_shape": (10, 3, 3, 4), "bias_shape": (10,)},
    "block_2_conv": {"weight_shape": (18, 3, 3, 10), "bias_shape": (18,)},
    "block_3_conv": {"weight_shape": (36, 3, 3, 18), "bias_shape": (36,)},
    "parent_logits": {"weight_shape": (3, 36), "bias_shape": (3,)},
}


def cosine(a: np.ndarray, b: np.ndarray) -> float | None:
    denom = float(np.linalg.norm(a) * np.linalg.norm(b))
    if denom <= 0.0:
        return None
    return float(np.dot(a, b) / denom)


def dequantize(value: np.ndarray, detail: dict[str, object]) -> np.ndarray:
    params = detail.get("quantization_parameters", {})
    scales = np.asarray(params.get("scales", []), dtype=np.float32)
    zero_points = np.asarray(params.get("zero_points", []), dtype=np.float32)
    if value.dtype not in (np.int8, np.int32) or scales.size == 0:
        return value.astype(np.float32)
    quant_dim = int(params.get("quantized_dimension", 0))
    if scales.size == 1:
        return (value.astype(np.float32) - float(zero_points[0])) * float(scales[0])
    shape = [1] * value.ndim
    shape[quant_dim] = scales.size
    return (value.astype(np.float32) - zero_points.reshape(shape)) * scales.reshape(shape)


def load_tensors(path: Path) -> dict[str, dict[str, np.ndarray | dict[str, object]]]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    tensors: dict[str, dict[str, np.ndarray | dict[str, object]]] = {}
    for detail in interpreter.get_tensor_details():
        name = str(detail["name"])
        shape = tuple(int(v) for v in detail["shape"])
        dtype = detail["dtype"]
        for layer_name, spec in LAYER_SPECS.items():
            if layer_name not in name:
                continue
            if dtype == np.int8 and shape == spec["weight_shape"]:
                role = "weight"
            elif dtype == np.int32 and shape == spec["bias_shape"]:
                role = "bias"
            else:
                continue
            tensors[f"{layer_name}.{role}"] = {
                "name": name,
                "shape": np.asarray(shape, dtype=np.int64),
                "raw": interpreter.get_tensor(int(detail["index"])),
                "value": dequantize(interpreter.get_tensor(int(detail["index"])), detail),
                "detail": detail,
            }
    return tensors


def compare_arrays(old: np.ndarray, new: np.ndarray) -> dict[str, object]:
    old_flat = np.asarray(old, dtype=np.float64).reshape(-1)
    new_flat = np.asarray(new, dtype=np.float64).reshape(-1)
    delta = new_flat - old_flat
    old_norm = float(np.linalg.norm(old_flat))
    new_norm = float(np.linalg.norm(new_flat))
    delta_norm = float(np.linalg.norm(delta))
    return {
        "old_norm": old_norm,
        "new_norm": new_norm,
        "delta_norm": delta_norm,
        "relative_delta": float(delta_norm / old_norm) if old_norm > 0 else None,
        "cosine": cosine(old_flat, new_flat),
        "max_abs_delta": float(np.max(np.abs(delta))) if delta.size else 0.0,
        "mean_abs_delta": float(np.mean(np.abs(delta))) if delta.size else 0.0,
    }


def compare_tensors(old_tensors: dict[str, dict[str, object]], new_tensors: dict[str, dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for key in sorted(set(old_tensors) | set(new_tensors)):
        if key not in old_tensors or key not in new_tensors:
            rows.append({"tensor": key, "status": "missing"})
            continue
        old_value = old_tensors[key]["value"]
        new_value = new_tensors[key]["value"]
        if np.shape(old_value) != np.shape(new_value):
            rows.append(
                {
                    "tensor": key,
                    "status": "shape_mismatch",
                    "old_shape": list(np.shape(old_value)),
                    "new_shape": list(np.shape(new_value)),
                }
            )
            continue
        layer, role = key.rsplit(".", 1)
        rows.append(
            {
                "tensor": key,
                "layer": layer,
                "role": role,
                "status": "ok",
                "shape": list(np.shape(old_value)),
                **compare_arrays(np.asarray(old_value), np.asarray(new_value)),
            }
        )
    return rows


def parent_head_rows(old_tensors: dict[str, dict[str, object]], new_tensors: dict[str, dict[str, object]]) -> list[dict[str, object]]:
    old_weight = np.asarray(old_tensors["parent_logits.weight"]["value"], dtype=np.float32)
    new_weight = np.asarray(new_tensors["parent_logits.weight"]["value"], dtype=np.float32)
    old_bias = np.asarray(old_tensors["parent_logits.bias"]["value"], dtype=np.float32)
    new_bias = np.asarray(new_tensors["parent_logits.bias"]["value"], dtype=np.float32)
    rows: list[dict[str, object]] = []
    for index, name in enumerate(PARENT_NAMES):
        rows.append(
            {
                "parent": name,
                "old_bias": float(old_bias[index]),
                "new_bias": float(new_bias[index]),
                "bias_delta": float(new_bias[index] - old_bias[index]),
                **compare_arrays(old_weight[index], new_weight[index]),
            }
        )
    for i, left in enumerate(PARENT_NAMES):
        for j, right in enumerate(PARENT_NAMES):
            if i >= j:
                continue
            old_boundary = old_weight[i] - old_weight[j]
            new_boundary = new_weight[i] - new_weight[j]
            old_bias_delta = float(old_bias[i] - old_bias[j])
            new_bias_delta = float(new_bias[i] - new_bias[j])
            rows.append(
                {
                    "parent": f"{left}_vs_{right}",
                    "old_bias": old_bias_delta,
                    "new_bias": new_bias_delta,
                    "bias_delta": float(new_bias_delta - old_bias_delta),
                    **compare_arrays(old_boundary, new_boundary),
                }
            )
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--new-tflite", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    old_tensors = load_tensors(args.old_tflite)
    new_tensors = load_tensors(args.new_tflite)
    tensor_rows = compare_tensors(old_tensors, new_tensors)
    parent_rows = parent_head_rows(old_tensors, new_tensors)
    ok_rows = [row for row in tensor_rows if row.get("status") == "ok"]
    top_delta = sorted(ok_rows, key=lambda row: float(row.get("relative_delta") or 0.0), reverse=True)
    summary = {
        "old_tflite": str(args.old_tflite),
        "new_tflite": str(args.new_tflite),
        "tensor_delta": tensor_rows,
        "parent_head": parent_rows,
        "top_relative_delta": top_delta,
    }
    write_csv(args.output_dir / "tflite_tensor_weight_delta.csv", tensor_rows)
    write_csv(args.output_dir / "tflite_parent_head_delta.csv", parent_rows)
    (args.output_dir / "tflite_weight_analysis_summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps({"top_relative_delta": top_delta, "parent_head": parent_rows}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
