import argparse
import copy
import json
import math
import os
import random
import time
from dataclasses import asdict
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_XLA_FLAGS", "--tf_xla_auto_jit=0")

import tensorflow as tf

from train_tiny32_quant_strategy_scan import (
    export_float_tflite,
    export_quant_tflite,
    representative_array,
    tflite_outputs,
)
from train_tiny32_sixclass_scan import (
    AUGMENTS,
    PARENT_NAMES,
    SUBCLASS_NAMES,
    SUBCLASS_TO_PARENT,
    AugmentConfig,
    ModelConfig,
    build_model,
    config_to_dict,
    estimate_board_latency_us,
    evaluate_arrays,
    load_dataset,
    load_existing_results,
    make_dataset,
    predict_model,
    set_reproducible,
    stratified_folds,
    stress_batch,
    train_eval_config,
)
from train_tiny32_v3_fine_scan import DEFAULT_STRESS, safe_name, summarize_stress_min


CUSTOM_AUGMENTS = {
    "v4_mild_roi": AugmentConfig(
        "v4_mild_roi",
        pad=1,
        brightness=0.10,
        contrast_delta=0.16,
        noise_std=0.030,
        salt_pepper=0.004,
        blur_prob=0.20,
        blur_kernel=3,
        downscale_prob=0.32,
        downscale_min=18,
    ),
    "v4_speed_mild": AugmentConfig(
        "v4_speed_mild",
        pad=2,
        brightness=0.14,
        contrast_delta=0.22,
        noise_std=0.045,
        salt_pepper=0.006,
        blur_prob=0.30,
        blur_kernel=5,
        downscale_prob=0.38,
        downscale_min=17,
    ),
    "v4_lowres_mix": AugmentConfig(
        "v4_lowres_mix",
        pad=2,
        brightness=0.12,
        contrast_delta=0.18,
        noise_std=0.038,
        salt_pepper=0.006,
        blur_prob=0.24,
        blur_kernel=3,
        downscale_prob=0.56,
        downscale_min=14,
    ),
    "v4_highspeed": AugmentConfig(
        "v4_highspeed",
        pad=3,
        brightness=0.16,
        contrast_delta=0.24,
        noise_std=0.055,
        salt_pepper=0.010,
        blur_prob=0.42,
        blur_kernel=5,
        downscale_prob=0.34,
        downscale_min=18,
    ),
}

ALL_AUGMENTS = dict(AUGMENTS) | CUSTOM_AUGMENTS

V4_STRESS = (
    "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5"
)


def jsonable(value):
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, list):
        return [jsonable(item) for item in value]
    if isinstance(value, tuple):
        return [jsonable(item) for item in value]
    if isinstance(value, dict):
        return {str(key): jsonable(item) for key, item in value.items()}
    return value


def augment_by_name(name: str) -> AugmentConfig:
    return ALL_AUGMENTS[name]


def make_config(
    prefix: str,
    tag: str,
    architecture: str,
    filters: tuple[int, int, int],
    dense: int,
    dropout: float,
    l2: float,
    lr: float,
    batch: int,
    pool: str,
    kernel: int,
    extra: bool,
    augment: str,
    activation: str = "relu",
) -> ModelConfig:
    name = (
        f"v4_{prefix}_{tag}_{architecture}_f{'-'.join(map(str, filters))}_d{dense}_do{dropout:g}"
        f"_l2{l2:g}_lr{lr:g}_b{batch}_{pool}_k{kernel}_x{int(extra)}_{augment}_{activation}"
    )
    return ModelConfig(
        name=name,
        filters=filters,
        dense_units=dense,
        dropout=dropout,
        l2=l2,
        learning_rate=lr,
        batch_size=batch,
        pool=pool,
        first_kernel=kernel,
        extra_conv=extra,
        augment=augment_by_name(augment),
        architecture=architecture,
        activation=activation,
        train_transforms="rot_mirror",
    )


def semantic_key(config: ModelConfig) -> tuple[object, ...]:
    return (
        config.architecture,
        tuple(config.filters),
        config.dense_units,
        round(config.dropout, 5),
        round(config.l2, 10),
        round(config.learning_rate, 10),
        config.batch_size,
        config.pool,
        config.first_kernel,
        config.extra_conv,
        config.augment.name,
        config.activation,
    )


