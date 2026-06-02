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

from analyze_v8_multisource_event_gate import build_model, parse_ints, parse_list
from stress_test_v8_low_margin import (
    apply_perturb,
    build_view_cache,
    classify_one,
    metric_weights_from_payload,
    perturb_by_name,
    prototypes_int8_from_payload,
    tflite_raw_int8,
)
from evaluate_v8_embedding_prototypes import write_csv


EventKey = tuple[str, int, str, int]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def safe_name(text: str) -> str:
    return "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in text)[:180]


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


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


def one_hot(values: np.ndarray, count: int) -> np.ndarray:
    out = np.zeros((len(values), count), dtype=np.float32)
    valid = (values >= 0) & (values < count)
    out[np.where(valid)[0], values[valid].astype(np.int64)] = 1.0
    return out


def margin_bucket_indexes(values: np.ndarray) -> np.ndarray:
    thresholds = np.asarray([1, 2, 4, 8, 16, 32, 64, 128], dtype=np.int64)
    return np.searchsorted(thresholds, values.astype(np.int64), side="left").astype(np.int64)


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


def select_training_rows(
    *,
    base_payload: dict[str, np.ndarray],
    excluded_base_rows: set[int],
    margin_max: int,
    max_rows: int,
) -> np.ndarray:
    margin = np.asarray(base_payload["int8_margin"], dtype=np.int64)
    parent = np.asarray(base_payload["parent"], dtype=np.int64)
    indexes = [
        int(index)
        for index in np.argsort(margin, kind="stable").tolist()
        if int(index) not in excluded_base_rows and int(margin[index]) <= int(margin_max)
    ]
    if max_rows <= 0 or len(indexes) <= max_rows:
        return np.asarray(indexes, dtype=np.int64)
    buckets: dict[int, list[int]] = defaultdict(list)
    for index in indexes:
        buckets[int(parent[index])].append(index)
    selected: list[int] = []
    cursor = 0
    parents = sorted(buckets)
    while len(selected) < max_rows:
        added = False
        for cls in parents:
            rows = buckets[cls]
            if cursor < len(rows):
                selected.append(rows[cursor])
                added = True
                if len(selected) >= max_rows:
                    break
        if not added:
            break
        cursor += 1
    return np.asarray(selected, dtype=np.int64)


def classify_features(
    *,
    features: np.ndarray,
    parents: np.ndarray,
    payload: dict[str, np.ndarray],
) -> dict[str, np.ndarray]:
    prototypes, prototype_parent = prototypes_int8_from_payload(payload)
    metric_weights = metric_weights_from_payload(payload, features.shape[1])
    pred = np.empty(len(features), dtype=np.int64)
    margin = np.empty(len(features), dtype=np.int64)
    correct_dist = np.empty(len(features), dtype=np.int64)
    nearest_wrong_parent = np.empty(len(features), dtype=np.int64)
    nearest_wrong_dist = np.empty(len(features), dtype=np.int64)
    nearest_pred_proto = np.empty(len(features), dtype=np.int64)
    nearest_second_parent = np.empty(len(features), dtype=np.int64)
    nearest_second_proto = np.empty(len(features), dtype=np.int64)
    for index, feature in enumerate(features):
        cls = classify_one(feature, prototypes, prototype_parent, metric_weights=metric_weights)
        parent = int(parents[index])
        pred[index] = int(cls["pred"])
        margin[index] = int(cls["margin"])
        correct_dist[index] = int(cls[f"class{parent}_dist"])
        order = sorted(range(3), key=lambda item: int(cls[f"class{item}_dist"]))
        nearest_pred_proto[index] = int(cls[f"nearest_parent{int(pred[index])}"])
        nearest_second_parent[index] = int(order[1])
        nearest_second_proto[index] = int(cls[f"nearest_parent{int(order[1])}"])
        wrong_parent = min([0, 1, 2], key=lambda item: int(cls[f"class{item}_dist"]) if item != parent else 10**18)
        nearest_wrong_parent[index] = int(wrong_parent)
        nearest_wrong_dist[index] = int(cls[f"class{wrong_parent}_dist"])
    return {
        "feature": features.astype(np.int8),
        "pred": pred,
        "margin": margin,
        "correct_dist": correct_dist,
        "nearest_wrong_parent": nearest_wrong_parent,
        "nearest_wrong_dist": nearest_wrong_dist,
        "nearest_pred_proto": nearest_pred_proto,
        "nearest_second_parent": nearest_second_parent,
        "nearest_second_proto": nearest_second_proto,
        "prototype_count": np.asarray(len(prototypes), dtype=np.int64),
    }


