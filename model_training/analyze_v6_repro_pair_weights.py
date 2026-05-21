import argparse
import csv
import json
import math
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_XLA_FLAGS", "--tf_xla_auto_jit=0")

import tensorflow as tf


PARENT_NAMES = ["supplies", "vehicle", "weapon"]
HEAD_LAYER_PREFIXES = (
    "parent",
    "subclass",
    "weapon_sub",
    "c4_attr",
    "c4_box",
    "c4_circuit",
    "c4_instance",
)


def load_keras(path: Path) -> tf.keras.Model:
    try:
        tf.keras.config.enable_unsafe_deserialization()
    except AttributeError:
        pass
    return tf.keras.models.load_model(path, compile=False, safe_mode=False)


def load_config(config_json: Path, config_name: str):
    import train_tiny32_v5_visual_subclass_scan as train

    data = json.loads(config_json.read_text(encoding="utf-8"))
    for index, item in enumerate(data.get("candidates", data if isinstance(data, list) else [])):
        config_data = item.get("config", item)
        label = str(item.get("label") or config_data.get("name") or f"candidate_{index:03d}")
        if label == config_name or config_data.get("name") == config_name:
            return train.config_from_dict(config_data, label)
    raise ValueError(f"config not found: {config_name}")


def load_model_or_weights(path: Path, config_json: Path | None, config_name: str | None) -> tf.keras.Model:
    if path.suffix == ".h5":
        if config_json is None or not config_name:
            raise ValueError("--*-config-json and --*-config-name are required for .weights.h5 inputs")
        import train_tiny32_v5_visual_subclass_scan as train

        config = load_config(config_json, config_name)
        model = train.build_model(config)
        model.load_weights(path)
        return model
    return load_keras(path)


def flatten(values: list[np.ndarray]) -> np.ndarray:
    if not values:
        return np.asarray([], dtype=np.float64)
    return np.concatenate([np.asarray(value, dtype=np.float64).reshape(-1) for value in values])


def cosine(a: np.ndarray, b: np.ndarray) -> float | None:
    denom = float(np.linalg.norm(a) * np.linalg.norm(b))
    if denom <= 0.0:
        return None
    return float(np.dot(a, b) / denom)


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


def is_head_layer(name: str) -> bool:
    return name.startswith(HEAD_LAYER_PREFIXES)


def layer_weight_stats(old_model: tf.keras.Model, new_model: tf.keras.Model) -> list[dict[str, object]]:
    new_layers = {layer.name: layer for layer in new_model.layers}
    rows: list[dict[str, object]] = []
    for old_layer in old_model.layers:
        new_layer = new_layers.get(old_layer.name)
        if new_layer is None:
            rows.append(
                {
                    "layer": old_layer.name,
                    "kind": old_layer.__class__.__name__,
                    "group": "missing_in_new",
                    "weight_count": len(old_layer.get_weights()),
                }
            )
            continue
        old_weights = old_layer.get_weights()
        new_weights = new_layer.get_weights()
        if len(old_weights) != len(new_weights):
            rows.append(
                {
                    "layer": old_layer.name,
                    "kind": old_layer.__class__.__name__,
                    "group": "shape_mismatch",
                    "old_weight_count": len(old_weights),
                    "new_weight_count": len(new_weights),
                }
            )
            continue
        if any(np.shape(a) != np.shape(b) for a, b in zip(old_weights, new_weights)):
            rows.append(
                {
                    "layer": old_layer.name,
                    "kind": old_layer.__class__.__name__,
                    "group": "shape_mismatch",
                    "old_shapes": [list(np.shape(value)) for value in old_weights],
                    "new_shapes": [list(np.shape(value)) for value in new_weights],
                }
            )
            continue
        old_flat = flatten(old_weights)
        new_flat = flatten(new_weights)
        stats = compare_arrays(old_flat, new_flat)
        rows.append(
            {
                "layer": old_layer.name,
                "kind": old_layer.__class__.__name__,
                "group": "head" if is_head_layer(old_layer.name) else "shared",
                "weight_count": int(old_flat.size),
                **stats,
            }
        )
    old_layer_names = {layer.name for layer in old_model.layers}
    for new_layer in new_model.layers:
        if new_layer.name not in old_layer_names:
            rows.append(
                {
                    "layer": new_layer.name,
                    "kind": new_layer.__class__.__name__,
                    "group": "new_only_head" if is_head_layer(new_layer.name) else "new_only",
                    "weight_count": int(flatten(new_layer.get_weights()).size),
                }
            )
    return rows


