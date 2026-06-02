import argparse
import csv
import json
import math
import os
import random
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_XLA_FLAGS", "--tf_xla_auto_jit=0")

import tensorflow as tf

from train_tiny32_sixclass_scan import (
    AUGMENTS,
    IMAGE_SIZE,
    PARENT_NAMES,
    SUBCLASS_NAMES,
    SUBCLASS_TO_PARENT,
    ModelConfig,
    config_to_dict,
    estimate_board_latency_us,
    evaluate_arrays,
    build_model as build_reference_model,
    load_dataset,
    metrics_from_confusion,
    set_reproducible,
    stratified_folds,
    stress_batch,
)


BASELINE_MODEL = Path(
    "experiments/v3_fine_fast_20260507_081935/final_exports/"
    "rank02_v3fine_fast_grid_f8-16-32_d0_do0_l20.0003_lr0.002_b32_max_k3_x1_v3_lowres_noise/"
    "sixclass_best.keras"
)

DEFAULT_STRESSES = [
    "rot90",
    "rot180",
    "rot270",
    "mirror_lr",
    "mirror_lr_rot90",
    "mirror_lr_rot180",
    "mirror_lr_rot270",
    "noise_0p06",
    "hblur5_noise_0p06",
    "diagblur5_noise_0p08",
    "noise_0p10",
]

PARENT_FROM_SUBCLASS = SUBCLASS_TO_PARENT.astype(np.int32)


@dataclass(frozen=True)
class Strategy:
    name: str
    phase: str
    source: str = "scratch"
    output: str = "subclass"
    dual_head: bool = False
    logits: bool = False
    activation: str = "relu"
    fake_quant: bool = False
    fake_quant_weights: bool = False
    equalize: bool = False
    bias_correction: bool = False
    distill: bool = False
    calibration: str = "balanced_clean"
    quant_mode: str = "int8"
    learning_rate: float = 0.002
    fine_tune_lr: float = 0.00025
    distill_alpha: float = 0.35
    distill_temperature: float = 3.0
    parent_loss_weight: float = 1.0
    subclass_loss_weight: float = 0.35


@tf.keras.utils.register_keras_serializable(package="TinyQuant")
class FixedFakeQuant(tf.keras.layers.Layer):
    def __init__(
        self,
        min_val: float = 0.0,
        max_val: float = 6.0,
        num_bits: int = 8,
        narrow_range: bool = False,
        **kwargs,
    ) -> None:
        super().__init__(**kwargs)
        self.min_val = float(min_val)
        self.max_val = float(max_val)
        self.num_bits = int(num_bits)
        self.narrow_range = bool(narrow_range)

    def call(self, inputs: tf.Tensor) -> tf.Tensor:
        return tf.quantization.fake_quant_with_min_max_vars(
            inputs,
            min=self.min_val,
            max=self.max_val,
            num_bits=self.num_bits,
            narrow_range=self.narrow_range,
        )

    def get_config(self) -> dict[str, object]:
        config = super().get_config()
        config.update(
            {
                "min_val": self.min_val,
                "max_val": self.max_val,
                "num_bits": self.num_bits,
                "narrow_range": self.narrow_range,
            }
        )
        return config


def baseline_config(name: str = "baseline") -> ModelConfig:
    return ModelConfig(
        name=name,
        filters=(8, 16, 32),
        dense_units=0,
        dropout=0.0,
        l2=3.0e-4,
        learning_rate=0.002,
        batch_size=32,
        pool="max",
        first_kernel=3,
        extra_conv=True,
        augment=AUGMENTS["v3_lowres_noise"],
        architecture="spacetodepth_conv",
        activation="relu",
        train_transforms="rot_mirror",
    )


def strategy_table() -> list[Strategy]:
    return [
        Strategy("A_audit_existing_default_calib", phase="audit", source="existing", calibration="balanced_clean"),
        Strategy("D_calib_first192", phase="calibration", source="existing", calibration="first192"),
        Strategy("D_calib_balanced_rotmirror", phase="calibration", source="existing", calibration="balanced_rotmirror"),
        Strategy("D_calib_mild_stress", phase="calibration", source="existing", calibration="mild_stress"),
        Strategy("D_calib_aggressive_stress", phase="calibration", source="existing", calibration="aggressive_stress"),
        Strategy("H_equalize_bias_existing", phase="post", source="existing", equalize=True, bias_correction=True),
        Strategy("I_weight_round_existing", phase="post", source="existing", fake_quant_weights=True),
        Strategy("J_int16x8_existing", phase="availability", source="existing", quant_mode="int16x8"),
        Strategy("B_qat_finetune_fakequant", phase="single", source="finetune", fake_quant=True, learning_rate=0.002),
        Strategy("B_qat_scratch_fakequant", phase="single", source="scratch", fake_quant=True, learning_rate=0.002),
        Strategy("C_logits_no_softmax", phase="single", source="scratch", logits=True, learning_rate=0.002),
        Strategy("E_relu6_clipping", phase="single", source="scratch", activation="relu6", learning_rate=0.002),
        Strategy("F_dual_head_parent_deploy", phase="single", source="scratch", dual_head=True, output="parent", learning_rate=0.002),
        Strategy(
            "G_qat_distill_finetune",
            phase="single",
            source="finetune",
            fake_quant=True,
            distill=True,
            logits=True,
            learning_rate=0.001,
            fine_tune_lr=0.00018,
            distill_alpha=0.45,
        ),
        Strategy(
            "BCE_logits_relu6_qat",
            phase="combo_seed",
            source="scratch",
            logits=True,
            activation="relu6",
            fake_quant=True,
            learning_rate=0.002,
        ),
        Strategy(
            "BCGH_logits_qat_distill_bias",
            phase="combo_seed",
            source="finetune",
            logits=True,
            fake_quant=True,
            distill=True,
            equalize=True,
            bias_correction=True,
            calibration="balanced_rotmirror",
            learning_rate=0.001,
            fine_tune_lr=0.00018,
            distill_alpha=0.45,
        ),
        Strategy(
            "FGE_dual_parent_relu6_qat_distill",
            phase="combo_seed",
            source="scratch",
            output="parent",
            dual_head=True,
            activation="relu6",
            fake_quant=True,
            distill=True,
            calibration="balanced_rotmirror",
            learning_rate=0.0015,
            distill_alpha=0.35,
        ),
        Strategy(
            "CEHI_logits_relu6_round_bias",
            phase="combo_seed",
            source="scratch",
            logits=True,
            activation="relu6",
            fake_quant_weights=True,
            equalize=True,
            bias_correction=True,
            calibration="mild_stress",
            learning_rate=0.002,
        ),
        Strategy(
            "BD_qat_finetune_mild_calib",
            phase="combo_followup",
            source="finetune",
            fake_quant=True,
            calibration="mild_stress",
            learning_rate=0.002,
            fine_tune_lr=0.00018,
        ),
        Strategy(
            "BD_qat_finetune_rotmirror_calib",
            phase="combo_followup",
            source="finetune",
            fake_quant=True,
            calibration="balanced_rotmirror",
            learning_rate=0.002,
            fine_tune_lr=0.00018,
        ),
        Strategy(
            "BCD_logits_qat_finetune_mild",
            phase="combo_followup",
            source="finetune",
            logits=True,
            fake_quant=True,
            calibration="mild_stress",
            learning_rate=0.001,
            fine_tune_lr=0.00018,
            distill_alpha=0.35,
        ),
        Strategy(
            "BGD_qat_distill_mild_calib",
            phase="combo_followup",
            source="finetune",
            logits=True,
            fake_quant=True,
            distill=True,
            calibration="mild_stress",
            learning_rate=0.001,
            fine_tune_lr=0.00018,
            distill_alpha=0.45,
        ),
        Strategy(
            "BDE_qat_finetune_relu6_mild",
            phase="combo_followup",
            source="finetune",
            activation="relu6",
            fake_quant=True,
            calibration="mild_stress",
            learning_rate=0.0015,
            fine_tune_lr=0.00018,
        ),
        Strategy(
            "CD_logits_mild_calib",
            phase="combo_followup",
            source="scratch",
            logits=True,
            calibration="mild_stress",
            learning_rate=0.002,
        ),
    ]


