import argparse
import csv
import json
import math
import os
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf


EventKey = tuple[str, int, str, int]
NormalKey = tuple[int, str]


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


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def parse_list(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def read_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def parse_feature(row: dict[str, str]) -> np.ndarray:
    if row.get("feature_json"):
        return np.asarray(json.loads(row["feature_json"]), dtype=np.int8)
    dim = int(float(row.get("feature_dim", 4)))
    return np.asarray([int(float(row.get(f"feature{index}", 0))) for index in range(dim)], dtype=np.int8)


def transformed_margin(margin: np.ndarray, dim: int, mode: str) -> np.ndarray:
    values = np.maximum(margin.astype(np.float64), 0.0)
    if mode == "raw":
        return values
    if mode == "per_sqrt_dim":
        return values / math.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return values / float(max(dim, 1))
    if mode == "log":
        return np.log1p(values)
    raise ValueError(f"unknown score mode: {mode}")


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    class_rows: list[np.ndarray] = []
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            class_dist[:, parent] = np.min(dist[:, indexes], axis=1)
        order = np.argsort(class_dist, axis=1)
        rows = np.arange(len(x))
        pred_rows.append(order[:, 0].astype(np.int64))
        margin_rows.append((class_dist[rows, order[:, 1]] - class_dist[rows, order[:, 0]]).astype(np.int64))
        class_rows.append(class_dist.astype(np.int64))
    return (
        np.concatenate(pred_rows).astype(np.int64),
        np.concatenate(margin_rows).astype(np.int64),
        np.concatenate(class_rows).astype(np.int64),
    )


def source_score_matrix(
    *,
    base_payload: dict[str, np.ndarray],
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    score_mode: str,
    normalize_p90: bool,
) -> tuple[np.ndarray, np.ndarray]:
    base_sample = np.asarray(base_payload["sample_index"], dtype=np.int64)
    base_view = np.asarray(base_payload["view_labels"]).astype(str)
    scores = np.full((len(base_sample), len(source_order)), -1.0e6, dtype=np.float64)
    present = np.zeros((len(base_sample), len(source_order)), dtype=bool)
    for source_index, source in enumerate(source_order):
        payload = payloads[source]
        key_map = normal_key_map(payload)
        margin = np.asarray(payload["int8_margin"], dtype=np.float64)
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)
        parent = np.asarray(payload["parent"], dtype=np.int64)
        dim = int(np.asarray(payload["embedding_int8"]).shape[1])
        source_scores = transformed_margin(margin, dim, score_mode)
        if normalize_p90:
            correct_scores = source_scores[pred == parent]
            scale = float(np.percentile(np.maximum(correct_scores, 1.0), 90)) if correct_scores.size else 1.0
            source_scores = source_scores / max(scale, 1.0)
        source_scores = np.where(pred == parent, source_scores, -1.0 - np.abs(source_scores))
        for row_index, key in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
            source_row = key_map.get((int(key[0]), str(key[1])))
            if source_row is None:
                continue
            scores[row_index, source_index] = float(source_scores[source_row])
            present[row_index, source_index] = True
    return scores, present


def build_features(
    *,
    codes: np.ndarray,
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    view_labels: np.ndarray,
    view_order: list[str],
    family_order: list[str],
    mode: str,
) -> np.ndarray:
    tokens = set(mode.split("+"))
    parts: list[np.ndarray] = []
    if "code" in tokens:
        parts.append(codes.astype(np.float32))
    if "dist" in tokens:
        parts.append(np.log1p(np.maximum(class_dist.astype(np.float32), 0.0)))
    if "margin" in tokens:
        parts.append(np.log1p(np.maximum(margin.astype(np.float32), 0.0))[:, None])
    if "pred" in tokens:
        one_hot = np.eye(3, dtype=np.float32)[pred.astype(np.int64)]
        parts.append(one_hot)
    if "view" in tokens:
        index = {name: offset for offset, name in enumerate(view_order)}
        rows = np.zeros((len(view_labels), len(view_order)), dtype=np.float32)
        for row_index, view in enumerate(view_labels.astype(str).tolist()):
            value = index.get(str(view))
            if value is not None:
                rows[row_index, value] = 1.0
        parts.append(rows)
    if "family" in tokens:
        index = {name: offset for offset, name in enumerate(family_order)}
        rows = np.zeros((len(view_labels), len(family_order)), dtype=np.float32)
        for row_index, view in enumerate(view_labels.astype(str).tolist()):
            value = index.get(view_family(str(view)))
            if value is not None:
                rows[row_index, value] = 1.0
        parts.append(rows)
    if not parts:
        raise ValueError(f"empty feature mode: {mode}")
    return np.concatenate(parts, axis=1).astype(np.float32)


