import argparse
import csv
import json
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf

from evaluate_v8_embedding_prototypes import metric_summary, parse_csv, write_csv
from train_v8_end_to_end_embedding import build_embedding_model, build_view_dataset, load_dynamic_qpair_teacher, parse_filters


def class_weights(y_parent: np.ndarray) -> np.ndarray:
    counts = np.bincount(y_parent.astype(np.int64), minlength=3).astype(np.float32)
    weights = np.mean(counts[counts > 0]) / np.maximum(counts, 1.0)
    return weights.astype(np.float32)


def per_sample_weights(y_parent: np.ndarray, mode: str) -> np.ndarray:
    if mode == "none":
        return np.ones_like(y_parent, dtype=np.float32)
    if mode == "parent_balanced":
        weights = class_weights(y_parent)
        return weights[y_parent].astype(np.float32)
    raise ValueError(f"unknown sample weight mode: {mode}")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def off_diagonal(matrix: tf.Tensor) -> tf.Tensor:
    size = tf.shape(matrix)[0]
    return tf.reshape(matrix - tf.linalg.diag(tf.linalg.diag_part(matrix)), (size * size,))


def stress_from_params(path: Path) -> list[str]:
    with np.load(path, allow_pickle=True) as data:
        view_labels = np.asarray(data["view_labels"]).astype(str)
    views: list[str] = []
    for view in view_labels.tolist():
        if view not in views:
            views.append(str(view))
    return [view for view in views if view != "clean"]


def load_prototype_teacher(
    path: Path | None,
    *,
    sample_count: int,
    code_dim: int,
    low_margin_threshold: int,
    low_margin_weight: float,
) -> dict[str, np.ndarray] | None:
    if path is None:
        return None
    with np.load(path, allow_pickle=True) as data:
        prototypes = np.asarray(data["prototypes_int8"], dtype=np.float32)
        prototype_parent = np.asarray(data["prototype_parent"], dtype=np.int64)
        embedding_int8 = np.asarray(data["embedding_int8"], dtype=np.float32) if "embedding_int8" in data.files else None
        margins = np.asarray(data["int8_margin"], dtype=np.int64) if "int8_margin" in data.files else None
    if prototypes.ndim != 2 or prototypes.shape[1] != code_dim:
        raise ValueError(f"expected {code_dim}D int8 prototypes, got {prototypes.shape}")
    if prototype_parent.shape[0] != prototypes.shape[0]:
        raise ValueError("prototype_parent length mismatch")
    weights = np.ones(sample_count, dtype=np.float32)
    if margins is not None:
        if len(margins) != sample_count:
            raise ValueError(f"teacher margin length {len(margins)} does not match dataset length {sample_count}")
        weights[margins <= low_margin_threshold] += float(low_margin_weight)
    if embedding_int8 is not None and embedding_int8.shape != (sample_count, prototypes.shape[1]):
        raise ValueError(f"teacher embedding_int8 shape {embedding_int8.shape} does not match {(sample_count, prototypes.shape[1])}")
    return {
        "prototypes": prototypes.astype(np.float32),
        "prototype_parent": prototype_parent.astype(np.int64),
        "embedding_int8": embedding_int8.astype(np.float32)
        if embedding_int8 is not None
        else np.zeros((sample_count, prototypes.shape[1]), dtype=np.float32),
        "has_embedding_int8": np.asarray(embedding_int8 is not None),
        "weights": weights.astype(np.float32),
        "margins": margins.astype(np.int64) if margins is not None else np.full(sample_count, -1, dtype=np.int64),
    }


def load_qpair_teacher(
    path: Path | None,
    flat: dict[str, Any],
    *,
    code_dim: int,
) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    teacher = {
        "correct": np.zeros((row_count, code_dim), dtype=np.float32),
        "wrong": np.zeros((row_count, code_dim), dtype=np.float32),
        "weights": np.zeros(row_count, dtype=np.float32),
    }
    if path is None:
        return teacher
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "correct_proto_int8", "wrong_proto_int8", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing qpair teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        correct = np.asarray(data["correct_proto_int8"], dtype=np.float32)
        wrong = np.asarray(data["wrong_proto_int8"], dtype=np.float32)
        weights = np.asarray(data["weight"], dtype=np.float32)
    if correct.ndim != 2 or correct.shape[1] != code_dim:
        raise ValueError(f"qpair correct_proto_int8 shape {correct.shape} does not match code_dim={code_dim}")
    if wrong.shape != correct.shape:
        raise ValueError(f"qpair wrong_proto_int8 shape {wrong.shape} does not match {correct.shape}")
    if len(weights) != len(correct) or len(teacher_sample) != len(correct) or len(teacher_view) != len(correct):
        raise ValueError("qpair teacher array lengths do not match")
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        teacher["correct"][row_index] = correct[index]
        teacher["wrong"][row_index] = wrong[index]
        teacher["weights"][row_index] = weights[index]
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"qpair teacher rows missing from training flat: {len(missing_keys)}, first {preview}")
    return teacher


def load_qanchor_teacher(
    path: Path | None,
    flat: dict[str, Any],
    *,
    code_dim: int,
) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    teacher = {
        "target": np.zeros((row_count, code_dim), dtype=np.float32),
        "weights": np.zeros(row_count, dtype=np.float32),
        "source_block_start": np.asarray(0, dtype=np.int64),
        "source_block_count": np.asarray(0, dtype=np.int64),
        "source_block_dim": np.asarray(0, dtype=np.int64),
    }
    if path is None:
        return teacher
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "embedding_int8"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing qanchor teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        target = np.asarray(data["embedding_int8"], dtype=np.float32)
        weights = np.asarray(data["qanchor_weight"], dtype=np.float32) if "qanchor_weight" in data.files else None
        source_block_start = int(np.asarray(data["base_embedding_dim"]).item()) if "base_embedding_dim" in data.files else 0
        source_block_count = int(np.asarray(data["source_block_count"]).item()) if "source_block_count" in data.files else 0
        source_block_dim_total = int(np.asarray(data["source_block_dim"]).item()) if "source_block_dim" in data.files else 0
    if target.ndim != 2 or target.shape[1] != code_dim:
        raise ValueError(f"qanchor embedding_int8 shape {target.shape} does not match code_dim={code_dim}")
    if len(teacher_sample) != len(target) or len(teacher_view) != len(target):
        raise ValueError("qanchor teacher array lengths do not match")
    if weights is None:
        weights = np.ones(len(target), dtype=np.float32)
    if len(weights) != len(target):
        raise ValueError("qanchor weight length does not match embedding_int8")
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        teacher["target"][row_index] = target[index]
        teacher["weights"][row_index] = weights[index]
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"qanchor teacher rows missing from training flat: {len(missing_keys)}, first {preview}")
    if source_block_count > 0:
        if source_block_start < 0 or source_block_start >= code_dim:
            raise ValueError(f"invalid source block start {source_block_start} for code_dim={code_dim}")
        if source_block_dim_total <= 0 or source_block_dim_total % source_block_count != 0:
            raise ValueError(
                f"invalid source block dim total/count: {source_block_dim_total}/{source_block_count}"
            )
        if source_block_start + source_block_dim_total > code_dim:
            raise ValueError(
                f"source block dims [{source_block_start}, {source_block_start + source_block_dim_total}) "
                f"do not fit code_dim={code_dim}"
            )
        teacher["source_block_start"] = np.asarray(source_block_start, dtype=np.int64)
        teacher["source_block_count"] = np.asarray(source_block_count, dtype=np.int64)
        teacher["source_block_dim"] = np.asarray(source_block_dim_total // source_block_count, dtype=np.int64)
    return teacher


def load_logit_teacher(path: Path | None, flat: dict[str, Any]) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    teacher = {
        "target_probs": np.zeros((row_count, 3), dtype=np.float32),
        "weights": np.zeros(row_count, dtype=np.float32),
    }
    if path is None:
        return teacher
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "target_probs", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing logit teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        target_probs = np.asarray(data["target_probs"], dtype=np.float32)
        weights = np.asarray(data["weight"], dtype=np.float32)
    if target_probs.ndim != 2 or target_probs.shape[1] != 3:
        raise ValueError(f"logit teacher target_probs shape {target_probs.shape} must be Nx3")
    if len(teacher_sample) != len(target_probs) or len(teacher_view) != len(target_probs) or len(weights) != len(target_probs):
        raise ValueError("logit teacher array lengths do not match")
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        teacher["target_probs"][row_index] = target_probs[index]
        teacher["weights"][row_index] = weights[index]
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"logit teacher rows missing from training flat: {len(missing_keys)}, first {preview}")
    return teacher


def load_source_decision_teacher(path: Path | None, flat: dict[str, Any]) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    teacher = {
        "wrong_parent": np.zeros(row_count, dtype=np.int64),
        "target_margin": np.zeros(row_count, dtype=np.float32),
        "weights": np.zeros(row_count, dtype=np.float32),
    }
    if path is None:
        return teacher
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent", "wrong_parent", "target_margin", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing source decision teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_parent = np.asarray(data["parent"], dtype=np.int64)
        wrong_parent = np.asarray(data["wrong_parent"], dtype=np.int64)
        target_margin = np.asarray(data["target_margin"], dtype=np.float32)
        weights = np.asarray(data["weight"], dtype=np.float32)
    if len(teacher_sample) != len(wrong_parent) or len(teacher_view) != len(wrong_parent):
        raise ValueError("source decision teacher array lengths do not match")
    if len(teacher_parent) != len(wrong_parent) or len(target_margin) != len(wrong_parent) or len(weights) != len(wrong_parent):
        raise ValueError("source decision teacher value lengths do not match")
    missing_keys: list[tuple[int, str]] = []
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        if int(teacher_parent[index]) != int(y_parent[row_index]):
            raise ValueError(
                f"source decision teacher parent mismatch at {sample}:{view}: "
                f"{teacher_parent[index]} != {y_parent[row_index]}"
            )
        teacher["wrong_parent"][row_index] = int(wrong_parent[index])
        teacher["target_margin"][row_index] = float(target_margin[index])
        teacher["weights"][row_index] = float(weights[index])
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"source decision teacher rows missing from training flat: {len(missing_keys)}, first {preview}")
    return teacher