def config_priority(config: ModelConfig, lane: str, rng: random.Random) -> tuple[float, int, str]:
    latency = estimate_board_latency_us(config)
    target_latency = {
        "fast": 7800,
        "core": 9200,
        "accuracy": 12500,
        "arch": 10500,
        "fine": 9200,
    }.get(lane, 9200)
    filter_target = (8, 16, 32) if lane != "accuracy" else (10, 20, 40)
    filter_penalty = sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, filter_target)) / 3.0
    l2_target = 3.0e-4 if lane in {"core", "fast"} else 1.0e-4
    if config.l2 <= 0:
        l2_penalty = 0.08 if l2_target <= 1.0e-5 else 0.20
    else:
        l2_penalty = min(0.8, abs(math.log(config.l2 / l2_target)) / 3.2)
    dropout_penalty = config.dropout * (1.7 if lane == "fast" else 1.2)
    dense_penalty = 0.00 if config.dense_units == 0 else 0.07 + config.dense_units / 200.0
    extra_credit = -0.06 if config.extra_conv and lane != "fast" else (0.04 if config.extra_conv else 0.0)
    pool_penalty = 0.0 if config.pool == "max" else 0.06
    kernel_penalty = 0.0 if config.first_kernel == 3 else 0.12
    activation_penalty = 0.0 if config.activation == "relu" else 0.10
    arch_penalty = {
        "spacetodepth_conv": 0.0,
        "stride_conv": 0.10,
        "depthwise_pool": 0.12,
        "conv_pool": 0.18,
    }.get(config.architecture, 0.22)
    if lane == "arch":
        arch_penalty *= 0.25
    augment_penalty = {
        "v3_lowres_noise": 0.00,
        "v4_lowres_mix": 0.01,
        "v4_mild_roi": 0.02,
        "v3_speed_noise": 0.03,
        "v4_speed_mild": 0.03,
        "v4_highspeed": 0.06,
        "v3_aggressive_noise": 0.10,
    }.get(config.augment.name, 0.08)
    latency_penalty = abs(latency - target_latency) / 12000.0
    if lane == "fast":
        latency_penalty = max(0, latency - 9000) / 9000 + max(0, 6200 - latency) / 9000
    score = (
        filter_penalty
        + 0.40 * l2_penalty
        + dropout_penalty
        + dense_penalty
        + extra_credit
        + pool_penalty
        + kernel_penalty
        + activation_penalty
        + arch_penalty
        + augment_penalty
        + latency_penalty
        + rng.random() * 1.0e-4
    )
    return score, latency, config.name


def add_unique(configs: list[ModelConfig], seen: set[tuple[object, ...]], config: ModelConfig) -> None:
    key = semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    configs.append(config)


def anchor_configs(prefix: str) -> list[ModelConfig]:
    anchors = [
        ("rank02_best", "spacetodepth_conv", (8, 16, 32), 0, 0.0, 3.0e-4, 0.0020, 32, "max", 3, True, "v3_lowres_noise", "relu"),
        ("rank02_mild", "spacetodepth_conv", (8, 16, 32), 0, 0.0, 3.0e-4, 0.0020, 32, "max", 3, True, "v4_speed_mild", "relu"),
        ("speed_fallback", "spacetodepth_conv", (8, 16, 32), 16, 0.0, 0.0, 0.0025, 16, "max", 3, False, "v3_lowres_noise", "relu"),
        ("low_l2", "spacetodepth_conv", (8, 16, 32), 0, 0.0, 3.0e-5, 0.0020, 16, "max", 3, True, "v3_speed_noise", "relu"),
        ("accuracy_10_20_40", "spacetodepth_conv", (10, 20, 40), 0, 0.0, 1.0e-4, 0.0020, 32, "max", 3, True, "v4_lowres_mix", "relu"),
    ]
    return [make_config(prefix, *item) for item in anchors]