def view_family(view: str) -> str:
    if view == "clean":
        return "clean"
    if view.startswith("rot") or view.startswith("mirror"):
        return "d4"
    if "noise" in view and "blur" in view:
        return "blur_noise"
    if "blur" in view:
        return "blur"
    if "noise" in view:
        return "noise"
    if "bright" in view:
        return "brightness"
    if "contrast" in view:
        return "contrast"
    if "shift" in view:
        return "shift"
    return "other"


def build_model(input_dim: int, output_dim: int, hidden_dim: int, learning_rate: float, seed: int) -> tf.keras.Model:
    tf.keras.utils.set_random_seed(seed)
    inputs = tf.keras.Input((input_dim,), name="gate_features")
    if hidden_dim > 0:
        x = tf.keras.layers.Dense(hidden_dim, activation="relu", name="hidden")(inputs)
    else:
        x = inputs
    outputs = tf.keras.layers.Dense(output_dim, name="source_logits")(x)
    model = tf.keras.Model(inputs, outputs, name="v8_source_gate_probe")
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=learning_rate),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
        metrics=[tf.keras.metrics.SparseCategoricalAccuracy(name="acc")],
    )
    return model


def softmax_numpy(logits: np.ndarray) -> np.ndarray:
    shifted = logits.astype(np.float64) - np.max(logits.astype(np.float64), axis=1, keepdims=True)
    exp = np.exp(shifted)
    return (exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-12)).astype(np.float64)