def load_source_gate_teacher(path: Path | None, flat: dict[str, Any], *, code_dim: int, gate_start: int) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    empty = {
        "target_probs": np.zeros((row_count, 0), dtype=np.float32),
        "weights": np.zeros(row_count, dtype=np.float32),
        "source_label": np.zeros(row_count, dtype=np.int64),
        "score_matrix": np.zeros((row_count, 0), dtype=np.float32),
        "source_names": np.asarray([], dtype=str),
    }
    if path is None:
        return empty
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent", "target_probs", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing source gate teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_parent = np.asarray(data["parent"], dtype=np.int64)
        target_probs = np.asarray(data["target_probs"], dtype=np.float32)
        weights = np.asarray(data["weight"], dtype=np.float32)
        source_label = (
            np.asarray(data["source_label"], dtype=np.int64)
            if "source_label" in data.files
            else np.argmax(target_probs, axis=1).astype(np.int64)
        )
        score_matrix = (
            np.asarray(data["score_matrix"], dtype=np.float32)
            if "score_matrix" in data.files
            else np.zeros_like(target_probs, dtype=np.float32)
        )
        source_names = np.asarray(data["source_names"]).astype(str) if "source_names" in data.files else np.asarray([], dtype=str)
    if target_probs.ndim != 2:
        raise ValueError(f"source gate target_probs must be 2D, got {target_probs.shape}")
    gate_dim = int(target_probs.shape[1])
    if gate_dim <= 1:
        raise ValueError(f"source gate teacher must have at least two source columns, got {gate_dim}")
    if gate_start < 0 or gate_start + gate_dim > code_dim:
        raise ValueError(f"source gate dims [{gate_start}, {gate_start + gate_dim}) do not fit code_dim={code_dim}")
    if score_matrix.shape != target_probs.shape:
        raise ValueError(f"source gate score_matrix shape {score_matrix.shape} must match target_probs {target_probs.shape}")
    lengths = {
        len(teacher_sample),
        len(teacher_view),
        len(teacher_parent),
        len(target_probs),
        len(weights),
        len(source_label),
        len(score_matrix),
    }
    if len(lengths) != 1:
        raise ValueError("source gate teacher array lengths do not match")
    target_rows = np.zeros((row_count, gate_dim), dtype=np.float32)
    score_rows = np.zeros((row_count, gate_dim), dtype=np.float32)
    weight_rows = np.zeros(row_count, dtype=np.float32)
    label_rows = np.zeros(row_count, dtype=np.int64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        if int(teacher_parent[index]) != int(y_parent[row_index]):
            raise ValueError(
                f"source gate teacher parent mismatch at {sample}:{view}: "
                f"{teacher_parent[index]} != {y_parent[row_index]}"
            )
        probs = target_probs[index].astype(np.float32)
        prob_sum = float(np.sum(probs))
        if not np.isfinite(prob_sum) or prob_sum <= 0.0:
            raise ValueError(f"invalid source gate target probabilities at row {index}")
        target_rows[row_index] = probs / prob_sum
        score_rows[row_index] = score_matrix[index].astype(np.float32)
        weight_rows[row_index] = float(weights[index])
        label_rows[row_index] = int(source_label[index])
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"source gate teacher rows missing from training flat: {len(missing_keys)}, first {preview}")
    return {
        "target_probs": target_rows.astype(np.float32),
        "weights": weight_rows.astype(np.float32),
        "source_label": label_rows.astype(np.int64),
        "score_matrix": score_rows.astype(np.float32),
        "source_names": source_names.astype(str),
    }


def weighted_mean(values: tf.Tensor, weights: tf.Tensor) -> tf.Tensor:
    return tf.reduce_sum(values * weights) / tf.maximum(tf.reduce_sum(weights), 1.0)


def teacher_margin_numpy(
    logits: np.ndarray,
    y_parent: np.ndarray,
    teacher: dict[str, np.ndarray] | None,
    *,
    output_scale: float,
    output_zero: int,
) -> dict[str, Any]:
    if teacher is None:
        return {}
    q = np.clip(np.rint(logits / float(output_scale) + int(output_zero)), -128, 127).astype(np.int32)
    proto = np.asarray(teacher["prototypes"], dtype=np.int32)
    proto_parent = np.asarray(teacher["prototype_parent"], dtype=np.int64)
    dist = np.sum((q[:, None, :] - proto[None, :, :]) ** 2, axis=2)
    by_class: list[np.ndarray] = []
    for parent in range(3):
        by_class.append(np.min(dist[:, proto_parent == parent], axis=1))
    class_dist = np.stack(by_class, axis=1).astype(np.int64)
    pred = np.argmin(class_dist, axis=1).astype(np.int64)
    sorted_dist = np.sort(class_dist, axis=1)
    margin = sorted_dist[:, 1] - sorted_dist[:, 0]
    return {
        "teacher_q_pred_accuracy": float(np.mean(pred == y_parent)),
        "teacher_margin_min": int(np.min(margin)),
        "teacher_margin_mean": float(np.mean(margin)),
        "teacher_margin_le_1": int(np.sum(margin <= 1)),
        "teacher_margin_le_2": int(np.sum(margin <= 2)),
        "teacher_margin_le_4": int(np.sum(margin <= 4)),
        "teacher_margin_le_8": int(np.sum(margin <= 8)),
        "teacher_margin_le_16": int(np.sum(margin <= 16)),
    }


def representative_dataset(samples: np.ndarray):
    def gen():
        for index in range(len(samples)):
            yield [samples[index : index + 1].astype(np.float32)]

    return gen


def predict_tflite(path: Path, images: np.ndarray) -> tuple[np.ndarray, list[str]]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_index = int(input_detail["index"])
    output_index = int(output_detail["index"])
    in_scale, in_zero = input_detail.get("quantization", (0.0, 0))
    out_scale, out_zero = output_detail.get("quantization", (0.0, 0))
    preds: list[int] = []
    for image in images:
        value = image[None, ...].astype(np.float32)
        if input_detail["dtype"] == np.int8:
            value = np.clip(np.rint(value / float(in_scale) + int(in_zero)), -128, 127).astype(np.int8)
        interpreter.set_tensor(input_index, value)
        interpreter.invoke()
        logits = interpreter.get_tensor(output_index)
        if output_detail["dtype"] == np.int8:
            logits = (logits.astype(np.float32) - int(out_zero)) * float(out_scale)
        preds.append(int(np.argmax(logits[0, :3])))
    op_names = [str(item["op_name"]) for item in interpreter._get_ops_details()]  # noqa: SLF001
    return np.asarray(preds, dtype=np.int64), sorted(set(op_names))


def export_tflite(model: tf.keras.Model, output_dir: Path, rep_samples: np.ndarray) -> dict[str, Any]:
    float_path = output_dir / "parent_float.tflite"
    int8_path = output_dir / "parent_int8.tflite"

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    float_path.write_bytes(converter.convert())

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset(rep_samples)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    int8_path.write_bytes(converter.convert())
    return {
        "float_tflite": str(float_path),
        "int8_tflite": str(int8_path),
        "float_tflite_bytes": int(float_path.stat().st_size),
        "int8_tflite_bytes": int(int8_path.stat().st_size),
    }


def set_weights_allow_partial_output(model: tf.keras.Model, init_model: tf.keras.Model) -> None:
    src_by_name = {layer.name: layer for layer in init_model.layers}
    for layer in model.layers:
        src = src_by_name.get(layer.name)
        if src is None:
            continue
        dst_weights = layer.get_weights()
        src_weights = src.get_weights()
        if len(dst_weights) != len(src_weights):
            continue
        new_weights: list[np.ndarray] = []
        changed = False
        compatible = True
        for dst, src_arr in zip(dst_weights, src_weights, strict=False):
            if dst.shape == src_arr.shape:
                new_weights.append(src_arr)
                changed = True
                continue
            if (
                dst.ndim == src_arr.ndim == 4
                and dst.shape[0] >= src_arr.shape[0]
                and dst.shape[1] >= src_arr.shape[1]
            ):
                copied = np.zeros_like(dst)
                h0 = (dst.shape[0] - src_arr.shape[0]) // 2
                w0 = (dst.shape[1] - src_arr.shape[1]) // 2
                in_ch = min(dst.shape[2], src_arr.shape[2])
                out_ch = min(dst.shape[3], src_arr.shape[3])
                copied[
                    h0 : h0 + src_arr.shape[0],
                    w0 : w0 + src_arr.shape[1],
                    :in_ch,
                    :out_ch,
                ] = src_arr[:, :, :in_ch, :out_ch]
                new_weights.append(copied)
                changed = True
                continue
            if dst.ndim == src_arr.ndim and dst.ndim > 0 and dst.shape[:-1] == src_arr.shape[:-1]:
                copied = dst.copy()
                width = min(dst.shape[-1], src_arr.shape[-1])
                copied[..., :width] = src_arr[..., :width]
                new_weights.append(copied)
                changed = True
                continue
            compatible = False
            break
        if compatible and changed:
            layer.set_weights(new_weights)


