import argparse
import csv
import json
import math
import os
import random
import time
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from typing import Callable

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_XLA_FLAGS", "--tf_xla_auto_jit=0")

import tensorflow as tf

import train_tiny32_sixclass_scan as tiny


IMAGE_SIZE = 32
VISUAL_CLASS_NAMES = [
    "first_aid_kit",
    "telescope",
    "ambulance",
    "armoured_car",
    "firearms_short",
    "firearms_long",
    "explosive_grenade",
    "explosive_c4",
]
PARENT_NAMES = ["supplies", "vehicle", "weapon"]
VISUAL_TO_PARENT = np.asarray([0, 0, 1, 1, 2, 2, 2, 2], dtype=np.int64)
VISUAL_TO_PARENT_NAME = {
    "first_aid_kit": "supplies",
    "telescope": "supplies",
    "ambulance": "vehicle",
    "armoured_car": "vehicle",
    "firearms_short": "weapon",
    "firearms_long": "weapon",
    "explosive_grenade": "weapon",
    "explosive_c4": "weapon",
}
DISK_LABEL_ALIASES = {
    "explosive_C4": "explosive_c4",
    "explosive_c4": "explosive_c4",
    "explosive_grenade": "explosive_grenade",
    "firearms_short": "firearms_short",
    "firearms_long": "firearms_long",
}
HARD_CLEAN_BASENAMES = [
    "first_aid_kit_050.jpg",
    "explosive_030.jpg",
    "explosive_070.jpg",
    "explosive_105.jpg",
    "explosive_124.jpg",
    "first_aid_kit_058.jpg",
    "explosive_092.jpg",
    "telescope_141.jpg",
    "firearms_002.jpg",
    "ambulance_020.jpg",
    "telescope_149.jpg",
    "firearms_175.jpg",
]
ROTATION_MIRROR_STRESSES = {
    "rot90",
    "rot180",
    "rot270",
    "mirror_lr",
    "mirror_lr_rot90",
    "mirror_lr_rot180",
    "mirror_lr_rot270",
}
DEFAULT_STRESS = (
    "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5"
)


# Reuse old tiny32 helpers only after changing their module-level class context.
tiny.SUBCLASS_NAMES = VISUAL_CLASS_NAMES
tiny.PARENT_NAMES = PARENT_NAMES
tiny.SUBCLASS_TO_PARENT = VISUAL_TO_PARENT
tiny.SUBCLASS_TO_PARENT_NAME = VISUAL_TO_PARENT_NAME

AugmentConfig = tiny.AugmentConfig


@dataclass(frozen=True)
class V5Config:
    name: str
    lane: str
    architecture: str
    filters: tuple[int, int, int]
    dense_units: int
    dropout: float
    l2: float
    learning_rate: float
    batch_size: int
    pool: str
    first_kernel: int
    extra_conv: bool
    augment: AugmentConfig
    activation: str = "relu"
    train_transforms: str = "rot_mirror"
    head: str = "subclass"
    logits: bool = False
    class_weight: str = "none"
    calibration: str = "mild_stress"
    parent_loss_weight: float = 1.0
    subclass_loss_weight: float = 0.35


def jsonable(value):
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, tuple):
        return [jsonable(item) for item in value]
    if isinstance(value, list):
        return [jsonable(item) for item in value]
    if isinstance(value, dict):
        return {str(key): jsonable(item) for key, item in value.items()}
    return value


