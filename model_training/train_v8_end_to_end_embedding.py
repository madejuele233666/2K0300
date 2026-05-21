import argparse
import json
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf

from evaluate_v8_embedding_prototypes import (
    ROT_MIRROR_VIEWS,
    evaluate_prototype_candidate,
    parse_csv,
    parse_floats,
    parse_ints,
    row_score,
    true_rebuild_loo,
    write_csv,
)
import train_tiny32_v5_visual_subclass_scan as train


STRICT_RECOMMENDED_RAW_TFLITE_OPS = [
    "SPACE_TO_DEPTH",
    "CONV_2D",
    "MAX_POOL_2D",
    "MEAN",
    "FULLY_CONNECTED",
]


def parse_filters(text: str) -> tuple[int, int, int]:
    values = tuple(parse_ints(text))
    if len(values) != 3:
        raise ValueError(f"--filters must contain exactly 3 values, got {text}")
    return values  # type: ignore[return-value]


def l2_layer(values: tf.Tensor) -> tf.Tensor:
    return values / tf.maximum(tf.norm(values, axis=-1, keepdims=True), 1.0e-8)


def safe_l2_normalize(values: tf.Tensor, axis: int = -1) -> tf.Tensor:
    return values / tf.maximum(tf.norm(values, axis=axis, keepdims=True), 1.0e-8)


def ste_int8(values: tf.Tensor, scale: float) -> tf.Tensor:
    scaled = values * float(scale)
    quantized = tf.clip_by_value(tf.round(scaled), -128.0, 127.0)
    return scaled + tf.stop_gradient(quantized - scaled)


def activation_layer(x: tf.Tensor, activation: str, name: str) -> tf.Tensor:
    if activation == "relu":
        return tf.keras.layers.ReLU(name=name)(x)
    if activation == "relu6":
        return tf.keras.layers.ReLU(max_value=6.0, name=name)(x)
    raise ValueError(f"unknown activation: {activation}")


