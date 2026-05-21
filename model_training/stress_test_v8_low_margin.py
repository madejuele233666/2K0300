import argparse
import csv
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf
from PIL import Image

import train_tiny32_v5_visual_subclass_scan as train


@dataclass(frozen=True)
class Perturb:
    name: str
    family: str
    severity: float


PERTURBS = [
    Perturb("identity", "identity", 0.0),
    Perturb("noise_0p02", "noise", 0.02),
    Perturb("noise_0p04", "noise", 0.04),
    Perturb("noise_0p06", "noise", 0.06),
    Perturb("noise_0p08", "noise", 0.08),
    Perturb("noise_0p10", "noise", 0.10),
    Perturb("blur3a0", "blur", 3.0),
    Perturb("blur3a45", "blur", 3.45),
    Perturb("blur3a90", "blur", 3.90),
    Perturb("blur3a135", "blur", 4.35),
    Perturb("blur5a0", "blur", 5.0),
    Perturb("blur5a45", "blur", 5.45),
    Perturb("blur5a90", "blur", 5.90),
    Perturb("blur5a135", "blur", 6.35),
    Perturb("blur7a45", "blur", 7.45),
    Perturb("blur7a135", "blur", 8.35),
    Perturb("blur5a45_noise0p04", "blur_noise", 6.0),
    Perturb("blur5a135_noise0p04", "blur_noise", 6.5),
    Perturb("blur7a45_noise0p04", "blur_noise", 8.0),
    Perturb("bright_p0p04", "brightness", 0.04),
    Perturb("bright_p0p08", "brightness", 0.08),
    Perturb("bright_p0p12", "brightness", 0.12),
    Perturb("bright_m0p04", "brightness", 0.04),
    Perturb("bright_m0p08", "brightness", 0.08),
    Perturb("bright_m0p12", "brightness", 0.12),
    Perturb("contrast_p0p10", "contrast", 0.10),
    Perturb("contrast_p0p20", "contrast", 0.20),
    Perturb("contrast_m0p10", "contrast", 0.10),
    Perturb("contrast_m0p20", "contrast", 0.20),
    Perturb("shift_u1", "shift", 1.0),
    Perturb("shift_d1", "shift", 1.0),
    Perturb("shift_l1", "shift", 1.0),
    Perturb("shift_r1", "shift", 1.0),
    Perturb("shift_ul1", "shift", 1.5),
    Perturb("shift_dr1", "shift", 1.5),
    Perturb("shift_u2", "shift", 2.0),
    Perturb("shift_d2", "shift", 2.0),
    Perturb("shift_l2", "shift", 2.0),
    Perturb("shift_r2", "shift", 2.0),
    Perturb("shift_u1_noise0p04", "shift_noise", 2.0),
    Perturb("shift_l1_noise0p04", "shift_noise", 2.0),
]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def save_stress_image(path: Path, image: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    arr = np.clip(image[..., 0] if image.ndim == 3 and image.shape[-1] == 1 else image, 0.0, 1.0)
    gray = np.rint(arr * 255.0).astype(np.uint8)
    Image.fromarray(gray, mode="L").save(path)


def safe_name(value: Any) -> str:
    text = str(value)
    out = []
    for ch in text:
        if ch.isalnum() or ch in {"-", "_", "."}:
            out.append(ch)
        else:
            out.append("_")
    return "".join(out).strip("_") or "item"


def parse_csv(text: str) -> list[str]:
    if text == "all":
        return [item.name for item in PERTURBS]
    return [item.strip() for item in text.split(",") if item.strip()]


def perturb_by_name(names: list[str]) -> list[Perturb]:
    by_name = {item.name: item for item in PERTURBS}
    missing = [name for name in names if name not in by_name]
    if missing:
        raise ValueError(f"unknown perturb names: {missing}")
    return [by_name[name] for name in names]


def margin_bucket(margin: int) -> str:
    if margin <= 1:
        return "le1"
    if margin <= 2:
        return "le2"
    if margin <= 4:
        return "le4"
    if margin <= 8:
        return "le8"
    if margin <= 16:
        return "le16"
    if margin <= 64:
        return "le64"
    return "gt64"


def shift_image(image: np.ndarray, dy: int, dx: int) -> np.ndarray:
    out = np.zeros_like(image)
    h, w = image.shape[:2]
    src_y0 = max(0, -dy)
    src_y1 = min(h, h - dy)
    dst_y0 = max(0, dy)
    dst_y1 = min(h, h + dy)
    src_x0 = max(0, -dx)
    src_x1 = min(w, w - dx)
    dst_x0 = max(0, dx)
    dst_x1 = min(w, w + dx)
    if src_y1 > src_y0 and src_x1 > src_x0:
        out[dst_y0:dst_y1, dst_x0:dst_x1] = image[src_y0:src_y1, src_x0:src_x1]
    return out.astype(np.float32)


def apply_perturb(image: np.ndarray, perturb: Perturb, rng: np.random.Generator) -> np.ndarray:
    name = perturb.name
    cur = image.astype(np.float32).copy()
    if name == "identity":
        return cur
    if name.startswith("noise_"):
        std = float(name.removeprefix("noise_").replace("p", "."))
        return np.clip(cur + rng.normal(0.0, std, cur.shape).astype(np.float32), 0.0, 1.0)
    if name.startswith("blur") and "_noise" not in name:
        raw = name.removeprefix("blur")
        length_text, angle_text = raw.split("a", 1)
        return train.camera_motion_blur_np(cur, int(length_text), int(angle_text))
    if name.startswith("blur") and "_noise" in name:
        blur_part, noise_part = name.split("_", 1)
        cur = apply_perturb(cur, Perturb(blur_part, "blur", perturb.severity), rng)
        std = float(noise_part.removeprefix("noise").replace("p", "."))
        return np.clip(cur + rng.normal(0.0, std, cur.shape).astype(np.float32), 0.0, 1.0)
    if name.startswith("bright_p"):
        value = float(name.removeprefix("bright_p").replace("p", "."))
        return np.clip(cur + value, 0.0, 1.0)
    if name.startswith("bright_m"):
        value = float(name.removeprefix("bright_m").replace("p", "."))
        return np.clip(cur - value, 0.0, 1.0)
    if name.startswith("contrast_p"):
        value = float(name.removeprefix("contrast_p").replace("p", "."))
        return np.clip((cur - 0.5) * (1.0 + value) + 0.5, 0.0, 1.0)
    if name.startswith("contrast_m"):
        value = float(name.removeprefix("contrast_m").replace("p", "."))
        return np.clip((cur - 0.5) * max(0.0, 1.0 - value) + 0.5, 0.0, 1.0)
    shift_map = {
        "shift_u1": (-1, 0),
        "shift_d1": (1, 0),
        "shift_l1": (0, -1),
        "shift_r1": (0, 1),
        "shift_ul1": (-1, -1),
        "shift_dr1": (1, 1),
        "shift_u2": (-2, 0),
        "shift_d2": (2, 0),
        "shift_l2": (0, -2),
        "shift_r2": (0, 2),
        "shift_u1_noise0p04": (-1, 0),
        "shift_l1_noise0p04": (0, -1),
    }
    if name in shift_map:
        dy, dx = shift_map[name]
        cur = shift_image(cur, dy, dx)
        if name.endswith("_noise0p04"):
            cur = np.clip(cur + rng.normal(0.0, 0.04, cur.shape).astype(np.float32), 0.0, 1.0)
        return cur.astype(np.float32)
    raise ValueError(f"unknown perturb: {name}")


def tflite_raw_int8(path: Path, images: np.ndarray) -> tuple[np.ndarray, list[str]]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_index = int(input_detail["index"])
    output_index = int(output_detail["index"])
    in_scale, in_zero = input_detail.get("quantization", (0.0, 0))
    if output_detail["dtype"] != np.int8:
        raise ValueError(f"{path} output dtype is {output_detail['dtype']}, expected int8")
    rows: list[np.ndarray] = []
    for image in images:
        value = image[None, ...].astype(np.float32)
        if input_detail["dtype"] == np.int8:
            value = np.clip(np.rint(value / float(in_scale) + int(in_zero)), -128, 127).astype(np.int8)
        interpreter.set_tensor(input_index, value)
        interpreter.invoke()
        rows.append(interpreter.get_tensor(output_index)[0].astype(np.int8))
    ops = sorted(set(str(item["op_name"]) for item in interpreter._get_ops_details()))  # noqa: SLF001
    return np.stack(rows).astype(np.int8), ops


def classify_one(
    feature: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    metric_weights: np.ndarray | None = None,
    cascade_mode: str = "none",
    cascade_k: int = 1,
    cascade_gate_margin: int = 0,
) -> dict[str, Any]:
    x = feature.astype(np.int32)
    p = prototypes.astype(np.int32)
    delta2 = (p - x[None, :]) ** 2
    if metric_weights is None:
        dist = np.sum(delta2, axis=1)
    else:
        weights = np.asarray(metric_weights, dtype=np.int32).reshape(1, -1)
        if weights.shape[1] != delta2.shape[1]:
            raise ValueError(f"metric weight dim {weights.shape[1]} != feature dim {delta2.shape[1]}")
        dist = np.sum(delta2 * weights, axis=1)
    class_dist: list[int] = []
    nearest: list[int] = []
    cascade_scores: list[float] = []
    topk_count = max(1, int(cascade_k))
    for parent in range(3):
        indexes = np.where(prototype_parent == parent)[0]
        local = dist[indexes]
        local_arg = int(np.argmin(local))
        class_dist.append(int(local[local_arg]))
        nearest.append(int(indexes[local_arg]))
        if cascade_mode == "mean":
            top = np.partition(local, min(topk_count, len(local)) - 1)[: min(topk_count, len(local))]
            cascade_scores.append(float(np.mean(top)))
        elif cascade_mode == "kth":
            kth = min(topk_count, len(local)) - 1
            cascade_scores.append(float(np.partition(local, kth)[kth]))
        elif cascade_mode == "none":
            cascade_scores.append(float(class_dist[-1]))
        else:
            raise ValueError(f"unknown cascade mode: {cascade_mode}")
    primary_order = np.argsort(np.asarray(class_dist, dtype=np.int64))
    primary_pred = int(primary_order[0])
    primary_margin = int(class_dist[int(primary_order[1])] - class_dist[int(primary_order[0])])
    cascade_used = cascade_mode != "none" and primary_margin <= int(cascade_gate_margin)
    if cascade_used:
        order = np.argsort(np.asarray(cascade_scores, dtype=np.float64))
        score_values = cascade_scores
    else:
        order = primary_order
        score_values = [float(value) for value in class_dist]
    pred = int(order[0])
    margin = int(round(float(score_values[int(order[1])] - score_values[int(order[0])])))
    return {
        "pred": pred,
        "margin": margin,
        "primary_pred": primary_pred,
        "primary_margin": primary_margin,
        "cascade_used": bool(cascade_used),
        "class0_dist": int(class_dist[0]),
        "class1_dist": int(class_dist[1]),
        "class2_dist": int(class_dist[2]),
        "cascade_class0_score": float(cascade_scores[0]),
        "cascade_class1_score": float(cascade_scores[1]),
        "cascade_class2_score": float(cascade_scores[2]),
        "nearest_parent0": int(nearest[0]),
        "nearest_parent1": int(nearest[1]),
        "nearest_parent2": int(nearest[2]),
    }


def class_dist_for_existing(
    feature: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    parent: int,
    metric_weights: np.ndarray | None = None,
) -> tuple[int, int]:
    result = classify_one(feature, prototypes, prototype_parent, metric_weights=metric_weights)
    return int(result[f"class{parent}_dist"]), int(result[f"nearest_parent{parent}"])


def infer_prototype_int8_scale(prototypes_float: np.ndarray, prototypes_int8: np.ndarray) -> float:
    limit = min(len(prototypes_float), len(prototypes_int8))
    if limit <= 0:
        raise ValueError("cannot infer int8 scale without overlapping prototypes")
    candidates = [1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128]
    best: tuple[float, float] | None = None
    for scale in candidates:
        q = np.clip(np.rint(prototypes_float[:limit] * float(scale)), -128, 127).astype(np.int8)
        err = float(np.mean(np.abs(q.astype(np.int16) - prototypes_int8[:limit].astype(np.int16))))
        score = (err, -float(scale))
        if best is None or score < best:
            best = score
            best_scale = float(scale)
    assert best is not None
    return best_scale


def prototypes_int8_from_payload(payload: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
    if "prototypes_int8" in payload:
        prototypes_int8 = np.asarray(payload["prototypes_int8"], dtype=np.int8)
        if len(prototypes_int8) == len(prototype_parent):
            return prototypes_int8, prototype_parent
    if "prototypes" not in payload:
        raise KeyError("params npz must contain matching prototypes_int8 or float prototypes")
    prototypes_float = np.asarray(payload["prototypes"], dtype=np.float32)
    if "int8_scale" in payload:
        scale = float(np.asarray(payload["int8_scale"]).reshape(-1)[0])
    elif "prototypes_int8" in payload:
        scale = infer_prototype_int8_scale(prototypes_float, np.asarray(payload["prototypes_int8"], dtype=np.int8))
    else:
        raise KeyError("params npz has float prototypes but no int8_scale/prototypes_int8 to infer quantization")
    prototypes_int8 = np.clip(np.rint(prototypes_float * scale), -128, 127).astype(np.int8)
    if len(prototypes_int8) != len(prototype_parent):
        raise ValueError(f"prototype length mismatch: {len(prototypes_int8)} prototypes vs {len(prototype_parent)} parents")
    return prototypes_int8, prototype_parent


def metric_weights_from_payload(payload: dict[str, Any], feature_dim: int) -> np.ndarray | None:
    if "metric_weights_int32" not in payload:
        return None
    weights = np.asarray(payload["metric_weights_int32"], dtype=np.int32).reshape(-1)
    if len(weights) != int(feature_dim):
        raise ValueError(f"metric_weights_int32 length {len(weights)} != feature dim {feature_dim}")
    if np.any(weights < 0):
        raise ValueError("metric_weights_int32 must be non-negative")
    if not np.any(weights > 0):
        raise ValueError("metric_weights_int32 must contain at least one positive weight")
    return weights


def replay_int8_fields(payload: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    if "int8_pred" in payload and "int8_margin" in payload:
        return np.asarray(payload["int8_pred"], dtype=np.int64), np.asarray(payload["int8_margin"], dtype=np.int64)
    if "embedding_int8" not in payload:
        raise KeyError("params npz must contain int8_margin or embedding_int8 for replay")
    embeddings = np.asarray(payload["embedding_int8"], dtype=np.int8)
    prototypes, prototype_parent = prototypes_int8_from_payload(payload)
    metric_weights = metric_weights_from_payload(payload, embeddings.shape[1])
    pred = np.empty(len(embeddings), dtype=np.int64)
    margin = np.empty(len(embeddings), dtype=np.int64)
    for index, feature in enumerate(embeddings):
        result = classify_one(feature, prototypes, prototype_parent, metric_weights=metric_weights)
        pred[index] = int(result["pred"])
        margin[index] = int(result["margin"])
    return pred, margin


def build_view_cache(dataset_dir: Path, view_names: list[str]) -> tuple[dict[str, np.ndarray], np.ndarray, np.ndarray, list[str]]:
    x, _y_sub, y_parent, paths, _rows = train.load_dataset_v5(dataset_dir)
    cache: dict[str, np.ndarray] = {}
    for view in view_names:
        cache[view] = x.astype(np.float32) if view == "clean" else train.stress_batch_any(view, x).astype(np.float32)
    return cache, x.astype(np.float32), y_parent.astype(np.int64), paths


def select_rows(
    *,
    margins: np.ndarray,
    parents: np.ndarray,
    view_labels: np.ndarray,
    low_threshold: int,
    max_low: int,
    control_margin_min: int,
    seed: int,
) -> tuple[np.ndarray, np.ndarray]:
    low = np.where(margins <= low_threshold)[0]
    low = low[np.argsort(margins[low], kind="stable")]
    if max_low > 0:
        low = low[:max_low]
    rng = np.random.default_rng(seed)
    control_pool = np.where(margins >= control_margin_min)[0]
    controls: list[int] = []
    for parent in sorted(np.unique(parents[low]).astype(int).tolist()):
        need = int(np.sum(parents[low] == parent))
        pool = control_pool[parents[control_pool] == parent]
        if len(pool) == 0 or need == 0:
            continue
        chosen = rng.choice(pool, size=min(need, len(pool)), replace=False)
        controls.extend(int(item) for item in chosen.tolist())
    if len(controls) < len(low):
        remaining = np.setdiff1d(control_pool, np.asarray(controls, dtype=np.int64), assume_unique=False)
        if len(remaining) > 0:
            extra = rng.choice(remaining, size=min(len(low) - len(controls), len(remaining)), replace=False)
            controls.extend(int(item) for item in extra.tolist())
    controls_arr = np.asarray(sorted(set(controls)), dtype=np.int64)
    return low.astype(np.int64), controls_arr


def map_selection_rows(
    *,
    selection_sample_index: np.ndarray,
    selection_view_labels: np.ndarray,
    target_sample_index: np.ndarray,
    target_view_labels: np.ndarray,
    selected: list[tuple[str, int]],
    allow_missing: bool,
) -> list[tuple[str, int, int]]:
    target_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(target_sample_index.tolist(), target_view_labels.tolist()))
    }
    mapped: list[tuple[str, int, int]] = []
    missing: list[tuple[int, str]] = []
    for group, selection_index in selected:
        key = (int(selection_sample_index[selection_index]), str(selection_view_labels[selection_index]))
        target_index = target_by_key.get(key)
        if target_index is None:
            if allow_missing:
                mapped.append((group, -1, int(selection_index)))
            else:
                missing.append(key)
            continue
        mapped.append((group, target_index, int(selection_index)))
    if missing:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing[:10])
        raise ValueError(f"target params are missing {len(missing)} selection rows, first: {preview}")
    return mapped


