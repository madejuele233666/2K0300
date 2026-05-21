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
WEAPON_PARENT_INDEX = 2
C4_SUBCLASS_INDEX = 7
WEAPON_SUBCLASS_NAMES = ["firearms_short", "firearms_long", "explosive_grenade", "explosive_c4"]
WEAPON_SUBCLASS_SOURCE_INDEXES = np.asarray([4, 5, 6, 7], dtype=np.int64)
WEAPON_SUBCLASS_TO_LOCAL = np.full(len(VISUAL_CLASS_NAMES), 0, dtype=np.int64)
for _local_index, _subclass_index in enumerate(WEAPON_SUBCLASS_SOURCE_INDEXES):
    WEAPON_SUBCLASS_TO_LOCAL[_subclass_index] = _local_index
C4_BOX_BASENAMES = frozenset(
    {
        "explosive_019.jpg",
        "explosive_124.jpg",
        "explosive_141.jpg",
        "explosive_142.jpg",
        "explosive_154.jpg",
    }
)
C4_CIRCUIT_BASENAMES = frozenset({"explosive_030.jpg"})
C4_INSTANCE_BASENAMES = (
    "explosive_019.jpg",
    "explosive_030.jpg",
    "explosive_124.jpg",
    "explosive_141.jpg",
    "explosive_142.jpg",
    "explosive_154.jpg",
)
C4_INSTANCE_TO_INDEX = {name: index for index, name in enumerate(C4_INSTANCE_BASENAMES)}
WEAPON_AUX_HEADS = {"parent_weapon_aux", "parent_weapon_c4", "parent_weapon_c4_box_circuit", "parent_weapon_c4_instance"}
C4_ATTR_HEADS = {
    "parent_c4_attr",
    "parent_weapon_c4",
    "parent_c4_box_circuit",
    "parent_weapon_c4_box_circuit",
    "parent_c4_instance",
    "parent_weapon_c4_instance",
}
C4_BOX_CIRCUIT_HEADS = {
    "parent_c4_box_circuit",
    "parent_weapon_c4_box_circuit",
    "parent_c4_instance",
    "parent_weapon_c4_instance",
}
C4_INSTANCE_HEADS = {"parent_c4_instance", "parent_weapon_c4_instance"}
V6_PARENT_HEADS = WEAPON_AUX_HEADS | C4_ATTR_HEADS | C4_BOX_CIRCUIT_HEADS | C4_INSTANCE_HEADS
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
ACTIVE_HARD_CLEAN_BASENAMES = HARD_CLEAN_BASENAMES
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
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,"
    "cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,"
    "cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04"
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
    validation_transforms: str = "clean"
    head: str = "subclass"
    logits: bool = False
    class_weight: str = "none"
    calibration: str = "mild_stress"
    parent_loss_weight: float = 1.0
    subclass_loss_weight: float = 0.35
    weapon_loss_weight: float = 0.10
    c4_loss_weight: float = 0.30
    c4_pos_weight: float = 4.0
    c4_focal_gamma: float = 0.0
    c4_box_loss_weight: float = 0.20
    c4_box_pos_weight: float = 4.0
    c4_circuit_loss_weight: float = 0.40
    c4_circuit_pos_weight: float = 8.0
    c4_instance_loss_weight: float = 0.03
    teacher_loss_weight: float = 0.0
    teacher_temperature: float = 2.0
    c4_teacher_scale: float = 1.0
    negative_margin_weight: float = 0.0
    negative_margin: float = 1.0
    geometric_consistency_weight: float = 0.0
    geometric_consistency_group: int = 8
    decoupled: bool = False
    decouple_parent_epochs: int = 40
    decouple_aux_epochs: int = 30
    decouple_joint_lr_scale: float = 0.25


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
        "v6_camera_mild": AugmentConfig(
            "v6_camera_mild",
            pad=2,
            brightness=0.10,
            contrast_delta=0.14,
            noise_std=0.025,
            salt_pepper=0.003,
            blur_prob=0.28,
            blur_kernel=3,
            downscale_prob=0.22,
            downscale_min=20,
        ),
        "v6_camera_blur_noise": AugmentConfig(
            "v6_camera_blur_noise",
            pad=2,
            brightness=0.12,
            contrast_delta=0.18,
            noise_std=0.040,
            salt_pepper=0.006,
            blur_prob=0.46,
            blur_kernel=5,
            downscale_prob=0.28,
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
                    "c4_group": c4_group_for_name(path.name),
                    "c4_instance_index": C4_INSTANCE_TO_INDEX.get(path.name, -1),
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


def load_hard_basenames(path: Path | None) -> list[str]:
    if path is None:
        return list(HARD_CLEAN_BASENAMES)
    names: list[str] = []
    seen: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name = Path(line.split(",", 1)[0].strip()).name
        if name and name not in seen:
            seen.add(name)
            names.append(name)
    if not names:
        raise ValueError(f"hard basenames file is empty: {path}")
    return names


def hard_indices(paths: list[str]) -> tuple[np.ndarray, list[str]]:
    by_name: dict[str, list[int]] = {}
    for index, path in enumerate(paths):
        by_name.setdefault(Path(path).name, []).append(index)
    selected: list[int] = []
    missing: list[str] = []
    for name in ACTIVE_HARD_CLEAN_BASENAMES:
        indexes = by_name.get(name)
        if not indexes:
            missing.append(name)
            continue
        selected.extend(indexes)
    return np.asarray(sorted(set(selected)), dtype=np.int64), missing


def c4_group_for_name(filename: str) -> str:
    if filename in C4_BOX_BASENAMES:
        return "c4_box_like"
    if filename in C4_CIRCUIT_BASENAMES:
        return "c4_circuit_like"
    return "non_c4"


def c4_metadata_for_paths(paths: list[str] | np.ndarray) -> dict[str, np.ndarray]:
    filenames = np.asarray([Path(str(path)).name for path in paths], dtype=object)
    c4_box = np.asarray([name in C4_BOX_BASENAMES for name in filenames], dtype=bool)
    c4_circuit = np.asarray([name in C4_CIRCUIT_BASENAMES for name in filenames], dtype=bool)
    c4_mask = c4_box | c4_circuit
    c4_instance = np.asarray([C4_INSTANCE_TO_INDEX.get(str(name), 0) for name in filenames], dtype=np.int64)
    return {
        "filename": filenames,
        "c4_mask": c4_mask,
        "c4_box": c4_box,
        "c4_circuit": c4_circuit,
        "c4_instance": c4_instance,
    }


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
        "c4_box_basenames": sorted(C4_BOX_BASENAMES),
        "c4_circuit_basenames": sorted(C4_CIRCUIT_BASENAMES),
        "c4_instance_basenames": list(C4_INSTANCE_BASENAMES),
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
            "c4_group",
            "c4_instance_index",
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
        "c4_closed_set": {
            "box_basenames": sorted(C4_BOX_BASENAMES),
            "circuit_basenames": sorted(C4_CIRCUIT_BASENAMES),
            "instance_basenames": list(C4_INSTANCE_BASENAMES),
            "box_count": int(sum(1 for row in manifest_rows if row.get("c4_group") == "c4_box_like")),
            "circuit_count": int(sum(1 for row in manifest_rows if row.get("c4_group") == "c4_circuit_like")),
        },
        "hard_clean_basenames": ACTIVE_HARD_CLEAN_BASENAMES,
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
    paths: list[str],
    indexes: np.ndarray,
    config: V5Config,
    seed: int,
    training: bool,
    teacher_parent_soft: np.ndarray | None = None,
    teacher_parent_weight: np.ndarray | None = None,
    parent_sample_weight: np.ndarray | None = None,
    negative_parent_id: np.ndarray | None = None,
    negative_parent_weight: np.ndarray | None = None,
) -> tf.data.Dataset:
    x_data = x[indexes]
    sub_data = y_sub[indexes]
    parent_data = y_parent[indexes]
    path_data = np.asarray(paths, dtype=object)[indexes]
    c4_meta = c4_metadata_for_paths(path_data)
    teacher_data = teacher_parent_soft[indexes] if teacher_parent_soft is not None else None
    teacher_weight_data = teacher_parent_weight[indexes] if teacher_parent_weight is not None else None
    parent_weight_data = parent_sample_weight[indexes] if parent_sample_weight is not None else None
    negative_id_data = negative_parent_id[indexes] if negative_parent_id is not None else None
    negative_weight_data = negative_parent_weight[indexes] if negative_parent_weight is not None else None
    use_consistency = config.geometric_consistency_weight > 0 and config.head != "subclass"
    if training and use_consistency:
        rng = np.random.default_rng(seed)
        order = rng.permutation(len(x_data))
        x_data = x_data[order]
        sub_data = sub_data[order]
        parent_data = parent_data[order]
        path_data = path_data[order]
        c4_meta = {key: value[order] for key, value in c4_meta.items()}
        if teacher_data is not None:
            teacher_data = teacher_data[order]
        if teacher_weight_data is not None:
            teacher_weight_data = teacher_weight_data[order]
        if parent_weight_data is not None:
            parent_weight_data = parent_weight_data[order]
        if negative_id_data is not None:
            negative_id_data = negative_id_data[order]
        if negative_weight_data is not None:
            negative_weight_data = negative_weight_data[order]
    expand_rot_mirror = (training and config.train_transforms == "rot_mirror") or (
        (not training) and config.validation_transforms == "rot_mirror"
    )
    if expand_rot_mirror:
        x_data, sub_data, parent_data = expand_rotation_mirror_with_parent(x_data, sub_data, parent_data)
        path_data = np.repeat(path_data, 8, axis=0)
        c4_meta = {key: np.repeat(value, 8, axis=0) for key, value in c4_meta.items()}
        if teacher_data is not None:
            teacher_data = np.repeat(teacher_data, 8, axis=0)
        if teacher_weight_data is not None:
            teacher_weight_data = np.repeat(teacher_weight_data, 8, axis=0)
        if parent_weight_data is not None:
            parent_weight_data = np.repeat(parent_weight_data, 8, axis=0)
        if negative_id_data is not None:
            negative_id_data = np.repeat(negative_id_data, 8, axis=0)
        if negative_weight_data is not None:
            negative_weight_data = np.repeat(negative_weight_data, 8, axis=0)
    elif training and config.train_transforms != "none" and config.train_transforms != "rot_mirror":
        raise ValueError(f"unknown train_transforms: {config.train_transforms}")
    elif (not training) and config.validation_transforms not in {"clean", "rot_mirror"}:
        raise ValueError(f"unknown validation_transforms: {config.validation_transforms}")

    use_teacher = config.teacher_loss_weight > 0
    if use_teacher and teacher_data is None:
        raise ValueError(f"{config.name} requires teacher_parent_soft")
    teacher_weights = None
    if use_teacher:
        teacher_preds = np.argmax(teacher_data, axis=1)  # type: ignore[arg-type]
        if teacher_weight_data is not None:
            teacher_weights = teacher_weight_data.astype(np.float32)
        else:
            teacher_weights = (teacher_preds == parent_data).astype(np.float32)
            teacher_weights = teacher_weights * np.where(sub_data == C4_SUBCLASS_INDEX, config.c4_teacher_scale, 1.0).astype(np.float32)
    parent_weights = parent_weight_data.astype(np.float32) if parent_weight_data is not None else np.ones_like(parent_data, dtype=np.float32)
    use_negative_margin = config.negative_margin_weight > 0
    if use_negative_margin and negative_id_data is None:
        raise ValueError(f"{config.name} requires negative_parent_id")
    negative_labels = None
    negative_weights = None
    if use_negative_margin:
        negative_ids = np.asarray(negative_id_data, dtype=np.int64)  # type: ignore[arg-type]
        negative_valid = (negative_ids >= 0) & (negative_ids < len(PARENT_NAMES)) & (negative_ids != parent_data)
        negative_labels = np.stack([parent_data, np.where(negative_valid, negative_ids, parent_data)], axis=1).astype(np.int64)
        if negative_weight_data is not None:
            negative_weights = np.asarray(negative_weight_data, dtype=np.float32) * negative_valid.astype(np.float32)
        else:
            negative_weights = negative_valid.astype(np.float32)

    if config.head == "dual_parent":
        labels = {"parent": parent_data, "subclass": sub_data}
        sample_weights = {"parent": parent_weights, "subclass": np.ones_like(parent_data, dtype=np.float32)} if (use_teacher or parent_weight_data is not None or use_negative_margin) else None
        if use_consistency and sample_weights is None:
            sample_weights = {"parent": np.ones_like(parent_data, dtype=np.float32), "subclass": np.ones_like(parent_data, dtype=np.float32)}
        if use_teacher:
            labels["parent_teacher"] = teacher_data.astype(np.float32)  # type: ignore[union-attr]
            sample_weights["parent_teacher"] = teacher_weights  # type: ignore[index]
        if use_consistency:
            labels["parent_consistency"] = np.zeros((len(parent_data), 1), dtype=np.float32)
            sample_weights["parent_consistency"] = np.ones_like(parent_data, dtype=np.float32)  # type: ignore[index]
        if use_negative_margin:
            labels["parent_margin"] = negative_labels  # type: ignore[assignment]
            sample_weights["parent_margin"] = negative_weights  # type: ignore[index]
    elif config.head == "parent":
        if use_teacher or use_negative_margin or use_consistency:
            labels = {"parent": parent_data}
            sample_weights = {
                "parent": parent_weights,
            }
            if use_teacher:
                labels["parent_teacher"] = teacher_data.astype(np.float32)  # type: ignore[union-attr]
                sample_weights["parent_teacher"] = teacher_weights
            if use_consistency:
                labels["parent_consistency"] = np.zeros((len(parent_data), 1), dtype=np.float32)
                sample_weights["parent_consistency"] = np.ones_like(parent_data, dtype=np.float32)
            if use_negative_margin:
                labels["parent_margin"] = negative_labels  # type: ignore[assignment]
                sample_weights["parent_margin"] = negative_weights
        else:
            labels = parent_data
            sample_weights = parent_weights if parent_weight_data is not None else None
    elif config.head == "subclass":
        labels = sub_data
        sample_weights = None
    elif config.head in V6_PARENT_HEADS:
        labels = {"parent": parent_data}
        sample_weights = {"parent": parent_weights}
        if use_teacher:
            labels["parent_teacher"] = teacher_data.astype(np.float32)  # type: ignore[union-attr]
            sample_weights["parent_teacher"] = teacher_weights
        if use_consistency:
            labels["parent_consistency"] = np.zeros((len(parent_data), 1), dtype=np.float32)
            sample_weights["parent_consistency"] = np.ones_like(parent_data, dtype=np.float32)
        if use_negative_margin:
            labels["parent_margin"] = negative_labels  # type: ignore[assignment]
            sample_weights["parent_margin"] = negative_weights
        if config.head in WEAPON_AUX_HEADS:
            weapon_mask = (parent_data == WEAPON_PARENT_INDEX).astype(np.float32)
            labels["weapon_sub"] = WEAPON_SUBCLASS_TO_LOCAL[sub_data].astype(np.int64)
            sample_weights["weapon_sub"] = weapon_mask
        if config.head in C4_ATTR_HEADS:
            labels["c4_attr"] = (sub_data == C4_SUBCLASS_INDEX).astype(np.float32)[:, None]
            sample_weights["c4_attr"] = np.ones_like(parent_data, dtype=np.float32)
        if config.head in C4_BOX_CIRCUIT_HEADS:
            labels["c4_box"] = c4_meta["c4_box"].astype(np.float32)[:, None]
            labels["c4_circuit"] = c4_meta["c4_circuit"].astype(np.float32)[:, None]
            sample_weights["c4_box"] = np.ones_like(parent_data, dtype=np.float32)
            sample_weights["c4_circuit"] = np.ones_like(parent_data, dtype=np.float32)
        if config.head in C4_INSTANCE_HEADS:
            labels["c4_instance"] = c4_meta["c4_instance"].astype(np.int64)
            sample_weights["c4_instance"] = c4_meta["c4_mask"].astype(np.float32)
    else:
        raise ValueError(f"unknown head: {config.head}")
    if sample_weights is None:
        ds = tf.data.Dataset.from_tensor_slices((x_data, labels))
    else:
        ds = tf.data.Dataset.from_tensor_slices((x_data, labels, sample_weights))
    if training:
        if not use_consistency:
            ds = ds.shuffle(len(x_data), seed=seed, reshuffle_each_iteration=True)
        if sample_weights is None:
            ds = ds.map(lambda image, label: (tiny.augment_image(image, config.augment), label), num_parallel_calls=tf.data.AUTOTUNE)
        else:
            ds = ds.map(lambda image, label, weight: (tiny.augment_image(image, config.augment), label, weight), num_parallel_calls=tf.data.AUTOTUNE)
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