def conv_block(
    x: tf.Tensor,
    *,
    filters: int,
    kernel: int,
    regularizer: tf.keras.regularizers.Regularizer | None,
    activation: str,
    name: str,
    strides: int = 1,
) -> tf.Tensor:
    x = tf.keras.layers.Conv2D(
        filters,
        kernel,
        strides=strides,
        padding="same",
        use_bias=False,
        kernel_regularizer=regularizer,
        name=f"{name}_conv",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_bn")(x)
    return activation_layer(x, activation, f"{name}_{activation}")


def depthwise_pointwise_block(
    x: tf.Tensor,
    *,
    filters: int,
    kernel: int,
    regularizer: tf.keras.regularizers.Regularizer | None,
    activation: str,
    name: str,
    strides: int = 1,
) -> tf.Tensor:
    x = tf.keras.layers.DepthwiseConv2D(
        kernel,
        strides=strides,
        padding="same",
        use_bias=False,
        depthwise_regularizer=regularizer,
        name=f"{name}_dw",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_dw_bn")(x)
    x = activation_layer(x, activation, f"{name}_dw_{activation}")
    x = tf.keras.layers.Conv2D(
        filters,
        1,
        padding="same",
        use_bias=False,
        kernel_regularizer=regularizer,
        name=f"{name}_pw",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_pw_bn")(x)
    return activation_layer(x, activation, f"{name}_pw_{activation}")


def maybe_pool(x: tf.Tensor, pool: str, name: str) -> tf.Tensor:
    if pool == "max":
        return tf.keras.layers.MaxPooling2D(2, name=f"{name}_max_pool")(x)
    if pool == "avg":
        return tf.keras.layers.AveragePooling2D(2, name=f"{name}_avg_pool")(x)
    raise ValueError(f"unknown pool: {pool}")


def space_to_depth(x: tf.Tensor, name: str) -> tf.Tensor:
    height = int(x.shape[1])
    width = int(x.shape[2])
    channels = int(x.shape[3])
    return tf.keras.layers.Lambda(
        lambda t: tf.nn.space_to_depth(t, 2),
        output_shape=(height // 2, width // 2, channels * 4),
        name=name,
    )(x)


def fixed_input_transform_kernels(input_transform: str) -> np.ndarray | None:
    if input_transform == "identity":
        return None
    identity = np.asarray(
        [
            [0.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
        dtype=np.float32,
    )
    lowpass = np.ones((3, 3), dtype=np.float32) / 9.0
    sobel_x = np.asarray(
        [
            [-1.0, 0.0, 1.0],
            [-2.0, 0.0, 2.0],
            [-1.0, 0.0, 1.0],
        ],
        dtype=np.float32,
    ) / 4.0
    sobel_y = np.asarray(
        [
            [-1.0, -2.0, -1.0],
            [0.0, 0.0, 0.0],
            [1.0, 2.0, 1.0],
        ],
        dtype=np.float32,
    ) / 4.0
    kernels_by_name = {
        "lowpass": [lowpass],
        "edge": [sobel_x, sobel_y],
        "low_edge": [lowpass, sobel_x, sobel_y],
        "raw_low_edge": [identity, lowpass, sobel_x, sobel_y],
    }
    kernels = kernels_by_name.get(input_transform)
    if kernels is None:
        raise ValueError(f"unknown input_transform: {input_transform}")
    return np.stack(kernels, axis=-1)[:, :, None, :].astype(np.float32)


def apply_fixed_input_transform(x: tf.Tensor, input_transform: str) -> tf.Tensor:
    kernels = fixed_input_transform_kernels(input_transform)
    if kernels is None:
        return x
    layer = tf.keras.layers.Conv2D(
        filters=int(kernels.shape[-1]),
        kernel_size=3,
        padding="same",
        use_bias=False,
        trainable=False,
        name=f"input_{input_transform}_conv",
    )
    y = layer(x)
    layer.set_weights([kernels])
    return y


def build_embedding_model(
    *,
    filters: tuple[int, int, int],
    embedding_dim: int,
    learning_rate: float,
    l2: float,
    dropout: float,
    first_kernel: int,
    embedding_output_mode: str,
    backbone_architecture: str,
    activation: str,
    pool: str,
    extra_conv: bool,
    input_transform: str = "identity",
) -> tf.keras.Model:
    regularizer = tf.keras.regularizers.l2(l2) if l2 > 0 else None
    inputs = tf.keras.Input((train.IMAGE_SIZE, train.IMAGE_SIZE, 1), name="gray32")
    x: tf.Tensor = apply_fixed_input_transform(inputs, input_transform)
    if backbone_architecture in {
        "spacetodepth_conv",
        "spacetodepth_depthwise",
        "spacetodepth_hybrid",
        "double_spacetodepth_conv",
    }:
        initial_s2d_name = "space_to_depth_1" if backbone_architecture == "double_spacetodepth_conv" else "space_to_depth"
        x = space_to_depth(x, initial_s2d_name)
    for index, filter_count in enumerate(filters):
        kernel = first_kernel if index == 0 else 3
        name = f"block_{index + 1}"
        if backbone_architecture == "spacetodepth_conv":
            x = conv_block(
                x,
                filters=filter_count,
                kernel=kernel,
                regularizer=regularizer,
                activation=activation,
                name=name,
            )
            if extra_conv and index == 2:
                x = conv_block(
                    x,
                    filters=filter_count,
                    kernel=3,
                    regularizer=regularizer,
                    activation=activation,
                    name="block_3_extra",
                )
            if index < 2:
                x = maybe_pool(x, pool, name)
        elif backbone_architecture == "depthwise_pool":
            x = depthwise_pointwise_block(
                x,
                filters=filter_count,
                kernel=kernel,
                regularizer=regularizer,
                activation=activation,
                name=name,
            )
            if extra_conv and index == 2:
                x = depthwise_pointwise_block(
                    x,
                    filters=filter_count,
                    kernel=3,
                    regularizer=regularizer,
                    activation=activation,
                    name="block_3_extra",
                )
            if index < 2:
                x = maybe_pool(x, pool, name)
        elif backbone_architecture == "spacetodepth_depthwise":
            x = depthwise_pointwise_block(
                x,
                filters=filter_count,
                kernel=kernel,
                regularizer=regularizer,
                activation=activation,
                name=name,
            )
            if extra_conv and index == 2:
                x = depthwise_pointwise_block(
                    x,
                    filters=filter_count,
                    kernel=3,
                    regularizer=regularizer,
                    activation=activation,
                    name="block_3_extra",
                )
            if index < 2:
                x = maybe_pool(x, pool, name)
        elif backbone_architecture == "spacetodepth_hybrid":
            if index == 0:
                x = conv_block(
                    x,
                    filters=filter_count,
                    kernel=kernel,
                    regularizer=regularizer,
                    activation=activation,
                    name=name,
                )
            else:
                x = depthwise_pointwise_block(
                    x,
                    filters=filter_count,
                    kernel=kernel,
                    regularizer=regularizer,
                    activation=activation,
                    name=name,
                )
            if extra_conv and index == 2:
                x = depthwise_pointwise_block(
                    x,
                    filters=filter_count,
                    kernel=3,
                    regularizer=regularizer,
                    activation=activation,
                    name="block_3_extra",
                )
            if index < 2:
                x = maybe_pool(x, pool, name)
        elif backbone_architecture == "double_spacetodepth_conv":
            x = conv_block(
                x,
                filters=filter_count,
                kernel=kernel,
                regularizer=regularizer,
                activation=activation,
                name=name,
            )
            if extra_conv and index == 2:
                x = conv_block(
                    x,
                    filters=filter_count,
                    kernel=3,
                    regularizer=regularizer,
                    activation=activation,
                    name="block_3_extra",
                )
            if index == 0:
                x = space_to_depth(x, "space_to_depth_2")
            elif index == 1:
                x = maybe_pool(x, pool, name)
        else:
            raise ValueError(f"unknown backbone_architecture: {backbone_architecture}")
    x = tf.keras.layers.GlobalAveragePooling2D(name="gap")(x)
    if dropout > 0:
        x = tf.keras.layers.Dropout(dropout, name="dropout")(x)
    x = tf.keras.layers.Dense(embedding_dim, kernel_regularizer=regularizer, name="embedding_dense")(x)
    if embedding_output_mode == "l2":
        outputs = tf.keras.layers.Lambda(l2_layer, name="embedding")(x)
    elif embedding_output_mode == "raw":
        outputs = x
    else:
        raise ValueError(f"unknown embedding_output_mode: {embedding_output_mode}")
    model = tf.keras.Model(inputs, outputs, name="v8_pure_embedding")
    model.optimizer = tf.keras.optimizers.Adam(learning_rate=learning_rate)  # type: ignore[attr-defined]
    return model


def off_diagonal(matrix: tf.Tensor) -> tf.Tensor:
    size = tf.shape(matrix)[0]
    return tf.reshape(matrix - tf.linalg.diag(tf.linalg.diag_part(matrix)), (size * size,))


def build_view_dataset(dataset_dir: Path, stress_names: list[str]) -> tuple[dict[str, Any], np.ndarray]:
    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(dataset_dir)
    view_order = ["clean"] + stress_names
    x_views: list[np.ndarray] = []
    for view in view_order:
        xs = x if view == "clean" else train.stress_batch_any(view, x)
        x_views.append(xs.astype(np.float32))
    sample_count = len(x)
    flat = {
        "view_names": view_order,
        "paths": paths,
        "sample_index": np.tile(np.arange(sample_count, dtype=np.int64), len(view_order)),
        "view_labels": np.asarray([view for view in view_order for _ in range(sample_count)]),
        "y_parent": np.tile(y_parent.astype(np.int64), len(view_order)),
        "y_sub": np.tile(y_sub.astype(np.int64), len(view_order)),
    }
    return flat, np.concatenate(x_views, axis=0).astype(np.float32)


def load_compiled_teacher(path: Path, embedding_dim: int) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        required = ["prototypes", "prototype_parent"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing compiled teacher arrays: {missing}")
        prototypes = np.asarray(data["prototypes"], dtype=np.float32)
        if prototypes.ndim != 2 or prototypes.shape[1] != embedding_dim:
            raise ValueError(f"compiled teacher dim mismatch: {prototypes.shape} vs embedding_dim={embedding_dim}")
        return {
            "prototypes": prototypes,
            "prototype_parent": np.asarray(data["prototype_parent"], dtype=np.int32),
        }


def load_low_margin_weights(
    path: Path | None,
    flat: dict[str, Any],
    *,
    threshold: int,
    extra_weight: float,
) -> np.ndarray:
    weights = np.ones(len(np.asarray(flat["sample_index"])), dtype=np.float32)
    if path is None or extra_weight <= 0:
        return weights
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "int8_margin"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing low-margin teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_margin = np.asarray(data["int8_margin"], dtype=np.int64)
    low_keys = {
        (int(sample), str(view))
        for sample, view, margin in zip(teacher_sample, teacher_view, teacher_margin, strict=False)
        if int(margin) <= int(threshold)
    }
    if not low_keys:
        return weights
    for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False)):
        if (int(sample), str(view)) in low_keys:
            weights[index] += float(extra_weight)
    return weights


def load_qpair_teacher(
    path: Path | None,
    flat: dict[str, Any],
    *,
    embedding_dim: int,
) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    teacher = {
        "correct": np.zeros((row_count, embedding_dim), dtype=np.float32),
        "wrong": np.zeros((row_count, embedding_dim), dtype=np.float32),
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
    if correct.ndim != 2 or correct.shape[1] != embedding_dim:
        raise ValueError(f"qpair correct_proto_int8 shape {correct.shape} does not match embedding_dim={embedding_dim}")
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


def load_dynamic_qpair_teacher(path: Path | None, flat: dict[str, Any]) -> dict[str, np.ndarray]:
    empty = {
        "query": np.zeros((0,), dtype=np.int32),
        "correct": np.zeros((0,), dtype=np.int32),
        "wrong": np.zeros((0,), dtype=np.int32),
        "weights": np.zeros((0,), dtype=np.float32),
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
        required = [
            "sample_index",
            "view_labels",
            "correct_proto_sample",
            "correct_proto_view",
            "wrong_proto_sample",
            "wrong_proto_view",
            "weight",
        ]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing dynamic qpair teacher arrays: {missing}")
        query_sample = np.asarray(data["sample_index"], dtype=np.int64)
        query_view = np.asarray(data["view_labels"]).astype(str)
        correct_sample = np.asarray(data["correct_proto_sample"], dtype=np.int64)
        correct_view = np.asarray(data["correct_proto_view"]).astype(str)
        wrong_sample = np.asarray(data["wrong_proto_sample"], dtype=np.int64)
        wrong_view = np.asarray(data["wrong_proto_view"]).astype(str)
        weights = np.asarray(data["weight"], dtype=np.float32)
    lengths = {len(query_sample), len(query_view), len(correct_sample), len(correct_view), len(wrong_sample), len(wrong_view), len(weights)}
    if len(lengths) != 1:
        raise ValueError("dynamic qpair teacher array lengths do not match")
    query_rows: list[int] = []
    correct_rows: list[int] = []
    wrong_rows: list[int] = []
    kept_weights: list[float] = []
    missing_keys: list[tuple[int, str]] = []
    for index in range(len(weights)):
        keys = [
            (int(query_sample[index]), str(query_view[index])),
            (int(correct_sample[index]), str(correct_view[index])),
            (int(wrong_sample[index]), str(wrong_view[index])),
        ]
        rows = [row_by_key.get(key) for key in keys]
        if any(row is None for row in rows):
            missing_keys.extend(key for key, row in zip(keys, rows, strict=False) if row is None)
            continue
        query_rows.append(int(rows[0]))
        correct_rows.append(int(rows[1]))
        wrong_rows.append(int(rows[2]))
        kept_weights.append(float(weights[index]))
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"dynamic qpair rows missing from training flat: {len(missing_keys)}, first {preview}")
    return {
        "query": np.asarray(query_rows, dtype=np.int32),
        "correct": np.asarray(correct_rows, dtype=np.int32),
        "wrong": np.asarray(wrong_rows, dtype=np.int32),
        "weights": np.asarray(kept_weights, dtype=np.float32),
    }


def weighted_mean(values: tf.Tensor, weights: tf.Tensor) -> tf.Tensor:
    return tf.reduce_sum(values * weights) / tf.maximum(tf.reduce_sum(weights), 1.0)


def view_indexes(flat: dict[str, Any], views: list[str]) -> np.ndarray:
    view_labels = np.asarray(flat["view_labels"])
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    sample_count = int(np.max(sample_index)) + 1
    rows: list[np.ndarray] = []
    for view in views:
        indexes = np.where(view_labels == view)[0]
        order = np.argsort(sample_index[indexes])
        rows.append(indexes[order])
    stacked = np.stack(rows)
    if stacked.shape[1] != sample_count:
        raise ValueError(f"unexpected index shape for {views}: {stacked.shape}")
    return stacked


def train_model(
    *,
    model: tf.keras.Model,
    flat: dict[str, Any],
    images: np.ndarray,
    output_dir: Path,
    embedding_dim: int,
    seed: int,
    epochs: int,
    warmup_epochs: int,
    proxy_scale: float,
    lambda_d4: float,
    lambda_stress: float,
    lambda_var: float,
    lambda_cov: float,
    variance_floor: float,
    compiled_teacher: dict[str, np.ndarray] | None,
    lambda_compiled_margin: float,
    lambda_compiled_pull: float,
    compiled_margin_target: float,
    compiled_margin_alpha: float,
    teacher_start_epoch: int,
    lambda_qcompiled_margin: float,
    qcompiled_margin_target: float,
    qcompiled_margin_alpha: float,
    qcompiled_scale: float,
    qcompiled_start_epoch: int,
    qcompiled_weight_mode: str,
    qproxy_weights: np.ndarray,
    qpair_teacher: dict[str, np.ndarray],
    lambda_qpair_margin: float,
    qpair_margin_target: float,
    qpair_margin_alpha: float,
    qpair_scale: float,
    qpair_start_epoch: int,
    dynamic_qpair_teacher: dict[str, np.ndarray],
    lambda_dynamic_qpair_margin: float,
    dynamic_qpair_margin_target: float,
    dynamic_qpair_margin_alpha: float,
    dynamic_qpair_scale: float,
    dynamic_qpair_start_epoch: int,
    lambda_qproxy_margin: float,
    qproxy_margin_target: float,
    qproxy_margin_alpha: float,
    qproxy_scale: float,
    qproxy_start_epoch: int,
    init_proxies_from_embeddings: bool,
    proxy_normalize_embeddings: bool,
    metric_normalize_embeddings: bool,
    lambda_norm: float,
    norm_target: float,
) -> dict[str, np.ndarray]:
    tf.keras.utils.set_random_seed(seed)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    clean_d4_views = ["clean"] + [view for view in ROT_MIRROR_VIEWS if view in set(flat["view_names"])]
    fixed_stress_views = [view for view in flat["view_names"] if view != "clean" and view not in set(ROT_MIRROR_VIEWS)]
    orbit_idx = view_indexes(flat, clean_d4_views)
    stress_idx = view_indexes(flat, fixed_stress_views) if fixed_stress_views else np.zeros((0, orbit_idx.shape[1]), dtype=np.int64)
    train_mask_warmup = (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    train_mask_full = np.ones(len(view_labels), dtype=bool)
    x_all = tf.constant(images, dtype=tf.float32)
    y_sub_tf = tf.constant(y_sub, dtype=tf.int32)
    y_parent_tf = tf.constant(y_parent, dtype=tf.int32)
    orbit_idx_tf = tf.constant(orbit_idx, dtype=tf.int32)
    stress_idx_tf = tf.constant(stress_idx, dtype=tf.int32)
    train_idx_warmup_tf = tf.constant(np.where(train_mask_warmup)[0], dtype=tf.int32)
    train_idx_full_tf = tf.constant(np.where(train_mask_full)[0], dtype=tf.int32)
    qproxy_weights_tf = tf.constant(qproxy_weights.astype(np.float32), dtype=tf.float32)
    qpair_correct_tf = tf.constant(np.asarray(qpair_teacher["correct"], dtype=np.float32), dtype=tf.float32)
    qpair_wrong_tf = tf.constant(np.asarray(qpair_teacher["wrong"], dtype=np.float32), dtype=tf.float32)
    qpair_weights_tf = tf.constant(np.asarray(qpair_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    dynamic_query_tf = tf.constant(np.asarray(dynamic_qpair_teacher["query"], dtype=np.int32), dtype=tf.int32)
    dynamic_correct_tf = tf.constant(np.asarray(dynamic_qpair_teacher["correct"], dtype=np.int32), dtype=tf.int32)
    dynamic_wrong_tf = tf.constant(np.asarray(dynamic_qpair_teacher["wrong"], dtype=np.int32), dtype=tf.int32)
    dynamic_weights_tf = tf.constant(np.asarray(dynamic_qpair_teacher["weights"], dtype=np.float32), dtype=tf.float32)
    proxy_parent_tf = tf.constant(train.VISUAL_TO_PARENT, dtype=tf.int32)
    has_stress = int(stress_idx.shape[0]) > 0
    if init_proxies_from_embeddings:
        initial_embeddings = model.predict(images, batch_size=256, verbose=0).astype(np.float32)
        proxy_rows: list[np.ndarray] = []
        rng = np.random.default_rng(seed + 101)
        for subclass in range(8):
            mask = y_sub == subclass
            if np.any(mask):
                center = np.mean(initial_embeddings[mask], axis=0)
            else:
                center = rng.normal(size=(embedding_dim,)).astype(np.float32)
            norm = np.linalg.norm(center)
            proxy_rows.append((center / max(float(norm), 1.0e-8)).astype(np.float32))
        proxy_init = np.stack(proxy_rows).astype(np.float32)
    else:
        proxy_init = tf.random.normal((8, embedding_dim), seed=seed + 101)
    proxies = tf.Variable(proxy_init, name="v8_subclass_proxies")
    ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True)
    optimizer = model.optimizer  # type: ignore[attr-defined]
    train_vars = model.trainable_variables + [proxies]
    if hasattr(optimizer, "build"):
        optimizer.build(train_vars)
    log_rows: list[dict[str, Any]] = []
    has_compiled_teacher = compiled_teacher is not None and (lambda_compiled_margin > 0.0 or lambda_compiled_pull > 0.0)
    if has_compiled_teacher:
        teacher_prototypes_tf = tf.constant(np.asarray(compiled_teacher["prototypes"], dtype=np.float32), dtype=tf.float32)
        teacher_parent_tf = tf.constant(np.asarray(compiled_teacher["prototype_parent"], dtype=np.int32), dtype=tf.int32)
    has_qcompiled_margin = compiled_teacher is not None and lambda_qcompiled_margin > 0.0
    if has_qcompiled_margin:
        qcompiled_teacher_prototypes = np.clip(
            np.rint(np.asarray(compiled_teacher["prototypes"], dtype=np.float32) * float(qcompiled_scale)),
            -128.0,
            127.0,
        ).astype(np.float32)
        qcompiled_teacher_prototypes_tf = tf.constant(qcompiled_teacher_prototypes, dtype=tf.float32)
        qcompiled_teacher_parent_tf = tf.constant(
            np.asarray(compiled_teacher["prototype_parent"], dtype=np.int32),
            dtype=tf.int32,
        )
    has_qproxy_margin = lambda_qproxy_margin > 0.0
    has_qpair_margin = lambda_qpair_margin > 0.0 and bool(np.any(np.asarray(qpair_teacher["weights"]) > 0.0))
    has_dynamic_qpair_margin = lambda_dynamic_qpair_margin > 0.0 and bool(
        np.any(np.asarray(dynamic_qpair_teacher["weights"]) > 0.0)
    )

    @tf.function(reduce_retracing=True)
    def train_step(
        train_idx_tf: tf.Tensor,
        stress_weight: tf.Tensor,
        compiled_weight: tf.Tensor,
        qcompiled_weight: tf.Tensor,
        qpair_weight: tf.Tensor,
        dynamic_qpair_weight: tf.Tensor,
        qproxy_weight: tf.Tensor,
    ) -> tuple[tf.Tensor, ...]:
        with tf.GradientTape() as tape:
            embeddings = model(x_all, training=True)
            selected = tf.gather(embeddings, train_idx_tf)
            selected_for_proxy = safe_l2_normalize(selected) if proxy_normalize_embeddings else selected
            metric_embeddings = safe_l2_normalize(embeddings) if metric_normalize_embeddings else embeddings
            selected_metric = tf.gather(metric_embeddings, train_idx_tf)
            y_selected = tf.gather(y_sub_tf, train_idx_tf)
            y_parent_selected = tf.gather(y_parent_tf, train_idx_tf)
            proxy_norm = safe_l2_normalize(proxies, axis=1)
            logits = proxy_scale * tf.matmul(selected_for_proxy, proxy_norm, transpose_b=True)
            proxy_loss = ce(y_selected, logits)

            orbit_embeddings = tf.gather(metric_embeddings, orbit_idx_tf)
            orbit_center = tf.stop_gradient(tf.reduce_mean(orbit_embeddings, axis=0, keepdims=True))
            d4_loss = tf.reduce_mean(tf.reduce_sum(tf.square(orbit_embeddings - orbit_center), axis=2))
            if has_stress:
                def compute_stress_loss() -> tf.Tensor:
                    stress_embeddings = tf.gather(metric_embeddings, stress_idx_tf)
                    return tf.reduce_mean(tf.reduce_sum(tf.square(stress_embeddings - orbit_center), axis=2))

                stress_loss = tf.cond(
                    stress_weight > 0.0,
                    compute_stress_loss,
                    lambda: tf.constant(0.0, dtype=tf.float32),
                )
            else:
                stress_loss = tf.constant(0.0, dtype=tf.float32)

            stddev = tf.sqrt(tf.math.reduce_variance(selected_metric, axis=0) + 1.0e-6)
            var_loss = tf.reduce_mean(tf.square(tf.nn.relu(variance_floor - stddev)))
            centered = selected_metric - tf.reduce_mean(selected_metric, axis=0, keepdims=True)
            cov = tf.matmul(centered, centered, transpose_a=True) / tf.cast(tf.shape(centered)[0] - 1, tf.float32)
            cov_loss = tf.reduce_sum(tf.square(off_diagonal(cov))) / tf.cast(embedding_dim, tf.float32)
            selected_norm = tf.norm(selected, axis=1)
            norm_loss = tf.reduce_mean(tf.square(selected_norm - norm_target))
            norm_mean = tf.reduce_mean(selected_norm)
            if has_compiled_teacher:
                diff = selected[:, None, :] - teacher_prototypes_tf[None, :, :]
                dist = tf.reduce_sum(tf.square(diff), axis=2)
                class_mins = []
                for parent in range(3):
                    mask = teacher_parent_tf == parent
                    class_mins.append(tf.reduce_min(tf.where(mask[None, :], dist, tf.constant(np.inf, tf.float32)), axis=1))
                class_dist = tf.stack(class_mins, axis=1)
                correct_dist = tf.gather(class_dist, y_parent_selected, batch_dims=1)
                wrong_dist = tf.reduce_min(
                    class_dist
                    + tf.one_hot(
                        y_parent_selected,
                        3,
                        on_value=tf.constant(np.inf, tf.float32),
                        off_value=tf.constant(0.0, tf.float32),
                    ),
                    axis=1,
                )
                teacher_margin = wrong_dist - correct_dist
                compiled_margin_loss = tf.reduce_mean(
                    tf.nn.softplus(compiled_margin_alpha * (compiled_margin_target - teacher_margin))
                )
                compiled_pull_loss = tf.reduce_mean(correct_dist)
            else:
                compiled_margin_loss = tf.constant(0.0, dtype=tf.float32)
                compiled_pull_loss = tf.constant(0.0, dtype=tf.float32)
            if has_qcompiled_margin:
                qcompiled_selected = ste_int8(selected, qcompiled_scale)
                qcompiled_diff = qcompiled_selected[:, None, :] - qcompiled_teacher_prototypes_tf[None, :, :]
                qcompiled_dist = tf.reduce_sum(tf.square(qcompiled_diff), axis=2)
                qcompiled_class_mins = []
                for parent in range(3):
                    mask = qcompiled_teacher_parent_tf == parent
                    qcompiled_class_mins.append(
                        tf.reduce_min(tf.where(mask[None, :], qcompiled_dist, tf.constant(np.inf, tf.float32)), axis=1)
                    )
                qcompiled_class_dist = tf.stack(qcompiled_class_mins, axis=1)
                qcompiled_correct = tf.gather(qcompiled_class_dist, y_parent_selected, batch_dims=1)
                qcompiled_wrong = tf.reduce_min(
                    qcompiled_class_dist
                    + tf.one_hot(
                        y_parent_selected,
                        3,
                        on_value=tf.constant(np.inf, tf.float32),
                        off_value=tf.constant(0.0, tf.float32),
                    ),
                    axis=1,
                )
                qcompiled_margin = qcompiled_wrong - qcompiled_correct
                raw_qcompiled_margin_loss = tf.nn.softplus(
                    qcompiled_margin_alpha * (qcompiled_margin_target - qcompiled_margin)
                )
                if qcompiled_weight_mode == "uniform":
                    qcompiled_margin_loss = tf.reduce_mean(raw_qcompiled_margin_loss)
                    qcompiled_margin_mean = tf.reduce_mean(qcompiled_margin)
                else:
                    q_weights = tf.gather(qproxy_weights_tf, train_idx_tf)
                    if qcompiled_weight_mode == "low_margin_extra":
                        qcompiled_weights = q_weights
                    elif qcompiled_weight_mode == "low_margin_only":
                        qcompiled_weights = tf.nn.relu(q_weights - 1.0)
                    else:
                        qcompiled_weights = tf.ones_like(q_weights)
                    qcompiled_margin_loss = weighted_mean(raw_qcompiled_margin_loss, qcompiled_weights)
                    qcompiled_margin_mean = weighted_mean(qcompiled_margin, qcompiled_weights)
            else:
                qcompiled_margin_loss = tf.constant(0.0, dtype=tf.float32)
                qcompiled_margin_mean = tf.constant(0.0, dtype=tf.float32)
            if has_qproxy_margin:
                q_selected = ste_int8(selected_for_proxy, qproxy_scale)
                q_proxy = ste_int8(proxy_norm, qproxy_scale)
                qdiff = q_selected[:, None, :] - q_proxy[None, :, :]
                qdist = tf.reduce_sum(tf.square(qdiff), axis=2)
                q_class_mins = []
                for parent in range(3):
                    mask = proxy_parent_tf == parent
                    q_class_mins.append(tf.reduce_min(tf.where(mask[None, :], qdist, tf.constant(np.inf, tf.float32)), axis=1))
                q_class_dist = tf.stack(q_class_mins, axis=1)
                q_correct = tf.gather(q_class_dist, y_parent_selected, batch_dims=1)
                q_wrong = tf.reduce_min(
                    q_class_dist
                    + tf.one_hot(
                        y_parent_selected,
                        3,
                        on_value=tf.constant(np.inf, tf.float32),
                        off_value=tf.constant(0.0, tf.float32),
                    ),
                    axis=1,
                )
                q_margin = q_wrong - q_correct
                q_weights = tf.gather(qproxy_weights_tf, train_idx_tf)
                raw_qproxy_margin_loss = tf.nn.softplus(qproxy_margin_alpha * (qproxy_margin_target - q_margin))
                qproxy_margin_loss = weighted_mean(raw_qproxy_margin_loss, q_weights)
                qproxy_margin_mean = tf.reduce_mean(q_margin)
            else:
                qproxy_margin_loss = tf.constant(0.0, dtype=tf.float32)
                qproxy_margin_mean = tf.constant(0.0, dtype=tf.float32)
            if has_qpair_margin:
                qpair_selected = ste_int8(selected, qpair_scale)
                qpair_correct = tf.gather(qpair_correct_tf, train_idx_tf)
                qpair_wrong = tf.gather(qpair_wrong_tf, train_idx_tf)
                qpair_weights = tf.gather(qpair_weights_tf, train_idx_tf)
                qpair_correct_dist = tf.reduce_sum(tf.square(qpair_selected - qpair_correct), axis=1)
                qpair_wrong_dist = tf.reduce_sum(tf.square(qpair_selected - qpair_wrong), axis=1)
                qpair_margin = qpair_wrong_dist - qpair_correct_dist
                raw_qpair_margin_loss = tf.nn.softplus(qpair_margin_alpha * (qpair_margin_target - qpair_margin))
                qpair_margin_loss = weighted_mean(raw_qpair_margin_loss, qpair_weights)
                qpair_margin_mean = weighted_mean(qpair_margin, qpair_weights)
            else:
                qpair_margin_loss = tf.constant(0.0, dtype=tf.float32)
                qpair_margin_mean = tf.constant(0.0, dtype=tf.float32)
            if has_dynamic_qpair_margin:
                q_all = ste_int8(embeddings, dynamic_qpair_scale)
                dynamic_query = tf.gather(q_all, dynamic_query_tf)
                dynamic_correct = tf.gather(q_all, dynamic_correct_tf)
                dynamic_wrong = tf.gather(q_all, dynamic_wrong_tf)
                dynamic_correct_dist = tf.reduce_sum(tf.square(dynamic_query - dynamic_correct), axis=1)
                dynamic_wrong_dist = tf.reduce_sum(tf.square(dynamic_query - dynamic_wrong), axis=1)
                dynamic_qpair_margin = dynamic_wrong_dist - dynamic_correct_dist
                raw_dynamic_qpair_loss = tf.nn.softplus(
                    dynamic_qpair_margin_alpha * (dynamic_qpair_margin_target - dynamic_qpair_margin)
                )
                dynamic_qpair_loss = weighted_mean(raw_dynamic_qpair_loss, dynamic_weights_tf)
                dynamic_qpair_margin_mean = weighted_mean(dynamic_qpair_margin, dynamic_weights_tf)
            else:
                dynamic_qpair_loss = tf.constant(0.0, dtype=tf.float32)
                dynamic_qpair_margin_mean = tf.constant(0.0, dtype=tf.float32)
            loss = (
                proxy_loss
                + lambda_d4 * d4_loss
                + stress_weight * stress_loss
                + lambda_var * var_loss
                + lambda_cov * cov_loss
                + lambda_norm * norm_loss
                + compiled_weight
                * (lambda_compiled_margin * compiled_margin_loss + lambda_compiled_pull * compiled_pull_loss)
                + qcompiled_weight * lambda_qcompiled_margin * qcompiled_margin_loss
                + qpair_weight * lambda_qpair_margin * qpair_margin_loss
                + dynamic_qpair_weight * lambda_dynamic_qpair_margin * dynamic_qpair_loss
                + qproxy_weight * lambda_qproxy_margin * qproxy_margin_loss
            )
        grads = tape.gradient(loss, train_vars)
        optimizer.apply_gradients(zip(grads, train_vars))
        pred = tf.argmax(logits, axis=1, output_type=tf.int32)
        acc = tf.reduce_mean(tf.cast(pred == y_selected, tf.float32))
        return (
            loss,
            proxy_loss,
            d4_loss,
            stress_loss,
            var_loss,
            cov_loss,
            norm_loss,
            norm_mean,
            compiled_margin_loss,
            compiled_pull_loss,
            qcompiled_margin_loss,
            qcompiled_margin_mean,
            qpair_margin_loss,
            qpair_margin_mean,
            dynamic_qpair_loss,
            dynamic_qpair_margin_mean,
            qproxy_margin_loss,
            qproxy_margin_mean,
            acc,
        )

    for epoch in range(1, epochs + 1):
        full_stage = epoch > warmup_epochs
        train_idx_tf = train_idx_full_tf if full_stage else train_idx_warmup_tf
        stress_weight = tf.constant(lambda_stress if full_stage else 0.0, dtype=tf.float32)
        compiled_weight = tf.constant(1.0 if epoch >= teacher_start_epoch else 0.0, dtype=tf.float32)
        qcompiled_weight = tf.constant(1.0 if epoch >= qcompiled_start_epoch else 0.0, dtype=tf.float32)
        qpair_weight = tf.constant(1.0 if epoch >= qpair_start_epoch else 0.0, dtype=tf.float32)
        dynamic_qpair_weight = tf.constant(1.0 if epoch >= dynamic_qpair_start_epoch else 0.0, dtype=tf.float32)
        qproxy_weight = tf.constant(1.0 if epoch >= qproxy_start_epoch else 0.0, dtype=tf.float32)
        (
            loss,
            proxy_loss,
            d4_loss,
            stress_loss,
            var_loss,
            cov_loss,
            norm_loss,
            norm_mean,
            compiled_margin_loss,
            compiled_pull_loss,
            qcompiled_margin_loss,
            qcompiled_margin_mean,
            qpair_margin_loss,
            qpair_margin_mean,
            dynamic_qpair_loss,
            dynamic_qpair_margin_mean,
            qproxy_margin_loss,
            qproxy_margin_mean,
            acc,
        ) = train_step(
            train_idx_tf,
            stress_weight,
            compiled_weight,
            qcompiled_weight,
            qpair_weight,
            dynamic_qpair_weight,
            qproxy_weight,
        )
        if epoch == 1 or epoch == warmup_epochs or epoch == epochs or epoch % max(1, epochs // 20) == 0:
            row = {
                "epoch": epoch,
                "loss": float(loss.numpy()),
                "proxy_loss": float(proxy_loss.numpy()),
                "d4_loss": float(d4_loss.numpy()),
                "stress_loss": float(stress_loss.numpy()),
                "var_loss": float(var_loss.numpy()),
                "cov_loss": float(cov_loss.numpy()),
                "norm_loss": float(norm_loss.numpy()),
                "norm_mean": float(norm_mean.numpy()),
                "compiled_margin_loss": float(compiled_margin_loss.numpy()),
                "compiled_pull_loss": float(compiled_pull_loss.numpy()),
                "compiled_weight": float(compiled_weight.numpy()),
                "qcompiled_margin_loss": float(qcompiled_margin_loss.numpy()),
                "qcompiled_margin_mean": float(qcompiled_margin_mean.numpy()),
                "qcompiled_weight": float(qcompiled_weight.numpy()),
                "qpair_margin_loss": float(qpair_margin_loss.numpy()),
                "qpair_margin_mean": float(qpair_margin_mean.numpy()),
                "qpair_weight": float(qpair_weight.numpy()),
                "dynamic_qpair_margin_loss": float(dynamic_qpair_loss.numpy()),
                "dynamic_qpair_margin_mean": float(dynamic_qpair_margin_mean.numpy()),
                "dynamic_qpair_weight": float(dynamic_qpair_weight.numpy()),
                "qproxy_margin_loss": float(qproxy_margin_loss.numpy()),
                "qproxy_margin_mean": float(qproxy_margin_mean.numpy()),
                "qproxy_weight": float(qproxy_weight.numpy()),
                "train_subclass_accuracy": float(acc.numpy()),
                "full_stage": bool(full_stage),
            }
            log_rows.append(row)
            print(json.dumps({"phaseB_train": row}, ensure_ascii=False), flush=True)

    write_csv(output_dir / "training_log.csv", log_rows)
    embeddings_np = model.predict(images, batch_size=256, verbose=0).astype(np.float32)
    return {"embedding_float": embeddings_np, "subclass_proxies": proxies.numpy().astype(np.float32)}


def run_prototype_sweep(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    output_dir: Path,
    transform_name: str,
    prototype_sources: list[str],
    k_values: list[int],
    quant_scales: list[float],
    seed: int,
    extra_payload: dict[str, np.ndarray],
    true_loo_top: int = 0,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    for source in prototype_sources:
        for k_value in k_values:
            row, payload = evaluate_prototype_candidate(
                embeddings=embeddings,
                flat=flat,
                transform_name=transform_name,
                source=source,
                k_per_subclass=k_value,
                seed=seed,
                quant_scales=quant_scales,
            )
            row["stage"] = "b_end_to_end_embedding"
            rows.append(row)
            payload.update(extra_payload)
            payload["transform_kind"] = np.asarray(transform_name)
            payloads.append(payload)
    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: row_score(item[0]), reverse=True)
    true_loo_rows: list[dict[str, Any]] = []
    true_loo_events: list[dict[str, Any]] = []
    for rank, (row, payload) in enumerate(sorted_pairs[: max(0, true_loo_top)], start=1):
        loo_summary, events = true_rebuild_loo(
            embeddings=np.asarray(payload["embedding_float"], dtype=np.float64),
            flat=flat,
            row=row,
            seed=seed,
        )
        row["true_loo_rank"] = rank
        row.update(loo_summary)
        true_loo_rows.append(dict(row))
        for event in events:
            event.update(
                {
                    "true_loo_rank": rank,
                    "prototype_source": row["prototype_source"],
                    "k_per_subclass": row["k_per_subclass"],
                    "prototype_count": row["prototype_count"],
                    "int8_scale": row["int8_scale"],
                }
            )
            true_loo_events.append(event)
    rows_sorted = [row for row, _payload in sorted_pairs]
    write_csv(output_dir / "candidate_results.csv", rows_sorted)
    if true_loo_rows:
        write_csv(output_dir / "true_rebuild_loo_top.csv", true_loo_rows)
        write_csv(output_dir / "true_rebuild_loo_events.csv", true_loo_events)
    if rows_sorted:
        np.savez_compressed(output_dir / "best_v8_embedding_prototype_params.npz", **sorted_pairs[0][1])
        write_csv(output_dir / "best_stress_summary.csv", json.loads(str(rows_sorted[0]["per_view_json"])))
    return rows_sorted


def main() -> None:
    parser = argparse.ArgumentParser(description="Train V8 Phase B end-to-end pure embedding backbone.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--filters", default="5,10,20")
    parser.add_argument("--embedding-dim", type=int, default=16)
    parser.add_argument("--seed", type=int, default=20260519)
    parser.add_argument("--epochs", type=int, default=900)
    parser.add_argument("--warmup-epochs", type=int, default=250)
    parser.add_argument("--learning-rate", type=float, default=0.0015)
    parser.add_argument("--l2", type=float, default=1.0e-4)
    parser.add_argument("--dropout", type=float, default=0.003)
    parser.add_argument("--first-kernel", type=int, default=3)
    parser.add_argument("--embedding-output-mode", choices=["l2", "raw"], default="l2")
    parser.add_argument(
        "--backbone-architecture",
        choices=[
            "spacetodepth_conv",
            "depthwise_pool",
            "spacetodepth_depthwise",
            "spacetodepth_hybrid",
            "double_spacetodepth_conv",
        ],
        default="spacetodepth_conv",
    )
    parser.add_argument("--activation", choices=["relu", "relu6"], default="relu6")
    parser.add_argument("--pool", choices=["max", "avg"], default="max")
    parser.add_argument("--extra-conv", action="store_true")
    parser.add_argument(
        "--input-transform",
        default="identity",
        choices=["identity", "lowpass", "edge", "low_edge", "raw_low_edge"],
        help="Optional fixed Conv2D input transform for frequency/edge diagnostics.",
    )
    parser.add_argument("--proxy-scale", type=float, default=16.0)
    parser.add_argument("--no-proxy-normalize-embeddings", action="store_false", dest="proxy_normalize_embeddings")
    parser.add_argument("--metric-normalize-embeddings", action="store_true")
    parser.add_argument("--lambda-d4", type=float, default=1.0)
    parser.add_argument("--lambda-stress", type=float, default=0.25)
    parser.add_argument("--lambda-var", type=float, default=0.25)
    parser.add_argument("--lambda-cov", type=float, default=0.02)
    parser.add_argument("--lambda-norm", type=float, default=0.0)
    parser.add_argument("--norm-target", type=float, default=1.0)
    parser.add_argument("--variance-floor", type=float, default=0.08)
    parser.add_argument("--init-model", type=Path, default=None)
    parser.add_argument("--compiled-teacher-npz", type=Path, default=None)
    parser.add_argument("--lambda-compiled-margin", type=float, default=0.0)
    parser.add_argument("--lambda-compiled-pull", type=float, default=0.0)
    parser.add_argument("--compiled-margin-target", type=float, default=0.02)
    parser.add_argument("--compiled-margin-alpha", type=float, default=32.0)
    parser.add_argument("--teacher-start-epoch", type=int, default=1)
    parser.add_argument("--lambda-qcompiled-margin", type=float, default=0.0)
    parser.add_argument("--qcompiled-margin-target", type=float, default=128.0)
    parser.add_argument("--qcompiled-margin-alpha", type=float, default=0.02)
    parser.add_argument("--qcompiled-scale", type=float, default=64.0)
    parser.add_argument("--qcompiled-start-epoch", type=int, default=1)
    parser.add_argument(
        "--qcompiled-weight-mode",
        choices=["uniform", "low_margin_extra", "low_margin_only"],
        default="uniform",
    )
    parser.add_argument("--low-margin-teacher-npz", type=Path, default=None)
    parser.add_argument("--low-margin-threshold", type=int, default=8)
    parser.add_argument("--low-margin-extra-weight", type=float, default=0.0)
    parser.add_argument("--qpair-teacher-npz", type=Path, default=None)
    parser.add_argument("--lambda-qpair-margin", type=float, default=0.0)
    parser.add_argument("--qpair-margin-target", type=float, default=128.0)
    parser.add_argument("--qpair-margin-alpha", type=float, default=0.02)
    parser.add_argument("--qpair-scale", type=float, default=64.0)
    parser.add_argument("--qpair-start-epoch", type=int, default=1)
    parser.add_argument("--dynamic-qpair-teacher-npz", type=Path, default=None)
    parser.add_argument("--lambda-dynamic-qpair-margin", type=float, default=0.0)
    parser.add_argument("--dynamic-qpair-margin-target", type=float, default=128.0)
    parser.add_argument("--dynamic-qpair-margin-alpha", type=float, default=0.02)
    parser.add_argument("--dynamic-qpair-scale", type=float, default=64.0)
    parser.add_argument("--dynamic-qpair-start-epoch", type=int, default=1)
    parser.add_argument("--lambda-qproxy-margin", type=float, default=0.0)
    parser.add_argument("--qproxy-margin-target", type=float, default=128.0)
    parser.add_argument("--qproxy-margin-alpha", type=float, default=0.02)
    parser.add_argument("--qproxy-scale", type=float, default=64.0)
    parser.add_argument("--qproxy-start-epoch", type=int, default=1)
    parser.add_argument("--init-proxies-from-embeddings", action="store_true")
    parser.add_argument("--stress", default="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04")
    parser.add_argument("--prototype-sources", default="medoid,kmeans")
    parser.add_argument("--k-values", default="1,2,4,8,16")
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--true-loo-top", type=int, default=0)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    filters = parse_filters(args.filters)
    stress_names = parse_csv(args.stress)
    flat, images = build_view_dataset(args.dataset_dir, stress_names)
    if args.init_model is not None:
        model = build_embedding_model(
            filters=filters,
            embedding_dim=args.embedding_dim,
            learning_rate=args.learning_rate,
            l2=args.l2,
            dropout=args.dropout,
            first_kernel=args.first_kernel,
            embedding_output_mode=args.embedding_output_mode,
            backbone_architecture=args.backbone_architecture,
            activation=args.activation,
            pool=args.pool,
            extra_conv=args.extra_conv,
            input_transform=args.input_transform,
        )
        model.load_weights(args.init_model)
        model.optimizer = tf.keras.optimizers.Adam(learning_rate=args.learning_rate)  # type: ignore[attr-defined]
    else:
        model = build_embedding_model(
            filters=filters,
            embedding_dim=args.embedding_dim,
            learning_rate=args.learning_rate,
            l2=args.l2,
            dropout=args.dropout,
            first_kernel=args.first_kernel,
            embedding_output_mode=args.embedding_output_mode,
            backbone_architecture=args.backbone_architecture,
            activation=args.activation,
            pool=args.pool,
            extra_conv=args.extra_conv,
            input_transform=args.input_transform,
        )
    compiled_teacher = (
        load_compiled_teacher(args.compiled_teacher_npz, args.embedding_dim) if args.compiled_teacher_npz is not None else None
    )
    qproxy_weights = load_low_margin_weights(
        args.low_margin_teacher_npz,
        flat,
        threshold=args.low_margin_threshold,
        extra_weight=args.low_margin_extra_weight,
    )
    qpair_teacher = load_qpair_teacher(args.qpair_teacher_npz, flat, embedding_dim=args.embedding_dim)
    dynamic_qpair_teacher = load_dynamic_qpair_teacher(args.dynamic_qpair_teacher_npz, flat)
    config = {
        "dataset_dir": str(args.dataset_dir),
        "filters": list(filters),
        "embedding_dim": args.embedding_dim,
        "seed": args.seed,
        "epochs": args.epochs,
        "warmup_epochs": args.warmup_epochs,
        "learning_rate": args.learning_rate,
        "l2": args.l2,
        "dropout": args.dropout,
        "first_kernel": args.first_kernel,
        "embedding_output_mode": args.embedding_output_mode,
        "backbone_architecture": args.backbone_architecture,
        "input_transform": args.input_transform,
        "activation": args.activation,
        "pool": args.pool,
        "extra_conv": args.extra_conv,
        "strict_recommended_raw_tflite_ops": STRICT_RECOMMENDED_RAW_TFLITE_OPS
        if args.embedding_output_mode == "raw"
        else None,
        "proxy_scale": args.proxy_scale,
        "proxy_normalize_embeddings": args.proxy_normalize_embeddings,
        "metric_normalize_embeddings": args.metric_normalize_embeddings,
        "lambda_d4": args.lambda_d4,
        "lambda_stress": args.lambda_stress,
        "lambda_var": args.lambda_var,
        "lambda_cov": args.lambda_cov,
        "lambda_norm": args.lambda_norm,
        "norm_target": args.norm_target,
        "variance_floor": args.variance_floor,
        "init_model": str(args.init_model) if args.init_model is not None else None,
        "compiled_teacher_npz": str(args.compiled_teacher_npz) if args.compiled_teacher_npz is not None else None,
        "lambda_compiled_margin": args.lambda_compiled_margin,
        "lambda_compiled_pull": args.lambda_compiled_pull,
        "compiled_margin_target": args.compiled_margin_target,
        "compiled_margin_alpha": args.compiled_margin_alpha,
        "teacher_start_epoch": args.teacher_start_epoch,
        "lambda_qcompiled_margin": args.lambda_qcompiled_margin,
        "qcompiled_margin_target": args.qcompiled_margin_target,
        "qcompiled_margin_alpha": args.qcompiled_margin_alpha,
        "qcompiled_scale": args.qcompiled_scale,
        "qcompiled_start_epoch": args.qcompiled_start_epoch,
        "qcompiled_weight_mode": args.qcompiled_weight_mode,
        "low_margin_teacher_npz": str(args.low_margin_teacher_npz) if args.low_margin_teacher_npz is not None else None,
        "low_margin_threshold": args.low_margin_threshold,
        "low_margin_extra_weight": args.low_margin_extra_weight,
        "low_margin_weighted_rows": int(np.sum(qproxy_weights > 1.0)),
        "qpair_teacher_npz": str(args.qpair_teacher_npz) if args.qpair_teacher_npz is not None else None,
        "qpair_active_rows": int(np.sum(qpair_teacher["weights"] > 0.0)),
        "lambda_qpair_margin": args.lambda_qpair_margin,
        "qpair_margin_target": args.qpair_margin_target,
        "qpair_margin_alpha": args.qpair_margin_alpha,
        "qpair_scale": args.qpair_scale,
        "qpair_start_epoch": args.qpair_start_epoch,
        "dynamic_qpair_teacher_npz": str(args.dynamic_qpair_teacher_npz) if args.dynamic_qpair_teacher_npz is not None else None,
        "dynamic_qpair_active_rows": int(np.sum(dynamic_qpair_teacher["weights"] > 0.0)),
        "lambda_dynamic_qpair_margin": args.lambda_dynamic_qpair_margin,
        "dynamic_qpair_margin_target": args.dynamic_qpair_margin_target,
        "dynamic_qpair_margin_alpha": args.dynamic_qpair_margin_alpha,
        "dynamic_qpair_scale": args.dynamic_qpair_scale,
        "dynamic_qpair_start_epoch": args.dynamic_qpair_start_epoch,
        "lambda_qproxy_margin": args.lambda_qproxy_margin,
        "qproxy_margin_target": args.qproxy_margin_target,
        "qproxy_margin_alpha": args.qproxy_margin_alpha,
        "qproxy_scale": args.qproxy_scale,
        "qproxy_start_epoch": args.qproxy_start_epoch,
        "init_proxies_from_embeddings": args.init_proxies_from_embeddings,
        "stress": stress_names,
        "true_loo_top": args.true_loo_top,
    }
    (args.output_dir / "train_config.json").write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")
    trained = train_model(
        model=model,
        flat=flat,
        images=images,
        output_dir=args.output_dir,
        embedding_dim=args.embedding_dim,
        seed=args.seed,
        epochs=args.epochs,
        warmup_epochs=args.warmup_epochs,
        proxy_scale=args.proxy_scale,
        lambda_d4=args.lambda_d4,
        lambda_stress=args.lambda_stress,
        lambda_var=args.lambda_var,
        lambda_cov=args.lambda_cov,
        variance_floor=args.variance_floor,
        compiled_teacher=compiled_teacher,
        lambda_compiled_margin=args.lambda_compiled_margin,
        lambda_compiled_pull=args.lambda_compiled_pull,
        compiled_margin_target=args.compiled_margin_target,
        compiled_margin_alpha=args.compiled_margin_alpha,
        teacher_start_epoch=args.teacher_start_epoch,
        lambda_qcompiled_margin=args.lambda_qcompiled_margin,
        qcompiled_margin_target=args.qcompiled_margin_target,
        qcompiled_margin_alpha=args.qcompiled_margin_alpha,
        qcompiled_scale=args.qcompiled_scale,
        qcompiled_start_epoch=args.qcompiled_start_epoch,
        qcompiled_weight_mode=args.qcompiled_weight_mode,
        qproxy_weights=qproxy_weights,
        qpair_teacher=qpair_teacher,
        lambda_qpair_margin=args.lambda_qpair_margin,
        qpair_margin_target=args.qpair_margin_target,
        qpair_margin_alpha=args.qpair_margin_alpha,
        qpair_scale=args.qpair_scale,
        qpair_start_epoch=args.qpair_start_epoch,
        dynamic_qpair_teacher=dynamic_qpair_teacher,
        lambda_dynamic_qpair_margin=args.lambda_dynamic_qpair_margin,
        dynamic_qpair_margin_target=args.dynamic_qpair_margin_target,
        dynamic_qpair_margin_alpha=args.dynamic_qpair_margin_alpha,
        dynamic_qpair_scale=args.dynamic_qpair_scale,
        dynamic_qpair_start_epoch=args.dynamic_qpair_start_epoch,
        lambda_qproxy_margin=args.lambda_qproxy_margin,
        qproxy_margin_target=args.qproxy_margin_target,
        qproxy_margin_alpha=args.qproxy_margin_alpha,
        qproxy_scale=args.qproxy_scale,
        qproxy_start_epoch=args.qproxy_start_epoch,
        init_proxies_from_embeddings=args.init_proxies_from_embeddings,
        proxy_normalize_embeddings=args.proxy_normalize_embeddings,
        metric_normalize_embeddings=args.metric_normalize_embeddings,
        lambda_norm=args.lambda_norm,
        norm_target=args.norm_target,
    )
    model.save(args.output_dir / "embedding_model.keras")
    np.savez_compressed(args.output_dir / "embedding_cache.npz", **trained)
    rows = run_prototype_sweep(
        embeddings=trained["embedding_float"],
        flat=flat,
        output_dir=args.output_dir,
        transform_name=f"phaseB_f{'-'.join(map(str, filters))}_d{args.embedding_dim}_seed{args.seed}",
        prototype_sources=parse_csv(args.prototype_sources),
        k_values=parse_ints(args.k_values),
        quant_scales=parse_floats(args.quant_scales),
        seed=args.seed,
        extra_payload=trained,
        true_loo_top=args.true_loo_top,
    )
    summary = {
        "stage": "b_end_to_end_embedding",
        "candidate_count": len(rows),
        "true_loo_top": args.true_loo_top,
        "true_loo_top_results": [row for row in rows if "true_loo_rank" in row],
        "best": rows[0] if rows else None,
        "top20": rows[:20],
        "config": config,
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"best": summary["best"], "candidate_count": len(rows)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