def summarize_selection(
    *,
    selected: np.ndarray,
    base_rows: list[dict[str, str]],
    common_keys: list[EventKey],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    for row_index, row in enumerate(base_rows):
        source = source_order[int(selected[row_index])]
        chosen[source] += 1
        pred = int(source_events[source][common_keys[row_index]]["stress_pred"])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
    wrong_events = int(sum(value[0] for value in grouped.values()))
    total_events = int(sum(value[1] for value in grouped.values()))
    return {
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
        "chosen_counts": dict(chosen),
    }


def split_by_sample(sample_index: np.ndarray, validation_mod: int) -> tuple[np.ndarray, np.ndarray]:
    if validation_mod <= 1:
        train = np.ones(len(sample_index), dtype=bool)
        val = np.zeros(len(sample_index), dtype=bool)
        return train, val
    val = (sample_index.astype(np.int64) % int(validation_mod)) == 0
    train = ~val
    if not np.any(train) or not np.any(val):
        return np.ones(len(sample_index), dtype=bool), np.zeros(len(sample_index), dtype=bool)
    return train, val


def main() -> None:
    parser = argparse.ArgumentParser(description="Train a normal-only learned source gate and evaluate on V8 high-pressure events.")
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--feature-modes", default="code,code+dist+margin+pred,code+dist+margin")
    parser.add_argument("--score-modes", default="raw,log,per_sqrt_dim")
    parser.add_argument("--hidden-dims", default="0,8,16")
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument(
        "--cascade-quantiles",
        default="",
        help="Normal-confidence quantiles for D4 fallback cascade policies, e.g. 0.5,0.7,0.9.",
    )
    parser.add_argument("--no-normalize-p90", action="store_false", dest="normalize_p90")
    parser.set_defaults(normalize_p90=True)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source order mismatch: {source_order} != {normal_order}")
    source_events = {name: read_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_keys:
        raise ValueError("source event files have no common high-pressure events")
    base_rows = [source_events[source_order[0]][key] for key in common_keys]
    normal_payloads = {name: load_npz(path) for name, path in normal_items}
    base_payload = load_npz(args.base_params_npz)
    base_sample = np.asarray(base_payload["sample_index"], dtype=np.int64)
    prototypes = np.asarray(base_payload["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base_payload["prototype_parent"], dtype=np.int64)

    normal_codes = np.asarray(base_payload["embedding_int8"], dtype=np.int8)
    normal_view_labels = np.asarray(base_payload["view_labels"]).astype(str)
    view_order = list(dict.fromkeys(normal_view_labels.tolist()))
    family_order = list(dict.fromkeys(view_family(view) for view in normal_view_labels.tolist()))
    normal_pred, normal_margin, normal_class_dist = class_distances(
        normal_codes,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )
    stress_codes = np.stack([parse_feature(row) for row in base_rows], axis=0).astype(np.int8)
    stress_view_labels = np.asarray([row["view_label"] for row in base_rows]).astype(str)
    stress_pred, stress_margin, stress_class_dist = class_distances(
        stress_codes,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )
    train_mask, val_mask = split_by_sample(base_sample, args.validation_mod)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    for score_mode in parse_list(args.score_modes):
        score_matrix, present = source_score_matrix(
            base_payload=base_payload,
            payloads=normal_payloads,
            source_order=source_order,
            score_mode=score_mode,
            normalize_p90=bool(args.normalize_p90),
        )
        valid = np.any(present, axis=1)
        labels = np.argmax(score_matrix, axis=1).astype(np.int64)
        class_counts = np.bincount(labels[valid], minlength=len(source_order)).astype(np.float64)
        class_weight = {
            index: float(np.mean(class_counts[class_counts > 0]) / max(class_counts[index], 1.0))
            for index in range(len(source_order))
        }
        for feature_mode in parse_list(args.feature_modes):
            x_normal = build_features(
                codes=normal_codes,
                pred=normal_pred,
                margin=normal_margin,
                class_dist=normal_class_dist,
                view_labels=normal_view_labels,
                view_order=view_order,
                family_order=family_order,
                mode=feature_mode,
            )
            x_stress = build_features(
                codes=stress_codes,
                pred=stress_pred,
                margin=stress_margin,
                class_dist=stress_class_dist,
                view_labels=stress_view_labels,
                view_order=view_order,
                family_order=family_order,
                mode=feature_mode,
            )
            mean = np.mean(x_normal[train_mask & valid], axis=0, keepdims=True)
            std = np.std(x_normal[train_mask & valid], axis=0, keepdims=True)
            std = np.maximum(std, 1.0e-6)
            x_train_all = (x_normal - mean) / std
            x_stress_all = (x_stress - mean) / std
            for hidden_dim in parse_ints(args.hidden_dims):
                model = build_model(
                    input_dim=x_train_all.shape[1],
                    output_dim=len(source_order),
                    hidden_dim=hidden_dim,
                    learning_rate=args.learning_rate,
                    seed=args.seed + hidden_dim + len(policy_rows),
                )
                train_indexes = train_mask & valid
                val_indexes = val_mask & valid
                callbacks: list[tf.keras.callbacks.Callback] = [
                    tf.keras.callbacks.EarlyStopping(
                        monitor="val_loss" if np.any(val_indexes) else "loss",
                        patience=10,
                        restore_best_weights=True,
                    )
                ]
                history = model.fit(
                    x_train_all[train_indexes],
                    labels[train_indexes],
                    validation_data=(x_train_all[val_indexes], labels[val_indexes]) if np.any(val_indexes) else None,
                    epochs=args.epochs,
                    batch_size=args.batch_size,
                    verbose=0,
                    class_weight=class_weight,
                    callbacks=callbacks,
                )
                train_pred = np.argmax(model.predict(x_train_all[train_indexes], batch_size=args.batch_size, verbose=0), axis=1)
                train_acc = float(np.mean(train_pred == labels[train_indexes]))
                val_acc = None
                if np.any(val_indexes):
                    val_pred = np.argmax(model.predict(x_train_all[val_indexes], batch_size=args.batch_size, verbose=0), axis=1)
                    val_acc = float(np.mean(val_pred == labels[val_indexes]))
                stress_logits = model.predict(x_stress_all, batch_size=args.batch_size, verbose=0)
                stress_probs = softmax_numpy(stress_logits)
                selected = np.argmax(stress_logits, axis=1).astype(np.int64)
                summary = summarize_selection(
                    selected=selected,
                    base_rows=base_rows,
                    common_keys=common_keys,
                    source_events=source_events,
                    source_order=source_order,
                )
                policy_rows.append(
                    {
                        "policy": f"learned_gate:{feature_mode}:{score_mode}:hidden{hidden_dim}",
                        "selection_label_usage": "none",
                        "normal_training_usage": "source-margin winner labels",
                        "feature_mode": feature_mode,
                        "score_mode": score_mode,
                        "hidden_dim": int(hidden_dim),
                        "normalize_p90": bool(args.normalize_p90),
                        "view_feature_count": int(len(view_order)) if "view" in feature_mode.split("+") else 0,
                        "family_feature_count": int(len(family_order)) if "family" in feature_mode.split("+") else 0,
                        "normal_label_counts_json": json.dumps(
                            {source_order[index]: int(class_counts[index]) for index in range(len(source_order))},
                            ensure_ascii=False,
                        ),
                        "train_source_label_acc": train_acc,
                        "val_source_label_acc": val_acc,
                        "epochs_ran": int(len(history.history["loss"])),
                        **{key: value for key, value in summary.items() if key != "chosen_counts"},
                        "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
                    }
                )
                quantiles = parse_list(args.cascade_quantiles)
                if quantiles:
                    ref_indexes = val_indexes if np.any(val_indexes) else train_indexes
                    normal_logits = model.predict(x_train_all[ref_indexes], batch_size=args.batch_size, verbose=0)
                    normal_probs = softmax_numpy(normal_logits)
                    normal_gate_pred = np.argmax(normal_probs, axis=1).astype(np.int64)
                    normal_label = labels[ref_indexes]
                    normal_conf = np.max(normal_probs, axis=1)
                    normal_gate_margin = (
                        np.partition(normal_probs, -2, axis=1)[:, -1]
                        - np.partition(normal_probs, -2, axis=1)[:, -2]
                    )
                    normal_pred_adv = normal_probs[np.arange(len(normal_probs)), normal_gate_pred] - normal_probs[:, 0]
                    nonbase_label_mask = normal_label != 0
                    correct_nonbase_mask = nonbase_label_mask & (normal_gate_pred == normal_label)
                    ref_adv_mask = correct_nonbase_mask if np.any(correct_nonbase_mask) else nonbase_label_mask
                    stress_conf = np.max(stress_probs, axis=1)
                    stress_gate_margin = (
                        np.partition(stress_probs, -2, axis=1)[:, -1]
                        - np.partition(stress_probs, -2, axis=1)[:, -2]
                    )
                    stress_pred_adv = stress_probs[np.arange(len(stress_probs)), selected] - stress_probs[:, 0]
                    for quantile_text in quantiles:
                        quantile = float(quantile_text)
                        if quantile < 0.0 or quantile > 1.0:
                            raise ValueError(f"cascade quantile must be in [0,1], got {quantile}")
                        conf_threshold = float(np.quantile(normal_conf, quantile))
                        margin_threshold = float(np.quantile(normal_gate_margin, quantile))
                        adv_values = normal_pred_adv[ref_adv_mask]
                        adv_threshold = float(np.quantile(adv_values, quantile)) if len(adv_values) else 0.0
                        cascade_specs = [
                            (
                                "conf",
                                conf_threshold,
                                (selected != 0) & (stress_conf >= conf_threshold),
                            ),
                            (
                                "prob_margin",
                                margin_threshold,
                                (selected != 0) & (stress_gate_margin >= margin_threshold),
                            ),
                            (
                                "base_adv",
                                adv_threshold,
                                (selected != 0) & (stress_pred_adv >= adv_threshold),
                            ),
                        ]
                        for cascade_name, threshold, take_gate in cascade_specs:
                            cascade_selected = np.where(take_gate, selected, 0).astype(np.int64)
                            cascade_summary = summarize_selection(
                                selected=cascade_selected,
                                base_rows=base_rows,
                                common_keys=common_keys,
                                source_events=source_events,
                                source_order=source_order,
                            )
                            policy_rows.append(
                                {
                                    "policy": (
                                        f"learned_gate_cascade:{cascade_name}:"
                                        f"{feature_mode}:{score_mode}:hidden{hidden_dim}:q{quantile:g}"
                                    ),
                                    "selection_label_usage": "none",
                                    "normal_training_usage": "source-margin winner labels",
                                    "threshold_selection_usage": "normal_confidence_quantile",
                                    "feature_mode": feature_mode,
                                    "score_mode": score_mode,
                                    "hidden_dim": int(hidden_dim),
                                    "normalize_p90": bool(args.normalize_p90),
                                    "cascade_rule": cascade_name,
                                    "cascade_quantile": quantile,
                                    "cascade_threshold": threshold,
                                    "cascade_gate_event_count": int(np.sum(take_gate)),
                                    "cascade_gate_event_rate": float(np.mean(take_gate)),
                                    "view_feature_count": int(len(view_order)) if "view" in feature_mode.split("+") else 0,
                                    "family_feature_count": int(len(family_order)) if "family" in feature_mode.split("+") else 0,
                                    "normal_label_counts_json": json.dumps(
                                        {source_order[index]: int(class_counts[index]) for index in range(len(source_order))},
                                        ensure_ascii=False,
                                    ),
                                    "train_source_label_acc": train_acc,
                                    "val_source_label_acc": val_acc,
                                    "epochs_ran": int(len(history.history["loss"])),
                                    **{key: value for key, value in cascade_summary.items() if key != "chosen_counts"},
                                    "chosen_counts_json": json.dumps(cascade_summary["chosen_counts"], ensure_ascii=False),
                                }
                            )

    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    summary = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "base_params_npz": str(args.base_params_npz),
        "source_order": source_order,
        "view_order": view_order,
        "family_order": family_order,
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "learned source-margin winner gate only",
        "settings": int(len(policy_rows)),
        "top_policies": policy_rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