def weighted_c4_bce(pos_weight: float, focal_gamma: float) -> Callable[[tf.Tensor, tf.Tensor], tf.Tensor]:
    def loss(y_true: tf.Tensor, y_pred: tf.Tensor) -> tf.Tensor:
        y_true_f = tf.cast(y_true, tf.float32)
        y_pred_f = tf.clip_by_value(tf.cast(y_pred, tf.float32), 1.0e-5, 1.0 - 1.0e-5)
        bce = -(y_true_f * tf.math.log(y_pred_f) + (1.0 - y_true_f) * tf.math.log(1.0 - y_pred_f))
        weights = 1.0 + y_true_f * max(0.0, pos_weight - 1.0)
        if focal_gamma > 0:
            p_t = y_true_f * y_pred_f + (1.0 - y_true_f) * (1.0 - y_pred_f)
            weights = weights * tf.pow(1.0 - p_t, focal_gamma)
        return tf.reduce_mean(bce * weights, axis=-1)

    return loss


def negative_parent_margin_loss(margin: float) -> Callable[[tf.Tensor, tf.Tensor], tf.Tensor]:
    def loss(y_true: tf.Tensor, y_pred: tf.Tensor) -> tf.Tensor:
        ids = tf.cast(y_true, tf.int32)
        true_id = ids[:, 0]
        negative_id = ids[:, 1]
        true_logit = tf.gather(y_pred, true_id, batch_dims=1)
        negative_logit = tf.gather(y_pred, negative_id, batch_dims=1)
        return tf.nn.relu(float(margin) - true_logit + negative_logit)

    return loss