def base_augments() -> dict[str, AugmentConfig]:
    v3_lowres = tiny.AUGMENTS["v3_lowres_noise"]
    return {
        **tiny.AUGMENTS,
        "sdiag_base": replace(v3_lowres, name="sdiag_base"),
        "sdiag_soft": replace(
            v3_lowres,
            name="sdiag_soft",
            blur_prob=0.34,
            blur_kernel=5,
            noise_std=0.042,
            downscale_prob=0.46,
            downscale_min=16,
        ),
        "sdiag_mid": replace(
            v3_lowres,
            name="sdiag_mid",
            blur_prob=0.42,
            blur_kernel=5,
            noise_std=0.050,
            salt_pepper=0.008,
            downscale_prob=0.46,
            downscale_min=16,
        ),
        "sdiag_hard": replace(
            v3_lowres,
            name="sdiag_hard",
            pad=3,
            brightness=0.14,
            contrast_delta=0.22,
            noise_std=0.060,
            salt_pepper=0.010,
            blur_prob=0.52,
            blur_kernel=5,
            downscale_prob=0.40,
            downscale_min=17,
        ),
        "sdiag_lowres": replace(
            v3_lowres,
            name="sdiag_lowres",
            blur_prob=0.38,
            blur_kernel=5,
            noise_std=0.044,
            downscale_prob=0.60,
            downscale_min=14,
        ),
        "sdiag_roi": replace(
            v3_lowres,
            name="sdiag_roi",
            pad=1,
            brightness=0.10,
            contrast_delta=0.16,
            noise_std=0.038,
            blur_prob=0.36,
            blur_kernel=5,
            downscale_prob=0.48,
            downscale_min=16,
        ),
        "sdiag_speed": AugmentConfig(
            "sdiag_speed",
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


ALL_AUGMENTS = base_augments()


def subtype_key(path: Path) -> str:
    parts = path.stem.split("_")
    return "_".join(parts[:-1]) if len(parts) > 1 else path.stem


def normalize_visual_label(label: str) -> str:
    return DISK_LABEL_ALIASES.get(label, label)


def infer_visual_label(dataset_dir: Path, path: Path, parent_name: str) -> tuple[str, str]:
    rel = path.relative_to(dataset_dir)
    disk_label = ""
    if len(rel.parts) >= 3:
        disk_label = rel.parts[1]
        visual = normalize_visual_label(disk_label)
    else:
        visual = subtype_key(path)
        disk_label = visual
    if visual not in VISUAL_TO_PARENT_NAME:
        raise ValueError(f"unknown V5 visual subclass for {path}: {visual}")
    expected_parent = VISUAL_TO_PARENT_NAME[visual]
    if expected_parent != parent_name:
        raise ValueError(f"path/label mismatch for {path}: label {visual} expects parent {expected_parent}")
    return visual, disk_label


def load_dataset_v5(dataset_dir: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str], list[dict[str, object]]]:
    images: list[np.ndarray] = []
    y_sub: list[int] = []
    y_parent: list[int] = []
    paths: list[str] = []
    rows: list[dict[str, object]] = []
    subclass_index = {name: i for i, name in enumerate(VISUAL_CLASS_NAMES)}
    parent_index = {name: i for i, name in enumerate(PARENT_NAMES)}
    hard_names = set(HARD_CLEAN_BASENAMES)
    for parent_name in PARENT_NAMES:
        parent_dir = dataset_dir / parent_name
        if not parent_dir.is_dir():
            raise FileNotFoundError(f"missing parent directory: {parent_dir}")
        for path in sorted(parent_dir.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in {".jpg", ".jpeg", ".png"}:
                continue
            visual, disk_label = infer_visual_label(dataset_dir, path, parent_name)
            image = tf.keras.utils.load_img(
                path,
                color_mode="grayscale",
                target_size=(IMAGE_SIZE, IMAGE_SIZE),
                interpolation="bilinear",
            )
            arr = tf.keras.utils.img_to_array(image).astype("float32") / 255.0
            sub_idx = subclass_index[visual]
            parent_idx = parent_index[parent_name]
            images.append(arr)
            y_sub.append(sub_idx)
            y_parent.append(parent_idx)
            path_str = str(path)
            paths.append(path_str)
            rows.append(
                {
                    "path": path_str,
                    "relative_path": str(path.relative_to(dataset_dir)),
                    "filename": path.name,
                    "parent": parent_name,
                    "parent_index": parent_idx,
                    "disk_visual_label": disk_label,
                    "visual_label": visual,
                    "visual_index": sub_idx,
                    "hard_clean": path.name in hard_names,
                }
            )
    if not images:
        raise FileNotFoundError(f"no images found under {dataset_dir}")
    return (
        np.stack(images).astype(np.float32),
        np.asarray(y_sub, dtype=np.int64),
        np.asarray(y_parent, dtype=np.int64),
        paths,
        rows,
    )


def hard_indices(paths: list[str]) -> tuple[np.ndarray, list[str]]:
    by_name: dict[str, list[int]] = {}
    for index, path in enumerate(paths):
        by_name.setdefault(Path(path).name, []).append(index)
    selected: list[int] = []
    missing: list[str] = []
    for name in HARD_CLEAN_BASENAMES:
        indexes = by_name.get(name)
        if not indexes:
            missing.append(name)
            continue
        selected.extend(indexes)
    return np.asarray(sorted(set(selected)), dtype=np.int64), missing


def write_maps_and_manifest(
    output_dir: Path,
    dataset_dir: Path,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    manifest_rows: list[dict[str, object]],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    class_map = {
        "visual_class_names": VISUAL_CLASS_NAMES,
        "disk_label_aliases": DISK_LABEL_ALIASES,
        "visual_to_parent_index": VISUAL_TO_PARENT.tolist(),
        "visual_to_parent_name": VISUAL_TO_PARENT_NAME,
        "note": "disk label explosive_C4 is normalized to explosive_c4",
    }
    parent_map = {"parent_names": PARENT_NAMES, "parent_to_index": {name: i for i, name in enumerate(PARENT_NAMES)}}
    (output_dir / "class_map.json").write_text(json.dumps(class_map, indent=2, ensure_ascii=False), encoding="utf-8")
    (output_dir / "parent_map.json").write_text(json.dumps(parent_map, indent=2, ensure_ascii=False), encoding="utf-8")
    with (output_dir / "dataset_manifest.csv").open("w", encoding="utf-8", newline="") as handle:
        fieldnames = [
            "path",
            "relative_path",
            "filename",
            "parent",
            "parent_index",
            "disk_visual_label",
            "visual_label",
            "visual_index",
            "hard_clean",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in manifest_rows:
            writer.writerow(row)
    hard_idx, missing = hard_indices(paths)
    dataset_summary = {
        "dataset_dir": str(dataset_dir),
        "sample_count": int(len(x)),
        "visual_counts": {VISUAL_CLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(VISUAL_CLASS_NAMES))},
        "parent_counts": {PARENT_NAMES[i]: int(np.sum(y_parent == i)) for i in range(len(PARENT_NAMES))},
        "hard_clean_count": int(len(hard_idx)),
        "hard_clean_missing": missing,
    }
    (output_dir / "dataset_summary.json").write_text(
        json.dumps(dataset_summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def stratified_folds(labels: np.ndarray, fold_count: int, seed: int) -> list[np.ndarray]:
    return tiny.stratified_folds(labels, fold_count, seed)


def expand_rotation_mirror_with_parent(
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    images: list[np.ndarray] = []
    sub_labels: list[int] = []
    parent_labels: list[int] = []
    for image, sub, parent in zip(x, y_sub, y_parent):
        for k in range(4):
            rotated = np.rot90(image, k, axes=(0, 1))
            images.append(rotated)
            sub_labels.append(int(sub))
            parent_labels.append(int(parent))
            images.append(np.flip(rotated, axis=1))
            sub_labels.append(int(sub))
            parent_labels.append(int(parent))
    return (
        np.stack(images).astype(np.float32),
        np.asarray(sub_labels, dtype=np.int64),
        np.asarray(parent_labels, dtype=np.int64),
    )


def make_dataset(
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    indexes: np.ndarray,
    config: V5Config,
    seed: int,
    training: bool,
) -> tf.data.Dataset:
    x_data = x[indexes]
    sub_data = y_sub[indexes]
    parent_data = y_parent[indexes]
    if training and config.train_transforms == "rot_mirror":
        x_data, sub_data, parent_data = expand_rotation_mirror_with_parent(x_data, sub_data, parent_data)
    elif config.train_transforms != "none" and config.train_transforms != "rot_mirror":
        raise ValueError(f"unknown train_transforms: {config.train_transforms}")

    if config.head == "dual_parent":
        labels = {"parent": parent_data, "subclass": sub_data}
    elif config.head == "parent":
        labels = parent_data
    elif config.head == "subclass":
        labels = sub_data
    else:
        raise ValueError(f"unknown head: {config.head}")
    ds = tf.data.Dataset.from_tensor_slices((x_data, labels))
    if training:
        ds = ds.shuffle(len(x_data), seed=seed, reshuffle_each_iteration=True)
        ds = ds.map(lambda image, label: (tiny.augment_image(image, config.augment), label), num_parallel_calls=tf.data.AUTOTUNE)
    return ds.batch(config.batch_size).prefetch(tf.data.AUTOTUNE)


def activation_layer(x: tf.Tensor, config: V5Config, name: str) -> tf.Tensor:
    if config.activation == "relu":
        return tf.keras.layers.ReLU(name=name)(x)
    if config.activation == "relu6":
        return tf.keras.layers.ReLU(max_value=6.0, name=name)(x)
    if config.activation == "hard_swish":
        return tf.keras.layers.Activation(tf.keras.activations.hard_silu, name=name)(x)
    raise ValueError(f"unknown activation: {config.activation}")


def conv_block(
    x: tf.Tensor,
    filters: int,
    kernel: int,
    config: V5Config,
    regularizer,
    name: str,
    strides: int = 1,
) -> tf.Tensor:
    x = tf.keras.layers.Conv2D(
        filters,
        kernel_size=kernel,
        strides=strides,
        padding="same",
        use_bias=True,
        kernel_regularizer=regularizer,
        name=f"{name}_conv",
    )(x)
    return activation_layer(x, config, f"{name}_{config.activation}")


def depthwise_pointwise_block(
    x: tf.Tensor,
    filters: int,
    kernel: int,
    config: V5Config,
    regularizer,
    name: str,
    strides: int = 1,
) -> tf.Tensor:
    x = tf.keras.layers.DepthwiseConv2D(
        kernel_size=kernel,
        strides=strides,
        padding="same",
        use_bias=False,
        depthwise_regularizer=regularizer,
        name=f"{name}_dw",
    )(x)
    x = activation_layer(x, config, f"{name}_dw_{config.activation}")
    x = tf.keras.layers.Conv2D(
        filters,
        kernel_size=1,
        padding="same",
        use_bias=True,
        kernel_regularizer=regularizer,
        name=f"{name}_pw",
    )(x)
    return activation_layer(x, config, f"{name}_pw_{config.activation}")


def maybe_pool(x: tf.Tensor, config: V5Config, name: str) -> tf.Tensor:
    if config.pool == "avg":
        return tf.keras.layers.AveragePooling2D(2, name=f"{name}_avg_pool")(x)
    if config.pool == "max":
        return tf.keras.layers.MaxPooling2D(2, name=f"{name}_max_pool")(x)
    raise ValueError(f"unknown pool: {config.pool}")


def build_model(config: V5Config) -> tf.keras.Model:
    regularizer = tf.keras.regularizers.l2(config.l2) if config.l2 > 0 else None
    inputs = tf.keras.Input((IMAGE_SIZE, IMAGE_SIZE, 1), name="gray32")
    x = inputs
    if config.architecture == "spacetodepth_conv":
        x = tf.keras.layers.Lambda(
            lambda t: tf.nn.space_to_depth(t, 2),
            output_shape=(IMAGE_SIZE // 2, IMAGE_SIZE // 2, 4),
            name="space_to_depth",
        )(x)
    for i, filters in enumerate(config.filters):
        kernel = config.first_kernel if i == 0 else 3
        name = f"block_{i + 1}"
        if config.architecture in {"depthwise_pool", "hardswish_depthwise"}:
            x = depthwise_pointwise_block(x, filters, kernel, config, regularizer, name)
            if config.extra_conv and i == 2:
                x = depthwise_pointwise_block(x, filters, 3, config, regularizer, "block_3_extra")
            if i < 2:
                x = maybe_pool(x, config, name)
        elif config.architecture == "stride_conv":
            stride = 2 if i < 2 else 1
            x = conv_block(x, filters, kernel, config, regularizer, name, strides=stride)
            if config.extra_conv and i == 2:
                x = conv_block(x, filters, 3, config, regularizer, "block_3_extra")
        elif config.architecture in {"conv_pool", "spacetodepth_conv"}:
            x = conv_block(x, filters, kernel, config, regularizer, name)
            if config.extra_conv and i == 2:
                x = conv_block(x, filters, 3, config, regularizer, "block_3_extra")
            if i < 2:
                x = maybe_pool(x, config, name)
        else:
            raise ValueError(f"unknown architecture: {config.architecture}")
    x = tf.keras.layers.GlobalAveragePooling2D(name="gap")(x)
    if config.dense_units > 0:
        x = tf.keras.layers.Dense(config.dense_units, kernel_regularizer=regularizer, name="dense")(x)
        x = activation_layer(x, config, "dense_activation")
    if config.dropout > 0:
        x = tf.keras.layers.Dropout(config.dropout, name="dropout")(x)

    subclass_logits = tf.keras.layers.Dense(len(VISUAL_CLASS_NAMES), activation=None, name="subclass_logits")(x)
    subclass_out = (
        tf.keras.layers.Activation("linear", name="subclass")(subclass_logits)
        if config.logits
        else tf.keras.layers.Softmax(name="subclass")(subclass_logits)
    )
    parent_logits = tf.keras.layers.Dense(len(PARENT_NAMES), activation=None, name="parent_logits")(x)
    parent_out = (
        tf.keras.layers.Activation("linear", name="parent")(parent_logits)
        if config.logits
        else tf.keras.layers.Softmax(name="parent")(parent_logits)
    )

    if config.head == "dual_parent":
        outputs = {"parent": parent_out, "subclass": subclass_out}
        model = tf.keras.Model(inputs, outputs, name=f"tiny32_v5_{config.name}")
        loss = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
        model.compile(
            optimizer=tf.keras.optimizers.Adam(config.learning_rate),
            loss={"parent": loss, "subclass": loss},
            loss_weights={"parent": config.parent_loss_weight, "subclass": config.subclass_loss_weight},
            jit_compile=False,
        )
        return model
    outputs = parent_out if config.head == "parent" else subclass_out
    model = tf.keras.Model(inputs, outputs, name=f"tiny32_v5_{config.name}")
    model.compile(
        optimizer=tf.keras.optimizers.Adam(config.learning_rate),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits),
        metrics=["accuracy"],
        jit_compile=False,
    )
    return model


def deploy_model(model: tf.keras.Model, config: V5Config) -> tf.keras.Model:
    if config.head == "dual_parent":
        return tf.keras.Model(model.input, model.get_layer("parent").output, name=f"{model.name}_deploy_parent")
    return model


def output_kind(config: V5Config) -> str:
    return "parent" if config.head in {"parent", "dual_parent"} else "subclass"


def metrics_from_confusion(matrix: np.ndarray, names: list[str]) -> dict[str, object]:
    return tiny.metrics_from_confusion(matrix, names)


def parent_metrics(parent_true: np.ndarray, parent_preds: np.ndarray) -> dict[str, object]:
    matrix = tiny.confusion(parent_true, parent_preds, len(PARENT_NAMES))
    return metrics_from_confusion(matrix, PARENT_NAMES)


def evaluate_predictions(
    subclass_true: np.ndarray,
    parent_true: np.ndarray,
    preds: np.ndarray,
    kind: str,
) -> dict[str, object]:
    if kind == "parent":
        return {"parent": parent_metrics(parent_true, preds)}
    parent_preds = VISUAL_TO_PARENT[preds.astype(np.int64)]
    return {
        "subclass": metrics_from_confusion(tiny.confusion(subclass_true, preds, len(VISUAL_CLASS_NAMES)), VISUAL_CLASS_NAMES),
        "parent": metrics_from_confusion(tiny.confusion(parent_true, parent_preds, len(PARENT_NAMES)), PARENT_NAMES),
    }


def keras_outputs(model: tf.keras.Model, x: np.ndarray, batch_size: int) -> np.ndarray:
    out = model.predict(x, batch_size=batch_size, verbose=0)
    if isinstance(out, dict):
        out = out["parent"] if "parent" in out else out[sorted(out)[0]]
    if isinstance(out, list):
        out = out[0]
    return np.asarray(out, dtype=np.float32)


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


def predictions_from_outputs(values: np.ndarray) -> np.ndarray:
    return np.argmax(values, axis=1).astype(np.int64)


def balanced_indices(labels: np.ndarray, limit: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    per_class = max(1, int(math.ceil(limit / len(set(labels.tolist())))))
    selected: list[int] = []
    for label in sorted(set(labels.tolist())):
        indexes = np.where(labels == label)[0]
        rng.shuffle(indexes)
        selected.extend(indexes[:per_class].tolist())
    rng.shuffle(selected)
    return np.asarray(selected[:limit], dtype=np.int64)


def augment_image_np(image: np.ndarray, rng: np.random.Generator, level: str) -> np.ndarray:
    cur = image.copy()
    if level == "mild":
        if rng.random() < 0.35:
            cur = tiny.stress_batch(str(rng.choice(["noise_0p06", "hblur5_noise_0p06", "diagblur5_noise_0p08"])), cur[None, ...])[0]
        cur = np.clip(cur + rng.normal(0.0, 0.025, cur.shape).astype(np.float32), 0.0, 1.0)
    elif level == "hard":
        if rng.random() < 0.65:
            cur = tiny.stress_batch(str(rng.choice(["noise_0p06", "hblur5_noise_0p06", "diagblur5_noise_0p08", "noise_0p10"])), cur[None, ...])[0]
        cur = np.clip(cur + rng.normal(0.0, 0.060, cur.shape).astype(np.float32), 0.0, 1.0)
    return cur.astype(np.float32)


def representative_array(
    x: np.ndarray,
    y_sub: np.ndarray,
    hard_idx: np.ndarray,
    strategy: str,
    seed: int,
    limit: int,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    limit = min(limit, len(x))
    if strategy == "first":
        return x[:limit].astype(np.float32)
    if strategy == "balanced_clean":
        return x[balanced_indices(y_sub, limit, seed)].astype(np.float32)
    if strategy == "balanced_rotmirror":
        idx = balanced_indices(y_sub, max(1, min(len(x), math.ceil(limit / 8))), seed)
        expanded, _, _ = expand_rotation_mirror_with_parent(x[idx], y_sub[idx], VISUAL_TO_PARENT[y_sub[idx]])
        return expanded[:limit].astype(np.float32)
    if strategy == "mild_stress":
        idx = balanced_indices(y_sub, limit, seed)
        return np.stack([augment_image_np(image, rng, "mild") for image in x[idx]]).astype(np.float32)
    if strategy == "hard_stress":
        idx = balanced_indices(y_sub, limit, seed)
        return np.stack([augment_image_np(image, rng, "hard") for image in x[idx]]).astype(np.float32)
    if strategy == "hard_clean" and len(hard_idx) > 0:
        idx = np.resize(hard_idx, limit)
        return x[idx].astype(np.float32)
    raise ValueError(f"unknown calibration strategy: {strategy}")


def representative_dataset(samples: np.ndarray) -> Callable[[], object]:
    def gen():
        for index in range(len(samples)):
            yield [samples[index : index + 1].astype(np.float32)]

    return gen


def export_float_tflite(model: tf.keras.Model, path: Path) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    data = converter.convert()
    path.write_bytes(data)
    return len(data)


def export_int8_tflite(model: tf.keras.Model, samples: np.ndarray, path: Path) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset(samples)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    data = converter.convert()
    path.write_bytes(data)
    return len(data)


def class_weight_for(labels: np.ndarray, mode: str) -> dict[int, float] | None:
    if mode == "none":
        return None
    counts = np.bincount(labels.astype(np.int64))
    weights: dict[int, float] = {}
    for index, count in enumerate(counts):
        if count <= 0:
            continue
        if mode == "balanced":
            weights[index] = float(np.mean(counts[counts > 0]) / count)
        elif mode == "sqrt_balanced":
            weights[index] = float(math.sqrt(np.mean(counts[counts > 0]) / count))
        elif mode == "scarce_soft":
            weights[index] = float(math.sqrt(np.mean(counts[counts > 0]) / count))
            if VISUAL_CLASS_NAMES[index] == "explosive_c4":
                weights[index] = min(3.0, weights[index] * 1.35)
        else:
            raise ValueError(f"unknown class_weight: {mode}")
    return weights


def fit_model(
    model: tf.keras.Model,
    train_ds: tf.data.Dataset,
    val_ds: tf.data.Dataset,
    config: V5Config,
    train_labels: np.ndarray,
    epochs: int,
    patience: int,
) -> dict[str, list[float]]:
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_loss",
            patience=patience,
            mode="min",
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
    kwargs: dict[str, object] = {}
    if config.head in {"subclass", "parent"}:
        weights = class_weight_for(train_labels, config.class_weight)
        if weights:
            kwargs["class_weight"] = weights
    history = model.fit(train_ds, validation_data=val_ds, epochs=epochs, callbacks=callbacks, verbose=0, **kwargs)
    return {key: [float(v) for v in values] for key, values in history.history.items()}


def stress_metrics(
    int8_path: Path,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    indexes: np.ndarray,
    kind: str,
    stress_names: list[str],
) -> dict[str, object]:
    results: dict[str, object] = {}
    for stress_name in stress_names:
        xs = tiny.stress_batch(stress_name, x[indexes])
        preds = predictions_from_outputs(tflite_outputs(int8_path, xs))
        results[stress_name] = evaluate_predictions(y_sub[indexes], y_parent[indexes], preds, kind)["parent"]
    return results


def score_run(
    config: V5Config,
    clean: dict[str, object],
    hard: dict[str, object],
    stress: dict[str, object],
    agreement: float,
    int8_bytes: int,
) -> float:
    clean_parent = clean["parent"]
    hard_parent = hard["parent"] if hard else clean_parent
    stress_items = [item for item in stress.values() if isinstance(item, dict)]
    stress_macro = float(np.mean([float(item["macro_recall"]) for item in stress_items])) if stress_items else float(clean_parent["macro_recall"])
    stress_worst = float(np.min([float(item["worst_recall"]) for item in stress_items])) if stress_items else float(clean_parent["worst_recall"])
    visual_macro = float(clean.get("subclass", clean_parent)["macro_recall"])
    estimated_us = tiny.estimate_board_latency_us(config)
    speed_score = max(0.0, min(1.0, (25000.0 - estimated_us) / 25000.0))
    size_score = max(0.0, min(1.0, (26000.0 - int8_bytes) / 26000.0))
    if config.lane == "fast":
        return float(
            0.16 * float(clean_parent["accuracy"])
            + 0.16 * float(clean_parent["worst_recall"])
            + 0.16 * float(hard_parent["accuracy"])
            + 0.12 * float(hard_parent["worst_recall"])
            + 0.10 * stress_macro
            + 0.10 * stress_worst
            + 0.14 * speed_score
            + 0.06 * size_score
            + 0.10 * min(float(clean_parent["worst_recall"]), stress_worst)
        )
    if config.lane == "accuracy":
        return float(
            0.16 * float(clean_parent["accuracy"])
            + 0.16 * float(clean_parent["worst_recall"])
            + 0.20 * float(hard_parent["accuracy"])
            + 0.14 * float(hard_parent["worst_recall"])
            + 0.14 * stress_macro
            + 0.10 * stress_worst
            + 0.06 * visual_macro
            + 0.04 * agreement
        )
    return float(
        0.18 * float(clean_parent["accuracy"])
        + 0.18 * float(clean_parent["worst_recall"])
        + 0.18 * float(hard_parent["accuracy"])
        + 0.12 * float(hard_parent["worst_recall"])
        + 0.12 * stress_macro
        + 0.10 * stress_worst
        + 0.04 * visual_macro
        + 0.04 * agreement
        + 0.04 * (0.65 * speed_score + 0.35 * size_score)
    )


def run_seed_case(
    config: V5Config,
    seed: int,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    args: argparse.Namespace,
    output_dir: Path,
    save_artifacts: bool,
) -> dict[str, object]:
    tiny.set_reproducible(seed)
    tf.keras.backend.clear_session()
    folds = stratified_folds(y_sub, 5, seed + 4242)
    test_idx = folds[0]
    val_idx = folds[1]
    train_idx = np.concatenate(folds[2:]).astype(np.int64)
    hard_idx, missing_hard = hard_indices(paths)
    trial_dir = output_dir / "artifacts" / safe_name(config.name) / f"seed_{seed}"
    if save_artifacts:
        trial_dir.mkdir(parents=True, exist_ok=True)
    else:
        trial_dir.mkdir(parents=True, exist_ok=True)

    model = build_model(config)
    train_ds = make_dataset(x, y_sub, y_parent, train_idx, config, seed, True)
    val_ds = make_dataset(x, y_sub, y_parent, val_idx, config, seed, False)
    label_source = y_parent if config.head == "parent" else y_sub
    history = fit_model(model, train_ds, val_ds, config, label_source[train_idx], args.epochs, args.patience)
    deploy = deploy_model(model, config)
    if save_artifacts:
        model.save(trial_dir / "model.keras")
        if deploy is not model:
            deploy.save(trial_dir / "deploy.keras")
    float_path = trial_dir / "model_float.tflite"
    int8_path = trial_dir / "model_int8.tflite"
    float_bytes = export_float_tflite(deploy, float_path)
    calibration_x = representative_array(x, y_sub, hard_idx, config.calibration, seed + 77, args.calibration_limit)
    int8_bytes = export_int8_tflite(deploy, calibration_x, int8_path)

    kind = output_kind(config)
    keras_test_outputs = keras_outputs(deploy, x[test_idx], config.batch_size)
    int8_test_outputs = tflite_outputs(int8_path, x[test_idx])
    keras_all_outputs = keras_outputs(deploy, x, config.batch_size)
    int8_all_outputs = tflite_outputs(int8_path, x)
    keras_test_preds = predictions_from_outputs(keras_test_outputs)
    int8_test_preds = predictions_from_outputs(int8_test_outputs)
    keras_all_preds = predictions_from_outputs(keras_all_outputs)
    int8_all_preds = predictions_from_outputs(int8_all_outputs)
    keras_hard_preds = predictions_from_outputs(keras_outputs(deploy, x[hard_idx], config.batch_size)) if len(hard_idx) else np.asarray([], dtype=np.int64)
    int8_hard_preds = predictions_from_outputs(tflite_outputs(int8_path, x[hard_idx])) if len(hard_idx) else np.asarray([], dtype=np.int64)
    clean = evaluate_predictions(y_sub[test_idx], y_parent[test_idx], int8_test_preds, kind)
    hard = evaluate_predictions(y_sub[hard_idx], y_parent[hard_idx], int8_hard_preds, kind) if len(hard_idx) else clean
    stress = stress_metrics(int8_path, x, y_sub, y_parent, test_idx, kind, args.stress_names)
    agreement_test = float(np.mean(keras_test_preds == int8_test_preds))
    agreement_all = float(np.mean(keras_all_preds == int8_all_preds))
    score = score_run(config, clean, hard, stress, agreement_all, int8_bytes)
    result = {
        "seed": seed,
        "status": "ok",
        "score": score,
        "epochs": len(history.get("loss", [])),
        "split_counts": {"train": int(len(train_idx)), "val": int(len(val_idx)), "test": int(len(test_idx)), "hard": int(len(hard_idx))},
        "c4_split_counts": {
            "train": int(np.sum(y_sub[train_idx] == VISUAL_CLASS_NAMES.index("explosive_c4"))),
            "val": int(np.sum(y_sub[val_idx] == VISUAL_CLASS_NAMES.index("explosive_c4"))),
            "test": int(np.sum(y_sub[test_idx] == VISUAL_CLASS_NAMES.index("explosive_c4"))),
        },
        "keras_test": evaluate_predictions(y_sub[test_idx], y_parent[test_idx], keras_test_preds, kind),
        "int8_test": clean,
        "keras_all": evaluate_predictions(y_sub, y_parent, keras_all_preds, kind),
        "int8_all": evaluate_predictions(y_sub, y_parent, int8_all_preds, kind),
        "keras_hard": evaluate_predictions(y_sub[hard_idx], y_parent[hard_idx], keras_hard_preds, kind) if len(hard_idx) else {},
        "int8_hard": hard,
        "int8_stress": stress,
        "agreement": {"keras_vs_int8_test": agreement_test, "keras_vs_int8_all": agreement_all},
        "export": {
            "float_path": str(float_path),
            "float_bytes": float_bytes,
            "int8_path": str(int8_path),
            "int8_bytes": int8_bytes,
            "calibration": config.calibration,
            "output_kind": kind,
        },
        "hard_clean_missing": missing_hard,
        "artifact_dir": str(trial_dir),
    }
    (trial_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result


def mean_float(values: list[float]) -> float:
    return float(np.mean(values)) if values else 0.0


def min_float(values: list[float]) -> float:
    return float(np.min(values)) if values else 0.0


def summarize_trial(config: V5Config, seed_results: list[dict[str, object]]) -> dict[str, object]:
    ok = [item for item in seed_results if item.get("status") == "ok"]
    scores = [float(item["score"]) for item in ok]
    clean_acc = [float(item["int8_test"]["parent"]["accuracy"]) for item in ok]
    clean_worst = [float(item["int8_test"]["parent"]["worst_recall"]) for item in ok]
    hard_acc = [float(item["int8_hard"]["parent"]["accuracy"]) for item in ok]
    hard_worst = [float(item["int8_hard"]["parent"]["worst_recall"]) for item in ok]
    stress_worsts = [
        min(float(stress["worst_recall"]) for stress in item["int8_stress"].values())
        if item.get("int8_stress")
        else float(item["int8_test"]["parent"]["worst_recall"])
        for item in ok
    ]
    stress_macros = [
        mean_float([float(stress["macro_recall"]) for stress in item["int8_stress"].values()])
        if item.get("int8_stress")
        else float(item["int8_test"]["parent"]["macro_recall"])
        for item in ok
    ]
    agreements = [float(item["agreement"]["keras_vs_int8_all"]) for item in ok]
    bytes_values = [float(item["export"]["int8_bytes"]) for item in ok]
    return {
        "trial": config.name,
        "lane": config.lane,
        "config": config_to_dict(config),
        "status": "ok" if len(ok) == len(seed_results) else "partial",
        "runs": ok,
        "score_mean": mean_float(scores),
        "score_min": min_float(scores),
        "score_std": float(np.std(scores)) if scores else 0.0,
        "clean_parent_accuracy_mean": mean_float(clean_acc),
        "clean_parent_accuracy_min": min_float(clean_acc),
        "clean_parent_worst_mean": mean_float(clean_worst),
        "clean_parent_worst_min": min_float(clean_worst),
        "hard_parent_accuracy_mean": mean_float(hard_acc),
        "hard_parent_accuracy_min": min_float(hard_acc),
        "hard_parent_worst_mean": mean_float(hard_worst),
        "hard_parent_worst_min": min_float(hard_worst),
        "stress_parent_macro_mean": mean_float(stress_macros),
        "stress_parent_worst_mean": mean_float(stress_worsts),
        "stress_parent_worst_min": min_float(stress_worsts),
        "agreement_mean": mean_float(agreements),
        "int8_bytes_mean": mean_float(bytes_values),
        "estimated_board_us": tiny.estimate_board_latency_us(config),
    }


def config_to_dict(config: V5Config) -> dict[str, object]:
    return {
        "name": config.name,
        "lane": config.lane,
        "architecture": config.architecture,
        "filters": list(config.filters),
        "dense_units": config.dense_units,
        "dropout": config.dropout,
        "l2": config.l2,
        "learning_rate": config.learning_rate,
        "batch_size": config.batch_size,
        "pool": config.pool,
        "first_kernel": config.first_kernel,
        "extra_conv": config.extra_conv,
        "augment": asdict(config.augment),
        "activation": config.activation,
        "train_transforms": config.train_transforms,
        "head": config.head,
        "logits": config.logits,
        "class_weight": config.class_weight,
        "calibration": config.calibration,
        "parent_loss_weight": config.parent_loss_weight,
        "subclass_loss_weight": config.subclass_loss_weight,
        "estimated_board_us": tiny.estimate_board_latency_us(config),
    }


def config_from_dict(data: dict[str, object], name: str | None = None) -> V5Config:
    augment_data = data["augment"]
    augment_name = str(augment_data["name"])
    augment = ALL_AUGMENTS.get(augment_name) or AugmentConfig(
        name=augment_name,
        pad=int(augment_data["pad"]),
        brightness=float(augment_data["brightness"]),
        contrast_delta=float(augment_data["contrast_delta"]),
        noise_std=float(augment_data["noise_std"]),
        salt_pepper=float(augment_data["salt_pepper"]),
        blur_prob=float(augment_data["blur_prob"]),
        blur_kernel=int(augment_data["blur_kernel"]),
        downscale_prob=float(augment_data["downscale_prob"]),
        downscale_min=int(augment_data["downscale_min"]),
    )
    return V5Config(
        name=str(name or data.get("name") or "candidate"),
        lane=str(data.get("lane", "balance")),
        architecture=str(data.get("architecture", "spacetodepth_conv")),
        filters=tuple(int(v) for v in data["filters"]),
        dense_units=int(data.get("dense_units", 0)),
        dropout=float(data.get("dropout", 0.0)),
        l2=float(data.get("l2", 1.0e-4)),
        learning_rate=float(data.get("learning_rate", 0.00318)),
        batch_size=int(data.get("batch_size", 16)),
        pool=str(data.get("pool", "max")),
        first_kernel=int(data.get("first_kernel", 3)),
        extra_conv=bool(data.get("extra_conv", False)),
        augment=augment,
        activation=str(data.get("activation", "relu")),
        train_transforms=str(data.get("train_transforms", "rot_mirror")),
        head=str(data.get("head", "subclass")),
        logits=bool(data.get("logits", False)),
        class_weight=str(data.get("class_weight", "none")),
        calibration=str(data.get("calibration", "mild_stress")),
        parent_loss_weight=float(data.get("parent_loss_weight", 1.0)),
        subclass_loss_weight=float(data.get("subclass_loss_weight", 0.35)),
    )


def semantic_key(config: V5Config) -> tuple[object, ...]:
    return (
        config.lane,
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
        config.head,
        config.logits,
        config.class_weight,
        config.calibration,
    )


def make_config(
    name: str,
    lane: str,
    architecture: str,
    filters: tuple[int, int, int],
    lr: float,
    l2: float,
    dropout: float,
    augment: str,
    *,
    dense_units: int = 0,
    batch_size: int = 16,
    pool: str = "max",
    first_kernel: int = 3,
    extra_conv: bool = False,
    activation: str = "relu",
    head: str = "subclass",
    logits: bool = False,
    class_weight: str = "none",
    calibration: str = "mild_stress",
) -> V5Config:
    return V5Config(
        name=name,
        lane=lane,
        architecture=architecture,
        filters=filters,
        dense_units=dense_units,
        dropout=dropout,
        l2=l2,
        learning_rate=lr,
        batch_size=batch_size,
        pool=pool,
        first_kernel=first_kernel,
        extra_conv=extra_conv,
        augment=ALL_AUGMENTS[augment],
        activation=activation,
        head=head,
        logits=logits,
        class_weight=class_weight,
        calibration=calibration,
    )


def anchor_configs() -> list[V5Config]:
    return [
        make_config("fast_stab024_v5", "fast", "spacetodepth_conv", (6, 12, 24), 0.00286, 1.0e-4, 0.0, "sdiag_base"),
        make_config("fast_sd104_v5", "fast", "spacetodepth_conv", (6, 12, 24), 0.00318, 1.0e-4, 0.0, "sdiag_base"),
        make_config("fast_lowres_v5", "fast", "spacetodepth_conv", (6, 12, 24), 0.00286, 1.0e-4, 0.003, "sdiag_lowres"),
        make_config("balance_sd097_v5", "balance", "spacetodepth_conv", (8, 16, 32), 0.00318, 1.0e-4, 0.003, "sdiag_lowres"),
        make_config("balance_sd175_v5", "balance", "spacetodepth_conv", (8, 16, 32), 0.00286, 1.0e-4, 0.005, "sdiag_soft"),
        make_config("balance_sd218_v5", "balance", "spacetodepth_conv", (8, 16, 32), 0.00286, 1.0e-4, 0.003, "sdiag_speed"),
        make_config("balance_stab007_v5", "balance", "spacetodepth_conv", (8, 16, 32), 0.00318, 1.0e-4, 0.0, "sdiag_base"),
        make_config("accuracy_wide_10_18_36_v5", "accuracy", "spacetodepth_conv", (10, 18, 36), 0.00286, 1.0e-4, 0.003, "sdiag_soft"),
        make_config("accuracy_wide_10_20_40_v5", "accuracy", "spacetodepth_conv", (10, 20, 40), 0.0023, 1.0e-4, 0.02, "sdiag_mid"),
        make_config(
            "accuracy_extra_v5",
            "accuracy",
            "spacetodepth_conv",
            (10, 18, 36),
            0.0023,
            1.0e-4,
            0.02,
            "sdiag_lowres",
            extra_conv=True,
        ),
    ]


def candidate_priority(config: V5Config, rng: random.Random) -> tuple[float, int, str]:
    if config.lane == "fast":
        target_filters = (6, 12, 24)
        target_us = 5600
    elif config.lane == "accuracy":
        target_filters = (10, 20, 40)
        target_us = 10500
    else:
        target_filters = (8, 16, 32)
        target_us = 7163
    filter_penalty = sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, target_filters)) / 3.0
    lr_targets = [0.00318, 0.00286] if config.lane != "accuracy" else [0.0023, 0.00286, 0.0016]
    lr_penalty = min(abs(math.log(config.learning_rate / target)) for target in lr_targets) / 0.45
    l2_penalty = abs(math.log(config.l2 / 1.0e-4)) / 2.4 if config.l2 > 0 else 0.35
    dropout_penalty = config.dropout * (4.0 if config.lane == "accuracy" else 7.0)
    dense_penalty = 0.0 if config.dense_units == 0 else 0.04 + config.dense_units / 240.0
    extra_penalty = 0.05 if config.extra_conv and config.lane != "accuracy" else 0.0
    arch_penalty = {
        "spacetodepth_conv": 0.0,
        "depthwise_pool": 0.08,
        "hardswish_depthwise": 0.12,
        "stride_conv": 0.18,
        "conv_pool": 0.42,
    }.get(config.architecture, 0.5)
    if config.lane == "accuracy":
        arch_penalty *= 0.45
    head_penalty = {"subclass": 0.0, "dual_parent": 0.035, "parent": 0.055}.get(config.head, 0.08)
    logits_penalty = 0.012 if config.logits else 0.0
    pool_penalty = 0.0 if config.pool == "max" else 0.045
    activation_penalty = 0.0 if config.activation == "relu" else 0.045
    latency = tiny.estimate_board_latency_us(config)
    latency_penalty = abs(latency - target_us) / 14000.0
    if config.lane == "fast":
        latency_penalty = max(0.0, latency - 6500) / 8000.0
    return (
        0.48 * filter_penalty
        + 0.38 * lr_penalty
        + 0.26 * l2_penalty
        + dropout_penalty
        + dense_penalty
        + extra_penalty
        + arch_penalty
        + head_penalty
        + logits_penalty
        + pool_penalty
        + activation_penalty
        + latency_penalty
        + rng.random() * 1.0e-4,
        latency,
        config.name,
    )


def generate_candidates(lane: str, limit: int, seed: int, aggressive: bool) -> list[V5Config]:
    rng = random.Random(seed)
    configs: list[V5Config] = []
    seen: set[tuple[object, ...]] = set()

    def add(config: V5Config) -> None:
        if lane != "all" and config.lane != lane:
            return
        if config.architecture == "conv_pool" and config.lane != "accuracy":
            return
        if config.lane != "accuracy" and tiny.estimate_board_latency_us(config) > 11500:
            return
        key = semantic_key(config)
        if key in seen:
            return
        seen.add(key)
        configs.append(config)

    for config in anchor_configs():
        add(config)

    lanes = ["fast", "balance", "accuracy"] if lane == "all" else [lane]
    lane_options = {
        "fast": {
            "filters": [(5, 10, 20), (6, 12, 24), (7, 14, 28), (8, 12, 24), (8, 16, 24), (8, 16, 32)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00318, 0.00326, 0.00334],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008],
            "batch": [16, 24, 32],
            "augment": ["sdiag_base", "sdiag_soft", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "dense": [0],
        },
        "balance": {
            "filters": [(7, 14, 28), (8, 16, 24), (8, 16, 32), (8, 18, 36), (10, 18, 36)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00310, 0.00318, 0.00326, 0.00334],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4, 3.0e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008, 0.01],
            "batch": [16, 24],
            "augment": ["sdiag_soft", "sdiag_mid", "sdiag_lowres", "sdiag_speed", "sdiag_roi", "v4_lowres_mix"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "dense": [0, 8],
        },
        "accuracy": {
            "filters": [(8, 18, 36), (10, 18, 36), (10, 20, 40), (10, 20, 48), (12, 24, 48), (16, 24, 48)],
            "lr": [0.0012, 0.0016, 0.0020, 0.0023, 0.0026, 0.00286, 0.00318],
            "l2": [1.0e-6, 5.0e-6, 1.0e-5, 3.0e-5, 7.0e-5, 1.0e-4, 3.0e-4, 6.0e-4],
            "dropout": [0.0, 0.003, 0.008, 0.02, 0.05, 0.08, 0.12, 0.20],
            "batch": [16, 24, 32],
            "augment": ["sdiag_soft", "sdiag_mid", "sdiag_hard", "sdiag_lowres", "sdiag_speed", "v4_highspeed"],
            "arch": ["spacetodepth_conv", "depthwise_pool", "hardswish_depthwise", "stride_conv", "conv_pool"],
            "dense": [0, 8, 16, 24, 32],
        },
    }
    pool: list[V5Config] = []

    def build_candidate(
        current_lane: str,
        opts: dict[str, list[object]],
        *,
        filters: tuple[int, int, int] | None = None,
        lr: float | None = None,
        l2: float | None = None,
        dropout: float | None = None,
        batch: int | None = None,
        augment: str | None = None,
        arch: str | None = None,
        dense: int | None = None,
        pool_name: str | None = None,
        activation: str | None = None,
        head: str | None = None,
        logits: bool | None = None,
        cw: str | None = None,
        calibration: str | None = None,
        extra: bool | None = None,
    ) -> V5Config:
        filters = filters if filters is not None else rng.choice(opts["filters"])  # type: ignore[arg-type]
        lr = float(lr if lr is not None else rng.choice(opts["lr"]))
        l2 = float(l2 if l2 is not None else rng.choice(opts["l2"]))
        dropout = float(dropout if dropout is not None else rng.choice(opts["dropout"]))
        batch = int(batch if batch is not None else rng.choice(opts["batch"]))
        augment = str(augment if augment is not None else rng.choice(opts["augment"]))
        arch = str(arch if arch is not None else rng.choice(opts["arch"]))
        dense = int(dense if dense is not None else rng.choice(opts["dense"]))
        pool_values = ["max", "avg"] if aggressive else ["max"]
        activation_values = ["relu", "relu6"] if aggressive else ["relu"]
        head_values = ["subclass", "dual_parent", "parent"] if aggressive and current_lane != "fast" else ["subclass"]
        logits_values = [False, True] if aggressive and current_lane != "fast" else [False]
        cw_values = ["none", "sqrt_balanced", "scarce_soft"] if aggressive and current_lane == "accuracy" else ["none"]
        calibration_values = ["mild_stress", "balanced_clean", "balanced_rotmirror"]
        if aggressive:
            calibration_values += ["hard_stress", "hard_clean"]
        pool_name = str(pool_name if pool_name is not None else rng.choice(pool_values))
        activation = str(activation if activation is not None else rng.choice(activation_values))
        if arch == "hardswish_depthwise":
            activation = "hard_swish"
        head = str(head if head is not None else rng.choice(head_values))
        logits = bool(logits if logits is not None else rng.choice(logits_values))
        cw = str(cw if cw is not None else rng.choice(cw_values))
        calibration = str(calibration if calibration is not None else rng.choice(calibration_values))
        extra_values = [False, True] if current_lane == "accuracy" and aggressive else [False]
        extra = bool(extra if extra is not None else rng.choice(extra_values))
        name = (
            f"v5_{current_lane}_{arch}_f{'-'.join(map(str, filters))}_"
            f"d{dense}_do{dropout:g}_l2{l2:g}_lr{lr:g}_b{batch}_"
            f"{pool_name}_k3_x{int(extra)}_{augment}_{activation}_{head}_"
            f"{'logits' if logits else 'softmax'}_{cw}_{calibration}"
        )
        return make_config(
            name,
            current_lane,
            arch,
            filters,
            lr,
            l2,
            dropout,
            augment,
            dense_units=dense,
            batch_size=batch,
            pool=pool_name,
            extra_conv=extra,
            activation=activation,
            head=head,
            logits=logits,
            class_weight=cw,
            calibration=calibration,
        )

    for current_lane in lanes:
        opts = lane_options[current_lane]
        # Axis probes keep strong local coverage around every important knob.
        base_filters = (6, 12, 24) if current_lane == "fast" else ((10, 20, 40) if current_lane == "accuracy" else (8, 16, 32))
        base_lr = 0.0023 if current_lane == "accuracy" else 0.00318
        base_aug = "sdiag_mid" if current_lane == "accuracy" else ("sdiag_base" if current_lane == "fast" else "sdiag_lowres")
        for filters in opts["filters"]:
            pool.append(build_candidate(current_lane, opts, filters=filters, lr=base_lr, l2=1.0e-4, dropout=0.003, batch=16, augment=base_aug, arch="spacetodepth_conv", dense=0))
        for lr in opts["lr"]:
            pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=float(lr), l2=1.0e-4, dropout=0.003, batch=16, augment=base_aug, arch="spacetodepth_conv", dense=0))
        for l2 in opts["l2"]:
            pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=base_lr, l2=float(l2), dropout=0.003, batch=16, augment=base_aug, arch="spacetodepth_conv", dense=0))
        for dropout in opts["dropout"]:
            pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=base_lr, l2=1.0e-4, dropout=float(dropout), batch=16, augment=base_aug, arch="spacetodepth_conv", dense=0))
        for augment in opts["augment"]:
            pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=base_lr, l2=1.0e-4, dropout=0.003, batch=16, augment=str(augment), arch="spacetodepth_conv", dense=0))
        for arch in opts["arch"]:
            pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=base_lr, l2=1.0e-4, dropout=0.003, batch=16, augment=base_aug, arch=str(arch), dense=0))
        if aggressive and current_lane != "fast":
            for head in ["subclass", "dual_parent", "parent"]:
                for logits in [False, True]:
                    pool.append(build_candidate(current_lane, opts, filters=base_filters, lr=base_lr, l2=1.0e-4, dropout=0.003, batch=16, augment=base_aug, arch="spacetodepth_conv", dense=0, head=head, logits=logits))

        target_pool = max(limit * (30 if current_lane == "accuracy" else 18), 300)
        attempts = 0
        while len(pool) < target_pool and attempts < target_pool * 8:
            attempts += 1
            pool.append(build_candidate(current_lane, opts))
    rng.shuffle(pool)
    pool.sort(key=lambda config: candidate_priority(config, rng))
    for config in pool:
        add(config)
        if len(configs) >= limit:
            break
    return configs[:limit]