def parent_head_stats(old_model: tf.keras.Model, new_model: tf.keras.Model) -> dict[str, object]:
    old_layer = old_model.get_layer("parent_logits")
    new_layer = new_model.get_layer("parent_logits")
    old_kernel, old_bias = old_layer.get_weights()
    new_kernel, new_bias = new_layer.get_weights()
    per_class: list[dict[str, object]] = []
    for index, name in enumerate(PARENT_NAMES):
        stats = compare_arrays(old_kernel[:, index], new_kernel[:, index])
        per_class.append(
            {
                "parent": name,
                "old_bias": float(old_bias[index]),
                "new_bias": float(new_bias[index]),
                "bias_delta": float(new_bias[index] - old_bias[index]),
                **stats,
            }
        )

    boundaries: list[dict[str, object]] = []
    for i, left in enumerate(PARENT_NAMES):
        for j, right in enumerate(PARENT_NAMES):
            if i >= j:
                continue
            old_boundary = old_kernel[:, i] - old_kernel[:, j]
            new_boundary = new_kernel[:, i] - new_kernel[:, j]
            stats = compare_arrays(old_boundary, new_boundary)
            old_bias_delta = float(old_bias[i] - old_bias[j])
            new_bias_delta = float(new_bias[i] - new_bias[j])
            boundaries.append(
                {
                    "boundary": f"{left}_vs_{right}",
                    "old_bias_delta": old_bias_delta,
                    "new_bias_delta": new_bias_delta,
                    "bias_delta_change": float(new_bias_delta - old_bias_delta),
                    **stats,
                }
            )
    return {"per_class": per_class, "boundaries": boundaries}


def aggregate(rows: list[dict[str, object]]) -> dict[str, object]:
    comparable = [row for row in rows if row.get("relative_delta") is not None]
    shared = [row for row in comparable if row.get("group") == "shared" and row.get("weight_count", 0) > 0]
    head = [row for row in comparable if row.get("group") == "head" and row.get("weight_count", 0) > 0]

    def summarize(items: list[dict[str, object]]) -> dict[str, object]:
        if not items:
            return {}
        rel = np.asarray([float(item["relative_delta"]) for item in items], dtype=np.float64)
        cos = np.asarray([float(item["cosine"]) for item in items if item.get("cosine") is not None], dtype=np.float64)
        return {
            "layers": len(items),
            "relative_delta_mean": float(np.mean(rel)),
            "relative_delta_max": float(np.max(rel)),
            "cosine_mean": float(np.mean(cos)) if cos.size else None,
            "cosine_min": float(np.min(cos)) if cos.size else None,
        }

    top_delta = sorted(
        comparable,
        key=lambda row: float(row.get("relative_delta") or -math.inf),
        reverse=True,
    )[:12]
    return {
        "all_comparable_layers": len(comparable),
        "shared": summarize(shared),
        "head": summarize(head),
        "top_relative_delta_layers": top_delta,
    }


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


def model_layer_inventory(model: tf.keras.Model) -> list[dict[str, object]]:
    rows = []
    for layer in model.layers:
        rows.append(
            {
                "name": layer.name,
                "kind": layer.__class__.__name__,
                "output_shape": str(getattr(layer, "output_shape", "")),
                "weight_shapes": [list(np.shape(value)) for value in layer.get_weights()],
                "weight_count": int(flatten(layer.get_weights()).size),
            }
        )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--old-model", type=Path, required=True)
    parser.add_argument("--old-config-json", type=Path)
    parser.add_argument("--old-config-name")
    parser.add_argument("--new-model", type=Path, required=True)
    parser.add_argument("--new-config-json", type=Path)
    parser.add_argument("--new-config-name")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    old_model = load_model_or_weights(args.old_model, args.old_config_json, args.old_config_name)
    new_model = load_model_or_weights(args.new_model, args.new_config_json, args.new_config_name)

    layer_rows = layer_weight_stats(old_model, new_model)
    parent_stats = parent_head_stats(old_model, new_model)
    summary = {
        "old_model": str(args.old_model),
        "new_model": str(args.new_model),
        "old_layers": model_layer_inventory(old_model),
        "new_layers": model_layer_inventory(new_model),
        "layer_delta_summary": aggregate(layer_rows),
        "parent_head": parent_stats,
    }

    write_csv(args.output_dir / "layer_weight_delta.csv", layer_rows)
    write_csv(args.output_dir / "parent_head_class_delta.csv", parent_stats["per_class"])
    write_csv(args.output_dir / "parent_head_boundary_delta.csv", parent_stats["boundaries"])
    (args.output_dir / "weight_analysis_summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(summary["layer_delta_summary"], indent=2, ensure_ascii=False))
    print(json.dumps(summary["parent_head"], indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