def summarize_group(rows: list[dict[str, Any]], keys: list[str]) -> list[dict[str, Any]]:
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for row in rows:
        groups.setdefault(tuple(row.get(key) for key in keys), []).append(row)
    out: list[dict[str, Any]] = []
    for values, items in groups.items():
        total = len(items)
        wrong = sum(1 for item in items if bool(item["wrong"]))
        margins = np.asarray([int(item["stress_margin"]) for item in items], dtype=np.int64)
        out.append(
            {
                **{key: value for key, value in zip(keys, values)},
                "total": int(total),
                "wrong": int(wrong),
                "accuracy": float((total - wrong) / max(1, total)),
                "wrong_rate": float(wrong / max(1, total)),
                "stress_margin_min": int(np.min(margins)),
                "stress_margin_p05": float(np.percentile(margins, 5)),
                "stress_margin_median": float(np.median(margins)),
            }
        )
    return sorted(out, key=lambda row: (row["accuracy"], -row["total"], str(row.get(keys[0], ""))))


def first_failures(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_base: dict[tuple[str, int], list[dict[str, Any]]] = {}
    for row in rows:
        if bool(row["wrong"]):
            by_base.setdefault((str(row["group"]), int(row["base_query_index"])), []).append(row)
    out: list[dict[str, Any]] = []
    for (_group, _index), items in by_base.items():
        items.sort(key=lambda row: (float(row["perturb_severity"]), str(row["perturb"])))
        out.append(items[0])
    return sorted(out, key=lambda row: (int(row["base_margin"]), float(row["perturb_severity"]), str(row["view_label"])))


def main() -> None:
    parser = argparse.ArgumentParser(description="Stress-test V8 low-margin prototype events with extra input perturbations.")
    parser.add_argument("--tflite", type=Path, required=True)
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--selection-params-npz", type=Path, default=None)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--low-margin-threshold", type=int, default=8)
    parser.add_argument("--max-low", type=int, default=0)
    parser.add_argument("--control-margin-min", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20260520)
    parser.add_argument("--perturbs", default="all")
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--cascade-mode", choices=["none", "mean", "kth"], default="none")
    parser.add_argument("--cascade-k", type=int, default=1)
    parser.add_argument("--cascade-gate-margin", type=int, default=0)
    parser.add_argument(
        "--allow-missing-selection-rows",
        action="store_true",
        help=(
            "Allow canonical selection rows that are absent from the target params. "
            "The selected sample/view is still stress-rendered through the target TFLite; "
            "target normal margin fields fall back to the selection margin for those rows."
        ),
    )
    parser.add_argument("--export-wrong-images", action="store_true")
    parser.add_argument("--wrong-image-dir", type=Path, default=None)
    parser.add_argument("--export-first-failures-only", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    perturb_specs = perturb_by_name(parse_csv(args.perturbs))
    with np.load(args.params_npz, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}
    _int8_pred, margins = replay_int8_fields(payload)
    parents = np.asarray(payload["parent"], dtype=np.int64)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    paths = np.asarray(payload["paths"]).astype(str)
    prototypes, prototype_parent = prototypes_int8_from_payload(payload)
    metric_weights = metric_weights_from_payload(payload, prototypes.shape[1])
    prototype_sample = np.asarray(payload.get("prototype_sample_index", np.full(len(prototypes), -1)), dtype=np.int64)
    prototype_view = np.asarray(payload.get("prototype_view_label", np.asarray([""] * len(prototypes)))).astype(str)

    selection_payload = payload
    if args.selection_params_npz is not None:
        with np.load(args.selection_params_npz, allow_pickle=True) as data:
            selection_payload = {key: data[key] for key in data.files}
    _selection_int8_pred, selection_margins = replay_int8_fields(selection_payload)
    selection_parents = np.asarray(selection_payload["parent"], dtype=np.int64)
    selection_sample_index = np.asarray(selection_payload["sample_index"], dtype=np.int64)
    selection_view_labels = np.asarray(selection_payload["view_labels"]).astype(str)

    low_rows, control_rows = select_rows(
        margins=selection_margins,
        parents=selection_parents,
        view_labels=selection_view_labels,
        low_threshold=args.low_margin_threshold,
        max_low=args.max_low,
        control_margin_min=args.control_margin_min,
        seed=args.seed,
    )
    selected_from_source = [("low", int(index)) for index in low_rows.tolist()] + [
        ("control", int(index)) for index in control_rows.tolist()
    ]
    selected = map_selection_rows(
        selection_sample_index=selection_sample_index,
        selection_view_labels=selection_view_labels,
        target_sample_index=sample_index,
        target_view_labels=view_labels,
        selected=selected_from_source,
        allow_missing=bool(args.allow_missing_selection_rows),
    )
    selected_view_names = sorted(
        set(
            str(view_labels[index]) if int(index) >= 0 else str(selection_view_labels[selection_index])
            for _group, index, selection_index in selected
        )
    )
    view_cache, _clean_x, y_parent_base, paths_base = build_view_cache(args.dataset_dir, selected_view_names)

    image_rows: list[np.ndarray] = []
    meta_rows: list[dict[str, Any]] = []
    for group, query_index, selection_index in selected:
        if int(query_index) >= 0:
            view = str(view_labels[query_index])
            sample = int(sample_index[query_index])
            target_margin = int(margins[query_index])
            parent_value = int(parents[query_index])
            rng_query_index = int(query_index)
            base_query_index = int(query_index)
            target_query_index = int(query_index)
            used_selection_fallback = False
        else:
            view = str(selection_view_labels[selection_index])
            sample = int(selection_sample_index[selection_index])
            target_margin = int(selection_margins[selection_index])
            parent_value = int(selection_parents[selection_index])
            rng_query_index = int(selection_index)
            base_query_index = int(selection_index)
            target_query_index = -1
            used_selection_fallback = True
        base_image = view_cache[view][sample]
        selection_margin = int(selection_margins[selection_index])
        for perturb in perturb_specs:
            rng_seed = args.seed + rng_query_index * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
            rng = np.random.default_rng(rng_seed)
            image_rows.append(apply_perturb(base_image, perturb, rng))
            meta_rows.append(
                {
                    "group": group,
                    "base_query_index": base_query_index,
                    "target_query_index": target_query_index,
                    "sample_index": sample,
                    "path": str(paths_base[sample]),
                    "view_label": view,
                    "parent": parent_value,
                    "base_margin": target_margin,
                    "base_margin_bucket": margin_bucket(target_margin),
                    "selection_query_index": int(selection_index),
                    "selection_margin": selection_margin,
                    "selection_margin_bucket": margin_bucket(selection_margin),
                    "selection_fallback_row": used_selection_fallback,
                    "perturb": perturb.name,
                    "perturb_family": perturb.family,
                    "perturb_severity": float(perturb.severity),
                }
            )
    images = np.stack(image_rows).astype(np.float32)
    features_chunks: list[np.ndarray] = []
    ops: list[str] = []
    for start in range(0, len(images), args.batch_size):
        chunk, ops = tflite_raw_int8(args.tflite, images[start : start + args.batch_size])
        features_chunks.append(chunk)
    features = np.concatenate(features_chunks, axis=0).astype(np.int8)

    result_rows: list[dict[str, Any]] = []
    wrong_rows: list[dict[str, Any]] = []
    wrong_image_dir = args.wrong_image_dir or (args.output_dir / "wrong_stress_images")
    pending_wrong_images: list[tuple[dict[str, Any], np.ndarray]] = []
    for event_index, (meta, feature) in enumerate(zip(meta_rows, features)):
        cls = classify_one(
            feature,
            prototypes,
            prototype_parent,
            metric_weights=metric_weights,
            cascade_mode=args.cascade_mode,
            cascade_k=args.cascade_k,
            cascade_gate_margin=args.cascade_gate_margin,
        )
        parent = int(meta["parent"])
        pred = int(cls["pred"])
        wrong = pred != parent
        correct_dist = int(cls[f"class{parent}_dist"])
        wrong_parent_order = [item for item in [0, 1, 2] if item != parent]
        nearest_wrong_parent = min(wrong_parent_order, key=lambda item: int(cls[f"class{item}_dist"]))
        nearest_correct = int(cls[f"nearest_parent{parent}"])
        nearest_wrong = int(cls[f"nearest_parent{nearest_wrong_parent}"])
        feature_values = {f"feature{index}": int(feature[index]) for index in range(min(3, len(feature)))}
        row = {
            **meta,
            "event_index": int(event_index),
            "stress_pred": pred,
            "wrong": bool(wrong),
            "primary_pred": int(cls["primary_pred"]),
            "primary_margin": int(cls["primary_margin"]),
            "cascade_used": bool(cls["cascade_used"]),
            "cascade_mode": args.cascade_mode,
            "cascade_k": int(args.cascade_k),
            "cascade_gate_margin": int(args.cascade_gate_margin),
            "stress_margin": int(cls["margin"]),
            "stress_margin_bucket": margin_bucket(int(cls["margin"])),
            "feature_dim": int(len(feature)),
            "feature_json": json.dumps([int(item) for item in feature.tolist()], separators=(",", ":")),
            **feature_values,
            "correct_dist": correct_dist,
            "nearest_wrong_parent": int(nearest_wrong_parent),
            "nearest_wrong_dist": int(cls[f"class{nearest_wrong_parent}_dist"]),
            "nearest_correct_proto": nearest_correct,
            "nearest_correct_proto_sample": int(prototype_sample[nearest_correct]),
            "nearest_correct_proto_view": str(prototype_view[nearest_correct]),
            "nearest_wrong_proto": nearest_wrong,
            "nearest_wrong_proto_sample": int(prototype_sample[nearest_wrong]),
            "nearest_wrong_proto_view": str(prototype_view[nearest_wrong]),
        }
        result_rows.append(row)
        if wrong:
            wrong_rows.append(row)
            if args.export_wrong_images:
                pending_wrong_images.append((row, images[event_index]))

    per_perturb = summarize_group(result_rows, ["group", "perturb_family", "perturb"])
    per_view = summarize_group(result_rows, ["group", "view_label"])
    per_bucket = summarize_group(result_rows, ["group", "base_margin_bucket"])
    per_selection_bucket = summarize_group(result_rows, ["group", "selection_margin_bucket"])
    per_group = summarize_group(result_rows, ["group"])
    failures = first_failures(result_rows)
    if args.export_wrong_images:
        export_ids: set[int] | None = None
        if args.export_first_failures_only:
            export_ids = {int(row["event_index"]) for row in failures}
        for row, image in pending_wrong_images:
            if export_ids is not None and int(row["event_index"]) not in export_ids:
                continue
            filename = (
                f"{int(row['event_index']):06d}_"
                f"{safe_name(row['group'])}_q{int(row['base_query_index'])}_s{int(row['sample_index'])}_"
                f"p{int(row['parent'])}_pred{int(row['stress_pred'])}_"
                f"base{int(row['base_margin'])}_stress{int(row['stress_margin'])}_"
                f"{safe_name(row['view_label'])}_{safe_name(row['perturb'])}.png"
            )
            image_path = wrong_image_dir / filename
            save_stress_image(image_path, image)
            row["wrong_image_path"] = str(image_path)
        if pending_wrong_images:
            by_event_path = {int(row["event_index"]): row.get("wrong_image_path", "") for row, _image in pending_wrong_images}
            for row in wrong_rows:
                if int(row["event_index"]) in by_event_path:
                    row["wrong_image_path"] = by_event_path[int(row["event_index"])]
            for row in failures:
                if int(row["event_index"]) in by_event_path:
                    row["wrong_image_path"] = by_event_path[int(row["event_index"])]
    write_csv(args.output_dir / "stress_events.csv", result_rows)
    write_csv(args.output_dir / "wrong_events.csv", wrong_rows)
    write_csv(args.output_dir / "first_failure_by_base.csv", failures)
    write_csv(args.output_dir / "per_perturb_summary.csv", per_perturb)
    write_csv(args.output_dir / "per_view_summary.csv", per_view)
    write_csv(args.output_dir / "per_margin_bucket_summary.csv", per_bucket)
    write_csv(args.output_dir / "per_selection_margin_bucket_summary.csv", per_selection_bucket)
    write_csv(args.output_dir / "per_group_summary.csv", per_group)
    summary = {
        "tflite": str(args.tflite),
        "params_npz": str(args.params_npz),
        "selection_params_npz": str(args.selection_params_npz or args.params_npz),
        "output_dir": str(args.output_dir),
        "ops": ops,
        "low_margin_threshold": int(args.low_margin_threshold),
        "low_rows": int(len(low_rows)),
        "control_rows": int(len(control_rows)),
        "perturb_count": int(len(perturb_specs)),
        "total_events": int(len(result_rows)),
        "wrong_events": int(len(wrong_rows)),
        "wrong_base_count": int(len(failures)),
        "export_wrong_images": bool(args.export_wrong_images),
        "export_first_failures_only": bool(args.export_first_failures_only),
        "wrong_image_dir": str(wrong_image_dir) if args.export_wrong_images else "",
        "wrong_images_exported": int(
            len(failures) if args.export_wrong_images and args.export_first_failures_only else len(wrong_rows)
        )
        if args.export_wrong_images
        else 0,
        "metric_weights_int32": [int(item) for item in metric_weights.tolist()] if metric_weights is not None else [],
        "per_perturb_top20": per_perturb[:20],
        "per_view_top20": per_view[:20],
        "per_group": per_group,
        "per_margin_bucket": per_bucket,
        "per_selection_margin_bucket": per_selection_bucket,
    }
    write_json(args.output_dir / "stress_summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