def broad_configs(lane: str, limit: int, seed: int) -> list[ModelConfig]:
    rng = random.Random(seed)
    configs: list[ModelConfig] = []
    seen: set[tuple[object, ...]] = set()
    for config in anchor_configs(lane):
        add_unique(configs, seen, config)

    if lane == "fast":
        archs = ["spacetodepth_conv", "stride_conv", "depthwise_pool"]
        filter_sets = [(5, 10, 20), (6, 12, 24), (7, 14, 28), (8, 16, 24), (8, 16, 32), (8, 16, 40)]
        dense_values = [0, 4, 8, 12, 16, 24]
        dropout_values = [0.0, 0.01, 0.03, 0.05, 0.08]
        l2_values = [0.0, 1.0e-6, 5.0e-6, 1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4]
        lr_values = [0.0016, 0.0020, 0.0023, 0.0026, 0.0030]
        batches = [16, 24, 32, 48]
        extras = [False, True]
        max_latency = 10500
        augments = ["v3_lowres_noise", "v4_mild_roi", "v4_speed_mild", "v3_speed_noise", "v4_lowres_mix"]
    elif lane == "accuracy":
        archs = ["spacetodepth_conv", "conv_pool", "stride_conv"]
        filter_sets = [(8, 16, 32), (8, 18, 36), (10, 20, 40), (10, 20, 48), (12, 24, 48), (16, 24, 48)]
        dense_values = [0, 4, 8, 12, 16, 24]
        dropout_values = [0.0, 0.02, 0.05, 0.08, 0.12, 0.20]
        l2_values = [0.0, 1.0e-6, 1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4, 6.0e-4]
        lr_values = [0.0012, 0.0016, 0.0020, 0.0023, 0.0026]
        batches = [16, 24, 32, 48]
        extras = [False, True]
        max_latency = 17000
        augments = ["v3_lowres_noise", "v4_lowres_mix", "v4_speed_mild", "v4_highspeed", "v3_speed_noise"]
    elif lane == "arch":
        archs = ["spacetodepth_conv", "stride_conv", "depthwise_pool", "conv_pool"]
        filter_sets = [(6, 12, 24), (8, 16, 32), (10, 20, 40), (12, 24, 48), (16, 24, 48)]
        dense_values = [0, 8, 16]
        dropout_values = [0.0, 0.05, 0.10, 0.20]
        l2_values = [0.0, 1.0e-5, 1.0e-4, 3.0e-4]
        lr_values = [0.0014, 0.0020, 0.0026]
        batches = [24, 32, 48]
        extras = [False, True]
        max_latency = 19000
        augments = ["v3_lowres_noise", "v4_speed_mild", "v4_lowres_mix", "v3_speed_noise"]
    else:
        archs = ["spacetodepth_conv"]
        filter_sets = [(6, 12, 24), (7, 14, 28), (8, 14, 32), (8, 16, 28), (8, 16, 32), (8, 16, 36), (8, 18, 32), (9, 16, 32), (10, 20, 40)]
        dense_values = [0, 4, 8, 12, 16]
        dropout_values = [0.0, 0.01, 0.02, 0.04, 0.06, 0.10]
        l2_values = [0.0, 1.0e-6, 5.0e-6, 1.0e-5, 2.0e-5, 3.0e-5, 5.0e-5, 1.0e-4, 2.0e-4, 3.0e-4, 5.0e-4]
        lr_values = [0.0015, 0.0018, 0.0020, 0.00215, 0.0023, 0.0025, 0.0028]
        batches = [16, 24, 32, 48]
        extras = [False, True]
        max_latency = 12500
        augments = ["v3_lowres_noise", "v4_mild_roi", "v4_speed_mild", "v4_lowres_mix", "v3_speed_noise"]

    candidates: list[ModelConfig] = []
    for arch in archs:
        for filters in filter_sets:
            for dense in dense_values:
                for dropout in dropout_values:
                    for l2 in l2_values:
                        for lr in lr_values:
                            for batch in batches:
                                for pool in ["max", "avg"]:
                                    for kernel in ([3, 5] if lane == "accuracy" else [3]):
                                        for extra in extras:
                                            for augment in augments:
                                                for activation in (["relu", "relu6"] if lane == "arch" else ["relu"]):
                                                    if arch == "stride_conv":
                                                        pool = "max"
                                                    config = make_config(
                                                        lane,
                                                        "broad",
                                                        arch,
                                                        filters,
                                                        dense,
                                                        dropout,
                                                        l2,
                                                        lr,
                                                        batch,
                                                        pool,
                                                        kernel,
                                                        extra,
                                                        augment,
                                                        activation,
                                                    )
                                                    latency = estimate_board_latency_us(config)
                                                    if 5500 <= latency <= max_latency:
                                                        candidates.append(config)
    rng.shuffle(candidates)
    candidates.sort(key=lambda config: config_priority(config, lane, rng))
    for config in candidates:
        add_unique(configs, seen, config)
        if len(configs) >= limit:
            break
    return configs[:limit]