def train_parent_classifier(
    *,
    model: tf.keras.Model,
    flat: dict[str, Any],
    images: np.ndarray,
    output_dir: Path,
    epochs: int,
    learning_rate: float,
    sample_weight_mode: str,
    log_every: int,
    prototype_teacher: dict[str, np.ndarray] | None,
    prototype_margin_weight: float,
    prototype_margin_target: float,
    prototype_margin_alpha: float,
    prototype_code_anchor_weight: float,
    prototype_output_scale: float,
    prototype_output_zero: int,
    qpair_teacher: dict[str, np.ndarray],
    qpair_margin_weight: float,
    qpair_margin_target: float,
    qpair_margin_alpha: float,
    qpair_start_epoch: int,
    dynamic_qpair_teacher: dict[str, np.ndarray],
    dynamic_qpair_margin_weight: float,
    dynamic_qpair_margin_target: float,
    dynamic_qpair_margin_alpha: float,
    dynamic_qpair_axis_weight: float,
    dynamic_qpair_axis_target: float,
    dynamic_qpair_axis_alpha: float,
    dynamic_qpair_start_epoch: int,
    qanchor_teacher: dict[str, np.ndarray],
    qanchor_weight: float,
    qanchor_start_epoch: int,
    source_block_margin_weight: float,
    source_block_margin_target: float,
    source_block_margin_alpha: float,
    source_block_margin_start_epoch: int,
    logit_teacher: dict[str, np.ndarray],
    logit_teacher_weight: float,
    logit_teacher_start_epoch: int,
    source_decision_teacher: dict[str, np.ndarray],
    source_decision_margin_weight: float,
    source_decision_margin_alpha: float,
    source_decision_start_epoch: int,
    source_decision_center_weight: float,
    source_decision_center_target: float,
    source_decision_center_alpha: float,
    source_decision_center_start_epoch: int,
    source_gate_teacher: dict[str, np.ndarray],
    source_gate_weight: float,
    source_gate_margin_weight: float,
    source_gate_margin_target: float,
    source_gate_margin_alpha: float,
    source_gate_balance_weight: float,
    source_gate_rank_weight: float,
    source_gate_rank_alpha: float,
    source_gate_rank_score_scale: float,
    source_gate_rank_max_target: float,
    source_gate_rank_min_gap: float,
    source_gate_center_weight: float,
    source_gate_center_target: float,
    source_gate_center_alpha: float,
    source_cluster_weight: float,
    source_cluster_target: float,
    source_cluster_alpha: float,
    source_cluster_start_epoch: int,
    source_gate_start: int,
    source_gate_start_epoch: int,
    orbit_consistency_weight: float,
    orbit_consistency_start_epoch: int,
    vicreg_var_weight: float,
    vicreg_cov_weight: float,
    vicreg_variance_floor: float,
    vicreg_start_epoch: int,
    code_dim: int,
) -> list[dict[str, Any]]:
    optimizer = tf.keras.optimizers.Adam(learning_rate)
    if hasattr(optimizer, "build"):
        optimizer.build(model.trainable_variables)
    ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True, reduction="none")
    x_all = tf.constant(images, dtype=tf.float32)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_tf = tf.constant(y_parent, dtype=tf.int32)
    w_tf = tf.constant(per_sample_weights(y_parent, sample_weight_mode), dtype=tf.float32)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    sample_index_tf = tf.constant(sample_index, dtype=tf.int32)
    sample_count = int(np.max(sample_index)) + 1
    if prototype_teacher is not None and (prototype_margin_weight > 0 or prototype_code_anchor_weight > 0):
        proto_tf = tf.constant(np.asarray(prototype_teacher["prototypes"], dtype=np.float32), dtype=tf.float32)
        proto_parent_tf = tf.constant(np.asarray(prototype_teacher["prototype_parent"], dtype=np.int32), dtype=tf.int32)
        proto_code_tf = tf.constant(np.asarray(prototype_teacher["embedding_int8"], dtype=np.float32), dtype=tf.float32)
        proto_w_tf = tf.constant(np.asarray(prototype_teacher["weights"], dtype=np.float32), dtype=tf.float32)
        has_code_anchor = bool(np.asarray(prototype_teacher["has_embedding_int8"]).item())
    else:
        proto_tf = tf.constant(np.zeros((1, code_dim), dtype=np.float32), dtype=tf.float32)
        proto_parent_tf = tf.constant(np.zeros((1,), dtype=np.int32), dtype=tf.int32)
        proto_code_tf = tf.constant(np.zeros((len(images), code_dim), dtype=np.float32), dtype=tf.float32)
        proto_w_tf = tf.constant(np.ones_like(y_parent, dtype=np.float32), dtype=tf.float32)
        has_code_anchor = False
    has_qpair_margin = qpair_margin_weight > 0.0 and bool(np.any(np.asarray(qpair_teacher["weights"]) > 0.0))
    if has_qpair_margin:
        qpair_correct_tf = tf.constant(np.asarray(qpair_teacher["correct"], dtype=np.float32), dtype=tf.float32)
        qpair_wrong_tf = tf.constant(np.asarray(qpair_teacher["wrong"], dtype=np.float32), dtype=tf.float32)
        qpair_weights_tf = tf.constant(np.asarray(qpair_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    else:
        qpair_correct_tf = tf.constant(np.zeros((len(images), code_dim), dtype=np.float32), dtype=tf.float32)
        qpair_wrong_tf = tf.constant(np.zeros((len(images), code_dim), dtype=np.float32), dtype=tf.float32)
        qpair_weights_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
    has_dynamic_qpair_margin = dynamic_qpair_margin_weight > 0.0 and bool(
        np.any(np.asarray(dynamic_qpair_teacher["weights"]) > 0.0)
    )
    dynamic_query_tf = tf.constant(np.asarray(dynamic_qpair_teacher["query"], dtype=np.int32), dtype=tf.int32)
    dynamic_correct_tf = tf.constant(np.asarray(dynamic_qpair_teacher["correct"], dtype=np.int32), dtype=tf.int32)
    dynamic_wrong_tf = tf.constant(np.asarray(dynamic_qpair_teacher["wrong"], dtype=np.int32), dtype=tf.int32)
    dynamic_weights_tf = tf.constant(np.asarray(dynamic_qpair_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    has_qanchor = qanchor_weight > 0.0 and bool(np.any(np.asarray(qanchor_teacher["weights"]) > 0.0))
    qanchor_source_block_start = int(np.asarray(qanchor_teacher["source_block_start"]).item())
    qanchor_source_block_count = int(np.asarray(qanchor_teacher["source_block_count"]).item())
    qanchor_source_block_dim = int(np.asarray(qanchor_teacher["source_block_dim"]).item())
    has_source_block_margin = (
        source_block_margin_weight > 0.0
        and qanchor_source_block_count > 0
        and qanchor_source_block_dim == 3
        and bool(np.any(np.asarray(qanchor_teacher["weights"]) > 0.0))
    )
    if source_block_margin_weight > 0.0 and qanchor_source_block_count > 0 and qanchor_source_block_dim != 3:
        raise ValueError(f"source block margin expects 3-class blocks, got dim={qanchor_source_block_dim}")
    if has_qanchor:
        qanchor_target_tf = tf.constant(np.asarray(qanchor_teacher["target"], dtype=np.float32), dtype=tf.float32)
        qanchor_weights_tf = tf.constant(np.asarray(qanchor_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    else:
        qanchor_target_tf = tf.constant(np.zeros((len(images), code_dim), dtype=np.float32), dtype=tf.float32)
        qanchor_weights_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
    has_logit_teacher = logit_teacher_weight > 0.0 and bool(np.any(np.asarray(logit_teacher["weights"]) > 0.0))
    if has_logit_teacher:
        logit_target_tf = tf.constant(np.asarray(logit_teacher["target_probs"], dtype=np.float32), dtype=tf.float32)
        logit_weights_tf = tf.constant(np.asarray(logit_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    else:
        logit_target_tf = tf.constant(np.zeros((len(images), 3), dtype=np.float32), dtype=tf.float32)
        logit_weights_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
    has_source_decision = (source_decision_margin_weight > 0.0 or source_decision_center_weight > 0.0) and bool(
        np.any(np.asarray(source_decision_teacher["weights"]) > 0.0)
    )
    if has_source_decision:
        source_decision_wrong_tf = tf.constant(np.asarray(source_decision_teacher["wrong_parent"], dtype=np.int32), dtype=tf.int32)
        source_decision_target_tf = tf.constant(
            np.asarray(source_decision_teacher["target_margin"], dtype=np.float32),
            dtype=tf.float32,
        )
        source_decision_weights_tf = tf.constant(np.asarray(source_decision_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    else:
        source_decision_wrong_tf = tf.constant(np.zeros(len(images), dtype=np.int32), dtype=tf.int32)
        source_decision_target_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
        source_decision_weights_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
    source_gate_target_np = np.asarray(source_gate_teacher["target_probs"], dtype=np.float32)
    source_gate_dim = int(source_gate_target_np.shape[1]) if source_gate_target_np.ndim == 2 else 0
    has_source_gate = (
        (
            source_gate_weight > 0.0
            or source_gate_margin_weight > 0.0
            or source_gate_balance_weight > 0.0
            or source_gate_rank_weight > 0.0
            or source_gate_center_weight > 0.0
            or source_cluster_weight > 0.0
        )
        and source_gate_dim > 1
        and bool(np.any(np.asarray(source_gate_teacher["weights"]) > 0.0))
    )
    if has_source_gate:
        if source_gate_start < 0 or source_gate_start + source_gate_dim > code_dim:
            raise ValueError(
                f"source gate dims [{source_gate_start}, {source_gate_start + source_gate_dim}) "
                f"do not fit code_dim={code_dim}"
            )
        source_gate_target_tf = tf.constant(source_gate_target_np, dtype=tf.float32)
        source_gate_weights_tf = tf.constant(np.asarray(source_gate_teacher["weights"], dtype=np.float32), dtype=tf.float32)
        source_gate_label_tf = tf.constant(np.asarray(source_gate_teacher["source_label"], dtype=np.int32), dtype=tf.int32)
        source_gate_score_tf = tf.constant(np.asarray(source_gate_teacher["score_matrix"], dtype=np.float32), dtype=tf.float32)
        source_gate_weight_sum = np.maximum(np.sum(np.asarray(source_gate_teacher["weights"], dtype=np.float32)), 1.0)
        source_gate_prior = (
            np.sum(source_gate_target_np * np.asarray(source_gate_teacher["weights"], dtype=np.float32)[:, None], axis=0)
            / source_gate_weight_sum
        ).astype(np.float32)
        source_gate_prior_tf = tf.constant(source_gate_prior, dtype=tf.float32)
    else:
        source_gate_target_tf = tf.constant(np.zeros((len(images), 1), dtype=np.float32), dtype=tf.float32)
        source_gate_weights_tf = tf.constant(np.zeros(len(images), dtype=np.float32), dtype=tf.float32)
        source_gate_label_tf = tf.constant(np.zeros(len(images), dtype=np.int32), dtype=tf.int32)
        source_gate_score_tf = tf.constant(np.zeros((len(images), 1), dtype=np.float32), dtype=tf.float32)
        source_gate_prior_tf = tf.constant(np.ones(1, dtype=np.float32), dtype=tf.float32)
    train_rows: list[dict[str, Any]] = []

    @tf.function(reduce_retracing=True)
    def train_step(
        qpair_weight: tf.Tensor,
        dynamic_qpair_weight: tf.Tensor,
        qanchor_step_weight: tf.Tensor,
        logit_teacher_step_weight: tf.Tensor,
        source_decision_step_weight: tf.Tensor,
        source_decision_center_step_weight: tf.Tensor,
        source_gate_step_weight: tf.Tensor,
        source_cluster_step_weight: tf.Tensor,
        source_block_margin_step_weight: tf.Tensor,
        orbit_step_weight: tf.Tensor,
        vicreg_step_weight: tf.Tensor,
    ) -> tuple[tf.Tensor, ...]:
        with tf.GradientTape() as tape:
            code = model(x_all, training=True)
            parent_logits = code[:, :3]
            losses = ce(y_tf, parent_logits)
            ce_loss = tf.reduce_sum(losses * w_tf) / tf.reduce_sum(w_tf)
            proto_loss = tf.constant(0.0, dtype=tf.float32)
            proto_margin_mean = tf.constant(0.0, dtype=tf.float32)
            code_anchor_loss = tf.constant(0.0, dtype=tf.float32)
            qpair_loss = tf.constant(0.0, dtype=tf.float32)
            qpair_margin_mean = tf.constant(0.0, dtype=tf.float32)
            dynamic_qpair_loss = tf.constant(0.0, dtype=tf.float32)
            dynamic_qpair_margin_mean = tf.constant(0.0, dtype=tf.float32)
            dynamic_qpair_axis_loss = tf.constant(0.0, dtype=tf.float32)
            dynamic_qpair_axis_margin_mean = tf.constant(0.0, dtype=tf.float32)
            qanchor_loss = tf.constant(0.0, dtype=tf.float32)
            logit_teacher_loss = tf.constant(0.0, dtype=tf.float32)
            source_decision_loss = tf.constant(0.0, dtype=tf.float32)
            source_decision_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_decision_center_loss = tf.constant(0.0, dtype=tf.float32)
            source_decision_center_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_gate_loss = tf.constant(0.0, dtype=tf.float32)
            source_gate_margin_loss = tf.constant(0.0, dtype=tf.float32)
            source_gate_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_gate_balance_loss = tf.constant(0.0, dtype=tf.float32)
            source_gate_rank_loss = tf.constant(0.0, dtype=tf.float32)
            source_gate_rank_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_gate_center_loss = tf.constant(0.0, dtype=tf.float32)
            source_gate_center_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_cluster_loss = tf.constant(0.0, dtype=tf.float32)
            source_cluster_margin_mean = tf.constant(0.0, dtype=tf.float32)
            source_block_margin_loss = tf.constant(0.0, dtype=tf.float32)
            source_block_margin_mean = tf.constant(0.0, dtype=tf.float32)
            orbit_loss = tf.constant(0.0, dtype=tf.float32)
            vicreg_var_loss = tf.constant(0.0, dtype=tf.float32)
            vicreg_cov_loss = tf.constant(0.0, dtype=tf.float32)
            if prototype_margin_weight > 0 and prototype_teacher is not None:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_logits = clipped + tf.stop_gradient(rounded - clipped)
                dist = tf.reduce_sum(tf.square(q_logits[:, None, :] - proto_tf[None, :, :]), axis=2)
                class_dist_rows = []
                for parent in range(3):
                    masked = tf.boolean_mask(dist, proto_parent_tf == parent, axis=1)
                    class_dist_rows.append(tf.reduce_min(masked, axis=1))
                class_dist = tf.stack(class_dist_rows, axis=1)
                pos = tf.gather(class_dist, y_tf, batch_dims=1)
                neg = tf.reduce_min(class_dist + tf.one_hot(y_tf, 3, on_value=1.0e9, off_value=0.0), axis=1)
                proto_margin = neg - pos
                raw_proto_loss = tf.nn.softplus(float(prototype_margin_alpha) * (float(prototype_margin_target) - proto_margin))
                proto_loss = tf.reduce_sum(raw_proto_loss * proto_w_tf) / tf.reduce_sum(proto_w_tf)
                proto_margin_mean = tf.reduce_mean(proto_margin)
            if prototype_code_anchor_weight > 0 and prototype_teacher is not None and has_code_anchor:
                raw_anchor = tf.reduce_mean(tf.square(code - proto_code_tf), axis=1)
                code_anchor_loss = tf.reduce_sum(raw_anchor * proto_w_tf) / tf.reduce_sum(proto_w_tf)
            if has_qpair_margin:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                qpair_correct_dist = tf.reduce_sum(tf.square(q_code - qpair_correct_tf), axis=1)
                qpair_wrong_dist = tf.reduce_sum(tf.square(q_code - qpair_wrong_tf), axis=1)
                qpair_margin = qpair_wrong_dist - qpair_correct_dist
                raw_qpair_loss = tf.nn.softplus(float(qpair_margin_alpha) * (float(qpair_margin_target) - qpair_margin))
                qpair_loss = weighted_mean(raw_qpair_loss, qpair_weights_tf)
                qpair_margin_mean = weighted_mean(qpair_margin, qpair_weights_tf)
            if has_dynamic_qpair_margin:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                dynamic_query = tf.gather(q_code, dynamic_query_tf)
                dynamic_correct = tf.gather(q_code, dynamic_correct_tf)
                dynamic_wrong = tf.gather(q_code, dynamic_wrong_tf)
                dynamic_correct_dist = tf.reduce_sum(tf.square(dynamic_query - dynamic_correct), axis=1)
                dynamic_wrong_dist = tf.reduce_sum(tf.square(dynamic_query - dynamic_wrong), axis=1)
                dynamic_qpair_margin = dynamic_wrong_dist - dynamic_correct_dist
                raw_dynamic_qpair_loss = tf.nn.softplus(
                    float(dynamic_qpair_margin_alpha) * (float(dynamic_qpair_margin_target) - dynamic_qpair_margin)
                )
                dynamic_qpair_loss = weighted_mean(raw_dynamic_qpair_loss, dynamic_weights_tf)
                dynamic_qpair_margin_mean = weighted_mean(dynamic_qpair_margin, dynamic_weights_tf)
                if dynamic_qpair_axis_weight > 0.0:
                    axis_margin = tf.square(dynamic_query - dynamic_wrong) - tf.square(dynamic_query - dynamic_correct)
                    raw_axis = tf.nn.softplus(
                        float(dynamic_qpair_axis_alpha) * (float(dynamic_qpair_axis_target) - axis_margin)
                    )
                    dynamic_qpair_axis_row = tf.reduce_mean(raw_axis, axis=1)
                    dynamic_qpair_axis_loss = weighted_mean(dynamic_qpair_axis_row, dynamic_weights_tf)
                    dynamic_qpair_axis_margin_mean = weighted_mean(tf.reduce_mean(axis_margin, axis=1), dynamic_weights_tf)
            if has_qanchor:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                raw_qanchor = tf.reduce_mean(tf.square(q_code - qanchor_target_tf), axis=1)
                qanchor_loss = weighted_mean(raw_qanchor, qanchor_weights_tf)
            if has_logit_teacher:
                raw_logit_teacher = tf.keras.losses.categorical_crossentropy(
                    logit_target_tf,
                    parent_logits,
                    from_logits=True,
                )
                logit_teacher_loss = weighted_mean(raw_logit_teacher, logit_weights_tf)
            if has_source_decision:
                scaled = code[:, :3] / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_parent_logits = clipped + tf.stop_gradient(rounded - clipped)
                parent_score = tf.gather(q_parent_logits, y_tf, batch_dims=1)
                wrong_score = tf.gather(q_parent_logits, source_decision_wrong_tf, batch_dims=1)
                source_decision_margin = parent_score - wrong_score
                raw_source_decision = tf.nn.softplus(
                    float(source_decision_margin_alpha) * (source_decision_target_tf - source_decision_margin)
                )
                source_decision_loss = weighted_mean(raw_source_decision, source_decision_weights_tf)
                source_decision_margin_mean = weighted_mean(source_decision_margin, source_decision_weights_tf)
                scaled_code = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped_code = tf.clip_by_value(scaled_code, -128.0, 127.0)
                rounded_code = tf.round(clipped_code)
                q_code_for_center = clipped_code + tf.stop_gradient(rounded_code - clipped_code)
                parent_centers = tf.math.unsorted_segment_mean(q_code_for_center, y_tf, 3)
                true_center = tf.gather(parent_centers, y_tf)
                wrong_center = tf.gather(parent_centers, source_decision_wrong_tf)
                true_center_dist = tf.reduce_sum(tf.square(q_code_for_center - true_center), axis=1)
                wrong_center_dist = tf.reduce_sum(tf.square(q_code_for_center - wrong_center), axis=1)
                source_decision_center_margin = wrong_center_dist - true_center_dist
                raw_source_decision_center = tf.nn.softplus(
                    float(source_decision_center_alpha)
                    * (float(source_decision_center_target) - source_decision_center_margin)
                )
                source_decision_center_loss = weighted_mean(raw_source_decision_center, source_decision_weights_tf)
                source_decision_center_margin_mean = weighted_mean(
                    source_decision_center_margin,
                    source_decision_weights_tf,
                )
            if has_source_gate:
                gate_logits = code[:, source_gate_start : source_gate_start + source_gate_dim]
                q_gate = tf.constant(0.0, dtype=tf.float32)
                if (
                    source_gate_margin_weight > 0.0
                    or source_gate_rank_weight > 0.0
                    or source_gate_center_weight > 0.0
                ):
                    scaled = gate_logits / float(prototype_output_scale) + float(prototype_output_zero)
                    clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                    rounded = tf.round(clipped)
                    q_gate = clipped + tf.stop_gradient(rounded - clipped)
                if source_gate_weight > 0.0:
                    raw_source_gate = tf.keras.losses.categorical_crossentropy(
                        source_gate_target_tf,
                        gate_logits,
                        from_logits=True,
                    )
                    source_gate_loss = weighted_mean(raw_source_gate, source_gate_weights_tf)
                if source_gate_margin_weight > 0.0:
                    label_score = tf.gather(q_gate, source_gate_label_tf, batch_dims=1)
                    other_score = tf.reduce_max(
                        q_gate + tf.one_hot(source_gate_label_tf, source_gate_dim, on_value=-1.0e9, off_value=0.0),
                        axis=1,
                    )
                    source_gate_margin = label_score - other_score
                    raw_source_gate_margin = tf.nn.softplus(
                        float(source_gate_margin_alpha) * (float(source_gate_margin_target) - source_gate_margin)
                    )
                    source_gate_margin_loss = weighted_mean(raw_source_gate_margin, source_gate_weights_tf)
                    source_gate_margin_mean = weighted_mean(source_gate_margin, source_gate_weights_tf)
                if source_gate_balance_weight > 0.0:
                    gate_probs = tf.nn.softmax(gate_logits, axis=1)
                    pred_prior = (
                        tf.reduce_sum(gate_probs * source_gate_weights_tf[:, None], axis=0)
                        / tf.maximum(tf.reduce_sum(source_gate_weights_tf), 1.0)
                    )
                    source_gate_balance_loss = tf.reduce_sum(tf.square(pred_prior - source_gate_prior_tf))
                if source_gate_rank_weight > 0.0:
                    score_diff = source_gate_score_tf[:, :, None] - source_gate_score_tf[:, None, :]
                    abs_diff = tf.abs(score_diff)
                    sign = tf.sign(score_diff)
                    gate_diff = q_gate[:, :, None] - q_gate[:, None, :]
                    rank_margin = sign * gate_diff
                    pair_active = abs_diff >= float(source_gate_rank_min_gap)
                    pair_active_f = tf.cast(pair_active, tf.float32)
                    target = tf.clip_by_value(
                        abs_diff / max(float(source_gate_rank_score_scale), 1.0e-6),
                        1.0,
                        float(source_gate_rank_max_target),
                    )
                    raw_rank = tf.nn.softplus(float(source_gate_rank_alpha) * (target - rank_margin))
                    pair_count = tf.maximum(tf.reduce_sum(pair_active_f, axis=(1, 2)), 1.0)
                    source_gate_rank_row = tf.reduce_sum(raw_rank * pair_active_f, axis=(1, 2)) / pair_count
                    source_gate_rank_loss = weighted_mean(source_gate_rank_row, source_gate_weights_tf)
                    source_gate_rank_margin_row = tf.reduce_sum(rank_margin * pair_active_f, axis=(1, 2)) / pair_count
                    source_gate_rank_margin_mean = weighted_mean(
                        source_gate_rank_margin_row,
                        source_gate_weights_tf,
                    )
                if source_gate_center_weight > 0.0:
                    weighted_gate = q_gate * source_gate_weights_tf[:, None]
                    center_sum = tf.math.unsorted_segment_sum(
                        weighted_gate,
                        source_gate_label_tf,
                        source_gate_dim,
                    )
                    center_count = tf.math.unsorted_segment_sum(
                        source_gate_weights_tf,
                        source_gate_label_tf,
                        source_gate_dim,
                    )
                    centers = center_sum / tf.maximum(center_count[:, None], 1.0e-6)
                    center_active = tf.cast(center_count > 0.0, tf.float32)
                    center_dist = tf.reduce_sum(tf.square(q_gate[:, None, :] - centers[None, :, :]), axis=2)
                    own_center_dist = tf.gather(center_dist, source_gate_label_tf, batch_dims=1)
                    other_center_dist = tf.reduce_min(
                        center_dist
                        + tf.one_hot(source_gate_label_tf, source_gate_dim, on_value=1.0e9, off_value=0.0)
                        + (1.0 - center_active)[None, :] * 1.0e9,
                        axis=1,
                    )
                    source_gate_center_margin = other_center_dist - own_center_dist
                    raw_source_gate_center = tf.nn.softplus(
                        float(source_gate_center_alpha)
                        * (float(source_gate_center_target) - source_gate_center_margin)
                    )
                    source_gate_center_loss = weighted_mean(raw_source_gate_center, source_gate_weights_tf)
                    source_gate_center_margin_mean = weighted_mean(
                        source_gate_center_margin,
                        source_gate_weights_tf,
                    )
            if source_cluster_weight > 0.0 and has_source_gate:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                cluster_count = 3 * int(source_gate_dim)
                cluster_label = y_tf * int(source_gate_dim) + source_gate_label_tf
                weighted_code = q_code * source_gate_weights_tf[:, None]
                cluster_sum = tf.math.unsorted_segment_sum(
                    weighted_code,
                    cluster_label,
                    cluster_count,
                )
                cluster_weight_sum = tf.math.unsorted_segment_sum(
                    source_gate_weights_tf,
                    cluster_label,
                    cluster_count,
                )
                cluster_centers = cluster_sum / tf.maximum(cluster_weight_sum[:, None], 1.0e-6)
                cluster_active = tf.cast(cluster_weight_sum > 0.0, tf.float32)
                cluster_dist = tf.reduce_sum(tf.square(q_code[:, None, :] - cluster_centers[None, :, :]), axis=2)
                own_cluster_dist = tf.gather(cluster_dist, cluster_label, batch_dims=1)
                same_parent_cluster = (
                    y_tf[:, None] * int(source_gate_dim)
                    + tf.range(int(source_gate_dim), dtype=tf.int32)[None, :]
                )
                same_parent_dist = tf.gather(cluster_dist, same_parent_cluster, batch_dims=1)
                same_parent_active = tf.gather(cluster_active, same_parent_cluster)
                other_same_parent_dist = tf.reduce_min(
                    same_parent_dist
                    + tf.one_hot(source_gate_label_tf, int(source_gate_dim), on_value=1.0e9, off_value=0.0)
                    + (1.0 - same_parent_active) * 1.0e9,
                    axis=1,
                )
                source_cluster_margin = other_same_parent_dist - own_cluster_dist
                raw_source_cluster = tf.nn.softplus(
                    float(source_cluster_alpha) * (float(source_cluster_target) - source_cluster_margin)
                )
                source_cluster_loss = weighted_mean(raw_source_cluster, source_gate_weights_tf)
                source_cluster_margin_mean = weighted_mean(source_cluster_margin, source_gate_weights_tf)
            if has_source_block_margin:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                block_values = q_code[
                    :,
                    qanchor_source_block_start : qanchor_source_block_start
                    + qanchor_source_block_count * qanchor_source_block_dim,
                ]
                block_values = tf.reshape(
                    block_values,
                    (-1, qanchor_source_block_count, qanchor_source_block_dim),
                )
                class_mask = tf.one_hot(y_tf, qanchor_source_block_dim, dtype=tf.float32)
                true_value = tf.reduce_sum(block_values * class_mask[:, None, :], axis=2)
                wrong_value = tf.reduce_min(block_values + class_mask[:, None, :] * 1.0e9, axis=2)
                source_block_margin = wrong_value - true_value
                raw_source_block_margin = tf.nn.softplus(
                    float(source_block_margin_alpha) * (float(source_block_margin_target) - source_block_margin)
                )
                source_block_row_loss = tf.reduce_mean(raw_source_block_margin, axis=1)
                source_block_margin_loss = weighted_mean(source_block_row_loss, qanchor_weights_tf)
                source_block_margin_mean = weighted_mean(tf.reduce_mean(source_block_margin, axis=1), qanchor_weights_tf)
            if orbit_consistency_weight > 0.0:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                centers = tf.math.unsorted_segment_mean(q_code, sample_index_tf, sample_count)
                centered = q_code - tf.gather(centers, sample_index_tf)
                orbit_loss = tf.reduce_mean(tf.reduce_sum(tf.square(centered), axis=1))
            if vicreg_var_weight > 0.0 or vicreg_cov_weight > 0.0:
                scaled = code / float(prototype_output_scale) + float(prototype_output_zero)
                clipped = tf.clip_by_value(scaled, -128.0, 127.0)
                rounded = tf.round(clipped)
                q_code = clipped + tf.stop_gradient(rounded - clipped)
                stddev = tf.sqrt(tf.math.reduce_variance(q_code, axis=0) + 1.0e-6)
                vicreg_var_loss = tf.reduce_mean(tf.square(tf.nn.relu(float(vicreg_variance_floor) - stddev)))
                centered = q_code - tf.reduce_mean(q_code, axis=0, keepdims=True)
                normalized = centered / tf.maximum(stddev[None, :], 1.0e-6)
                denom = tf.cast(tf.maximum(tf.shape(normalized)[0] - 1, 1), tf.float32)
                corr = tf.matmul(normalized, normalized, transpose_a=True) / denom
                vicreg_cov_loss = tf.reduce_sum(tf.square(off_diagonal(corr))) / tf.cast(code_dim, tf.float32)
            loss = (
                ce_loss
                + float(prototype_margin_weight) * proto_loss
                + float(prototype_code_anchor_weight) * code_anchor_loss
                + qpair_weight * float(qpair_margin_weight) * qpair_loss
                + dynamic_qpair_weight * float(dynamic_qpair_margin_weight) * dynamic_qpair_loss
                + dynamic_qpair_weight * float(dynamic_qpair_axis_weight) * dynamic_qpair_axis_loss
                + qanchor_step_weight * float(qanchor_weight) * qanchor_loss
                + logit_teacher_step_weight * float(logit_teacher_weight) * logit_teacher_loss
                + source_decision_step_weight * float(source_decision_margin_weight) * source_decision_loss
                + source_decision_center_step_weight
                * float(source_decision_center_weight)
                * source_decision_center_loss
                + source_gate_step_weight * float(source_gate_weight) * source_gate_loss
                + source_gate_step_weight * float(source_gate_margin_weight) * source_gate_margin_loss
                + source_gate_step_weight * float(source_gate_balance_weight) * source_gate_balance_loss
                + source_gate_step_weight * float(source_gate_rank_weight) * source_gate_rank_loss
                + source_gate_step_weight * float(source_gate_center_weight) * source_gate_center_loss
                + source_cluster_step_weight * float(source_cluster_weight) * source_cluster_loss
                + source_block_margin_step_weight * float(source_block_margin_weight) * source_block_margin_loss
                + orbit_step_weight * float(orbit_consistency_weight) * orbit_loss
                + vicreg_step_weight * float(vicreg_var_weight) * vicreg_var_loss
                + vicreg_step_weight * float(vicreg_cov_weight) * vicreg_cov_loss
            )
        grads = tape.gradient(loss, model.trainable_variables)
        optimizer.apply_gradients(zip(grads, model.trainable_variables))
        pred = tf.argmax(parent_logits, axis=1, output_type=tf.int32)
        acc = tf.reduce_mean(tf.cast(pred == y_tf, tf.float32))
        return (
            loss,
            acc,
            ce_loss,
            proto_loss,
            proto_margin_mean,
            code_anchor_loss,
            qpair_loss,
            qpair_margin_mean,
            dynamic_qpair_loss,
            dynamic_qpair_margin_mean,
            dynamic_qpair_axis_loss,
            dynamic_qpair_axis_margin_mean,
            qanchor_loss,
            logit_teacher_loss,
            source_decision_loss,
            source_decision_margin_mean,
            source_decision_center_loss,
            source_decision_center_margin_mean,
            source_gate_loss,
            source_gate_margin_loss,
            source_gate_margin_mean,
            source_gate_balance_loss,
            source_gate_rank_loss,
            source_gate_rank_margin_mean,
            source_gate_center_loss,
            source_gate_center_margin_mean,
            source_cluster_loss,
            source_cluster_margin_mean,
            source_block_margin_loss,
            source_block_margin_mean,
            orbit_loss,
            vicreg_var_loss,
            vicreg_cov_loss,
        )

    for epoch in range(1, epochs + 1):
        qpair_weight = tf.constant(1.0 if epoch >= qpair_start_epoch else 0.0, dtype=tf.float32)
        dynamic_qpair_weight = tf.constant(1.0 if epoch >= dynamic_qpair_start_epoch else 0.0, dtype=tf.float32)
        qanchor_step_weight = tf.constant(1.0 if epoch >= qanchor_start_epoch else 0.0, dtype=tf.float32)
        logit_teacher_step_weight = tf.constant(1.0 if epoch >= logit_teacher_start_epoch else 0.0, dtype=tf.float32)
        source_decision_step_weight = tf.constant(1.0 if epoch >= source_decision_start_epoch else 0.0, dtype=tf.float32)
        source_decision_center_step_weight = tf.constant(
            1.0 if epoch >= source_decision_center_start_epoch else 0.0,
            dtype=tf.float32,
        )
        source_gate_step_weight = tf.constant(1.0 if epoch >= source_gate_start_epoch else 0.0, dtype=tf.float32)
        source_cluster_step_weight = tf.constant(1.0 if epoch >= source_cluster_start_epoch else 0.0, dtype=tf.float32)
        source_block_margin_step_weight = tf.constant(1.0 if epoch >= source_block_margin_start_epoch else 0.0, dtype=tf.float32)
        orbit_step_weight = tf.constant(1.0 if epoch >= orbit_consistency_start_epoch else 0.0, dtype=tf.float32)
        vicreg_step_weight = tf.constant(1.0 if epoch >= vicreg_start_epoch else 0.0, dtype=tf.float32)
        (
            loss,
            acc,
            ce_loss,
            proto_loss,
            proto_margin_mean,
            code_anchor_loss,
            qpair_loss,
            qpair_margin_mean,
            dynamic_qpair_loss,
            dynamic_qpair_margin_mean,
            dynamic_qpair_axis_loss,
            dynamic_qpair_axis_margin_mean,
            qanchor_loss,
            logit_teacher_loss,
            source_decision_loss,
            source_decision_margin_mean,
            source_decision_center_loss,
            source_decision_center_margin_mean,
            source_gate_loss,
            source_gate_margin_loss,
            source_gate_margin_mean,
            source_gate_balance_loss,
            source_gate_rank_loss,
            source_gate_rank_margin_mean,
            source_gate_center_loss,
            source_gate_center_margin_mean,
            source_cluster_loss,
            source_cluster_margin_mean,
            source_block_margin_loss,
            source_block_margin_mean,
            orbit_loss,
            vicreg_var_loss,
            vicreg_cov_loss,
        ) = train_step(
            qpair_weight,
            dynamic_qpair_weight,
            qanchor_step_weight,
            logit_teacher_step_weight,
            source_decision_step_weight,
            source_decision_center_step_weight,
            source_gate_step_weight,
            source_cluster_step_weight,
            source_block_margin_step_weight,
            orbit_step_weight,
            vicreg_step_weight,
        )
        if epoch == 1 or epoch == epochs or epoch % log_every == 0:
            code = model.predict(images, batch_size=512, verbose=0)
            pred = np.argmax(code[:, :3], axis=1).astype(np.int64)
            row = {
                "epoch": int(epoch),
                "loss": float(loss.numpy()),
                "ce_loss": float(ce_loss.numpy()),
                "prototype_margin_loss": float(proto_loss.numpy()),
                "prototype_margin_mean_train": float(proto_margin_mean.numpy()),
                "prototype_code_anchor_loss": float(code_anchor_loss.numpy()),
                "qpair_margin_loss": float(qpair_loss.numpy()),
                "qpair_margin_mean": float(qpair_margin_mean.numpy()),
                "qpair_weight": float(qpair_weight.numpy()),
                "dynamic_qpair_margin_loss": float(dynamic_qpair_loss.numpy()),
                "dynamic_qpair_margin_mean": float(dynamic_qpair_margin_mean.numpy()),
                "dynamic_qpair_axis_loss": float(dynamic_qpair_axis_loss.numpy()),
                "dynamic_qpair_axis_margin_mean": float(dynamic_qpair_axis_margin_mean.numpy()),
                "dynamic_qpair_weight": float(dynamic_qpair_weight.numpy()),
                "qanchor_loss": float(qanchor_loss.numpy()),
                "qanchor_weight": float(qanchor_step_weight.numpy()),
                "logit_teacher_loss": float(logit_teacher_loss.numpy()),
                "logit_teacher_weight": float(logit_teacher_step_weight.numpy()),
                "source_decision_margin_loss": float(source_decision_loss.numpy()),
                "source_decision_margin_mean": float(source_decision_margin_mean.numpy()),
                "source_decision_weight": float(source_decision_step_weight.numpy()),
                "source_decision_center_loss": float(source_decision_center_loss.numpy()),
                "source_decision_center_margin_mean": float(source_decision_center_margin_mean.numpy()),
                "source_decision_center_weight": float(source_decision_center_step_weight.numpy()),
                "source_gate_loss": float(source_gate_loss.numpy()),
                "source_gate_margin_loss": float(source_gate_margin_loss.numpy()),
                "source_gate_margin_mean": float(source_gate_margin_mean.numpy()),
                "source_gate_balance_loss": float(source_gate_balance_loss.numpy()),
                "source_gate_rank_loss": float(source_gate_rank_loss.numpy()),
                "source_gate_rank_margin_mean": float(source_gate_rank_margin_mean.numpy()),
                "source_gate_center_loss": float(source_gate_center_loss.numpy()),
                "source_gate_center_margin_mean": float(source_gate_center_margin_mean.numpy()),
                "source_gate_weight": float(source_gate_step_weight.numpy()),
                "source_cluster_loss": float(source_cluster_loss.numpy()),
                "source_cluster_margin_mean": float(source_cluster_margin_mean.numpy()),
                "source_cluster_weight": float(source_cluster_step_weight.numpy()),
                "source_block_margin_loss": float(source_block_margin_loss.numpy()),
                "source_block_margin_mean": float(source_block_margin_mean.numpy()),
                "source_block_margin_weight": float(source_block_margin_step_weight.numpy()),
                "orbit_consistency_loss": float(orbit_loss.numpy()),
                "orbit_consistency_weight": float(orbit_step_weight.numpy()),
                "vicreg_var_loss": float(vicreg_var_loss.numpy()),
                "vicreg_cov_loss": float(vicreg_cov_loss.numpy()),
                "vicreg_weight": float(vicreg_step_weight.numpy()),
                "train_accuracy": float(acc.numpy()),
                "eval_accuracy": float(np.mean(pred == y_parent)),
            }
            row.update(
                teacher_margin_numpy(
                    code,
                    y_parent,
                    prototype_teacher,
                    output_scale=prototype_output_scale,
                    output_zero=prototype_output_zero,
                )
            )
            row.update(
                metric_summary(
                    view_order=list(flat["view_names"]),
                    view_labels=np.asarray(flat["view_labels"]).astype(str),
                    y_parent=y_parent,
                    pred=pred,
                )
            )
            train_rows.append(row)
            print(json.dumps({"parent_train": row}, ensure_ascii=False), flush=True)
    write_csv(output_dir / "training_log.csv", train_rows)
    return train_rows


def evaluate_model(model: tf.keras.Model, flat: dict[str, Any], images: np.ndarray, output_dir: Path) -> dict[str, Any]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    code = model.predict(images, batch_size=512, verbose=0)
    parent_logits = code[:, :3]
    pred = np.argmax(parent_logits, axis=1).astype(np.int64)
    row: dict[str, Any] = {
        "stage": "v8_parent_classifier",
        "prototype_count": 0,
        "estimated_distance_macs": 0,
        "code_dim": int(code.shape[1]),
        "float_margin_min": float(np.min(np.sort(parent_logits, axis=1)[:, -1] - np.sort(parent_logits, axis=1)[:, -2])),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    write_csv(output_dir / "candidate_results.csv", [row])
    return row


def main() -> None:
    parser = argparse.ArgumentParser(description="Train a V8 tiny parent-logit classifier without prototype distance replay.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--filters", default="2,4,8")
    parser.add_argument("--code-dim", type=int, default=3)
    parser.add_argument("--seed", type=int, default=20260930)
    parser.add_argument("--epochs", type=int, default=2500)
    parser.add_argument("--learning-rate", type=float, default=0.002)
    parser.add_argument("--l2", type=float, default=1.0e-4)
    parser.add_argument("--dropout", type=float, default=0.0)
    parser.add_argument("--first-kernel", type=int, default=3)
    parser.add_argument("--backbone-architecture", default="spacetodepth_conv")
    parser.add_argument("--activation", default="relu6")
    parser.add_argument("--pool", default="max")
    parser.add_argument("--extra-conv", action="store_true")
    parser.add_argument(
        "--input-transform",
        default="identity",
        choices=["identity", "lowpass", "edge", "low_edge", "raw_low_edge"],
        help="Optional fixed Conv2D input transform for frequency/edge diagnostics.",
    )
    parser.add_argument("--sample-weight-mode", default="parent_balanced", choices=["none", "parent_balanced"])
    parser.add_argument("--stress", default="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04")
    parser.add_argument("--stress-from-prototype-npz", action="store_true")
    parser.add_argument("--init-model", type=Path, default=None)
    parser.add_argument("--prototype-teacher-npz", type=Path, default=None)
    parser.add_argument("--prototype-margin-weight", type=float, default=0.0)
    parser.add_argument("--prototype-margin-target", type=float, default=16.0)
    parser.add_argument("--prototype-margin-alpha", type=float, default=0.05)
    parser.add_argument("--prototype-code-anchor-weight", type=float, default=0.0)
    parser.add_argument("--prototype-output-scale", type=float, default=0.0879310742020607)
    parser.add_argument("--prototype-output-zero", type=int, default=36)
    parser.add_argument("--prototype-low-margin-threshold", type=int, default=8)
    parser.add_argument("--prototype-low-margin-weight", type=float, default=3.0)
    parser.add_argument("--qpair-teacher-npz", type=Path, default=None)
    parser.add_argument("--qpair-margin-weight", type=float, default=0.0)
    parser.add_argument("--qpair-margin-target", type=float, default=8.0)
    parser.add_argument("--qpair-margin-alpha", type=float, default=0.05)
    parser.add_argument("--qpair-start-epoch", type=int, default=1)
    parser.add_argument("--dynamic-qpair-teacher-npz", type=Path, default=None)
    parser.add_argument("--dynamic-qpair-margin-weight", type=float, default=0.0)
    parser.add_argument("--dynamic-qpair-margin-target", type=float, default=8.0)
    parser.add_argument("--dynamic-qpair-margin-alpha", type=float, default=0.05)
    parser.add_argument("--dynamic-qpair-axis-weight", type=float, default=0.0)
    parser.add_argument("--dynamic-qpair-axis-target", type=float, default=0.0)
    parser.add_argument("--dynamic-qpair-axis-alpha", type=float, default=0.02)
    parser.add_argument("--dynamic-qpair-start-epoch", type=int, default=1)
    parser.add_argument("--qanchor-teacher-npz", type=Path, default=None)
    parser.add_argument("--qanchor-weight", type=float, default=0.0)
    parser.add_argument("--qanchor-start-epoch", type=int, default=1)
    parser.add_argument("--source-block-margin-weight", type=float, default=0.0)
    parser.add_argument("--source-block-margin-target", type=float, default=8.0)
    parser.add_argument("--source-block-margin-alpha", type=float, default=0.05)
    parser.add_argument("--source-block-margin-start-epoch", type=int, default=1)
    parser.add_argument("--logit-teacher-npz", type=Path, default=None)
    parser.add_argument("--logit-teacher-weight", type=float, default=0.0)
    parser.add_argument("--logit-teacher-start-epoch", type=int, default=1)
    parser.add_argument("--source-decision-teacher-npz", type=Path, default=None)
    parser.add_argument("--source-decision-margin-weight", type=float, default=0.0)
    parser.add_argument("--source-decision-margin-alpha", type=float, default=0.05)
    parser.add_argument("--source-decision-start-epoch", type=int, default=1)
    parser.add_argument("--source-decision-center-weight", type=float, default=0.0)
    parser.add_argument("--source-decision-center-target", type=float, default=16.0)
    parser.add_argument("--source-decision-center-alpha", type=float, default=0.01)
    parser.add_argument("--source-decision-center-start-epoch", type=int, default=1)
    parser.add_argument("--source-gate-teacher-npz", type=Path, default=None)
    parser.add_argument("--source-gate-weight", type=float, default=0.0)
    parser.add_argument("--source-gate-margin-weight", type=float, default=0.0)
    parser.add_argument("--source-gate-margin-target", type=float, default=8.0)
    parser.add_argument("--source-gate-margin-alpha", type=float, default=0.05)
    parser.add_argument("--source-gate-balance-weight", type=float, default=0.0)
    parser.add_argument("--source-gate-rank-weight", type=float, default=0.0)
    parser.add_argument("--source-gate-rank-alpha", type=float, default=0.05)
    parser.add_argument("--source-gate-rank-score-scale", type=float, default=64.0)
    parser.add_argument("--source-gate-rank-max-target", type=float, default=16.0)
    parser.add_argument("--source-gate-rank-min-gap", type=float, default=1.0)
    parser.add_argument("--source-gate-center-weight", type=float, default=0.0)
    parser.add_argument("--source-gate-center-target", type=float, default=16.0)
    parser.add_argument("--source-gate-center-alpha", type=float, default=0.02)
    parser.add_argument("--source-cluster-weight", type=float, default=0.0)
    parser.add_argument("--source-cluster-target", type=float, default=64.0)
    parser.add_argument("--source-cluster-alpha", type=float, default=0.01)
    parser.add_argument("--source-cluster-start-epoch", type=int, default=1)
    parser.add_argument("--source-gate-start", type=int, default=3)
    parser.add_argument("--source-gate-start-epoch", type=int, default=1)
    parser.add_argument("--orbit-consistency-weight", type=float, default=0.0)
    parser.add_argument("--orbit-consistency-start-epoch", type=int, default=1)
    parser.add_argument("--vicreg-var-weight", type=float, default=0.0)
    parser.add_argument("--vicreg-cov-weight", type=float, default=0.0)
    parser.add_argument("--vicreg-variance-floor", type=float, default=8.0)
    parser.add_argument("--vicreg-start-epoch", type=int, default=1)
    parser.add_argument("--allow-partial-init-output", action="store_true")
    parser.add_argument("--log-every", type=int, default=100)
    args = parser.parse_args()

    tf.keras.utils.set_random_seed(args.seed)
    if args.code_dim < 3:
        raise ValueError("--code-dim must be at least 3 because the first three channels are parent logits")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stress_names = (
        stress_from_params(args.prototype_teacher_npz)
        if args.stress_from_prototype_npz and args.prototype_teacher_npz is not None
        else parse_csv(args.stress)
    )
    flat, images = build_view_dataset(args.dataset_dir, stress_names)
    model = build_embedding_model(
        filters=parse_filters(args.filters),
        embedding_dim=args.code_dim,
        learning_rate=args.learning_rate,
        l2=args.l2,
        dropout=args.dropout,
        first_kernel=args.first_kernel,
        embedding_output_mode="raw",
        backbone_architecture=args.backbone_architecture,
        activation=args.activation,
        pool=args.pool,
        extra_conv=args.extra_conv,
        input_transform=args.input_transform,
    )
    if args.init_model is not None:
        init_model = tf.keras.models.load_model(args.init_model, safe_mode=False)
        if args.allow_partial_init_output:
            set_weights_allow_partial_output(model, init_model)
        else:
            model.set_weights(init_model.get_weights())
    prototype_teacher = load_prototype_teacher(
        args.prototype_teacher_npz,
        sample_count=len(images),
        code_dim=args.code_dim,
        low_margin_threshold=args.prototype_low_margin_threshold,
        low_margin_weight=args.prototype_low_margin_weight,
    )
    qpair_teacher = load_qpair_teacher(args.qpair_teacher_npz, flat, code_dim=args.code_dim)
    dynamic_qpair_teacher = load_dynamic_qpair_teacher(args.dynamic_qpair_teacher_npz, flat)
    qanchor_teacher = load_qanchor_teacher(args.qanchor_teacher_npz, flat, code_dim=args.code_dim)
    logit_teacher = load_logit_teacher(args.logit_teacher_npz, flat)
    source_decision_teacher = load_source_decision_teacher(args.source_decision_teacher_npz, flat)
    source_gate_teacher = load_source_gate_teacher(
        args.source_gate_teacher_npz,
        flat,
        code_dim=args.code_dim,
        gate_start=args.source_gate_start,
    )
    config = {key: (str(value) if isinstance(value, Path) else value) for key, value in vars(args).items()}
    config["filters"] = list(parse_filters(args.filters))
    config["embedding_dim"] = args.code_dim
    config["code_dim"] = args.code_dim
    config["embedding_output_mode"] = "parent_logits" if args.code_dim == 3 else "parent_logits_plus_raw_code"
    config["effective_stress"] = stress_names
    config["qpair_active_rows"] = int(np.sum(qpair_teacher["weights"] > 0.0))
    config["dynamic_qpair_active_rows"] = int(np.sum(dynamic_qpair_teacher["weights"] > 0.0))
    config["qanchor_active_rows"] = int(np.sum(qanchor_teacher["weights"] > 0.0))
    config["qanchor_source_block_start"] = int(np.asarray(qanchor_teacher["source_block_start"]).item())
    config["qanchor_source_block_count"] = int(np.asarray(qanchor_teacher["source_block_count"]).item())
    config["qanchor_source_block_dim"] = int(np.asarray(qanchor_teacher["source_block_dim"]).item())
    config["logit_teacher_active_rows"] = int(np.sum(logit_teacher["weights"] > 0.0))
    config["source_decision_active_rows"] = int(np.sum(source_decision_teacher["weights"] > 0.0))
    config["source_gate_active_rows"] = int(np.sum(source_gate_teacher["weights"] > 0.0))
    config["source_gate_dim"] = int(np.asarray(source_gate_teacher["target_probs"]).shape[1])
    write_json(args.output_dir / "train_config.json", config)

    train_parent_classifier(
        model=model,
        flat=flat,
        images=images,
        output_dir=args.output_dir,
        epochs=args.epochs,
        learning_rate=args.learning_rate,
        sample_weight_mode=args.sample_weight_mode,
        log_every=args.log_every,
        prototype_teacher=prototype_teacher,
        prototype_margin_weight=args.prototype_margin_weight,
        prototype_margin_target=args.prototype_margin_target,
        prototype_margin_alpha=args.prototype_margin_alpha,
        prototype_code_anchor_weight=args.prototype_code_anchor_weight,
        prototype_output_scale=args.prototype_output_scale,
        prototype_output_zero=args.prototype_output_zero,
        qpair_teacher=qpair_teacher,
        qpair_margin_weight=args.qpair_margin_weight,
        qpair_margin_target=args.qpair_margin_target,
        qpair_margin_alpha=args.qpair_margin_alpha,
        qpair_start_epoch=args.qpair_start_epoch,
        dynamic_qpair_teacher=dynamic_qpair_teacher,
        dynamic_qpair_margin_weight=args.dynamic_qpair_margin_weight,
        dynamic_qpair_margin_target=args.dynamic_qpair_margin_target,
        dynamic_qpair_margin_alpha=args.dynamic_qpair_margin_alpha,
        dynamic_qpair_axis_weight=args.dynamic_qpair_axis_weight,
        dynamic_qpair_axis_target=args.dynamic_qpair_axis_target,
        dynamic_qpair_axis_alpha=args.dynamic_qpair_axis_alpha,
        dynamic_qpair_start_epoch=args.dynamic_qpair_start_epoch,
        qanchor_teacher=qanchor_teacher,
        qanchor_weight=args.qanchor_weight,
        qanchor_start_epoch=args.qanchor_start_epoch,
        source_block_margin_weight=args.source_block_margin_weight,
        source_block_margin_target=args.source_block_margin_target,
        source_block_margin_alpha=args.source_block_margin_alpha,
        source_block_margin_start_epoch=args.source_block_margin_start_epoch,
        logit_teacher=logit_teacher,
        logit_teacher_weight=args.logit_teacher_weight,
        logit_teacher_start_epoch=args.logit_teacher_start_epoch,
        source_decision_teacher=source_decision_teacher,
        source_decision_margin_weight=args.source_decision_margin_weight,
        source_decision_margin_alpha=args.source_decision_margin_alpha,
        source_decision_start_epoch=args.source_decision_start_epoch,
        source_decision_center_weight=args.source_decision_center_weight,
        source_decision_center_target=args.source_decision_center_target,
        source_decision_center_alpha=args.source_decision_center_alpha,
        source_decision_center_start_epoch=args.source_decision_center_start_epoch,
        source_gate_teacher=source_gate_teacher,
        source_gate_weight=args.source_gate_weight,
        source_gate_margin_weight=args.source_gate_margin_weight,
        source_gate_margin_target=args.source_gate_margin_target,
        source_gate_margin_alpha=args.source_gate_margin_alpha,
        source_gate_balance_weight=args.source_gate_balance_weight,
        source_gate_rank_weight=args.source_gate_rank_weight,
        source_gate_rank_alpha=args.source_gate_rank_alpha,
        source_gate_rank_score_scale=args.source_gate_rank_score_scale,
        source_gate_rank_max_target=args.source_gate_rank_max_target,
        source_gate_rank_min_gap=args.source_gate_rank_min_gap,
        source_gate_center_weight=args.source_gate_center_weight,
        source_gate_center_target=args.source_gate_center_target,
        source_gate_center_alpha=args.source_gate_center_alpha,
        source_cluster_weight=args.source_cluster_weight,
        source_cluster_target=args.source_cluster_target,
        source_cluster_alpha=args.source_cluster_alpha,
        source_cluster_start_epoch=args.source_cluster_start_epoch,
        source_gate_start=args.source_gate_start,
        source_gate_start_epoch=args.source_gate_start_epoch,
        orbit_consistency_weight=args.orbit_consistency_weight,
        orbit_consistency_start_epoch=args.orbit_consistency_start_epoch,
        vicreg_var_weight=args.vicreg_var_weight,
        vicreg_cov_weight=args.vicreg_cov_weight,
        vicreg_variance_floor=args.vicreg_variance_floor,
        vicreg_start_epoch=args.vicreg_start_epoch,
        code_dim=args.code_dim,
    )
    model.save(args.output_dir / "parent_model.keras")
    row = evaluate_model(model, flat, images, args.output_dir)
    rng = np.random.default_rng(args.seed)
    rep_idx = rng.choice(len(images), size=min(512, len(images)), replace=False)
    export_info = export_tflite(model, args.output_dir, images[rep_idx])
    float_pred, float_ops = predict_tflite(Path(export_info["float_tflite"]), images)
    int8_pred, int8_ops = predict_tflite(Path(export_info["int8_tflite"]), images)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    tflite_row: dict[str, Any] = {
        **export_info,
        "float_unique_ops": float_ops,
        "int8_unique_ops": int8_ops,
    }
    tflite_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=float_pred, prefix="tflite_float_"))
    tflite_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="tflite_int8_"))
    write_json(args.output_dir / "tflite_summary.json", tflite_row)
    write_json(args.output_dir / "summary.json", {"best": row, "tflite": tflite_row, "config": config})
    print(json.dumps({"best": row, "tflite": tflite_row}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