def parent_geometric_consistency_loss(group_size: int) -> Callable[[tf.Tensor, tf.Tensor], tf.Tensor]:
    def loss(y_true: tf.Tensor, y_pred: tf.Tensor) -> tf.Tensor:
        del y_true
        probs = tf.clip_by_value(tf.cast(y_pred, tf.float32), 1.0e-6, 1.0)
        batch = tf.shape(probs)[0]
        usable = (batch // int(group_size)) * int(group_size)
        grouped = tf.reshape(probs[:usable], (-1, int(group_size), tf.shape(probs)[-1]))
        mean_probs = tf.clip_by_value(tf.reduce_mean(grouped, axis=1, keepdims=True), 1.0e-6, 1.0)
        kl = tf.reduce_sum(grouped * (tf.math.log(grouped) - tf.math.log(mean_probs)), axis=-1)
        tail = tf.zeros((batch - usable,), dtype=tf.float32)
        return tf.concat([tf.reshape(kl, (-1,)), tail], axis=0)

    return loss


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
    parent_margin_out = (
        tf.keras.layers.Activation("linear", name="parent_margin")(parent_logits)
        if config.negative_margin_weight > 0
        else None
    )
    parent_teacher_out = (
        tf.keras.layers.Lambda(
            lambda t: tf.nn.softmax(t / max(config.teacher_temperature, 1.0e-6)),
            name="parent_teacher",
        )(parent_logits)
        if config.teacher_loss_weight > 0
        else None
    )
    parent_consistency_out = (
        tf.keras.layers.Softmax(name="parent_consistency")(parent_logits)
        if config.geometric_consistency_weight > 0
        else None
    )
    weapon_sub_logits = tf.keras.layers.Dense(len(WEAPON_SUBCLASS_NAMES), activation=None, name="weapon_sub_logits")(x)
    weapon_sub_out = (
        tf.keras.layers.Activation("linear", name="weapon_sub")(weapon_sub_logits)
        if config.logits
        else tf.keras.layers.Softmax(name="weapon_sub")(weapon_sub_logits)
    )
    c4_attr_out = tf.keras.layers.Dense(1, activation="sigmoid", name="c4_attr")(x)
    c4_box_out = tf.keras.layers.Dense(1, activation="sigmoid", name="c4_box")(x)
    c4_circuit_out = tf.keras.layers.Dense(1, activation="sigmoid", name="c4_circuit")(x)
    c4_instance_logits = tf.keras.layers.Dense(len(C4_INSTANCE_BASENAMES), activation=None, name="c4_instance_logits")(x)
    c4_instance_out = (
        tf.keras.layers.Activation("linear", name="c4_instance")(c4_instance_logits)
        if config.logits
        else tf.keras.layers.Softmax(name="c4_instance")(c4_instance_logits)
    )

    if config.head == "dual_parent":
        outputs = {"parent": parent_out, "subclass": subclass_out}
        if parent_teacher_out is not None:
            outputs["parent_teacher"] = parent_teacher_out
        loss = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
        losses: dict[str, object] = {"parent": loss, "subclass": loss}
        loss_weights: dict[str, float] = {"parent": config.parent_loss_weight, "subclass": config.subclass_loss_weight}
        if parent_teacher_out is not None:
            losses["parent_teacher"] = tf.keras.losses.KLDivergence()
            loss_weights["parent_teacher"] = config.teacher_loss_weight
        if parent_consistency_out is not None:
            outputs["parent_consistency"] = parent_consistency_out
            losses["parent_consistency"] = parent_geometric_consistency_loss(config.geometric_consistency_group)
            loss_weights["parent_consistency"] = config.geometric_consistency_weight
        if parent_margin_out is not None:
            outputs["parent_margin"] = parent_margin_out
            losses["parent_margin"] = negative_parent_margin_loss(config.negative_margin)
            loss_weights["parent_margin"] = config.negative_margin_weight
        model = tf.keras.Model(inputs, outputs, name=f"tiny32_v5_{config.name}")
        model.compile(
            optimizer=tf.keras.optimizers.Adam(config.learning_rate),
            loss=losses,
            loss_weights=loss_weights,
            jit_compile=False,
        )
        return model
    if config.head in V6_PARENT_HEADS or config.teacher_loss_weight > 0 or config.negative_margin_weight > 0 or config.geometric_consistency_weight > 0:
        outputs = {"parent": parent_out}
        losses: dict[str, object] = {"parent": tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)}
        loss_weights: dict[str, float] = {"parent": config.parent_loss_weight}
        if parent_teacher_out is not None:
            outputs["parent_teacher"] = parent_teacher_out
            losses["parent_teacher"] = tf.keras.losses.KLDivergence()
            loss_weights["parent_teacher"] = config.teacher_loss_weight
        if parent_consistency_out is not None:
            outputs["parent_consistency"] = parent_consistency_out
            losses["parent_consistency"] = parent_geometric_consistency_loss(config.geometric_consistency_group)
            loss_weights["parent_consistency"] = config.geometric_consistency_weight
        if parent_margin_out is not None:
            outputs["parent_margin"] = parent_margin_out
            losses["parent_margin"] = negative_parent_margin_loss(config.negative_margin)
            loss_weights["parent_margin"] = config.negative_margin_weight
        if config.head in WEAPON_AUX_HEADS:
            outputs["weapon_sub"] = weapon_sub_out
            losses["weapon_sub"] = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
            loss_weights["weapon_sub"] = config.weapon_loss_weight
        if config.head in C4_ATTR_HEADS:
            outputs["c4_attr"] = c4_attr_out
            losses["c4_attr"] = weighted_c4_bce(config.c4_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_attr"] = config.c4_loss_weight
        if config.head in C4_BOX_CIRCUIT_HEADS:
            outputs["c4_box"] = c4_box_out
            losses["c4_box"] = weighted_c4_bce(config.c4_box_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_box"] = config.c4_box_loss_weight
            outputs["c4_circuit"] = c4_circuit_out
            losses["c4_circuit"] = weighted_c4_bce(config.c4_circuit_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_circuit"] = config.c4_circuit_loss_weight
        if config.head in C4_INSTANCE_HEADS:
            outputs["c4_instance"] = c4_instance_out
            losses["c4_instance"] = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
            loss_weights["c4_instance"] = config.c4_instance_loss_weight
        model = tf.keras.Model(inputs, outputs, name=f"tiny32_v5_{config.name}")
        model.compile(
            optimizer=tf.keras.optimizers.Adam(config.learning_rate),
            loss=losses,
            loss_weights=loss_weights,
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
    if config.head == "dual_parent" or config.head in V6_PARENT_HEADS or config.teacher_loss_weight > 0 or config.negative_margin_weight > 0 or config.geometric_consistency_weight > 0:
        return tf.keras.Model(model.input, model.get_layer("parent").output, name=f"{model.name}_deploy_parent")
    return model


def output_kind(config: V5Config) -> str:
    return "parent" if config.head in {"parent", "dual_parent"} or config.head in V6_PARENT_HEADS or config.teacher_loss_weight > 0 or config.negative_margin_weight > 0 or config.geometric_consistency_weight > 0 else "subclass"


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


def parent_preds_from_output_preds(preds: np.ndarray, kind: str) -> np.ndarray:
    if kind == "parent":
        return preds.astype(np.int64)
    return VISUAL_TO_PARENT[preds.astype(np.int64)]


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


def keras_output_dict(model: tf.keras.Model, x: np.ndarray, batch_size: int) -> dict[str, np.ndarray]:
    out = model.predict(x, batch_size=batch_size, verbose=0)
    if isinstance(out, dict):
        return {str(key): np.asarray(value, dtype=np.float32) for key, value in out.items()}
    if isinstance(out, list):
        return {str(name): np.asarray(value, dtype=np.float32) for name, value in zip(model.output_names, out)}
    return {str(model.output_names[0] if model.output_names else "output"): np.asarray(out, dtype=np.float32)}


def c4_aux_score_dict(model: tf.keras.Model, x: np.ndarray, batch_size: int, config: V5Config) -> dict[str, np.ndarray]:
    if config.head not in C4_ATTR_HEADS and config.head not in C4_BOX_CIRCUIT_HEADS and config.head not in C4_INSTANCE_HEADS:
        return {}
    outputs = keras_output_dict(model, x, batch_size)
    scores: dict[str, np.ndarray] = {}
    for name in ("c4_attr", "c4_box", "c4_circuit"):
        values = outputs.get(name)
        if values is not None:
            scores[name] = np.asarray(values, dtype=np.float32).reshape(-1)
    instance = outputs.get("c4_instance")
    if instance is not None:
        instance_values = np.asarray(instance, dtype=np.float32)
        scores["c4_instance_max"] = np.max(instance_values, axis=1)
        scores["c4_instance_pred"] = np.argmax(instance_values, axis=1).astype(np.float32)
    evidence_sources = [scores[name] for name in ("c4_attr", "c4_box", "c4_circuit", "c4_instance_max") if name in scores]
    if evidence_sources:
        scores["c4_evidence"] = np.max(np.stack(evidence_sources, axis=0), axis=0)
    return scores


def c4_aux_scores(model: tf.keras.Model, x: np.ndarray, batch_size: int, config: V5Config) -> np.ndarray | None:
    scores = c4_aux_score_dict(model, x, batch_size, config)
    evidence = scores.get("c4_evidence")
    return evidence if evidence is not None else None


def c4_eval_metrics(
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    parent_preds: np.ndarray,
    c4_scores: np.ndarray | dict[str, np.ndarray] | None,
    paths: list[str] | np.ndarray | None = None,
    threshold: float = 0.5,
) -> dict[str, object]:
    c4_mask = y_sub == C4_SUBCLASS_INDEX
    if paths is not None:
        c4_meta = c4_metadata_for_paths(paths)
        c4_box_mask = c4_meta["c4_box"]
        c4_circuit_mask = c4_meta["c4_circuit"]
        c4_instance = c4_meta["c4_instance"]
        filenames = c4_meta["filename"]
    else:
        c4_box_mask = np.zeros_like(c4_mask, dtype=bool)
        c4_circuit_mask = np.zeros_like(c4_mask, dtype=bool)
        c4_instance = np.zeros_like(y_sub, dtype=np.int64)
        filenames = np.asarray([""] * len(y_sub), dtype=object)
    non_c4_weapon_mask = (y_parent == WEAPON_PARENT_INDEX) & (~c4_mask)
    non_weapon_mask = y_parent != WEAPON_PARENT_INDEX
    if isinstance(c4_scores, dict):
        evidence_scores = c4_scores.get("c4_evidence")
        c4_attr_scores = c4_scores.get("c4_attr", evidence_scores)
        c4_box_scores = c4_scores.get("c4_box")
        c4_circuit_scores = c4_scores.get("c4_circuit")
        c4_instance_pred = c4_scores.get("c4_instance_pred")
    else:
        evidence_scores = c4_scores
        c4_attr_scores = c4_scores
        c4_box_scores = None
        c4_circuit_scores = None
        c4_instance_pred = None
    metrics: dict[str, object] = {
        "c4_count": int(np.sum(c4_mask)),
        "c4_box_count": int(np.sum(c4_box_mask)),
        "c4_circuit_count": int(np.sum(c4_circuit_mask)),
        "non_c4_weapon_count": int(np.sum(non_c4_weapon_mask)),
        "non_weapon_count": int(np.sum(non_weapon_mask)),
        "c4_parent_recall": float(np.mean(parent_preds[c4_mask] == WEAPON_PARENT_INDEX)) if np.any(c4_mask) else 0.0,
        "closed_set_c4_parent_recall": float(np.mean(parent_preds[c4_mask] == WEAPON_PARENT_INDEX)) if np.any(c4_mask) else 0.0,
        "c4_box_parent_recall": float(np.mean(parent_preds[c4_box_mask] == WEAPON_PARENT_INDEX)) if np.any(c4_box_mask) else 0.0,
        "c4_circuit_parent_recall": float(np.mean(parent_preds[c4_circuit_mask] == WEAPON_PARENT_INDEX)) if np.any(c4_circuit_mask) else 0.0,
    }
    if evidence_scores is not None:
        evidence_scores = np.asarray(evidence_scores, dtype=np.float32).reshape(-1)
        c4_positive = evidence_scores >= threshold
        attr_positive = np.asarray(c4_attr_scores, dtype=np.float32).reshape(-1) >= threshold if c4_attr_scores is not None else c4_positive
        box_positive = np.asarray(c4_box_scores, dtype=np.float32).reshape(-1) >= threshold if c4_box_scores is not None else c4_positive
        circuit_positive = np.asarray(c4_circuit_scores, dtype=np.float32).reshape(-1) >= threshold if c4_circuit_scores is not None else c4_positive
        if np.any(~c4_mask):
            fp0_threshold = float(np.nextafter(float(np.max(evidence_scores[~c4_mask])), np.inf))
        else:
            fp0_threshold = threshold
        c4_at_fp0 = evidence_scores >= fp0_threshold
        metrics.update(
            {
                "c4_attr_recall": float(np.mean(attr_positive[c4_mask])) if np.any(c4_mask) else 0.0,
                "closed_set_c4_evidence_recall": float(np.mean(c4_positive[c4_mask])) if np.any(c4_mask) else 0.0,
                "c4_box_attr_recall": float(np.mean(box_positive[c4_box_mask])) if np.any(c4_box_mask) else 0.0,
                "c4_circuit_attr_recall": float(np.mean(circuit_positive[c4_circuit_mask])) if np.any(c4_circuit_mask) else 0.0,
                "non_c4_weapon_false_positive": float(np.mean(c4_positive[non_c4_weapon_mask])) if np.any(non_c4_weapon_mask) else 0.0,
                "non_weapon_false_positive": float(np.mean(c4_positive[non_weapon_mask])) if np.any(non_weapon_mask) else 0.0,
                "false_positive_rate": float(np.mean(c4_positive[~c4_mask])) if np.any(~c4_mask) else 0.0,
                "c4_fp0_threshold": fp0_threshold,
                "c4_recall_at_fp0": float(np.mean(c4_at_fp0[c4_mask])) if np.any(c4_mask) else 0.0,
                "c4_attr_mean": float(np.mean(evidence_scores[c4_mask])) if np.any(c4_mask) else 0.0,
                "non_c4_attr_mean": float(np.mean(evidence_scores[~c4_mask])) if np.any(~c4_mask) else 0.0,
            }
        )
    else:
        metrics.update(
            {
                "c4_attr_recall": 0.0,
                "closed_set_c4_evidence_recall": 0.0,
                "c4_box_attr_recall": 0.0,
                "c4_circuit_attr_recall": 0.0,
                "non_c4_weapon_false_positive": 0.0,
                "non_weapon_false_positive": 0.0,
                "false_positive_rate": 0.0,
                "c4_fp0_threshold": 1.0,
                "c4_recall_at_fp0": 0.0,
                "c4_attr_mean": 0.0,
                "non_c4_attr_mean": 0.0,
            }
        )
    per_instance: dict[str, object] = {}
    instance_recalls: list[float] = []
    for basename, instance_index in C4_INSTANCE_TO_INDEX.items():
        mask = c4_mask & (filenames == basename)
        if not np.any(mask):
            continue
        parent_ok = parent_preds[mask] == WEAPON_PARENT_INDEX
        instance_metrics: dict[str, object] = {
            "count": int(np.sum(mask)),
            "parent_recall": float(np.mean(parent_ok)),
        }
        instance_recalls.append(float(instance_metrics["parent_recall"]))
        if evidence_scores is not None:
            evidence_positive = np.asarray(evidence_scores, dtype=np.float32).reshape(-1) >= threshold
            instance_metrics["evidence_recall"] = float(np.mean(evidence_positive[mask]))
            instance_metrics["evidence_mean"] = float(np.mean(np.asarray(evidence_scores, dtype=np.float32).reshape(-1)[mask]))
        if c4_instance_pred is not None:
            pred_values = np.asarray(c4_instance_pred, dtype=np.int64)
            instance_metrics["instance_recall"] = float(np.mean(pred_values[mask] == instance_index))
        per_instance[basename] = instance_metrics
    metrics["per_c4_instance"] = per_instance
    metrics["c4_instance_worst_parent_recall"] = float(np.min(instance_recalls)) if instance_recalls else 0.0
    return metrics


TEACHER_PARENT_CACHE: dict[tuple[str, float], np.ndarray] = {}
CORRECT_TEACHER_CACHE: dict[str, dict[str, np.ndarray | dict[str, object]]] = {}


def parent_soft_from_old_sixclass(outputs: np.ndarray, temperature: float) -> np.ndarray:
    values = np.asarray(outputs, dtype=np.float32)
    if values.shape[1] == len(PARENT_NAMES):
        parent = values
    elif values.shape[1] == 6:
        probs = values
        if np.min(probs) < 0.0 or np.max(np.sum(probs, axis=1)) > 1.2:
            shifted = probs - np.max(probs, axis=1, keepdims=True)
            probs = np.exp(shifted) / np.sum(np.exp(shifted), axis=1, keepdims=True)
        parent = np.stack(
            [
                probs[:, 0] + probs[:, 1],
                probs[:, 2] + probs[:, 3],
                probs[:, 4] + probs[:, 5],
            ],
            axis=1,
        )
    else:
        raise ValueError(f"teacher output must have 3 or 6 columns, got {values.shape}")
    parent = np.clip(parent, 1.0e-6, 1.0)
    parent = parent / np.sum(parent, axis=1, keepdims=True)
    if temperature != 1.0:
        logits = np.log(parent) / max(temperature, 1.0e-6)
        logits = logits - np.max(logits, axis=1, keepdims=True)
        parent = np.exp(logits) / np.sum(np.exp(logits), axis=1, keepdims=True)
    return parent.astype(np.float32)


def teacher_parent_soft_labels(path: Path | None, x: np.ndarray, temperature: float) -> np.ndarray | None:
    if path is None:
        return None
    key = (str(path.resolve()), float(temperature))
    cached = TEACHER_PARENT_CACHE.get(key)
    if cached is not None:
        return cached
    outputs = tflite_outputs(path, x)
    parent = parent_soft_from_old_sixclass(outputs, temperature)
    TEACHER_PARENT_CACHE[key] = parent
    return parent


def correct_teacher_bundle(path: Path | None, paths: list[str], y_parent: np.ndarray) -> dict[str, np.ndarray | dict[str, object]] | None:
    if path is None:
        return None
    key = str(path.resolve())
    cached = CORRECT_TEACHER_CACHE.get(key)
    if cached is not None:
        return cached
    with np.load(path, allow_pickle=False) as data:
        teacher_parent_soft = data["teacher_parent_soft"].astype(np.float32)
        teacher_parent_weight = data["teacher_parent_weight"].astype(np.float32)
        parent_sample_weight = data["parent_sample_weight"].astype(np.float32)
        negative_parent_id = data["negative_parent_id"].astype(np.int64) if "negative_parent_id" in data.files else np.full(len(paths), -1, dtype=np.int64)
        negative_parent_weight = data["negative_parent_weight"].astype(np.float32) if "negative_parent_weight" in data.files else np.zeros(len(paths), dtype=np.float32)
        stored_paths = data["paths"].astype(str).tolist()
        meta_raw = data["meta_json"].item() if "meta_json" in data.files else "{}"
    if stored_paths != list(paths):
        raise ValueError(f"correct teacher paths do not match loaded dataset order: {path}")
    if teacher_parent_soft.shape != (len(paths), len(PARENT_NAMES)):
        raise ValueError(f"correct teacher soft labels shape mismatch: {teacher_parent_soft.shape}")
    if teacher_parent_weight.shape != (len(paths),):
        raise ValueError(f"correct teacher weights shape mismatch: {teacher_parent_weight.shape}")
    if parent_sample_weight.shape != (len(paths),):
        raise ValueError(f"parent sample weights shape mismatch: {parent_sample_weight.shape}")
    if negative_parent_id.shape != (len(paths),):
        raise ValueError(f"negative parent ids shape mismatch: {negative_parent_id.shape}")
    if negative_parent_weight.shape != (len(paths),):
        raise ValueError(f"negative parent weights shape mismatch: {negative_parent_weight.shape}")
    valid_negative = negative_parent_weight > 0
    if np.any(valid_negative & ((negative_parent_id < 0) | (negative_parent_id >= len(PARENT_NAMES)) | (negative_parent_id == y_parent))):
        bad = int(np.sum(valid_negative & ((negative_parent_id < 0) | (negative_parent_id >= len(PARENT_NAMES)) | (negative_parent_id == y_parent))))
        raise ValueError(f"correct teacher bundle has {bad} invalid active negative parent ids")
    teacher_preds = np.argmax(teacher_parent_soft, axis=1).astype(np.int64)
    if np.any((teacher_parent_weight > 0) & (teacher_preds != y_parent)):
        bad = int(np.sum((teacher_parent_weight > 0) & (teacher_preds != y_parent)))
        raise ValueError(f"correct teacher bundle has {bad} active teacher labels whose argmax is not the human parent")
    bundle: dict[str, np.ndarray | dict[str, object]] = {
        "teacher_parent_soft": teacher_parent_soft,
        "teacher_parent_weight": teacher_parent_weight,
        "parent_sample_weight": parent_sample_weight,
        "negative_parent_id": negative_parent_id,
        "negative_parent_weight": negative_parent_weight,
        "meta": json.loads(str(meta_raw)),
    }
    CORRECT_TEACHER_CACHE[key] = bundle
    return bundle


def training_teacher_data(
    args: argparse.Namespace,
    x: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    config: V5Config,
) -> tuple[np.ndarray | None, np.ndarray | None, np.ndarray | None, np.ndarray | None, np.ndarray | None, dict[str, object]]:
    bundle = correct_teacher_bundle(args.correct_teacher_labels, paths, y_parent) if getattr(args, "correct_teacher_labels", None) is not None else None
    parent_sample_weight = bundle["parent_sample_weight"] if bundle is not None else None
    teacher_parent_weight = bundle["teacher_parent_weight"] if bundle is not None and config.teacher_loss_weight > 0 else None
    negative_parent_id = bundle["negative_parent_id"] if bundle is not None and config.negative_margin_weight > 0 else None
    negative_parent_weight = bundle["negative_parent_weight"] if bundle is not None and config.negative_margin_weight > 0 else None
    teacher_parent_soft = None
    if config.teacher_loss_weight > 0:
        if bundle is not None:
            teacher_parent_soft = bundle["teacher_parent_soft"]
        else:
            teacher_parent_soft = teacher_parent_soft_labels(args.teacher_tflite, x, config.teacher_temperature)
    return (
        teacher_parent_soft,  # type: ignore[return-value]
        teacher_parent_weight,  # type: ignore[return-value]
        parent_sample_weight,  # type: ignore[return-value]
        negative_parent_id,  # type: ignore[return-value]
        negative_parent_weight,  # type: ignore[return-value]
        dict(bundle["meta"]) if bundle is not None else {},  # type: ignore[arg-type]
    )


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


def is_camera_stress_name(name: str) -> bool:
    return name.startswith("cam_")


def camera_motion_blur_np(image: np.ndarray, length: int, angle: int) -> np.ndarray:
    size = max(3, length if length % 2 == 1 else length + 1)
    center = size // 2
    kernel = np.zeros((size, size), dtype=np.float32)
    offsets = list(range(-(length // 2), length - length // 2))
    for offset in offsets:
        if angle == 0:
            row, col = center, center + offset
        elif angle == 90:
            row, col = center + offset, center
        elif angle == 45:
            row, col = center + offset, center + offset
        elif angle == 135:
            row, col = center + offset, center - offset
        else:
            raise ValueError(f"unsupported camera blur angle: {angle}")
        if 0 <= row < size and 0 <= col < size:
            kernel[row, col] = 1.0
    total = float(np.sum(kernel))
    if total <= 0.0:
        kernel[center, center] = 1.0
    else:
        kernel /= total
    kernel4 = tf.reshape(tf.constant(kernel), (size, size, 1, 1))
    out = tf.nn.depthwise_conv2d(image[None, ...], kernel4, strides=[1, 1, 1, 1], padding="SAME")[0]
    return np.asarray(out, dtype=np.float32)


def camera_stress_one(image: np.ndarray, name: str, rng: np.random.Generator) -> np.ndarray:
    cur = image.copy()
    for part in name.removeprefix("cam_").split("_"):
        if part == "rot90":
            cur = np.rot90(cur, 1, axes=(0, 1))
        elif part == "rot180":
            cur = np.rot90(cur, 2, axes=(0, 1))
        elif part == "rot270":
            cur = np.rot90(cur, 3, axes=(0, 1))
        elif part == "mirror":
            cur = np.flip(cur, axis=1)
        elif part == "lr":
            continue
        elif part.startswith("blur"):
            # Format: blur5 or blur5a45. A following token a45 is also accepted.
            raw = part.removeprefix("blur")
            length_text = raw.split("a", 1)[0]
            length = int(length_text or "3")
            angle = int(raw.split("a", 1)[1]) if "a" in raw else 0
            cur = camera_motion_blur_np(cur, length, angle)
        elif part.startswith("a") and part[1:].isdigit():
            continue
        elif part.startswith("noise"):
            value = float(part.removeprefix("noise").replace("p", "."))
            cur = np.clip(cur + rng.normal(0.0, value, cur.shape).astype(np.float32), 0.0, 1.0)
        elif part.startswith("bright"):
            raw = part.removeprefix("bright")
            sign = -1.0 if raw.startswith("m") else 1.0
            raw = raw[1:] if raw.startswith(("p", "m")) else raw
            value = sign * float(raw.replace("p", "."))
            cur = np.clip(cur + value, 0.0, 1.0)
        elif part.startswith("contrast"):
            raw = part.removeprefix("contrast")
            sign = -1.0 if raw.startswith("m") else 1.0
            raw = raw[1:] if raw.startswith(("p", "m")) else raw
            value = sign * float(raw.replace("p", "."))
            cur = np.clip((cur - 0.5) * max(0.0, 1.0 + value) + 0.5, 0.0, 1.0)
        elif part.startswith("shift"):
            shift = part.removeprefix("shift")
            direction = "".join(ch for ch in shift if ch.isalpha())
            amount_text = "".join(ch for ch in shift if ch.isdigit())
            amount = int(amount_text or "1")
            dy = (-amount if "u" in direction else 0) + (amount if "d" in direction else 0)
            dx = (-amount if "l" in direction else 0) + (amount if "r" in direction else 0)
            out = np.zeros_like(cur)
            h, w = cur.shape[:2]
            src_y0 = max(0, -dy)
            src_y1 = min(h, h - dy)
            dst_y0 = max(0, dy)
            dst_y1 = min(h, h + dy)
            src_x0 = max(0, -dx)
            src_x1 = min(w, w - dx)
            dst_x0 = max(0, dx)
            dst_x1 = min(w, w + dx)
            if src_y1 > src_y0 and src_x1 > src_x0:
                out[dst_y0:dst_y1, dst_x0:dst_x1] = cur[src_y0:src_y1, src_x0:src_x1]
            cur = out
        elif not part:
            continue
        else:
            raise ValueError(f"unknown camera stress part {part!r} in {name}")
    return cur.astype(np.float32)


def stress_batch_any(name: str, x: np.ndarray) -> np.ndarray:
    if not is_camera_stress_name(name):
        return tiny.stress_batch(name, x)
    seed = 9107 + sum((index + 1) * ord(ch) for index, ch in enumerate(name))
    rng = np.random.default_rng(seed)
    return np.stack([camera_stress_one(image, name, rng) for image in x]).astype(np.float32)


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


def compile_training_model(
    model: tf.keras.Model,
    config: V5Config,
    learning_rate: float,
    *,
    parent_weight: float | None = None,
    subclass_weight: float | None = None,
    weapon_weight: float | None = None,
    c4_weight: float | None = None,
    c4_box_weight: float | None = None,
    c4_circuit_weight: float | None = None,
    c4_instance_weight: float | None = None,
    teacher_weight: float | None = None,
) -> None:
    parent_weight = config.parent_loss_weight if parent_weight is None else parent_weight
    subclass_weight = config.subclass_loss_weight if subclass_weight is None else subclass_weight
    weapon_weight = config.weapon_loss_weight if weapon_weight is None else weapon_weight
    c4_weight = config.c4_loss_weight if c4_weight is None else c4_weight
    c4_box_weight = config.c4_box_loss_weight if c4_box_weight is None else c4_box_weight
    c4_circuit_weight = config.c4_circuit_loss_weight if c4_circuit_weight is None else c4_circuit_weight
    c4_instance_weight = config.c4_instance_loss_weight if c4_instance_weight is None else c4_instance_weight
    teacher_weight = config.teacher_loss_weight if teacher_weight is None else teacher_weight

    if config.head == "dual_parent":
        loss = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
        losses: dict[str, object] = {"parent": loss, "subclass": loss}
        loss_weights: dict[str, float] = {"parent": parent_weight, "subclass": subclass_weight}
        if config.teacher_loss_weight > 0:
            losses["parent_teacher"] = tf.keras.losses.KLDivergence()
            loss_weights["parent_teacher"] = teacher_weight
        if config.geometric_consistency_weight > 0:
            losses["parent_consistency"] = parent_geometric_consistency_loss(config.geometric_consistency_group)
            loss_weights["parent_consistency"] = config.geometric_consistency_weight
        if config.negative_margin_weight > 0:
            losses["parent_margin"] = negative_parent_margin_loss(config.negative_margin)
            loss_weights["parent_margin"] = config.negative_margin_weight
        model.compile(
            optimizer=tf.keras.optimizers.Adam(learning_rate),
            loss=losses,
            loss_weights=loss_weights,
            jit_compile=False,
        )
        return

    if config.head in V6_PARENT_HEADS or config.teacher_loss_weight > 0 or config.negative_margin_weight > 0 or config.geometric_consistency_weight > 0:
        losses = {"parent": tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)}
        loss_weights = {"parent": parent_weight}
        if config.teacher_loss_weight > 0:
            losses["parent_teacher"] = tf.keras.losses.KLDivergence()
            loss_weights["parent_teacher"] = teacher_weight
        if config.geometric_consistency_weight > 0:
            losses["parent_consistency"] = parent_geometric_consistency_loss(config.geometric_consistency_group)
            loss_weights["parent_consistency"] = config.geometric_consistency_weight
        if config.negative_margin_weight > 0:
            losses["parent_margin"] = negative_parent_margin_loss(config.negative_margin)
            loss_weights["parent_margin"] = config.negative_margin_weight
        if config.head in WEAPON_AUX_HEADS:
            losses["weapon_sub"] = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
            loss_weights["weapon_sub"] = weapon_weight
        if config.head in C4_ATTR_HEADS:
            losses["c4_attr"] = weighted_c4_bce(config.c4_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_attr"] = c4_weight
        if config.head in C4_BOX_CIRCUIT_HEADS:
            losses["c4_box"] = weighted_c4_bce(config.c4_box_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_box"] = c4_box_weight
            losses["c4_circuit"] = weighted_c4_bce(config.c4_circuit_pos_weight, config.c4_focal_gamma)
            loss_weights["c4_circuit"] = c4_circuit_weight
        if config.head in C4_INSTANCE_HEADS:
            losses["c4_instance"] = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits)
            loss_weights["c4_instance"] = c4_instance_weight
        model.compile(
            optimizer=tf.keras.optimizers.Adam(learning_rate),
            loss=losses,
            loss_weights=loss_weights,
            jit_compile=False,
        )
        return

    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=config.logits),
        metrics=["accuracy"],
        jit_compile=False,
    )


def fit_callbacks(patience: int) -> list[tf.keras.callbacks.Callback]:
    return [
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


def fit_model(
    model: tf.keras.Model,
    train_ds: tf.data.Dataset,
    val_ds: tf.data.Dataset,
    config: V5Config,
    train_labels: np.ndarray,
    epochs: int,
    patience: int,
) -> dict[str, list[float]]:
    callbacks = fit_callbacks(patience)
    kwargs: dict[str, object] = {}
    if config.head in {"subclass", "parent"}:
        weights = class_weight_for(train_labels, config.class_weight)
        if weights:
            kwargs["class_weight"] = weights
    history = model.fit(train_ds, validation_data=val_ds, epochs=epochs, callbacks=callbacks, verbose=0, **kwargs)
    return {key: [float(v) for v in values] for key, values in history.history.items()}


def fit_model_decoupled(
    model: tf.keras.Model,
    train_ds: tf.data.Dataset,
    val_ds: tf.data.Dataset,
    config: V5Config,
    epochs: int,
    patience: int,
) -> dict[str, list[float]]:
    if config.head not in V6_PARENT_HEADS:
        return fit_model(model, train_ds, val_ds, config, np.asarray([], dtype=np.int64), epochs, patience)

    parent_epochs = max(1, min(config.decouple_parent_epochs, max(1, epochs // 3)))
    aux_epochs = max(1, min(config.decouple_aux_epochs, max(1, epochs // 4)))
    joint_epochs = max(1, epochs - parent_epochs - aux_epochs)
    merged: dict[str, list[float]] = {}

    def add_history(prefix: str, history: tf.keras.callbacks.History) -> None:
        for key, values in history.history.items():
            merged.setdefault(f"{prefix}_{key}", []).extend(float(v) for v in values)

    for layer in model.layers:
        layer.trainable = True
    compile_training_model(
        model,
        config,
        config.learning_rate,
        weapon_weight=0.0,
        c4_weight=0.0,
        c4_box_weight=0.0,
        c4_circuit_weight=0.0,
        c4_instance_weight=0.0,
        teacher_weight=config.teacher_loss_weight,
    )
    add_history(
        "parent",
        model.fit(train_ds, validation_data=val_ds, epochs=parent_epochs, callbacks=fit_callbacks(max(2, patience // 3)), verbose=0),
    )

    for layer in model.layers:
        layer.trainable = any(token in layer.name for token in ("weapon_sub", "c4_attr", "c4_box", "c4_circuit", "c4_instance"))
    compile_training_model(
        model,
        config,
        config.learning_rate,
        parent_weight=0.0,
        weapon_weight=config.weapon_loss_weight,
        c4_weight=config.c4_loss_weight,
        c4_box_weight=config.c4_box_loss_weight,
        c4_circuit_weight=config.c4_circuit_loss_weight,
        c4_instance_weight=config.c4_instance_loss_weight,
        teacher_weight=0.0,
    )
    add_history(
        "aux",
        model.fit(train_ds, validation_data=val_ds, epochs=aux_epochs, callbacks=fit_callbacks(max(2, patience // 3)), verbose=0),
    )

    for layer in model.layers:
        layer.trainable = True
    compile_training_model(
        model,
        config,
        max(1.0e-5, config.learning_rate * config.decouple_joint_lr_scale),
    )
    add_history(
        "joint",
        model.fit(train_ds, validation_data=val_ds, epochs=joint_epochs, callbacks=fit_callbacks(patience), verbose=0),
    )
    merged["loss"] = merged.get("parent_loss", []) + merged.get("aux_loss", []) + merged.get("joint_loss", [])
    return merged


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
        xs = stress_batch_any(stress_name, x[indexes])
        preds = predictions_from_outputs(tflite_outputs(int8_path, xs))
        results[stress_name] = evaluate_predictions(y_sub[indexes], y_parent[indexes], preds, kind)["parent"]
    return results


def c4_camera_stress_eval(
    model: tf.keras.Model,
    int8_path: Path,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    kind: str,
    config: V5Config,
    stress_names: list[str],
) -> dict[str, object]:
    camera_names = [name for name in stress_names if is_camera_stress_name(name)]
    by_stress: dict[str, object] = {}
    if not camera_names:
        return {
            "by_stress": by_stress,
            "c4_camera_stress_recall": 0.0,
            "c4_circuit_camera_recall": 0.0,
            "c4_camera_false_positive_rate": 0.0,
        }
    c4_recalls: list[float] = []
    circuit_recalls: list[float] = []
    false_positives: list[float] = []
    for stress_name in camera_names:
        xs = stress_batch_any(stress_name, x)
        preds = predictions_from_outputs(tflite_outputs(int8_path, xs))
        parent_preds = parent_preds_from_output_preds(preds, kind)
        c4_scores = c4_aux_score_dict(model, xs, config.batch_size, config)
        metrics = c4_eval_metrics(y_sub, y_parent, parent_preds, c4_scores, paths)
        by_stress[stress_name] = metrics
        c4_recalls.append(float(metrics.get("closed_set_c4_parent_recall", 0.0)))
        circuit_recalls.append(float(metrics.get("c4_circuit_parent_recall", 0.0)))
        false_positives.append(float(metrics.get("false_positive_rate", 0.0)))
    return {
        "by_stress": by_stress,
        "c4_camera_stress_recall": float(np.min(c4_recalls)) if c4_recalls else 0.0,
        "c4_circuit_camera_recall": float(np.min(circuit_recalls)) if circuit_recalls else 0.0,
        "c4_camera_false_positive_rate": float(np.max(false_positives)) if false_positives else 0.0,
        "camera_stress_count": len(camera_names),
    }


def score_run(
    config: V5Config,
    clean: dict[str, object],
    hard: dict[str, object],
    stress: dict[str, object],
    agreement: float,
    int8_bytes: int,
    c4_eval: dict[str, object] | None = None,
    c4_camera_eval: dict[str, object] | None = None,
) -> float:
    clean_parent = clean["parent"]
    hard_parent = hard["parent"] if hard else clean_parent
    stress_items = [item for item in stress.values() if isinstance(item, dict)]
    camera_stress_items = [item for name, item in stress.items() if is_camera_stress_name(str(name)) and isinstance(item, dict)]
    stress_macro = float(np.mean([float(item["macro_recall"]) for item in stress_items])) if stress_items else float(clean_parent["macro_recall"])
    stress_worst = float(np.min([float(item["worst_recall"]) for item in stress_items])) if stress_items else float(clean_parent["worst_recall"])
    camera_stress_worst = (
        float(np.min([float(item["worst_recall"]) for item in camera_stress_items]))
        if camera_stress_items
        else stress_worst
    )
    visual_macro = float(clean.get("subclass", clean_parent)["macro_recall"])
    estimated_us = tiny.estimate_board_latency_us(config)
    speed_score = max(0.0, min(1.0, (25000.0 - estimated_us) / 25000.0))
    size_score = max(0.0, min(1.0, (26000.0 - int8_bytes) / 26000.0))
    if config.head in V6_PARENT_HEADS or str(config.lane).startswith("v6"):
        c4_parent = float((c4_eval or {}).get("closed_set_c4_parent_recall", (c4_eval or {}).get("c4_parent_recall", 0.0)))
        c4_camera_recall = float((c4_camera_eval or {}).get("c4_camera_stress_recall", c4_parent))
        c4_camera_false_positive = float((c4_camera_eval or {}).get("c4_camera_false_positive_rate", (c4_eval or {}).get("false_positive_rate", 0.0)))
        latency_size_penalty = 0.045 * (1.0 - speed_score) + 0.025 * (1.0 - size_score)
        return float(
            0.28 * float(clean_parent["accuracy"])
            + 0.18 * stress_worst
            + 0.14 * camera_stress_worst
            + 0.14 * float(hard_parent["accuracy"])
            + 0.08 * float(hard_parent["worst_recall"])
            + 0.08 * c4_parent
            + 0.05 * c4_camera_recall
            + 0.03 * (1.0 - c4_camera_false_positive)
            + 0.02 * agreement
            - latency_size_penalty
        )
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
    teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight, teacher_meta = training_teacher_data(args, x, y_parent, paths, config)
    train_ds = make_dataset(x, y_sub, y_parent, paths, train_idx, config, seed, True, teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight)
    val_ds = make_dataset(x, y_sub, y_parent, paths, val_idx, config, seed, False, teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight)
    label_source = y_parent if config.head == "parent" else y_sub
    if config.decoupled:
        history = fit_model_decoupled(model, train_ds, val_ds, config, args.epochs, args.patience)
    else:
        history = fit_model(model, train_ds, val_ds, config, label_source[train_idx], args.epochs, args.patience)
    deploy = deploy_model(model, config)
    artifact_save_errors: dict[str, str] = {}
    if save_artifacts:
        model.save_weights(trial_dir / "model.weights.h5")
        try:
            model.save(trial_dir / "model.keras")
        except Exception as exc:  # noqa: BLE001
            artifact_save_errors["model.keras"] = f"{type(exc).__name__}: {exc}"
        if deploy is not model:
            deploy.save_weights(trial_dir / "deploy.weights.h5")
            try:
                deploy.save(trial_dir / "deploy.keras")
            except Exception as exc:  # noqa: BLE001
                artifact_save_errors["deploy.keras"] = f"{type(exc).__name__}: {exc}"
        if artifact_save_errors:
            (trial_dir / "artifact_save_errors.json").write_text(
                json.dumps(artifact_save_errors, indent=2, ensure_ascii=False),
                encoding="utf-8",
            )
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
    test_parent_preds = parent_preds_from_output_preds(int8_test_preds, kind)
    all_parent_preds = parent_preds_from_output_preds(int8_all_preds, kind)
    hard_parent_preds = parent_preds_from_output_preds(int8_hard_preds, kind) if len(hard_idx) else np.asarray([], dtype=np.int64)
    c4_scores_test = c4_aux_score_dict(model, x[test_idx], config.batch_size, config)
    c4_scores_all = c4_aux_score_dict(model, x, config.batch_size, config)
    c4_scores_hard = c4_aux_score_dict(model, x[hard_idx], config.batch_size, config) if len(hard_idx) else {}
    c4_eval = {
        "test": c4_eval_metrics(y_sub[test_idx], y_parent[test_idx], test_parent_preds, c4_scores_test, np.asarray(paths, dtype=object)[test_idx]),
        "all": c4_eval_metrics(y_sub, y_parent, all_parent_preds, c4_scores_all, paths),
        "hard": c4_eval_metrics(y_sub[hard_idx], y_parent[hard_idx], hard_parent_preds, c4_scores_hard, np.asarray(paths, dtype=object)[hard_idx]) if len(hard_idx) else {},
    }
    c4_camera_eval = c4_camera_stress_eval(model, int8_path, x, y_sub, y_parent, paths, kind, config, args.stress_names)
    teacher_mask = {}
    if teacher_parent_soft is not None:
        teacher_preds = np.argmax(teacher_parent_soft, axis=1).astype(np.int64)
        teacher_correct = teacher_preds == y_parent
        teacher_mask = {
            "enabled": True,
            "all_correct_count": int(np.sum(teacher_correct)),
            "all_wrong_count": int(np.sum(~teacher_correct)),
            "all_keep_rate": float(np.mean(teacher_correct)),
            "train_keep_rate": float(np.mean(teacher_correct[train_idx])),
            "val_keep_rate": float(np.mean(teacher_correct[val_idx])),
            "test_keep_rate": float(np.mean(teacher_correct[test_idx])),
            "c4_keep_count": int(np.sum(teacher_correct & (y_sub == C4_SUBCLASS_INDEX))),
            "c4_wrong_count": int(np.sum((~teacher_correct) & (y_sub == C4_SUBCLASS_INDEX))),
            "active_weight_count": int(np.sum(teacher_parent_weight > 0)) if teacher_parent_weight is not None else int(np.sum(teacher_correct)),
            "active_weight_mean": float(np.mean(teacher_parent_weight)) if teacher_parent_weight is not None else float(np.mean(teacher_correct)),
            "source": "correct_teacher_labels" if teacher_meta else "teacher_tflite",
            "meta": teacher_meta,
        }
    if negative_parent_id is not None and negative_parent_weight is not None:
        active_negative = negative_parent_weight > 0
        teacher_mask["negative_margin"] = {
            "enabled": True,
            "active_weight_count": int(np.sum(active_negative)),
            "active_weight_mean": float(np.mean(negative_parent_weight)),
            "hard_active_count": int(np.sum(active_negative[hard_idx])) if len(hard_idx) else 0,
            "c4_active_count": int(np.sum(active_negative & (y_sub == C4_SUBCLASS_INDEX))),
        }
    score = score_run(config, clean, hard, stress, agreement_all, int8_bytes, c4_eval["all"], c4_camera_eval)
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
        "c4_eval": c4_eval,
        "c4_camera_eval": c4_camera_eval,
        "teacher_mask": teacher_mask,
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
        "artifact_save_errors": artifact_save_errors,
    }
    (trial_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result


def mean_float(values: list[float]) -> float:
    return float(np.mean(values)) if values else 0.0


def min_float(values: list[float]) -> float:
    return float(np.min(values)) if values else 0.0


def summarize_trial(config: V5Config, seed_results: list[dict[str, object]]) -> dict[str, object]:
    ok = [item for item in seed_results if item.get("status") == "ok"]
    failures = [item for item in seed_results if item.get("status") != "ok"]
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
    camera_stress_worsts = [
        min(float(stress["worst_recall"]) for name, stress in item["int8_stress"].items() if is_camera_stress_name(str(name)))
        if item.get("int8_stress") and any(is_camera_stress_name(str(name)) for name in item["int8_stress"])
        else float(item["int8_test"]["parent"]["worst_recall"])
        for item in ok
    ]
    agreements = [float(item["agreement"]["keras_vs_int8_all"]) for item in ok]
    bytes_values = [float(item["export"]["int8_bytes"]) for item in ok]
    c4_parent_recall_all = [float(item.get("c4_eval", {}).get("all", {}).get("closed_set_c4_parent_recall", item.get("c4_eval", {}).get("all", {}).get("c4_parent_recall", 0.0))) for item in ok]
    c4_parent_recall_test = [float(item.get("c4_eval", {}).get("test", {}).get("c4_parent_recall", 0.0)) for item in ok]
    c4_false_positive = [float(item.get("c4_eval", {}).get("all", {}).get("false_positive_rate", 0.0)) for item in ok]
    c4_attr_recall = [float(item.get("c4_eval", {}).get("all", {}).get("c4_attr_recall", 0.0)) for item in ok]
    c4_evidence_recall = [float(item.get("c4_eval", {}).get("all", {}).get("closed_set_c4_evidence_recall", 0.0)) for item in ok]
    c4_box_recall = [float(item.get("c4_eval", {}).get("all", {}).get("c4_box_parent_recall", 0.0)) for item in ok]
    c4_circuit_recall = [float(item.get("c4_eval", {}).get("all", {}).get("c4_circuit_parent_recall", 0.0)) for item in ok]
    c4_instance_worst = [float(item.get("c4_eval", {}).get("all", {}).get("c4_instance_worst_parent_recall", 0.0)) for item in ok]
    c4_camera_recall = [float(item.get("c4_camera_eval", {}).get("c4_camera_stress_recall", 0.0)) for item in ok]
    c4_circuit_camera_recall = [float(item.get("c4_camera_eval", {}).get("c4_circuit_camera_recall", 0.0)) for item in ok]
    c4_camera_false_positive = [float(item.get("c4_camera_eval", {}).get("c4_camera_false_positive_rate", 0.0)) for item in ok]
    teacher_keep = [float(item.get("teacher_mask", {}).get("all_keep_rate", 0.0)) for item in ok if item.get("teacher_mask")]
    return {
        "trial": config.name,
        "lane": config.lane,
        "config": config_to_dict(config),
        "status": "ok" if len(ok) == len(seed_results) else "partial",
        "runs": ok,
        "failures": failures,
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
        "camera_stress_parent_worst_mean": mean_float(camera_stress_worsts),
        "camera_stress_parent_worst_min": min_float(camera_stress_worsts),
        "c4_parent_recall_all_mean": mean_float(c4_parent_recall_all),
        "c4_parent_recall_all_min": min_float(c4_parent_recall_all),
        "c4_parent_recall_test_mean": mean_float(c4_parent_recall_test),
        "c4_parent_recall_test_min": min_float(c4_parent_recall_test),
        "closed_set_c4_evidence_recall_mean": mean_float(c4_evidence_recall),
        "c4_box_recall_mean": mean_float(c4_box_recall),
        "c4_circuit_recall_mean": mean_float(c4_circuit_recall),
        "c4_instance_worst_recall_min": min_float(c4_instance_worst),
        "c4_camera_stress_recall_mean": mean_float(c4_camera_recall),
        "c4_camera_stress_recall_min": min_float(c4_camera_recall),
        "c4_circuit_camera_recall_mean": mean_float(c4_circuit_camera_recall),
        "c4_circuit_camera_recall_min": min_float(c4_circuit_camera_recall),
        "c4_false_positive_mean": mean_float(c4_false_positive),
        "c4_false_positive_max": float(np.max(c4_false_positive)) if c4_false_positive else 0.0,
        "c4_camera_false_positive_mean": mean_float(c4_camera_false_positive),
        "c4_camera_false_positive_max": float(np.max(c4_camera_false_positive)) if c4_camera_false_positive else 0.0,
        "c4_attr_recall_mean": mean_float(c4_attr_recall),
        "teacher_keep_rate_mean": mean_float(teacher_keep),
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
        "validation_transforms": config.validation_transforms,
        "head": config.head,
        "logits": config.logits,
        "class_weight": config.class_weight,
        "calibration": config.calibration,
        "parent_loss_weight": config.parent_loss_weight,
        "subclass_loss_weight": config.subclass_loss_weight,
        "weapon_loss_weight": config.weapon_loss_weight,
        "c4_loss_weight": config.c4_loss_weight,
        "c4_pos_weight": config.c4_pos_weight,
        "c4_focal_gamma": config.c4_focal_gamma,
        "c4_box_loss_weight": config.c4_box_loss_weight,
        "c4_box_pos_weight": config.c4_box_pos_weight,
        "c4_circuit_loss_weight": config.c4_circuit_loss_weight,
        "c4_circuit_pos_weight": config.c4_circuit_pos_weight,
        "c4_instance_loss_weight": config.c4_instance_loss_weight,
        "teacher_loss_weight": config.teacher_loss_weight,
        "teacher_temperature": config.teacher_temperature,
        "c4_teacher_scale": config.c4_teacher_scale,
        "negative_margin_weight": config.negative_margin_weight,
        "negative_margin": config.negative_margin,
        "geometric_consistency_weight": config.geometric_consistency_weight,
        "geometric_consistency_group": config.geometric_consistency_group,
        "decoupled": config.decoupled,
        "decouple_parent_epochs": config.decouple_parent_epochs,
        "decouple_aux_epochs": config.decouple_aux_epochs,
        "decouple_joint_lr_scale": config.decouple_joint_lr_scale,
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
        validation_transforms=str(data.get("validation_transforms", "clean")),
        head=str(data.get("head", "subclass")),
        logits=bool(data.get("logits", False)),
        class_weight=str(data.get("class_weight", "none")),
        calibration=str(data.get("calibration", "mild_stress")),
        parent_loss_weight=float(data.get("parent_loss_weight", 1.0)),
        subclass_loss_weight=float(data.get("subclass_loss_weight", 0.35)),
        weapon_loss_weight=float(data.get("weapon_loss_weight", 0.10)),
        c4_loss_weight=float(data.get("c4_loss_weight", 0.30)),
        c4_pos_weight=float(data.get("c4_pos_weight", 4.0)),
        c4_focal_gamma=float(data.get("c4_focal_gamma", 0.0)),
        c4_box_loss_weight=float(data.get("c4_box_loss_weight", 0.20)),
        c4_box_pos_weight=float(data.get("c4_box_pos_weight", 4.0)),
        c4_circuit_loss_weight=float(data.get("c4_circuit_loss_weight", 0.40)),
        c4_circuit_pos_weight=float(data.get("c4_circuit_pos_weight", 8.0)),
        c4_instance_loss_weight=float(data.get("c4_instance_loss_weight", 0.03)),
        teacher_loss_weight=float(data.get("teacher_loss_weight", 0.0)),
        teacher_temperature=float(data.get("teacher_temperature", 2.0)),
        c4_teacher_scale=float(data.get("c4_teacher_scale", 1.0)),
        negative_margin_weight=float(data.get("negative_margin_weight", 0.0)),
        negative_margin=float(data.get("negative_margin", 1.0)),
        geometric_consistency_weight=float(data.get("geometric_consistency_weight", 0.0)),
        geometric_consistency_group=int(data.get("geometric_consistency_group", 8)),
        decoupled=bool(data.get("decoupled", False)),
        decouple_parent_epochs=int(data.get("decouple_parent_epochs", 40)),
        decouple_aux_epochs=int(data.get("decouple_aux_epochs", 30)),
        decouple_joint_lr_scale=float(data.get("decouple_joint_lr_scale", 0.25)),
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
        config.train_transforms,
        config.validation_transforms,
        config.head,
        config.logits,
        config.class_weight,
        config.calibration,
        round(config.parent_loss_weight, 5),
        round(config.subclass_loss_weight, 5),
        round(config.weapon_loss_weight, 5),
        round(config.c4_loss_weight, 5),
        round(config.c4_pos_weight, 5),
        round(config.c4_focal_gamma, 5),
        round(config.c4_box_loss_weight, 5),
        round(config.c4_box_pos_weight, 5),
        round(config.c4_circuit_loss_weight, 5),
        round(config.c4_circuit_pos_weight, 5),
        round(config.c4_instance_loss_weight, 5),
        round(config.teacher_loss_weight, 5),
        round(config.teacher_temperature, 5),
        round(config.c4_teacher_scale, 5),
        round(config.negative_margin_weight, 5),
        round(config.negative_margin, 5),
        round(config.geometric_consistency_weight, 5),
        config.geometric_consistency_group,
        config.decoupled,
        config.decouple_parent_epochs,
        config.decouple_aux_epochs,
        round(config.decouple_joint_lr_scale, 5),
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
    train_transforms: str = "rot_mirror",
    validation_transforms: str = "clean",
    head: str = "subclass",
    logits: bool = False,
    class_weight: str = "none",
    calibration: str = "mild_stress",
    parent_loss_weight: float = 1.0,
    subclass_loss_weight: float = 0.35,
    weapon_loss_weight: float = 0.10,
    c4_loss_weight: float = 0.30,
    c4_pos_weight: float = 4.0,
    c4_focal_gamma: float = 0.0,
    c4_box_loss_weight: float = 0.20,
    c4_box_pos_weight: float = 4.0,
    c4_circuit_loss_weight: float = 0.40,
    c4_circuit_pos_weight: float = 8.0,
    c4_instance_loss_weight: float = 0.03,
    teacher_loss_weight: float = 0.0,
    teacher_temperature: float = 2.0,
    c4_teacher_scale: float = 1.0,
    negative_margin_weight: float = 0.0,
    negative_margin: float = 1.0,
    geometric_consistency_weight: float = 0.0,
    geometric_consistency_group: int = 8,
    decoupled: bool = False,
    decouple_parent_epochs: int = 40,
    decouple_aux_epochs: int = 30,
    decouple_joint_lr_scale: float = 0.25,
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
        train_transforms=train_transforms,
        validation_transforms=validation_transforms,
        head=head,
        logits=logits,
        class_weight=class_weight,
        calibration=calibration,
        parent_loss_weight=parent_loss_weight,
        subclass_loss_weight=subclass_loss_weight,
        weapon_loss_weight=weapon_loss_weight,
        c4_loss_weight=c4_loss_weight,
        c4_pos_weight=c4_pos_weight,
        c4_focal_gamma=c4_focal_gamma,
        c4_box_loss_weight=c4_box_loss_weight,
        c4_box_pos_weight=c4_box_pos_weight,
        c4_circuit_loss_weight=c4_circuit_loss_weight,
        c4_circuit_pos_weight=c4_circuit_pos_weight,
        c4_instance_loss_weight=c4_instance_loss_weight,
        teacher_loss_weight=teacher_loss_weight,
        teacher_temperature=teacher_temperature,
        c4_teacher_scale=c4_teacher_scale,
        negative_margin_weight=negative_margin_weight,
        negative_margin=negative_margin,
        geometric_consistency_weight=geometric_consistency_weight,
        geometric_consistency_group=geometric_consistency_group,
        decoupled=decoupled,
        decouple_parent_epochs=decouple_parent_epochs,
        decouple_aux_epochs=decouple_aux_epochs,
        decouple_joint_lr_scale=decouple_joint_lr_scale,
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
            float(item.get("c4_parent_recall_all_mean", 0.0)),
            float(item.get("c4_camera_stress_recall_min", 0.0)),
            float(item.get("c4_circuit_camera_recall_min", 0.0)),
            -float(item.get("c4_false_positive_mean", 1.0)),
            -float(item.get("c4_camera_false_positive_max", 1.0)),
            float(item.get("hard_parent_worst_min", 0.0)),
            float(item.get("camera_stress_parent_worst_min", 0.0)),
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
                "camera_stress_parent_worst_min": item.get("camera_stress_parent_worst_min", 0.0),
                "c4_parent_recall_all_mean": item.get("c4_parent_recall_all_mean", 0.0),
                "closed_set_c4_evidence_recall_mean": item.get("closed_set_c4_evidence_recall_mean", 0.0),
                "c4_box_recall_mean": item.get("c4_box_recall_mean", 0.0),
                "c4_circuit_recall_mean": item.get("c4_circuit_recall_mean", 0.0),
                "c4_instance_worst_recall_min": item.get("c4_instance_worst_recall_min", 0.0),
                "c4_camera_stress_recall_min": item.get("c4_camera_stress_recall_min", 0.0),
                "c4_circuit_camera_recall_min": item.get("c4_circuit_camera_recall_min", 0.0),
                "c4_false_positive_mean": item.get("c4_false_positive_mean", 0.0),
                "c4_camera_false_positive_max": item.get("c4_camera_false_positive_max", 0.0),
                "c4_attr_recall_mean": item.get("c4_attr_recall_mean", 0.0),
                "teacher_keep_rate_mean": item.get("teacher_keep_rate_mean", 0.0),
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
            "camera_stress_parent_worst_min",
            "c4_parent_recall_all_mean",
            "closed_set_c4_evidence_recall_mean",
            "c4_box_recall_mean",
            "c4_circuit_recall_mean",
            "c4_instance_worst_recall_min",
            "c4_camera_stress_recall_min",
            "c4_circuit_camera_recall_min",
            "c4_false_positive_mean",
            "c4_camera_false_positive_max",
            "c4_attr_recall_mean",
            "teacher_keep_rate_mean",
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
    teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight, teacher_meta = training_teacher_data(args, x, y_parent, paths, config)
    train_ds = make_dataset(x, y_sub, y_parent, paths, all_idx, config, seed, True, teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight)
    full_epochs = args.full_epochs if args.full_epochs > 0 else args.epochs
    if config.decoupled:
        val_ds = make_dataset(x, y_sub, y_parent, paths, all_idx, config, seed, False, teacher_parent_soft, teacher_parent_weight, parent_sample_weight, negative_parent_id, negative_parent_weight)
        fit_model_decoupled(model, train_ds, val_ds, config, full_epochs, args.patience)
    else:
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
    parser.add_argument("--hard-basenames-file", type=Path)
    parser.add_argument("--teacher-tflite", type=Path)
    parser.add_argument("--correct-teacher-labels", type=Path)
    parser.add_argument("--save-artifacts", action="store_true")
    args = parser.parse_args()
    args.stress_names = [name.strip() for name in args.stress.split(",") if name.strip()]

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(int(os.environ.get("TF_NUM_INTEROP_THREADS", "2")))
    tf.config.threading.set_intra_op_parallelism_threads(int(os.environ.get("TF_NUM_INTRAOP_THREADS", "4")))

    global ACTIVE_HARD_CLEAN_BASENAMES
    ACTIVE_HARD_CLEAN_BASENAMES = load_hard_basenames(args.hard_basenames_file)

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
            make_config(
                "smoke_v6_closed_box_circuit",
                "v6_balance",
                "spacetodepth_conv",
                (6, 12, 24),
                0.00318,
                1.0e-4,
                0.003,
                "v6_camera_mild",
                head="parent_weapon_c4_box_circuit",
                c4_box_loss_weight=0.20,
                c4_circuit_loss_weight=0.40,
            ),
            make_config(
                "smoke_v6_closed_instance",
                "v6_balance",
                "spacetodepth_conv",
                (6, 12, 24),
                0.00318,
                1.0e-4,
                0.003,
                "v6_camera_mild",
                head="parent_weapon_c4_instance",
                c4_box_loss_weight=0.20,
                c4_circuit_loss_weight=0.40,
                c4_instance_loss_weight=0.03,
            ),
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
        "camera_stress_names": [name for name in args.stress_names if is_camera_stress_name(name)],
        "visual_class_names": VISUAL_CLASS_NAMES,
        "parent_names": PARENT_NAMES,
        "visual_to_parent": VISUAL_TO_PARENT.tolist(),
        "c4_closed_set": {
            "box_basenames": sorted(C4_BOX_BASENAMES),
            "circuit_basenames": sorted(C4_CIRCUIT_BASENAMES),
            "instance_basenames": list(C4_INSTANCE_BASENAMES),
        },
        "sample_count": int(len(y_sub)),
        "visual_counts": {VISUAL_CLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(VISUAL_CLASS_NAMES))},
        "parent_counts": {PARENT_NAMES[i]: int(np.sum(y_parent == i)) for i in range(len(PARENT_NAMES))},
        "hard_clean": {
            "basenames": ACTIVE_HARD_CLEAN_BASENAMES,
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
                error = f"{type(exc).__name__}: {exc}"
                print(f"{config.name} seed={seed} failed {error}", flush=True)
                seed_results.append({"seed": seed, "status": "failed", "error": error, "score": 0.0})
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
            f"cam_worst_min={summary.get('camera_stress_parent_worst_min', 0.0):.4f} "
            f"c4_parent={summary.get('c4_parent_recall_all_mean', 0.0):.4f} "
            f"c4_cam={summary.get('c4_camera_stress_recall_min', 0.0):.4f} "
            f"c4_circuit_cam={summary.get('c4_circuit_camera_recall_min', 0.0):.4f} "
            f"c4_fp={summary.get('c4_false_positive_mean', 0.0):.4f} "
            f"c4_cam_fp={summary.get('c4_camera_false_positive_max', 0.0):.4f} "
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