def load_source_configs(paths: list[Path], top_n: int) -> list[dict[str, object]]:
    configs: list[dict[str, object]] = []
    for path in paths:
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        items = data.get("top_results", [])
        if data.get("final_exports"):
            final_items = sorted(
                data["final_exports"],
                key=lambda item: float(item.get("final_score", 0.0)),
                reverse=True,
            )
            items = final_items + items
        for item in items[:top_n]:
            cfg = item.get("config") or item.get("search", {}).get("config")
            if isinstance(cfg, dict):
                configs.append(cfg)
    return configs


def dict_to_model_config(data: dict[str, object], name: str) -> ModelConfig:
    augment_data = data["augment"]
    augment_name = str(augment_data["name"]) if isinstance(augment_data, dict) else str(augment_data)
    return ModelConfig(
        name=name,
        filters=tuple(int(v) for v in data["filters"]),
        dense_units=int(data["dense_units"]),
        dropout=float(data["dropout"]),
        l2=float(data["l2"]),
        learning_rate=float(data["learning_rate"]),
        batch_size=int(data["batch_size"]),
        pool=str(data["pool"]),
        first_kernel=int(data["first_kernel"]),
        extra_conv=bool(data["extra_conv"]),
        augment=augment_by_name(augment_name),
        architecture=str(data.get("architecture", "spacetodepth_conv")),
        activation=str(data.get("activation", "relu")),
        train_transforms=str(data.get("train_transforms", "rot_mirror")),
    )