def arrays_from_stress_rows(rows: list[dict[str, str]], payload: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    feature = np.stack([np.asarray(json.loads(row["feature_json"]), dtype=np.int8) for row in rows], axis=0)
    pred = np.asarray([int(row["stress_pred"]) for row in rows], dtype=np.int64)
    parent = np.asarray([int(row["parent"]) for row in rows], dtype=np.int64)
    nearest_wrong_parent = np.asarray([int(row["nearest_wrong_parent"]) for row in rows], dtype=np.int64)
    nearest_correct_proto = np.asarray([int(row["nearest_correct_proto"]) for row in rows], dtype=np.int64)
    nearest_wrong_proto = np.asarray([int(row["nearest_wrong_proto"]) for row in rows], dtype=np.int64)
    nearest_pred_proto = np.where(pred == parent, nearest_correct_proto, nearest_wrong_proto).astype(np.int64)
    nearest_second_parent = np.where(pred == parent, nearest_wrong_parent, parent).astype(np.int64)
    nearest_second_proto = np.where(pred == parent, nearest_wrong_proto, nearest_correct_proto).astype(np.int64)
    return {
        "feature": feature.astype(np.int8),
        "pred": pred,
        "margin": np.asarray([int(row["stress_margin"]) for row in rows], dtype=np.int64),
        "correct_dist": np.asarray([int(row["correct_dist"]) for row in rows], dtype=np.int64),
        "nearest_wrong_parent": nearest_wrong_parent,
        "nearest_wrong_dist": np.asarray([int(row["nearest_wrong_dist"]) for row in rows], dtype=np.int64),
        "nearest_pred_proto": nearest_pred_proto,
        "nearest_second_parent": nearest_second_parent,
        "nearest_second_proto": nearest_second_proto,
        "prototype_count": np.asarray(len(np.asarray(payload["prototypes_int8"])), dtype=np.int64),
    }


def build_feature_matrix(
    *,
    source_arrays: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    families: np.ndarray,
    family_order: list[str],
    perturbs: np.ndarray,
    perturb_order: list[str],
    mode: str,
) -> np.ndarray:
    tokens = {token for token in mode.split("+") if token}
    parts: list[np.ndarray] = []
    for source in source_order:
        arrays = source_arrays[source]
        dim = int(arrays["feature"].shape[1])
        if "code" in tokens:
            parts.append(arrays["feature"].astype(np.float32) / 128.0)
        if "margin" in tokens:
            values = np.log1p(np.maximum(arrays["margin"].astype(np.float32), 0.0))
            parts.append((values / math.sqrt(float(max(dim, 1))))[:, None])
        if "pred" in tokens:
            parts.append(one_hot(arrays["pred"].astype(np.int64), 3))
        if "dist" in tokens:
            correct = np.log1p(np.maximum(arrays["correct_dist"].astype(np.float32), 0.0))
            wrong = np.log1p(np.maximum(arrays["nearest_wrong_dist"].astype(np.float32), 0.0))
            parts.append((correct / math.sqrt(float(max(dim, 1))))[:, None])
            parts.append((wrong / math.sqrt(float(max(dim, 1))))[:, None])
        if "wrong_parent" in tokens:
            parts.append(one_hot(arrays["nearest_wrong_parent"].astype(np.int64), 3))
        if "second_parent" in tokens:
            parts.append(one_hot(arrays["nearest_second_parent"].astype(np.int64), 3))
        if "bucket" in tokens:
            parts.append(one_hot(margin_bucket_indexes(arrays["margin"].astype(np.int64)), 9))
        if "proto" in tokens:
            parts.append(
                one_hot(
                    arrays["nearest_pred_proto"].astype(np.int64),
                    int(np.asarray(arrays["prototype_count"]).reshape(())),
                )
            )
        if "second_proto" in tokens:
            parts.append(
                one_hot(
                    arrays["nearest_second_proto"].astype(np.int64),
                    int(np.asarray(arrays["prototype_count"]).reshape(())),
                )
            )
    if "family" in tokens:
        family_to_index = {family: index for index, family in enumerate(family_order)}
        rows = np.zeros((len(families), len(family_order)), dtype=np.float32)
        for row_index, family in enumerate(families.astype(str).tolist()):
            index = family_to_index.get(str(family))
            if index is not None:
                rows[row_index, index] = 1.0
        parts.append(rows)
    if "perturb" in tokens:
        perturb_to_index = {perturb: index for index, perturb in enumerate(perturb_order)}
        rows = np.zeros((len(perturbs), len(perturb_order)), dtype=np.float32)
        for row_index, perturb in enumerate(perturbs.astype(str).tolist()):
            index = perturb_to_index.get(str(perturb))
            if index is not None:
                rows[row_index, index] = 1.0
        parts.append(rows)
    if not parts:
        raise ValueError(f"empty feature mode: {mode}")
    return np.concatenate(parts, axis=1).astype(np.float32)


def label_scores(source_arrays: dict[str, dict[str, np.ndarray]], source_order: list[str], parents: np.ndarray) -> np.ndarray:
    columns: list[np.ndarray] = []
    for source in source_order:
        arrays = source_arrays[source]
        score = np.log1p(np.maximum(arrays["margin"].astype(np.float64), 0.0))
        good = arrays["pred"].astype(np.int64) == parents.astype(np.int64)
        columns.append(np.where(good, score, -1.0 - score))
    return np.stack(columns, axis=1)


def split_train_val(
    *,
    row_indexes: np.ndarray,
    sample_indexes: np.ndarray,
    families: np.ndarray,
    perturbs: np.ndarray,
    validation_mod: int,
    holdout_mode: str,
    holdout_families: set[str],
    holdout_perturbs: set[str],
) -> tuple[np.ndarray, np.ndarray]:
    if holdout_mode == "base_mod":
        if validation_mod <= 1:
            return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
        val = (row_indexes.astype(np.int64) % int(validation_mod)) == 0
    elif holdout_mode == "sample_mod":
        if validation_mod <= 1:
            return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
        val = (sample_indexes.astype(np.int64) % int(validation_mod)) == 0
    elif holdout_mode == "family":
        if not holdout_families:
            return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
        val = np.asarray([str(item) in holdout_families for item in families.astype(str).tolist()], dtype=bool)
    elif holdout_mode == "perturb":
        if not holdout_perturbs:
            return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
        val = np.asarray([str(item) in holdout_perturbs for item in perturbs.astype(str).tolist()], dtype=bool)
    elif holdout_mode == "none":
        return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
    else:
        raise ValueError(f"unknown holdout_mode: {holdout_mode}")
    train = ~val
    if not np.any(train) or not np.any(val):
        return np.ones(len(row_indexes), dtype=bool), np.zeros(len(row_indexes), dtype=bool)
    return train, val


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


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train an offline source gate from non-canonical synthetic perturbations and evaluate on frozen V8 high-pressure events."
    )
    parser.add_argument("--source-tflite", action="append", required=True, help="name=parent_int8.tflite")
    parser.add_argument("--source-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--source-stress", action="append", required=True, help="name=highpressure/stress_events.csv")
    parser.add_argument("--base-params", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--runtime-feature-sources",
        default="all",
        help=(
            "Comma-separated source names whose runtime features are fed to the gate, "
            "or 'all'. Labels are still built from all sources."
        ),
    )
    parser.add_argument(
        "--perturbs",
        default="shift_u1,shift_d1,shift_l1,shift_r1,shift_u2,shift_d2,shift_l2,shift_r2,blur5a45,blur7a45,blur5a45_noise0p04,contrast_m0p20,bright_m0p12",
    )
    parser.add_argument("--normal-margin-max", type=int, default=64)
    parser.add_argument("--max-train-base-rows", type=int, default=800)
    parser.add_argument("--feature-modes", default="margin+pred+family,code+margin+pred+family,code+dist+margin+pred+wrong_parent+family")
    parser.add_argument("--hidden-dims", default="0,32")
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument(
        "--holdout-mode",
        choices=["base_mod", "sample_mod", "family", "perturb", "none"],
        default="base_mod",
        help="Synthetic source-label validation split; high-pressure remains evaluation-only.",
    )
    parser.add_argument("--holdout-families", default="", help="Comma-separated families for --holdout-mode family.")
    parser.add_argument("--holdout-perturbs", default="", help="Comma-separated perturb names for --holdout-mode perturb.")
    parser.add_argument(
        "--save-policy-artifacts",
        action="store_true",
        help="Save trained gate model, scaler, and selected source indexes for each evaluated policy.",
    )
    args = parser.parse_args()

    tflite_items = [parse_named_path(item) for item in args.source_tflite]
    param_items = [parse_named_path(item) for item in args.source_params]
    stress_items = [parse_named_path(item) for item in args.source_stress]
    source_order = [name for name, _path in tflite_items]
    if [name for name, _path in param_items] != source_order or [name for name, _path in stress_items] != source_order:
        raise ValueError("source names/order must match across tflite, params, and stress inputs")
    if args.runtime_feature_sources.strip().lower() == "all":
        runtime_source_order = list(source_order)
    else:
        runtime_source_order = parse_list(args.runtime_feature_sources)
        missing = [name for name in runtime_source_order if name not in source_order]
        if missing:
            raise ValueError(f"runtime feature source(s) not present in --source-tflite order: {missing}")
        if not runtime_source_order:
            raise ValueError("--runtime-feature-sources produced an empty source list")

    source_events = {name: read_events(path) for name, path in stress_items}
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_event_keys:
        raise ValueError("source stress files have no common high-pressure events")
    base_rows = [source_events[source_order[0]][key] for key in common_event_keys]
    excluded_base_rows = {int(row["base_query_index"]) for row in base_rows}

    base_payload = load_npz(args.base_params)
    train_base_rows = select_training_rows(
        base_payload=base_payload,
        excluded_base_rows=excluded_base_rows,
        margin_max=int(args.normal_margin_max),
        max_rows=int(args.max_train_base_rows),
    )
    sample_index = np.asarray(base_payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base_payload["view_labels"]).astype(str)
    parents = np.asarray(base_payload["parent"], dtype=np.int64)
    perturb_specs = perturb_by_name(parse_list(args.perturbs))
    selected_view_names = sorted(set(str(view_labels[index]) for index in train_base_rows.tolist()))
    view_cache, _clean_x, _y_parent_base, _paths_base = build_view_cache(args.dataset_dir, selected_view_names)

    image_rows: list[np.ndarray] = []
    train_parent: list[int] = []
    train_base_index: list[int] = []
    train_family: list[str] = []
    train_perturb: list[str] = []
    for query_index in train_base_rows.tolist():
        view = str(view_labels[query_index])
        sample = int(sample_index[query_index])
        base_image = view_cache[view][sample]
        for perturb in perturb_specs:
            rng_seed = int(args.seed) + int(query_index) * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
            rng = np.random.default_rng(rng_seed)
            image_rows.append(apply_perturb(base_image, perturb, rng))
            train_parent.append(int(parents[query_index]))
            train_base_index.append(int(query_index))
            train_family.append(str(perturb.family))
            train_perturb.append(str(perturb.name))
    images = np.stack(image_rows).astype(np.float32)
    train_parent_arr = np.asarray(train_parent, dtype=np.int64)
    train_base_index_arr = np.asarray(train_base_index, dtype=np.int64)
    train_sample_arr = sample_index[train_base_index_arr].astype(np.int64)
    train_family_arr = np.asarray(train_family).astype(str)
    train_perturb_arr = np.asarray(train_perturb).astype(str)

    payloads = {name: load_npz(path) for name, path in param_items}
    train_arrays: dict[str, dict[str, np.ndarray]] = {}
    for name, tflite_path in tflite_items:
        chunks: list[np.ndarray] = []
        for start in range(0, len(images), int(args.batch_size)):
            features, _ops = tflite_raw_int8(tflite_path, images[start : start + int(args.batch_size)])
            chunks.append(features)
        source_features = np.concatenate(chunks, axis=0).astype(np.int8)
        train_arrays[name] = classify_features(
            features=source_features,
            parents=train_parent_arr,
            payload=payloads[name],
        )

    train_scores = label_scores(train_arrays, source_order, train_parent_arr)
    labels = np.argmax(train_scores, axis=1).astype(np.int64)
    label_counts = np.bincount(labels, minlength=len(source_order)).astype(np.float64)

    eval_arrays: dict[str, dict[str, np.ndarray]] = {}
    for name in source_order:
        rows = [source_events[name][key] for key in common_event_keys]
        eval_arrays[name] = arrays_from_stress_rows(rows, payloads[name])
    eval_family = np.asarray([row["perturb_family"] for row in base_rows]).astype(str)
    eval_perturb = np.asarray([row["perturb"] for row in base_rows]).astype(str)
    family_order = sorted(set(train_family_arr.tolist()) | set(eval_family.tolist()))
    perturb_order = sorted(set(train_perturb_arr.tolist()) | set(eval_perturb.tolist()))
    train_mask, val_mask = split_train_val(
        row_indexes=train_base_index_arr,
        sample_indexes=train_sample_arr,
        families=train_family_arr,
        perturbs=train_perturb_arr,
        validation_mod=int(args.validation_mod),
        holdout_mode=str(args.holdout_mode),
        holdout_families={item.strip() for item in args.holdout_families.split(",") if item.strip()},
        holdout_perturbs={item.strip() for item in args.holdout_perturbs.split(",") if item.strip()},
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    for feature_mode in parse_list(args.feature_modes):
        x_train = build_feature_matrix(
            source_arrays=train_arrays,
            source_order=runtime_source_order,
            families=train_family_arr,
            family_order=family_order,
            perturbs=train_perturb_arr,
            perturb_order=perturb_order,
            mode=feature_mode,
        )
        x_eval = build_feature_matrix(
            source_arrays=eval_arrays,
            source_order=runtime_source_order,
            families=eval_family,
            family_order=family_order,
            perturbs=eval_perturb,
            perturb_order=perturb_order,
            mode=feature_mode,
        )
        mean = np.mean(x_train[train_mask], axis=0, keepdims=True)
        std = np.maximum(np.std(x_train[train_mask], axis=0, keepdims=True), 1.0e-6)
        x_train_scaled = (x_train - mean) / std
        x_eval_scaled = (x_eval - mean) / std
        present = label_counts > 0
        class_weight = {
            index: float(np.mean(label_counts[present]) / max(label_counts[index], 1.0))
            for index in range(len(source_order))
        }
        for hidden_dim in parse_ints(args.hidden_dims):
            model = build_model(
                input_dim=x_train_scaled.shape[1],
                output_dim=len(source_order),
                hidden_dim=hidden_dim,
                learning_rate=float(args.learning_rate),
                seed=int(args.seed) + int(hidden_dim) + len(policy_rows),
            )
            callbacks: list[tf.keras.callbacks.Callback] = [
                tf.keras.callbacks.EarlyStopping(
                    monitor="val_loss" if np.any(val_mask) else "loss",
                    patience=10,
                    restore_best_weights=True,
                )
            ]
            history = model.fit(
                x_train_scaled[train_mask],
                labels[train_mask],
                validation_data=(x_train_scaled[val_mask], labels[val_mask]) if np.any(val_mask) else None,
                epochs=int(args.epochs),
                batch_size=int(args.batch_size),
                verbose=0,
                class_weight=class_weight,
                callbacks=callbacks,
            )
            train_pred = np.argmax(model.predict(x_train_scaled[train_mask], batch_size=int(args.batch_size), verbose=0), axis=1)
            train_acc = float(np.mean(train_pred == labels[train_mask]))
            val_acc = None
            if np.any(val_mask):
                val_pred = np.argmax(model.predict(x_train_scaled[val_mask], batch_size=int(args.batch_size), verbose=0), axis=1)
                val_acc = float(np.mean(val_pred == labels[val_mask]))
            selected = np.argmax(model.predict(x_eval_scaled, batch_size=int(args.batch_size), verbose=0), axis=1)
            summary = summarize_selection(
                selected=selected.astype(np.int64),
                base_rows=base_rows,
                common_keys=common_event_keys,
                source_events=source_events,
                source_order=source_order,
            )
            policy_name = f"synthetic_source_gate:{feature_mode}:hidden{hidden_dim}"
            artifact_dir = ""
            if args.save_policy_artifacts:
                artifact_root = args.output_dir / "policy_artifacts" / safe_name(policy_name)
                artifact_root.mkdir(parents=True, exist_ok=True)
                model.save(artifact_root / "gate_model.keras")
                np.savez_compressed(
                    artifact_root / "gate_scaler_and_selection.npz",
                    mean=mean.astype(np.float32),
                    std=std.astype(np.float32),
                    selected_source_index=selected.astype(np.int64),
                    source_order=np.asarray(source_order).astype(str),
                    runtime_source_order=np.asarray(runtime_source_order).astype(str),
                    feature_mode=np.asarray(str(feature_mode)),
                    hidden_dim=np.asarray(int(hidden_dim), dtype=np.int64),
                    family_order=np.asarray(family_order).astype(str),
                    perturb_order=np.asarray(perturb_order).astype(str),
                    train_source_label_acc=np.asarray(train_acc, dtype=np.float32),
                    val_source_label_acc=np.asarray(-1.0 if val_acc is None else val_acc, dtype=np.float32),
                )
                write_json(
                    artifact_root / "policy_summary.json",
                    {
                        "policy": policy_name,
                        "feature_mode": feature_mode,
                        "hidden_dim": int(hidden_dim),
                        "source_order": source_order,
                        "runtime_source_order": runtime_source_order,
                        "family_order": family_order,
                        "perturb_order": perturb_order,
                        "high_pressure_usage": "evaluation_only",
                        "selection_label_usage": "non_highpressure_synthetic_row_winner",
                        "runtime_feature_usage": (
                            "all_source_embeddings_diagnostic_not_deployable"
                            if runtime_source_order == source_order
                            else "restricted_source_embeddings_probe"
                        ),
                        "summary": summary,
                    },
                )
                artifact_dir = str(artifact_root)
            policy_rows.append(
                {
                    "policy": policy_name,
                    "feature_mode": feature_mode,
                    "hidden_dim": int(hidden_dim),
                    "input_dim": int(x_train_scaled.shape[1]),
                    "train_source_label_acc": train_acc,
                    "val_source_label_acc": val_acc,
                    "epochs_ran": int(len(history.history["loss"])),
                    "selection_label_usage": "non_highpressure_synthetic_row_winner",
                    "high_pressure_usage": "evaluation_only",
                    "runtime_feature_usage": (
                        "all_source_embeddings_diagnostic_not_deployable"
                        if runtime_source_order == source_order
                        else "restricted_source_embeddings_probe"
                    ),
                    "runtime_source_order_json": json.dumps(runtime_source_order, ensure_ascii=False),
                    "artifact_dir": artifact_dir,
                    **{key: value for key, value in summary.items() if key != "chosen_counts"},
                    "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
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
        "source_order": source_order,
        "runtime_source_order": runtime_source_order,
        "source_tflite": {name: str(path) for name, path in tflite_items},
        "source_params": {name: str(path) for name, path in param_items},
        "source_stress": {name: str(path) for name, path in stress_items},
        "base_params": str(args.base_params),
        "dataset_dir": str(args.dataset_dir),
        "excluded_highpressure_base_rows": int(len(excluded_base_rows)),
        "train_base_rows": int(len(train_base_rows)),
        "train_synthetic_events": int(len(images)),
        "train_split_events": int(np.sum(train_mask)),
        "validation_split_events": int(np.sum(val_mask)),
        "holdout_mode": str(args.holdout_mode),
        "holdout_families": [item.strip() for item in args.holdout_families.split(",") if item.strip()],
        "holdout_perturbs": [item.strip() for item in args.holdout_perturbs.split(",") if item.strip()],
        "perturbs": [item.name for item in perturb_specs],
        "family_order": family_order,
        "perturb_order": perturb_order,
        "label_counts": {source_order[index]: int(label_counts[index]) for index in range(len(source_order))},
        "high_pressure_usage": "evaluation_only",
        "selection_label_usage": "non_highpressure_synthetic_row_winner",
        "runtime_feature_usage": (
            "all_source_embeddings_diagnostic_not_deployable"
            if runtime_source_order == source_order
            else "restricted_source_embeddings_probe"
        ),
        "common_high_pressure_events": int(len(common_event_keys)),
        "top_policies": policy_rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