def strategy_by_name() -> dict[str, Strategy]:
    return {strategy.name: strategy for strategy in strategy_table()}


def parent_metrics(parent_true: np.ndarray, parent_preds: np.ndarray) -> dict[str, object]:
    matrix = np.zeros((len(PARENT_NAMES), len(PARENT_NAMES)), dtype=np.int64)
    for y, pred in zip(parent_true.tolist(), parent_preds.tolist()):
        matrix[int(y), int(pred)] += 1
    return metrics_from_confusion(matrix, PARENT_NAMES)


def expand_rotation_mirror_with_parent(
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    images: list[np.ndarray] = []
    subclass_labels: list[int] = []
    parent_labels: list[int] = []
    for image, sub, parent in zip(x, y_sub, y_parent):
        for k in range(4):
            rotated = np.rot90(image, k, axes=(0, 1))
            images.append(rotated)
            subclass_labels.append(int(sub))
            parent_labels.append(int(parent))
            images.append(np.flip(rotated, axis=1))
            subclass_labels.append(int(sub))
            parent_labels.append(int(parent))
    return (
        np.stack(images).astype(np.float32),
        np.asarray(subclass_labels, dtype=np.int64),
        np.asarray(parent_labels, dtype=np.int64),
    )


def augment_image_np(image: np.ndarray, rng: np.random.Generator, level: str) -> np.ndarray:
    cur = image.copy()
    if level in {"mild", "aggressive"}:
        if rng.random() < (0.35 if level == "mild" else 0.65):
            stress = rng.choice(["noise_0p06", "hblur5_noise_0p06", "diagblur5_noise_0p08", "noise_0p10"])
            cur = stress_batch(str(stress), cur[None, ...])[0]
        noise = 0.025 if level == "mild" else 0.07
        cur = np.clip(cur + rng.normal(0.0, noise, cur.shape).astype(np.float32), 0.0, 1.0)
    return cur.astype(np.float32)


def balanced_indices(labels: np.ndarray, limit: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    per_class = max(1, int(math.ceil(limit / len(np.unique(labels)))))
    selected: list[int] = []
    for label in sorted(set(labels.tolist())):
        indexes = np.where(labels == label)[0]
        rng.shuffle(indexes)
        selected.extend(indexes[:per_class].tolist())
    rng.shuffle(selected)
    return np.asarray(selected[:limit], dtype=np.int64)


def representative_array(
    x: np.ndarray,
    y_sub: np.ndarray,
    strategy: str,
    seed: int,
    limit: int,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    if strategy == "first192":
        return x[: min(limit, len(x))].astype(np.float32)
    if strategy == "balanced_clean":
        return x[balanced_indices(y_sub, min(limit, len(x)), seed)].astype(np.float32)
    if strategy == "balanced_rotmirror":
        idx = balanced_indices(y_sub, min(max(1, limit // 8), len(x)), seed)
        expanded, _, _ = expand_rotation_mirror_with_parent(x[idx], y_sub[idx], SUBCLASS_TO_PARENT[y_sub[idx]])
        return expanded[:limit].astype(np.float32)
    if strategy == "mild_stress":
        idx = balanced_indices(y_sub, min(limit, len(x)), seed)
        return np.stack([augment_image_np(image, rng, "mild") for image in x[idx]]).astype(np.float32)
    if strategy == "aggressive_stress":
        idx = balanced_indices(y_sub, min(limit, len(x)), seed)
        return np.stack([augment_image_np(image, rng, "aggressive") for image in x[idx]]).astype(np.float32)
    raise ValueError(f"unknown calibration strategy: {strategy}")


def representative_dataset(samples: np.ndarray) -> Callable[[], object]:
    def gen():
        for index in range(len(samples)):
            yield [samples[index : index + 1].astype(np.float32)]

    return gen


def make_train_dataset(
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    indexes: np.ndarray,
    batch_size: int,
    seed: int,
    dual_head: bool,
    parent_only: bool,
    training: bool,
) -> tf.data.Dataset:
    x_data = x[indexes]
    y_sub_data = y_sub[indexes]
    y_parent_data = y_parent[indexes]
    if training:
        x_data, y_sub_data, y_parent_data = expand_rotation_mirror_with_parent(x_data, y_sub_data, y_parent_data)
    if dual_head:
        labels = {"parent": y_parent_data, "subclass": y_sub_data}
    elif parent_only:
        labels = y_parent_data
    else:
        labels = y_sub_data
    ds = tf.data.Dataset.from_tensor_slices((x_data, labels))
    if training:
        ds = ds.shuffle(len(x_data), seed=seed, reshuffle_each_iteration=True)
        augment = AUGMENTS["v3_lowres_noise"]

        def mapped(image, label):
            from train_tiny32_sixclass_scan import augment_image

            return augment_image(image, augment), label

        ds = ds.map(mapped, num_parallel_calls=tf.data.AUTOTUNE)
    return ds.batch(batch_size).prefetch(tf.data.AUTOTUNE)


def activation_layer(x: tf.Tensor, strategy: Strategy, name: str) -> tf.Tensor:
    if strategy.activation == "relu6" or strategy.fake_quant:
        x = tf.keras.layers.ReLU(max_value=6.0, name=f"{name}_relu6")(x)
    elif strategy.activation == "relu":
        x = tf.keras.layers.ReLU(name=f"{name}_relu")(x)
    else:
        raise ValueError(f"unknown activation: {strategy.activation}")
    if strategy.fake_quant:
        x = FixedFakeQuant(0.0, 6.0, name=f"{name}_fq")(x)
    return x


def build_student_model(config: ModelConfig, strategy: Strategy) -> tf.keras.Model:
    regularizer = tf.keras.regularizers.l2(config.l2) if config.l2 > 0 else None
    inputs = tf.keras.Input((IMAGE_SIZE, IMAGE_SIZE, 1), name="gray32")
    x = inputs
    if strategy.fake_quant:
        x = FixedFakeQuant(0.0, 1.0, name="input_fq")(x)
    if config.architecture == "spacetodepth_conv":
        x = tf.keras.layers.Lambda(
            lambda t: tf.nn.space_to_depth(t, 2),
            output_shape=(IMAGE_SIZE // 2, IMAGE_SIZE // 2, 4),
            name="space_to_depth",
        )(x)
    for i, filters in enumerate(config.filters):
        kernel = config.first_kernel if i == 0 else 3
        name = f"block_{i + 1}"
        x = tf.keras.layers.Conv2D(
            filters,
            kernel_size=kernel,
            padding="same",
            use_bias=True,
            kernel_regularizer=regularizer,
            name=f"{name}_conv",
        )(x)
        x = activation_layer(x, strategy, name)
        if config.extra_conv and i == 2:
            x = tf.keras.layers.Conv2D(
                filters,
                kernel_size=3,
                padding="same",
                use_bias=True,
                kernel_regularizer=regularizer,
                name="block_3_extra_conv",
            )(x)
            x = activation_layer(x, strategy, "block_3_extra")
        if i < 2:
            if config.pool == "avg":
                x = tf.keras.layers.AveragePooling2D(2, name=f"{name}_avg_pool")(x)
            else:
                x = tf.keras.layers.MaxPooling2D(2, name=f"{name}_max_pool")(x)
    x = tf.keras.layers.GlobalAveragePooling2D(name="gap")(x)
    if config.dense_units > 0:
        x = tf.keras.layers.Dense(
            config.dense_units,
            use_bias=True,
            kernel_regularizer=regularizer,
            name="dense",
        )(x)
        x = activation_layer(x, strategy, "dense")
    if config.dropout > 0:
        x = tf.keras.layers.Dropout(config.dropout, name="dropout")(x)

    subclass_logits = tf.keras.layers.Dense(len(SUBCLASS_NAMES), activation=None, name="subclass_logits")(x)
    if not strategy.logits:
        subclass_out = tf.keras.layers.Softmax(name="subclass")(subclass_logits)
    else:
        subclass_out = tf.keras.layers.Activation("linear", name="subclass")(subclass_logits)

    if strategy.dual_head:
        parent_logits = tf.keras.layers.Dense(len(PARENT_NAMES), activation=None, name="parent_logits")(x)
        if strategy.logits:
            parent_out = tf.keras.layers.Activation("linear", name="parent")(parent_logits)
        else:
            parent_out = tf.keras.layers.Softmax(name="parent")(parent_logits)
        return tf.keras.Model(inputs, {"parent": parent_out, "subclass": subclass_out}, name=f"tiny32_{strategy.name}")

    if strategy.output == "parent":
        parent_logits = tf.keras.layers.Dense(len(PARENT_NAMES), activation=None, name="parent_logits")(x)
        if strategy.logits:
            outputs = tf.keras.layers.Activation("linear", name="parent")(parent_logits)
        else:
            outputs = tf.keras.layers.Softmax(name="parent")(parent_logits)
        return tf.keras.Model(inputs, outputs, name=f"tiny32_{strategy.name}")

    return tf.keras.Model(inputs, subclass_out, name=f"tiny32_{strategy.name}")


def deploy_model(model: tf.keras.Model, strategy: Strategy) -> tf.keras.Model:
    if strategy.dual_head:
        return tf.keras.Model(model.input, model.get_layer("parent").output, name=f"{model.name}_deploy_parent")
    return model


def compile_model(model: tf.keras.Model, strategy: Strategy) -> None:
    if strategy.dual_head:
        losses = {
            "parent": tf.keras.losses.SparseCategoricalCrossentropy(from_logits=strategy.logits),
            "subclass": tf.keras.losses.SparseCategoricalCrossentropy(from_logits=strategy.logits),
        }
        model.compile(
            optimizer=tf.keras.optimizers.Adam(strategy.learning_rate),
            loss=losses,
            loss_weights={"parent": strategy.parent_loss_weight, "subclass": strategy.subclass_loss_weight},
            jit_compile=False,
        )
    else:
        model.compile(
            optimizer=tf.keras.optimizers.Adam(strategy.learning_rate),
            loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=strategy.logits),
            metrics=["accuracy"],
            jit_compile=False,
        )


def matching_weight_transfer(target: tf.keras.Model, source: tf.keras.Model) -> list[str]:
    source_by_name = {layer.name: layer for layer in source.layers}
    loaded: list[str] = []
    for layer in target.layers:
        source_layer = source_by_name.get(layer.name)
        if source_layer is None:
            continue
        src_weights = source_layer.get_weights()
        dst_weights = layer.get_weights()
        if len(src_weights) != len(dst_weights):
            continue
        if all(src.shape == dst.shape for src, dst in zip(src_weights, dst_weights)):
            layer.set_weights(src_weights)
            loaded.append(layer.name)
    if "subclass_logits" not in loaded and "subclass_probs" in source_by_name:
        try:
            target_layer = target.get_layer("subclass_logits")
        except ValueError:
            target_layer = None
        if target_layer is not None:
            src_weights = source_by_name["subclass_probs"].get_weights()
            dst_weights = target_layer.get_weights()
            if len(src_weights) == len(dst_weights) and all(
                src.shape == dst.shape for src, dst in zip(src_weights, dst_weights)
            ):
                target_layer.set_weights(src_weights)
                loaded.append("subclass_logits<-subclass_probs")
    return loaded


def quantize_weights_in_place(model: tf.keras.Model) -> None:
    for layer in model.layers:
        if not isinstance(layer, (tf.keras.layers.Conv2D, tf.keras.layers.Dense)):
            continue
        weights = layer.get_weights()
        if not weights:
            continue
        kernel = weights[0].astype(np.float32)
        if isinstance(layer, tf.keras.layers.Conv2D):
            axes = tuple(range(kernel.ndim - 1))
            max_abs = np.max(np.abs(kernel), axis=axes, keepdims=True)
        else:
            max_abs = np.max(np.abs(kernel), axis=0, keepdims=True)
        scale = np.maximum(max_abs / 127.0, 1.0e-8)
        weights[0] = np.round(kernel / scale) * scale
        layer.set_weights(weights)


def conv_like_layers(model: tf.keras.Model) -> list[tf.keras.layers.Layer]:
    return [layer for layer in model.layers if isinstance(layer, tf.keras.layers.Conv2D)]


def equalize_conv_pairs_in_place(model: tf.keras.Model) -> None:
    layers = conv_like_layers(model)
    for left, right in zip(layers[:-1], layers[1:]):
        left_weights = left.get_weights()
        right_weights = right.get_weights()
        if not left_weights or not right_weights:
            continue
        w1 = left_weights[0].astype(np.float32)
        w2 = right_weights[0].astype(np.float32)
        if w1.shape[-1] != w2.shape[-2]:
            continue
        b1 = left_weights[1].astype(np.float32) if len(left_weights) > 1 else None
        left_range = np.max(np.abs(w1), axis=(0, 1, 2))
        if b1 is not None:
            left_range = np.maximum(left_range, np.abs(b1))
        right_range = np.max(np.abs(w2), axis=(0, 1, 3))
        scale = np.sqrt(np.maximum(right_range, 1.0e-8) / np.maximum(left_range, 1.0e-8))
        scale = np.clip(scale, 0.25, 4.0).astype(np.float32)
        w1 *= scale.reshape((1, 1, 1, -1))
        if b1 is not None:
            b1 *= scale
        w2 /= scale.reshape((1, 1, -1, 1))
        left_weights[0] = w1
        if b1 is not None:
            left_weights[1] = b1
        right_weights[0] = w2
        left.set_weights(left_weights)
        right.set_weights(right_weights)


def output_layer_for_bias(model: tf.keras.Model) -> tf.keras.layers.Layer | None:
    for name in ["parent_logits", "subclass_logits"]:
        try:
            layer = model.get_layer(name)
            if isinstance(layer, tf.keras.layers.Dense):
                return layer
        except ValueError:
            continue
    dense_layers = [layer for layer in model.layers if isinstance(layer, tf.keras.layers.Dense)]
    return dense_layers[-1] if dense_layers else None


def tflite_outputs(model_path: Path, x: np.ndarray) -> np.ndarray:
    interp = tf.lite.Interpreter(model_path=str(model_path), num_threads=1)
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]
    values: list[np.ndarray] = []
    for image in x:
        data = image[None, ...].astype(np.float32)
        if inp["dtype"] in (np.int8, np.uint8, np.int16):
            scale, zero = inp["quantization"]
            data = data / scale + zero if scale > 0 else data
        interp.set_tensor(inp["index"], data.astype(inp["dtype"]))
        interp.invoke()
        raw = interp.get_tensor(out["index"])[0]
        if out["dtype"] in (np.int8, np.uint8, np.int16):
            scale, zero = out["quantization"]
            raw = (raw.astype(np.float32) - zero) * scale if scale > 0 else raw.astype(np.float32)
        values.append(raw.astype(np.float32))
    return np.stack(values).astype(np.float32)


def keras_outputs(model: tf.keras.Model, x: np.ndarray, batch_size: int) -> np.ndarray:
    out = model.predict(x, batch_size=batch_size, verbose=0)
    if isinstance(out, dict):
        key = "parent" if "parent" in out else sorted(out)[0]
        out = out[key]
    if isinstance(out, list):
        out = out[0]
    return np.asarray(out, dtype=np.float32)


def export_float_tflite(model: tf.keras.Model, path: Path) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    data = converter.convert()
    path.write_bytes(data)
    return len(data)


def export_quant_tflite(
    model: tf.keras.Model,
    samples: np.ndarray,
    path: Path,
    quant_mode: str,
) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset(samples)
    if quant_mode == "int16x8":
        converter.target_spec.supported_ops = [
            tf.lite.OpsSet.EXPERIMENTAL_TFLITE_BUILTINS_ACTIVATIONS_INT16_WEIGHTS_INT8
        ]
        converter.inference_input_type = tf.int16
        converter.inference_output_type = tf.int16
    else:
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
    data = converter.convert()
    path.write_bytes(data)
    return len(data)


def evaluate_prediction_set(
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    preds: np.ndarray,
    output: str,
) -> dict[str, object]:
    if output == "parent":
        return {"parent": parent_metrics(y_parent, preds)}
    return evaluate_arrays(y_sub, y_parent, preds)


def score_parent(result: dict[str, object], all_result: dict[str, object], estimated_us: int) -> float:
    parent = result["parent"]
    all_parent = all_result["parent"]
    speed_score = max(0.0, min(1.0, (25000.0 - estimated_us) / 25000.0))
    return float(
        0.36 * float(parent["accuracy"])
        + 0.26 * float(parent["worst_recall"])
        + 0.18 * float(parent["macro_recall"])
        + 0.12 * float(all_parent["accuracy"])
        + 0.08 * float(all_parent["worst_recall"])
        + 0.10 * speed_score
    )


def train_with_distillation(
    model: tf.keras.Model,
    teacher: tf.keras.Model,
    train_ds: tf.data.Dataset,
    val_ds: tf.data.Dataset,
    strategy: Strategy,
    epochs: int,
    patience: int,
    output: str,
) -> dict[str, list[float]]:
    optimizer = tf.keras.optimizers.Adam(strategy.fine_tune_lr if strategy.source == "finetune" else strategy.learning_rate)
    hard_loss = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=strategy.logits)
    kl_loss = tf.keras.losses.KLDivergence()
    temperature = float(strategy.distill_temperature)
    alpha = float(strategy.distill_alpha)
    best_weights = model.get_weights()
    best_val = float("inf")
    wait = 0
    history = {"loss": [], "val_loss": []}
    for _epoch in range(epochs):
        losses: list[float] = []
        for xb, yb in train_ds:
            if isinstance(yb, dict):
                hard_labels = yb["parent"] if output == "parent" else yb["subclass"]
            else:
                hard_labels = yb
            with tf.GradientTape() as tape:
                student_out = model(xb, training=True)
                if isinstance(student_out, dict):
                    student_logits = student_out["parent"] if output == "parent" else student_out["subclass"]
                else:
                    student_logits = student_out
                teacher_probs = teacher(xb, training=False)
                if output == "parent":
                    parent_map = tf.constant(PARENT_FROM_SUBCLASS, dtype=tf.int32)
                    teacher_soft = tf.stack(
                        [
                            tf.reduce_sum(tf.boolean_mask(teacher_probs, parent_map == parent_index, axis=1), axis=1)
                            for parent_index in range(len(PARENT_NAMES))
                        ],
                        axis=1,
                    )
                else:
                    teacher_logits = tf.math.log(tf.clip_by_value(teacher_probs, 1.0e-7, 1.0))
                    teacher_soft = tf.nn.softmax(teacher_logits / temperature)
                if strategy.logits:
                    student_soft = tf.nn.softmax(student_logits / temperature)
                else:
                    student_soft = tf.nn.softmax(tf.math.log(tf.clip_by_value(student_logits, 1.0e-7, 1.0)) / temperature)
                loss = (1.0 - alpha) * hard_loss(hard_labels, student_logits) + alpha * (temperature**2) * kl_loss(
                    teacher_soft, student_soft
                )
                if isinstance(student_out, dict) and "subclass" in student_out and output == "parent":
                    loss += strategy.subclass_loss_weight * hard_loss(yb["subclass"], student_out["subclass"])
            grads = tape.gradient(loss, model.trainable_variables)
            optimizer.apply_gradients(zip(grads, model.trainable_variables))
            losses.append(float(loss.numpy()))
        val_losses: list[float] = []
        for xb, yb in val_ds:
            hard_labels = yb["parent"] if isinstance(yb, dict) and output == "parent" else yb
            if isinstance(yb, dict) and output != "parent":
                hard_labels = yb["subclass"]
            val_out = model(xb, training=False)
            if isinstance(val_out, dict):
                val_logits = val_out["parent"] if output == "parent" else val_out["subclass"]
            else:
                val_logits = val_out
            val_losses.append(float(hard_loss(hard_labels, val_logits).numpy()))
        train_loss = float(np.mean(losses)) if losses else 0.0
        val_loss = float(np.mean(val_losses)) if val_losses else train_loss
        history["loss"].append(train_loss)
        history["val_loss"].append(val_loss)
        if val_loss < best_val - 1.0e-4:
            best_val = val_loss
            best_weights = model.get_weights()
            wait = 0
        else:
            wait += 1
        if wait >= patience:
            break
    model.set_weights(best_weights)
    return history


def train_standard(
    model: tf.keras.Model,
    train_ds: tf.data.Dataset,
    val_ds: tf.data.Dataset,
    strategy: Strategy,
    epochs: int,
    patience: int,
) -> dict[str, list[float]]:
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_loss",
            patience=patience,
            restore_best_weights=True,
            min_delta=1.0e-4,
        ),
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss",
            factor=0.5,
            patience=max(2, patience // 2),
            min_lr=1.0e-5,
            verbose=0,
        ),
    ]
    history = model.fit(train_ds, validation_data=val_ds, epochs=epochs, callbacks=callbacks, verbose=0)
    return {key: [float(v) for v in values] for key, values in history.history.items()}


def bias_correct_final_layer(
    model: tf.keras.Model,
    tflite_path: Path,
    calibration_x: np.ndarray,
    batch_size: int,
) -> bool:
    layer = output_layer_for_bias(model)
    if layer is None:
        return False
    weights = layer.get_weights()
    if len(weights) < 2:
        return False
    float_out = keras_outputs(model, calibration_x, batch_size)
    quant_out = tflite_outputs(tflite_path, calibration_x)
    if float_out.shape != quant_out.shape:
        return False
    correction = np.mean(float_out - quant_out, axis=0).astype(np.float32)
    weights[1] = weights[1].astype(np.float32) + correction
    layer.set_weights(weights)
    return True


def load_teacher(path: Path) -> tf.keras.Model | None:
    if not path.exists():
        return None
    return tf.keras.models.load_model(path, compile=False, safe_mode=False)


def load_rebuilt_reference(path: Path, config: ModelConfig) -> tf.keras.Model | None:
    source = load_teacher(path)
    if source is None:
        return None
    rebuilt = build_reference_model(config)
    matching_weight_transfer(rebuilt, source)
    return rebuilt


def run_case(
    strategy: Strategy,
    round_index: int,
    args: argparse.Namespace,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    output_dir: Path,
    teacher: tf.keras.Model | None,
) -> dict[str, object]:
    stable_strategy_hash = sum((index + 1) * ord(char) for index, char in enumerate(strategy.name)) % 997
    seed = args.seed + round_index * 1009 + stable_strategy_hash
    set_reproducible(seed)
    tf.keras.backend.clear_session()
    config = baseline_config(strategy.name)
    config = ModelConfig(
        name=config.name,
        filters=config.filters,
        dense_units=config.dense_units,
        dropout=config.dropout,
        l2=config.l2,
        learning_rate=strategy.learning_rate,
        batch_size=config.batch_size,
        pool=config.pool,
        first_kernel=config.first_kernel,
        extra_conv=config.extra_conv,
        augment=config.augment,
        architecture=config.architecture,
        activation=strategy.activation,
        train_transforms=config.train_transforms,
    )
    folds = stratified_folds(y_sub, 5, seed + 4242)
    test_idx = folds[0]
    val_idx = folds[1]
    train_idx = np.concatenate(folds[2:]).astype(np.int64)
    parent_only = strategy.output == "parent" and not strategy.dual_head

    case_dir = output_dir / "cases" / f"round{round_index:02d}_{strategy.name}"
    case_dir.mkdir(parents=True, exist_ok=True)
    model_path = case_dir / "model.keras"
    deploy_path = case_dir / "deploy.keras"
    train_history: dict[str, list[float]] = {"loss": [], "val_loss": []}
    transferred_layers: list[str] = []
    loaded_existing = False

    if strategy.source == "existing":
        model = load_rebuilt_reference(args.base_model, config)
        if model is None:
            raise FileNotFoundError(args.base_model)
        loaded_existing = True
        deploy = model
    else:
        model = build_student_model(config, strategy)
        if strategy.source == "finetune":
            source = load_rebuilt_reference(args.base_model, config)
            if source is not None:
                transferred_layers = matching_weight_transfer(model, source)
        compile_model(model, strategy)
        train_ds = make_train_dataset(
            x, y_sub, y_parent, train_idx, config.batch_size, seed, strategy.dual_head, parent_only, True
        )
        val_ds = make_train_dataset(
            x, y_sub, y_parent, val_idx, config.batch_size, seed, strategy.dual_head, parent_only, False
        )
        if strategy.distill and teacher is not None:
            train_history = train_with_distillation(
                model,
                teacher,
                train_ds,
                val_ds,
                strategy,
                args.epochs,
                args.patience,
                "parent" if strategy.output == "parent" else "subclass",
            )
        else:
            if strategy.source == "finetune":
                model.optimizer.learning_rate.assign(strategy.fine_tune_lr)
            train_history = train_standard(model, train_ds, val_ds, strategy, args.epochs, args.patience)
        model.save(model_path)
        deploy = deploy_model(model, strategy)
        deploy.save(deploy_path)

    if strategy.equalize:
        equalize_conv_pairs_in_place(deploy)
    if strategy.fake_quant_weights:
        quantize_weights_in_place(deploy)

    calibration_x = representative_array(x, y_sub, strategy.calibration, seed + 7, args.calibration_limit)
    float_path = case_dir / "model_float.tflite"
    quant_path = case_dir / f"model_{strategy.quant_mode}.tflite"
    float_bytes = export_float_tflite(deploy, float_path)
    quant_bytes = 0
    quant_error = ""
    try:
        quant_bytes = export_quant_tflite(deploy, calibration_x, quant_path, strategy.quant_mode)
    except Exception as exc:  # noqa: BLE001
        quant_error = f"{type(exc).__name__}: {exc}"

    bias_corrected = False
    if strategy.bias_correction and not quant_error:
        bias_corrected = bias_correct_final_layer(deploy, quant_path, calibration_x, config.batch_size)
        if bias_corrected:
            quant_bytes = export_quant_tflite(deploy, calibration_x, quant_path, strategy.quant_mode)

    output_kind = "parent" if strategy.output == "parent" or strategy.dual_head else "subclass"
    keras_test_out = keras_outputs(deploy, x[test_idx], config.batch_size)
    float_test_out = tflite_outputs(float_path, x[test_idx])
    keras_all_out = keras_outputs(deploy, x, config.batch_size)
    float_all_out = tflite_outputs(float_path, x)
    keras_test_preds = np.argmax(keras_test_out, axis=1).astype(np.int64)
    float_test_preds = np.argmax(float_test_out, axis=1).astype(np.int64)
    keras_all_preds = np.argmax(keras_all_out, axis=1).astype(np.int64)
    float_all_preds = np.argmax(float_all_out, axis=1).astype(np.int64)
    keras_test = evaluate_prediction_set(y_sub[test_idx], y_parent[test_idx], keras_test_preds, output_kind)
    float_test = evaluate_prediction_set(y_sub[test_idx], y_parent[test_idx], float_test_preds, output_kind)
    keras_all = evaluate_prediction_set(y_sub, y_parent, keras_all_preds, output_kind)
    float_all = evaluate_prediction_set(y_sub, y_parent, float_all_preds, output_kind)

    quant_test: dict[str, object] | None = None
    quant_all: dict[str, object] | None = None
    quant_test_preds: np.ndarray | None = None
    quant_all_preds: np.ndarray | None = None
    if not quant_error:
        quant_test_out = tflite_outputs(quant_path, x[test_idx])
        quant_all_out = tflite_outputs(quant_path, x)
        quant_test_preds = np.argmax(quant_test_out, axis=1).astype(np.int64)
        quant_all_preds = np.argmax(quant_all_out, axis=1).astype(np.int64)
        quant_test = evaluate_prediction_set(y_sub[test_idx], y_parent[test_idx], quant_test_preds, output_kind)
        quant_all = evaluate_prediction_set(y_sub, y_parent, quant_all_preds, output_kind)

    stress_results: dict[str, object] = {}
    for stress_name in args.stresses:
        xs = stress_batch(stress_name, x[test_idx])
        keras_stress = np.argmax(keras_outputs(deploy, xs, config.batch_size), axis=1).astype(np.int64)
        item: dict[str, object] = {
            "keras": evaluate_prediction_set(y_sub[test_idx], y_parent[test_idx], keras_stress, output_kind)
        }
        if not quant_error:
            quant_stress = np.argmax(tflite_outputs(quant_path, xs), axis=1).astype(np.int64)
            item["quant"] = evaluate_prediction_set(y_sub[test_idx], y_parent[test_idx], quant_stress, output_kind)
        stress_results[stress_name] = item

    estimated_us = estimate_board_latency_us(config)
    final_score = (
        score_parent(quant_test, quant_all, estimated_us) if quant_test is not None and quant_all is not None else 0.0
    )
    quant_loss_acc = 0.0
    quant_loss_worst = 0.0
    if quant_test is not None:
        quant_loss_acc = float(keras_test["parent"]["accuracy"]) - float(quant_test["parent"]["accuracy"])
        quant_loss_worst = float(keras_test["parent"]["worst_recall"]) - float(quant_test["parent"]["worst_recall"])

    result = {
        "case_id": f"round{round_index:02d}:{strategy.name}",
        "round": round_index,
        "strategy": asdict(strategy),
        "config": config_to_dict(config),
        "status": "ok" if not quant_error else "quant_export_failed",
        "quant_error": quant_error,
        "loaded_existing": loaded_existing,
        "transferred_layers": transferred_layers,
        "bias_corrected": bias_corrected,
        "epochs": len(train_history.get("loss", [])),
        "split_counts": {"train": int(len(train_idx)), "val": int(len(val_idx)), "test": int(len(test_idx))},
        "export": {
            "float_path": str(float_path),
            "float_bytes": float_bytes,
            "quant_path": str(quant_path) if not quant_error else "",
            "quant_bytes": quant_bytes,
            "quant_mode": strategy.quant_mode,
            "calibration": strategy.calibration,
            "output_kind": output_kind,
        },
        "keras_test": keras_test,
        "float_tflite_test": float_test,
        "quant_test": quant_test,
        "keras_all": keras_all,
        "float_tflite_all": float_all,
        "quant_all": quant_all,
        "agreement": {
            "keras_vs_float_test": float(np.mean(keras_test_preds == float_test_preds)),
            "keras_vs_float_all": float(np.mean(keras_all_preds == float_all_preds)),
            "float_vs_quant_test": float(np.mean(float_test_preds == quant_test_preds)) if quant_test_preds is not None else 0.0,
            "keras_vs_quant_test": float(np.mean(keras_test_preds == quant_test_preds)) if quant_test_preds is not None else 0.0,
            "keras_vs_quant_all": float(np.mean(keras_all_preds == quant_all_preds)) if quant_all_preds is not None else 0.0,
        },
        "quant_loss": {
            "test_parent_accuracy": quant_loss_acc,
            "test_parent_worst_recall": quant_loss_worst,
        },
        "stress": stress_results,
        "estimated_board_us": estimated_us,
        "final_score": final_score,
        "case_dir": str(case_dir),
    }
    (case_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result


def load_existing_results(path: Path) -> tuple[list[dict[str, object]], set[str]]:
    results: list[dict[str, object]] = []
    seen: set[str] = set()
    if not path.exists():
        return results, seen
    for raw in path.read_bytes().splitlines():
        line = raw.replace(b"\x00", b"").strip()
        if not line:
            continue
        try:
            item = json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        results.append(item)
        seen.add(str(item.get("case_id", "")))
    return results, seen


def aggregate_results(results: list[dict[str, object]]) -> dict[str, object]:
    by_strategy: dict[str, list[dict[str, object]]] = {}
    for result in results:
        name = result["strategy"]["name"]
        by_strategy.setdefault(name, []).append(result)

    strategy_rows = []
    for name, items in by_strategy.items():
        ok_items = [item for item in items if item.get("status") == "ok" and item.get("quant_test")]
        if not ok_items:
            strategy_rows.append({"strategy": name, "runs": len(items), "ok_runs": 0, "mean_score": 0.0})
            continue
        strategy_rows.append(
            {
                "strategy": name,
                "phase": ok_items[0]["strategy"]["phase"],
                "runs": len(items),
                "ok_runs": len(ok_items),
                "mean_score": float(np.mean([float(item["final_score"]) for item in ok_items])),
                "min_score": float(np.min([float(item["final_score"]) for item in ok_items])),
                "mean_int8_test_acc": float(
                    np.mean([float(item["quant_test"]["parent"]["accuracy"]) for item in ok_items])
                ),
                "mean_int8_test_worst": float(
                    np.mean([float(item["quant_test"]["parent"]["worst_recall"]) for item in ok_items])
                ),
                "mean_quant_loss_acc": float(
                    np.mean([float(item["quant_loss"]["test_parent_accuracy"]) for item in ok_items])
                ),
                "mean_agreement": float(
                    np.mean([float(item["agreement"]["keras_vs_quant_test"]) for item in ok_items])
                ),
                "mean_bytes": float(np.mean([float(item["export"]["quant_bytes"]) for item in ok_items])),
                "case_dirs": [item["case_dir"] for item in ok_items],
            }
        )
    strategy_rows.sort(
        key=lambda row: (
            float(row.get("mean_score", 0.0)),
            float(row.get("mean_int8_test_worst", 0.0)),
            float(row.get("mean_int8_test_acc", 0.0)),
        ),
        reverse=True,
    )
    return {"strategies": strategy_rows, "best": strategy_rows[0] if strategy_rows else None}


def write_csv_summary(path: Path, rows: list[dict[str, object]]) -> None:
    fieldnames = [
        "strategy",
        "phase",
        "runs",
        "ok_runs",
        "mean_score",
        "min_score",
        "mean_int8_test_acc",
        "mean_int8_test_worst",
        "mean_quant_loss_acc",
        "mean_agreement",
        "mean_bytes",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def select_strategies(plan: str, explicit: str) -> list[Strategy]:
    table = strategy_table()
    if explicit:
        mapping = strategy_by_name()
        return [mapping[name.strip()] for name in explicit.split(",") if name.strip()]
    if plan == "audit":
        return [strategy for strategy in table if strategy.phase in {"audit", "calibration", "post", "availability"}]
    if plan == "single":
        return [strategy for strategy in table if strategy.phase in {"audit", "calibration", "post", "availability", "single"}]
    if plan == "combo":
        return [strategy for strategy in table if strategy.phase in {"combo_seed", "combo_followup"}]
    if plan == "full":
        return table
    if plan == "smoke":
        return [
            strategy_by_name()["A_audit_existing_default_calib"],
            strategy_by_name()["C_logits_no_softmax"],
            strategy_by_name()["E_relu6_clipping"],
        ]
    raise ValueError(plan)


def main() -> None:
    parser = argparse.ArgumentParser(description="Quantization-loss strategy scan for tiny32 v3 board model.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--base-model", type=Path, default=BASELINE_MODEL)
    parser.add_argument("--plan", choices=["smoke", "audit", "single", "combo", "full"], default="full")
    parser.add_argument("--strategies", default="")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--epochs", type=int, default=160)
    parser.add_argument("--patience", type=int, default=24)
    parser.add_argument("--calibration-limit", type=int, default=192)
    parser.add_argument("--seed", type=int, default=20260601)
    parser.add_argument("--stress", default=",".join(DEFAULT_STRESSES))
    args = parser.parse_args()
    args.stresses = [name.strip() for name in args.stress.split(",") if name.strip()]

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime("v3_quant_strategy_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    results_path = output_dir / "trial_results.jsonl"

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    strategies = select_strategies(args.plan, args.strategies)
    rounds = 1 if args.plan in {"smoke", "audit"} else args.rounds
    run_config = {
        "args": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items() if key != "stresses"},
        "stresses": args.stresses,
        "sample_count": int(len(y_sub)),
        "subclass_names": SUBCLASS_NAMES,
        "parent_names": PARENT_NAMES,
        "subclass_to_parent": SUBCLASS_TO_PARENT.tolist(),
        "subclass_counts": {SUBCLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(SUBCLASS_NAMES))},
        "parent_counts": {PARENT_NAMES[i]: int(np.sum(y_parent == i)) for i in range(len(PARENT_NAMES))},
        "paths": paths,
        "strategies": [asdict(strategy) for strategy in strategies],
        "rounds": rounds,
        "baseline_model": str(args.base_model),
        "baseline_config": config_to_dict(baseline_config()),
    }
    (output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2, ensure_ascii=False), encoding="utf-8")

    existing_results, completed = load_existing_results(results_path) if args.resume else ([], set())
    results = list(existing_results)
    teacher = load_rebuilt_reference(args.base_model, baseline_config())
    if teacher is None:
        raise FileNotFoundError(f"missing teacher/base model: {args.base_model}")

    total = rounds * len(strategies)
    print(f"output_dir={output_dir}", flush=True)
    print(f"plan={args.plan} rounds={rounds} strategies={len(strategies)} total_cases={total}", flush=True)
    print("subclass_counts=" + json.dumps(run_config["subclass_counts"], ensure_ascii=False), flush=True)

    completed_count = len(completed)
    for round_index in range(rounds):
        for strategy in strategies:
            case_id = f"round{round_index:02d}:{strategy.name}"
            if case_id in completed:
                print(f"{case_id} skipped_existing", flush=True)
                continue
            started = time.time()
            try:
                result = run_case(strategy, round_index, args, x, y_sub, y_parent, output_dir, teacher)
            except Exception as exc:  # noqa: BLE001
                result = {
                    "case_id": case_id,
                    "round": round_index,
                    "strategy": asdict(strategy),
                    "status": "failed",
                    "error": f"{type(exc).__name__}: {exc}",
                    "final_score": 0.0,
                }
            result["seconds"] = round(time.time() - started, 3)
            with results_path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(result, ensure_ascii=False) + "\n")
                handle.flush()
                os.fsync(handle.fileno())
            results.append(result)
            completed_count += 1
            if result.get("status") == "ok":
                parent = result["quant_test"]["parent"]
                print(
                    f"[{completed_count:03d}/{total:03d}] {case_id} ok "
                    f"score={float(result['final_score']):.4f} "
                    f"acc={float(parent['accuracy']):.4f} worst={float(parent['worst_recall']):.4f} "
                    f"loss_acc={float(result['quant_loss']['test_parent_accuracy']):+.4f} "
                    f"bytes={result['export']['quant_bytes']} seconds={result['seconds']}",
                    flush=True,
                )
            else:
                print(f"[{completed_count:03d}/{total:03d}] {case_id} {result.get('status')} {result.get('error', result.get('quant_error', ''))}", flush=True)

            aggregate = aggregate_results(results)
            (output_dir / "summary.json").write_text(json.dumps(aggregate, indent=2, ensure_ascii=False), encoding="utf-8")
            write_csv_summary(output_dir / "strategy_summary.csv", aggregate["strategies"])

    aggregate = aggregate_results(results)
    (output_dir / "summary.json").write_text(json.dumps(aggregate, indent=2, ensure_ascii=False), encoding="utf-8")
    write_csv_summary(output_dir / "strategy_summary.csv", aggregate["strategies"])
    print("summary_path=" + str(output_dir / "summary.json"), flush=True)
    if aggregate["best"]:
        print("best_strategy=" + json.dumps(aggregate["best"], ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