def load_candidates(path: Path, limit: int, lane: str) -> list[V5Config]:
    data = json.loads(path.read_text(encoding="utf-8"))
    loaded: list[V5Config] = []
    seen: set[tuple[object, ...]] = set()
    for index, item in enumerate(data.get("candidates", data if isinstance(data, list) else [])):
        config_data = item.get("config", item)
        label = str(item.get("label") or config_data.get("name") or f"candidate_{index:03d}")
        config = config_from_dict(config_data, label)
        if lane != "all" and config.lane != lane:
            continue
        key = semantic_key(config)
        if key in seen:
            continue
        seen.add(key)
        loaded.append(config)
        if len(loaded) >= limit:
            break
    return loaded


def save_candidates(path: Path, configs: list[V5Config]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"candidates": [{"label": config.name, "config": config_to_dict(config)} for config in configs]}
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


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
        seen.add(str(item.get("trial", "")))
    return results, seen


def safe_name(value: str) -> str:
    return "".join(ch if ch.isalnum() or ch in {"-", "_", "."} else "_" for ch in value)[:180]


def sort_results(results: list[dict[str, object]]) -> list[dict[str, object]]:
    return sorted(
        results,
        key=lambda item: (
            float(item.get("score_min", 0.0)),
            float(item.get("score_mean", 0.0)),
            float(item.get("hard_parent_worst_min", 0.0)),
            float(item.get("stress_parent_worst_min", 0.0)),
            float(item.get("clean_parent_worst_min", 0.0)),
            -float(item.get("estimated_board_us", 999999)),
        ),
        reverse=True,
    )