def fine_configs(summary_paths: list[Path], limit: int, seed: int) -> list[ModelConfig]:
    rng = random.Random(seed)
    base_dicts = load_source_configs(summary_paths, top_n=18)
    if not base_dicts:
        return broad_configs("core", limit, seed)

    configs: list[ModelConfig] = []
    seen: set[tuple[object, ...]] = set()
    for base_index, data in enumerate(base_dicts):
        base = dict_to_model_config(data, f"fine_base_{base_index:02d}")
        add_unique(configs, seen, base)
        filter_options = {
            base.filters,
            tuple(max(4, v - 2) for v in base.filters),
            tuple(v + 2 for v in base.filters),
            (base.filters[0], max(8, base.filters[1] + 2), base.filters[2]),
            (base.filters[0], base.filters[1], max(16, base.filters[2] + 4)),
            (8, 16, 32),
            (10, 20, 40),
        }
        dense_options = sorted({base.dense_units, 0, 4, 8, 12, 16})
        dropout_options = sorted({0.0, base.dropout, min(0.12, base.dropout + 0.02), max(0.0, base.dropout - 0.02), 0.04})
        if base.l2 <= 0:
            l2_options = [0.0, 1.0e-6, 5.0e-6, 1.0e-5, 3.0e-5, 1.0e-4]
        else:
            l2_options = sorted({0.0, base.l2, base.l2 * 0.5, base.l2 * 0.75, base.l2 * 1.25, base.l2 * 1.5, 3.0e-4})
        lr_options = sorted({base.learning_rate * 0.75, base.learning_rate * 0.9, base.learning_rate, base.learning_rate * 1.1, base.learning_rate * 1.25, 0.0020})
        batch_options = sorted({base.batch_size, 16, 24, 32})
        augment_options = sorted({base.augment.name, "v3_lowres_noise", "v4_mild_roi", "v4_speed_mild", "v4_lowres_mix", "v3_speed_noise"})
        candidates: list[ModelConfig] = []
        local_seen: set[tuple[object, ...]] = set()
        target_per_base = max(10, limit // max(1, len(base_dicts)) + 4)
        pool_cap = max(72, target_per_base * 12)

        def maybe_add(
            filters: tuple[int, int, int],
            dense: int,
            dropout: float,
            l2: float,
            lr: float,
            batch: int,
            pool: str,
            extra: bool,
            augment: str,
        ) -> None:
            if len(candidates) >= pool_cap:
                return
            config = make_config(
                "fine",
                f"near{base_index:02d}",
                base.architecture,
                tuple(int(v) for v in filters),
                dense,
                float(dropout),
                float(l2),
                float(lr),
                int(batch),
                pool,
                base.first_kernel,
                bool(extra),
                augment,
                base.activation,
            )
            latency = estimate_board_latency_us(config)
            if not 6000 <= latency <= 14000:
                return
            key = semantic_key(config)
            if key in local_seen:
                return
            local_seen.add(key)
            candidates.append(config)

        base_values = (
            base.filters,
            base.dense_units,
            base.dropout,
            base.l2,
            base.learning_rate,
            base.batch_size,
            base.pool,
            base.extra_conv,
            base.augment.name,
        )
        maybe_add(*base_values)
        one_dim_options = [
            ("filters", filter_options),
            ("dense", dense_options),
            ("dropout", dropout_options),
            ("l2", l2_options),
            ("lr", lr_options),
            ("batch", batch_options),
            ("pool", sorted({base.pool, "max", "avg"})),
            ("extra", sorted({base.extra_conv, True, False})),
            ("augment", augment_options),
        ]
        for field, options in one_dim_options:
            for value in options:
                values = list(base_values)
                index = {"filters": 0, "dense": 1, "dropout": 2, "l2": 3, "lr": 4, "batch": 5, "pool": 6, "extra": 7, "augment": 8}[field]
                values[index] = value
                maybe_add(*values)

        deterministic_filters = sorted({base.filters, (8, 16, 32), (10, 20, 40), tuple(v + 2 for v in base.filters)})
        deterministic_l2 = sorted({base.l2, 3.0e-5, 1.0e-4, 3.0e-4})
        deterministic_lr = sorted({base.learning_rate, base.learning_rate * 0.9, base.learning_rate * 1.1, 0.0020, 0.0026})
        for filters in deterministic_filters:
            for l2 in deterministic_l2:
                for lr in deterministic_lr:
                    for augment in augment_options[:4]:
                        maybe_add(filters, base.dense_units, base.dropout, l2, lr, base.batch_size, base.pool, base.extra_conv, augment)

        attempts = 0
        while len(candidates) < pool_cap and attempts < pool_cap * 12:
            attempts += 1
            maybe_add(
                rng.choice(tuple(filter_options)),
                rng.choice(tuple(dense_options)),
                rng.choice(tuple(dropout_options)),
                rng.choice(tuple(l2_options)),
                rng.choice(tuple(lr_options)),
                rng.choice(tuple(batch_options)),
                rng.choice(tuple(sorted({base.pool, "max", "avg"}))),
                rng.choice(tuple(sorted({base.extra_conv, True, False}))),
                rng.choice(tuple(augment_options)),
            )
        rng.shuffle(candidates)
        candidates.sort(key=lambda config: config_priority(config, "fine", rng))
        for config in candidates[: max(8, limit // max(1, len(base_dicts)) + 2)]:
            add_unique(configs, seen, config)
            if len(configs) >= limit:
                return configs
    return configs[:limit]


def predict_tflite_classes(path: Path, x: np.ndarray) -> np.ndarray:
    outputs = tflite_outputs(path, x)
    return np.argmax(outputs, axis=1).astype(np.int64)


def final_score(final_summary: dict[str, object], estimated_us: int, speed_weight: float) -> float:
    parent = final_summary["int8_test"]["parent"]
    all_parent = final_summary["int8_all"]["parent"]
    stress_items = [item for item in final_summary.get("int8_stress", {}).values() if isinstance(item, dict)]
    stress_macro = float(np.mean([float(item["macro_recall"]) for item in stress_items])) if stress_items else float(parent["macro_recall"])
    stress_worst = float(np.mean([float(item["worst_recall"]) for item in stress_items])) if stress_items else float(parent["worst_recall"])
    speed_score = max(0.0, min(1.0, (25000.0 - estimated_us) / 25000.0))
    return float(
        0.30 * float(parent["accuracy"])
        + 0.22 * float(parent["worst_recall"])
        + 0.12 * float(parent["macro_recall"])
        + 0.10 * float(all_parent["accuracy"])
        + 0.08 * float(all_parent["worst_recall"])
        + 0.08 * stress_macro
        + 0.05 * stress_worst
        + speed_weight * speed_score
    )


def export_and_eval(
    model: tf.keras.Model,
    config: ModelConfig,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    test_idx: np.ndarray,
    output_dir: Path,
    prefix: str,
    args: argparse.Namespace,
) -> dict[str, object]:
    float_path = output_dir / f"{prefix}_float.tflite"
    int8_path = output_dir / f"{prefix}_mild_int8.tflite"
    float_bytes = export_float_tflite(model, float_path)
    calibration_x = representative_array(x, y_sub, "mild_stress", args.seed + 77, args.calibration_limit)
    int8_bytes = export_quant_tflite(model, calibration_x, int8_path, "int8")

    keras_test_preds = predict_model(model, x[test_idx], config.batch_size)
    int8_test_preds = predict_tflite_classes(int8_path, x[test_idx])
    keras_all_preds = predict_model(model, x, config.batch_size)
    int8_all_preds = predict_tflite_classes(int8_path, x)
    stress_results: dict[str, object] = {}
    for stress_name in args.final_stress.split(","):
        stress_name = stress_name.strip()
        if not stress_name:
            continue
        xs = stress_batch(stress_name, x[test_idx])
        stress_preds = predict_tflite_classes(int8_path, xs)
        stress_results[stress_name] = evaluate_arrays(y_sub[test_idx], y_parent[test_idx], stress_preds)["parent"]
    return {
        "export": {
            "float_path": str(float_path),
            "float_bytes": float_bytes,
            "int8_path": str(int8_path),
            "int8_bytes": int8_bytes,
            "calibration": "mild_stress",
        },
        "keras_test": evaluate_arrays(y_sub[test_idx], y_parent[test_idx], keras_test_preds),
        "int8_test": evaluate_arrays(y_sub[test_idx], y_parent[test_idx], int8_test_preds),
        "keras_all": evaluate_arrays(y_sub, y_parent, keras_all_preds),
        "int8_all": evaluate_arrays(y_sub, y_parent, int8_all_preds),
        "int8_stress": stress_results,
        "keras_vs_int8_test_agreement": float(np.mean(keras_test_preds == int8_test_preds)),
        "keras_vs_int8_all_agreement": float(np.mean(keras_all_preds == int8_all_preds)),
    }


def final_train_and_export_mild(
    config: ModelConfig,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    args: argparse.Namespace,
    output_dir: Path,
) -> dict[str, object]:
    folds = stratified_folds(y_sub, 5, args.seed + 4242)
    test_idx = folds[0]
    val_idx = folds[1]
    train_idx = np.concatenate(folds[2:]).astype(np.int64)
    set_reproducible(args.seed + 9000)
    tf.keras.backend.clear_session()
    model = build_model(config)
    train_ds = make_dataset(x, y_sub, train_idx, config.batch_size, True, config.augment, args.seed + 9000, config.train_transforms)
    val_ds = make_dataset(x, y_sub, val_idx, config.batch_size, False, config.augment, args.seed + 9000)
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_loss",
            patience=args.final_patience,
            restore_best_weights=True,
            min_delta=1.0e-4,
        ),
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss",
            factor=0.5,
            patience=max(2, args.final_patience // 2),
            min_lr=1.0e-5,
            verbose=0,
        ),
    ]
    history = model.fit(train_ds, validation_data=val_ds, epochs=args.final_epochs, callbacks=callbacks, verbose=0)
    model.save(output_dir / "sixclass_best.keras")
    best_eval = export_and_eval(model, config, x, y_sub, y_parent, test_idx, output_dir, "sixclass_best", args)

    full_epochs = args.full_epochs if args.full_epochs > 0 else len(history.history["loss"])
    if args.skip_full:
        full_eval = None
    else:
        set_reproducible(args.seed + 19000)
        tf.keras.backend.clear_session()
        full_model = build_model(config)
        all_idx = np.arange(len(y_sub), dtype=np.int64)
        full_ds = make_dataset(x, y_sub, all_idx, config.batch_size, True, config.augment, args.seed + 19000, config.train_transforms)
        full_model.fit(full_ds, epochs=full_epochs, verbose=0)
        full_model.save(output_dir / "sixclass_full.keras")
        full_eval = export_and_eval(full_model, config, x, y_sub, y_parent, test_idx, output_dir, "sixclass_full", args)
    return {
        "epochs": len(history.history["loss"]),
        "full_epochs": full_epochs,
        "split_counts": {"train": int(len(train_idx)), "val": int(len(val_idx)), "test": int(len(test_idx))},
        **best_eval,
        "full_data_model": full_eval,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="V4 broad/fine scan using validated mild-stress int8 calibration.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--mode", choices=["smoke", "broad", "fine"], default="broad")
    parser.add_argument("--lane", choices=["core", "fast", "accuracy", "arch"], default="core")
    parser.add_argument("--summary", action="append", type=Path, default=[])
    parser.add_argument("--max-trials", type=int, default=96)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--folds", type=int, default=3)
    parser.add_argument("--epochs", type=int, default=150)
    parser.add_argument("--patience", type=int, default=20)
    parser.add_argument("--final-top-k", type=int, default=8)
    parser.add_argument("--final-epochs", type=int, default=260)
    parser.add_argument("--final-patience", type=int, default=36)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--skip-full", action="store_true")
    parser.add_argument("--seed", type=int, default=20260610)
    parser.add_argument("--stress", default=V4_STRESS)
    parser.add_argument("--final-stress", default="noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10")
    parser.add_argument("--speed-weight", type=float, default=0.08)
    parser.add_argument("--calibration-limit", type=int, default=192)
    args = parser.parse_args()

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime(f"v4_{args.mode}_{args.lane}_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    if args.mode == "smoke":
        configs = anchor_configs("smoke")[:2]
        args.max_trials = len(configs)
        args.folds = max(2, args.folds)
        args.epochs = min(args.epochs, 3)
        args.final_epochs = min(args.final_epochs, 4)
        args.final_top_k = min(args.final_top_k, 1)
        args.skip_full = True
    elif args.mode == "fine":
        configs = fine_configs(args.summary, args.max_trials, args.seed)
    else:
        configs = broad_configs(args.lane, args.max_trials, args.seed)

    pre_shard_trials = len(configs)
    if args.shard_count < 1:
        raise ValueError("--shard-count must be >= 1")
    if not 0 <= args.shard_index < args.shard_count:
        raise ValueError("--shard-index must satisfy 0 <= index < count")
    if args.shard_count > 1:
        configs = [config for index, config in enumerate(configs) if index % args.shard_count == args.shard_index]

    folds = stratified_folds(y_sub, args.folds, args.seed)
    run_config = {
        "args": {key: jsonable(value) for key, value in vars(args).items()},
        "subclass_names": SUBCLASS_NAMES,
        "parent_names": PARENT_NAMES,
        "subclass_to_parent": SUBCLASS_TO_PARENT.tolist(),
        "sample_count": int(len(y_sub)),
        "subclass_counts": {SUBCLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(SUBCLASS_NAMES))},
        "parent_counts": {PARENT_NAMES[i]: int(np.sum(y_parent == i)) for i in range(len(PARENT_NAMES))},
        "pre_shard_trials": pre_shard_trials,
        "generated_trials": len(configs),
        "calibration_strategy": "mild_stress",
        "paths": paths,
        "custom_augments": {name: asdict(augment) for name, augment in CUSTOM_AUGMENTS.items()},
        "configs": [config_to_dict(config) | {"name": config.name} for config in configs],
    }
    (output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2, ensure_ascii=False), encoding="utf-8")

    results: list[dict[str, object]] = []
    completed_trials: set[str] = set()
    skipped_corrupt = 0
    trial_results_path = output_dir / "trial_results.jsonl"
    if args.resume and trial_results_path.exists():
        results, skipped_corrupt = load_existing_results(trial_results_path)
        completed_trials = {str(result["trial"]) for result in results}
    pending_count = sum(1 for config in configs if config.name not in completed_trials)
    print("output_dir=" + str(output_dir), flush=True)
    print(f"mode={args.mode} lane={args.lane} trials={len(configs)} pending={pending_count} completed={len(completed_trials)} folds={len(folds)}", flush=True)
    if skipped_corrupt:
        print(f"resume_skipped_corrupt_lines={skipped_corrupt}", flush=True)

    for index, config in enumerate(configs, start=1):
        if config.name in completed_trials:
            print(f"[{index:03d}/{len(configs):03d}] {config.name} skipped_existing", flush=True)
            continue
        result = train_eval_config(config, x, y_sub, y_parent, folds, args, output_dir)
        results.append(result)
        print(
            f"[{index:03d}/{len(configs):03d}] {config.name} "
            f"score={result['mean_score']:.4f} quality={result['mean_quality_score']:.4f} "
            f"est_us={result['estimated_board_us']} clean_acc={result['mean_clean_parent_accuracy']:.4f} "
            f"clean_worst={result['mean_clean_parent_worst_recall']:.4f} stress_min={summarize_stress_min(result):.4f} "
            f"sub_acc={result['mean_subclass_accuracy']:.4f} epochs={result['mean_epochs']:.1f}",
            flush=True,
        )

    if not results:
        raise RuntimeError("no results available")
    results.sort(
        key=lambda item: (
            item["mean_score"],
            item["mean_clean_parent_worst_recall"],
            summarize_stress_min(item),
            item["mean_clean_parent_accuracy"],
        ),
        reverse=True,
    )
    summary: dict[str, object] = {
        "best": results[0],
        "top_results": results[: min(30, len(results))],
        "final_exports": [],
    }
    (output_dir / "search_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print("best_trial=" + str(results[0]["trial"]), flush=True)

    config_by_name = {config.name: config for config in configs}
    final_exports: list[dict[str, object]] = []
    for rank, result in enumerate(results[: max(0, args.final_top_k)], start=1):
        config = config_by_name[str(result["trial"])]
        candidate_dir = output_dir / "final_exports" / f"rank{rank:02d}_{safe_name(config.name)}"
        candidate_dir.mkdir(parents=True, exist_ok=True)
        final_args = copy.copy(args)
        final_summary = final_train_and_export_mild(config, x, y_sub, y_parent, final_args, candidate_dir)
        score = final_score(final_summary, int(result["estimated_board_us"]), args.speed_weight)
        final_rank = {
            "rank": rank,
            "trial": result["trial"],
            "search": result,
            "final": final_summary,
            "final_score": score,
            "output_dir": str(candidate_dir),
        }
        final_exports.append(final_rank)
        parent = final_summary["int8_test"]["parent"]
        all_parent = final_summary["int8_all"]["parent"]
        print(
            f"final_rank={rank} trial={result['trial']} final_score={score:.4f} "
            f"int8_test_acc={parent['accuracy']:.4f} int8_test_worst={parent['worst_recall']:.4f} "
            f"int8_all_acc={all_parent['accuracy']:.4f} int8_all_worst={all_parent['worst_recall']:.4f} "
            f"bytes={final_summary['export']['int8_bytes']} dir={candidate_dir}",
            flush=True,
        )

    summary["final_exports"] = final_exports
    if final_exports:
        summary["final_best"] = max(
            final_exports,
            key=lambda item: (
                float(item["final_score"]),
                float(item["final"]["int8_test"]["parent"]["worst_recall"]),
                float(item["final"]["int8_test"]["parent"]["accuracy"]),
            ),
        )
    (output_dir / "search_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print("search_summary_path=" + str(output_dir / "search_summary.json"), flush=True)


if __name__ == "__main__":
    main()