def write_summary(output_dir: Path, results: list[dict[str, object]]) -> None:
    ranked = sort_results(results)
    summary = {
        "best": ranked[0] if ranked else None,
        "top_results": ranked[: min(50, len(ranked))],
        "summary": [
            {
                "trial": item["trial"],
                "lane": item["lane"],
                "score_mean": item["score_mean"],
                "score_min": item["score_min"],
                "clean_parent_accuracy_mean": item["clean_parent_accuracy_mean"],
                "clean_parent_worst_min": item["clean_parent_worst_min"],
                "hard_parent_accuracy_mean": item["hard_parent_accuracy_mean"],
                "hard_parent_worst_min": item["hard_parent_worst_min"],
                "stress_parent_worst_min": item["stress_parent_worst_min"],
                "agreement_mean": item["agreement_mean"],
                "estimated_board_us": item["estimated_board_us"],
                "int8_bytes_mean": item["int8_bytes_mean"],
            }
            for item in ranked
        ],
    }
    (output_dir / "search_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    with (output_dir / "top_models.csv").open("w", encoding="utf-8", newline="") as handle:
        fieldnames = [
            "trial",
            "lane",
            "score_mean",
            "score_min",
            "clean_parent_accuracy_mean",
            "clean_parent_worst_min",
            "hard_parent_accuracy_mean",
            "hard_parent_worst_min",
            "stress_parent_worst_min",
            "agreement_mean",
            "estimated_board_us",
            "int8_bytes_mean",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in summary["summary"]:
            writer.writerow(row)


def final_retrain_full(
    config: V5Config,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    args: argparse.Namespace,
    output_dir: Path,
    rank: int,
) -> dict[str, object]:
    seed = args.seed + 19000 + rank * 1009
    tiny.set_reproducible(seed)
    tf.keras.backend.clear_session()
    final_dir = output_dir / "final_exports" / f"rank{rank:02d}_{safe_name(config.name)}"
    final_dir.mkdir(parents=True, exist_ok=True)
    all_idx = np.arange(len(y_sub), dtype=np.int64)
    model = build_model(config)
    train_ds = make_dataset(x, y_sub, y_parent, all_idx, config, seed, True)
    full_epochs = args.full_epochs if args.full_epochs > 0 else args.epochs
    model.fit(train_ds, epochs=full_epochs, verbose=0)
    deploy = deploy_model(model, config)
    model.save(final_dir / "model_full.keras")
    if deploy is not model:
        deploy.save(final_dir / "deploy_full.keras")
    hard_idx, missing_hard = hard_indices(paths)
    float_path = final_dir / "v5_full_float.tflite"
    int8_path = final_dir / "v5_full_int8.tflite"
    float_bytes = export_float_tflite(deploy, float_path)
    calibration_x = representative_array(x, y_sub, hard_idx, config.calibration, seed + 77, args.calibration_limit)
    int8_bytes = export_int8_tflite(deploy, calibration_x, int8_path)
    kind = output_kind(config)
    keras_all_preds = predictions_from_outputs(keras_outputs(deploy, x, config.batch_size))
    int8_all_preds = predictions_from_outputs(tflite_outputs(int8_path, x))
    int8_hard_preds = predictions_from_outputs(tflite_outputs(int8_path, x[hard_idx])) if len(hard_idx) else np.asarray([], dtype=np.int64)
    stress = stress_metrics(int8_path, x, y_sub, y_parent, all_idx, kind, args.stress_names)
    result = {
        "rank": rank,
        "trial": config.name,
        "config": config_to_dict(config),
        "seed": seed,
        "epochs": full_epochs,
        "export": {
            "float_path": str(float_path),
            "float_bytes": float_bytes,
            "int8_path": str(int8_path),
            "int8_bytes": int8_bytes,
            "calibration": config.calibration,
            "output_kind": kind,
        },
        "keras_all": evaluate_predictions(y_sub, y_parent, keras_all_preds, kind),
        "int8_all": evaluate_predictions(y_sub, y_parent, int8_all_preds, kind),
        "int8_hard": evaluate_predictions(y_sub[hard_idx], y_parent[hard_idx], int8_hard_preds, kind) if len(hard_idx) else {},
        "int8_stress_all": stress,
        "agreement_all": float(np.mean(keras_all_preds == int8_all_preds)),
        "hard_clean_missing": missing_hard,
        "output_dir": str(final_dir),
    }
    (final_dir / "final_result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="V5 8-visual-subclass tiny32 scan with int8 hard/stress scoring.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--lane", choices=["fast", "balance", "accuracy", "all"], default="balance")
    parser.add_argument("--mode", choices=["manifest", "smoke", "coarse", "retest", "fine", "final"], default="coarse")
    parser.add_argument("--candidates-json", type=Path)
    parser.add_argument("--write-candidates", type=Path)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--aggressive", action="store_true")
    parser.add_argument("--max-trials", type=int, default=120)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--seeds", default="20261701,20261702")
    parser.add_argument("--seed", type=int, default=20261700)
    parser.add_argument("--epochs", type=int, default=180)
    parser.add_argument("--patience", type=int, default=24)
    parser.add_argument("--final-top-k", type=int, default=0)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--calibration-limit", type=int, default=192)
    parser.add_argument("--save-artifacts", action="store_true")
    args = parser.parse_args()
    args.stress_names = [name.strip() for name in args.stress.split(",") if name.strip()]

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(int(os.environ.get("TF_NUM_INTEROP_THREADS", "2")))
    tf.config.threading.set_intra_op_parallelism_threads(int(os.environ.get("TF_NUM_INTRAOP_THREADS", "4")))

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime(f"v5_visual_subclass_{args.mode}_{args.lane}_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    x, y_sub, y_parent, paths, manifest_rows = load_dataset_v5(Path(args.dataset_dir))
    write_maps_and_manifest(output_dir, Path(args.dataset_dir), x, y_sub, y_parent, paths, manifest_rows)
    if args.mode == "manifest":
        print("output_dir=" + str(output_dir), flush=True)
        print("sample_count=" + str(len(y_sub)), flush=True)
        print("visual_counts=" + json.dumps({VISUAL_CLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(VISUAL_CLASS_NAMES))}, ensure_ascii=False), flush=True)
        return

    if args.mode == "smoke":
        configs = [
            make_config("smoke_fast_6_12_24", "fast", "spacetodepth_conv", (6, 12, 24), 0.00286, 1.0e-4, 0.0, "sdiag_base"),
            make_config("smoke_balance_8_16_32", "balance", "spacetodepth_conv", (8, 16, 32), 0.00318, 1.0e-4, 0.003, "sdiag_lowres"),
        ]
        args.seeds = args.seeds.split(",")[0]
        args.epochs = min(args.epochs, 4)
        args.patience = min(args.patience, 2)
        args.final_top_k = max(args.final_top_k, 2)
    elif args.candidates_json:
        configs = load_candidates(args.candidates_json, args.max_trials, args.lane)
    else:
        configs = generate_candidates(args.lane, args.max_trials, args.seed, args.aggressive or args.mode in {"coarse", "fine"})

    pre_shard_count = len(configs)
    if args.shard_count < 1:
        raise ValueError("--shard-count must be >= 1")
    if not 0 <= args.shard_index < args.shard_count:
        raise ValueError("--shard-index must satisfy 0 <= index < count")
    if args.shard_count > 1:
        configs = [config for index, config in enumerate(configs) if index % args.shard_count == args.shard_index]
    if args.write_candidates:
        save_candidates(args.write_candidates, configs)

    run_config = {
        "args": {key: jsonable(value) for key, value in vars(args).items() if key != "stress_names"},
        "stress_names": args.stress_names,
        "visual_class_names": VISUAL_CLASS_NAMES,
        "parent_names": PARENT_NAMES,
        "visual_to_parent": VISUAL_TO_PARENT.tolist(),
        "sample_count": int(len(y_sub)),
        "visual_counts": {VISUAL_CLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(VISUAL_CLASS_NAMES))},
        "parent_counts": {PARENT_NAMES[i]: int(np.sum(y_parent == i)) for i in range(len(PARENT_NAMES))},
        "hard_clean": {
            "basenames": HARD_CLEAN_BASENAMES,
            "count": int(len(hard_indices(paths)[0])),
            "missing": hard_indices(paths)[1],
        },
        "pre_shard_trials": pre_shard_count,
        "generated_trials": len(configs),
        "configs": [config_to_dict(config) for config in configs],
    }
    (output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2, ensure_ascii=False), encoding="utf-8")

    seeds = [int(part.strip()) for part in str(args.seeds).split(",") if part.strip()]
    results_path = output_dir / "trial_results.jsonl"
    results, completed = load_existing_results(results_path) if args.resume else ([], set())
    pending = [config for config in configs if config.name not in completed]
    print("output_dir=" + str(output_dir), flush=True)
    print("mode=" + args.mode + " lane=" + args.lane + f" trials={len(configs)} pending={len(pending)} completed={len(completed)} seeds={seeds}", flush=True)
    print("visual_counts=" + json.dumps(run_config["visual_counts"], ensure_ascii=False), flush=True)

    for index, config in enumerate(configs, start=1):
        if config.name in completed:
            print(f"[{index:03d}/{len(configs):03d}] {config.name} skipped_existing", flush=True)
            continue
        started = time.time()
        seed_results: list[dict[str, object]] = []
        for seed in seeds:
            try:
                seed_results.append(run_seed_case(config, seed, x, y_sub, y_parent, paths, args, output_dir, args.save_artifacts or args.mode == "smoke"))
            except Exception as exc:  # noqa: BLE001
                seed_results.append({"seed": seed, "status": "failed", "error": f"{type(exc).__name__}: {exc}", "score": 0.0})
        summary = summarize_trial(config, seed_results)
        summary["seconds"] = round(time.time() - started, 3)
        with results_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(summary, ensure_ascii=False) + "\n")
            handle.flush()
            os.fsync(handle.fileno())
        results.append(summary)
        print(
            f"[{index:03d}/{len(configs):03d}] {config.name} lane={config.lane} "
            f"score_mean={summary['score_mean']:.4f} score_min={summary['score_min']:.4f} "
            f"clean_acc={summary['clean_parent_accuracy_mean']:.4f} clean_worst_min={summary['clean_parent_worst_min']:.4f} "
            f"hard_acc={summary['hard_parent_accuracy_mean']:.4f} stress_worst_min={summary['stress_parent_worst_min']:.4f} "
            f"agree={summary['agreement_mean']:.4f} us={summary['estimated_board_us']} bytes={summary['int8_bytes_mean']:.0f} "
            f"seconds={summary['seconds']}",
            flush=True,
        )
        write_summary(output_dir, results)

    if not results:
        raise RuntimeError("no results available")
    write_summary(output_dir, results)
    ranked = sort_results(results)
    final_exports = []
    config_by_name = {config.name: config for config in configs}
    for rank, item in enumerate(ranked[: max(0, args.final_top_k)], start=1):
        config = config_by_name.get(str(item["trial"]))
        if config is None:
            config = config_from_dict(item["config"], str(item["trial"]))
        final_exports.append(final_retrain_full(config, x, y_sub, y_parent, paths, args, output_dir, rank))
        print(
            f"final_rank={rank} trial={config.name} "
            f"int8_all_acc={final_exports[-1]['int8_all']['parent']['accuracy']:.4f} "
            f"int8_all_worst={final_exports[-1]['int8_all']['parent']['worst_recall']:.4f} "
            f"hard_acc={final_exports[-1].get('int8_hard', {}).get('parent', {}).get('accuracy', 0.0):.4f} "
            f"bytes={final_exports[-1]['export']['int8_bytes']} dir={final_exports[-1]['output_dir']}",
            flush=True,
        )
    if final_exports:
        (output_dir / "final_summary.json").write_text(json.dumps({"final_exports": final_exports}, indent=2, ensure_ascii=False), encoding="utf-8")
    print("search_summary_path=" + str(output_dir / "search_summary.json"), flush=True)


if __name__ == "__main__":
    main()
