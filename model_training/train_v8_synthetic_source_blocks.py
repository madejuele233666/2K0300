import argparse
import csv
import json
import os
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf

from analyze_v8_synthetic_source_event_gate import (
    EventKey,
    event_key,
    load_npz,
    parse_list,
    parse_named_path,
    read_events,
    select_training_rows,
)
from estimate_v8_board_time import calibrated_conservative_us
from evaluate_v8_embedding_prototypes import metric_summary, write_csv
from stress_test_v8_low_margin import (
    apply_perturb,
    build_view_cache,
    classify_one,
    metric_weights_from_payload,
    parse_csv as parse_perturb_csv,
    perturb_by_name,
    prototypes_int8_from_payload,
    tflite_raw_int8,
)
from train_v8_end_to_end_embedding import build_embedding_model, build_view_dataset, parse_filters
from train_v8_parent_classifier import export_tflite, set_weights_allow_partial_output
from train_v8_synthetic_route_head import (
    DEFAULT_STRESS,
    mask_parent_output_grads,
    parent_weights,
    source_class_weights,
)


STRICT_RECOMMENDED_RAW_TFLITE_OPS = {
    "SPACE_TO_DEPTH",
    "CONV_2D",
    "MAX_POOL_2D",
    "MEAN",
    "FULLY_CONNECTED",
    "DELEGATE",
}


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def softmax(logits: np.ndarray, axis: int = -1) -> np.ndarray:
    shifted = logits - np.max(logits, axis=axis, keepdims=True)
    exp = np.exp(shifted)
    return (exp / np.maximum(np.sum(exp, axis=axis, keepdims=True), 1.0e-12)).astype(np.float32)


def one_hot(values: np.ndarray, count: int) -> np.ndarray:
    out = np.zeros((len(values), count), dtype=np.float32)
    out[np.arange(len(values)), values.astype(np.int64)] = 1.0
    return out


def classify_source_features(
    *,
    features: np.ndarray,
    parents: np.ndarray,
    payload: dict[str, np.ndarray],
) -> dict[str, np.ndarray]:
    prototypes, prototype_parent = prototypes_int8_from_payload(payload)
    metric_weights = metric_weights_from_payload(payload, features.shape[1])
    pred = np.empty(len(features), dtype=np.int64)
    margin = np.empty(len(features), dtype=np.int64)
    class_dist = np.empty((len(features), 3), dtype=np.int64)
    for index, feature in enumerate(features):
        cls = classify_one(feature, prototypes, prototype_parent, metric_weights=metric_weights)
        pred[index] = int(cls["pred"])
        margin[index] = int(cls["margin"])
        for parent in range(3):
            class_dist[index, parent] = int(cls[f"class{parent}_dist"])
    true_dist = class_dist[np.arange(len(features)), parents.astype(np.int64)]
    wrong_dist = np.min(
        np.where(np.arange(3)[None, :] == parents.astype(np.int64)[:, None], np.iinfo(np.int64).max, class_dist),
        axis=1,
    )
    score = np.log1p(np.maximum(margin.astype(np.float64), 0.0))
    score = np.where(pred == parents.astype(np.int64), score, -1.0 - score)
    return {
        "feature": features.astype(np.int8),
        "pred": pred,
        "margin": margin,
        "class_dist": class_dist,
        "true_dist": true_dist.astype(np.int64),
        "wrong_dist": wrong_dist.astype(np.int64),
        "score": score.astype(np.float32),
    }


def source_targets_from_distances(
    *,
    class_dist: np.ndarray,
    pred: np.ndarray,
    mode: str,
    temperature: float,
) -> np.ndarray:
    if mode == "pred":
        return one_hot(pred.astype(np.int64), 3)
    if mode == "dist":
        logits = -np.log1p(np.maximum(class_dist.astype(np.float32), 0.0)) / max(float(temperature), 1.0e-6)
        return softmax(logits, axis=1)
    raise ValueError(f"unknown source block target mode: {mode}")


def build_synthetic_training_set(
    *,
    source_tflite: list[tuple[str, Path]],
    source_params: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    base_params: dict[str, np.ndarray],
    dataset_dir: Path,
    perturbs_text: str,
    normal_margin_max: int,
    max_train_base_rows: int,
    seed: int,
    batch_size: int,
    block_target_mode: str,
    block_target_temperature: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, dict[str, Any]]:
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    excluded_base_rows = {int(source_events[source_order[0]][key]["base_query_index"]) for key in common_event_keys}
    train_base_rows = select_training_rows(
        base_payload=base_params,
        excluded_base_rows=excluded_base_rows,
        margin_max=int(normal_margin_max),
        max_rows=int(max_train_base_rows),
    )
    sample_index = np.asarray(base_params["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base_params["view_labels"]).astype(str)
    parents = np.asarray(base_params["parent"], dtype=np.int64)
    perturb_specs = perturb_by_name(parse_perturb_csv(perturbs_text))
    selected_view_names = sorted({str(view_labels[index]) for index in train_base_rows.tolist()})
    view_cache, _clean_x, _y_parent_base, _paths_base = build_view_cache(dataset_dir, selected_view_names)

    image_rows: list[np.ndarray] = []
    parent_rows: list[int] = []
    base_index_rows: list[int] = []
    perturb_rows: list[str] = []
    for query_index in train_base_rows.tolist():
        view = str(view_labels[query_index])
        sample = int(sample_index[query_index])
        base_image = view_cache[view][sample]
        for perturb in perturb_specs:
            rng_seed = int(seed) + int(query_index) * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
            rng = np.random.default_rng(rng_seed)
            image_rows.append(apply_perturb(base_image, perturb, rng))
            parent_rows.append(int(parents[query_index]))
            base_index_rows.append(int(query_index))
            perturb_rows.append(str(perturb.name))
    images = np.stack(image_rows).astype(np.float32)
    parent = np.asarray(parent_rows, dtype=np.int64)

    score_columns: list[np.ndarray] = []
    target_blocks: list[np.ndarray] = []
    pred_columns: list[np.ndarray] = []
    margin_columns: list[np.ndarray] = []
    for name, tflite_path in source_tflite:
        chunks: list[np.ndarray] = []
        for start in range(0, len(images), int(batch_size)):
            features, _ops = tflite_raw_int8(tflite_path, images[start : start + int(batch_size)])
            chunks.append(features)
        features_all = np.concatenate(chunks, axis=0).astype(np.int8)
        arrays = classify_source_features(features=features_all, parents=parent, payload=source_params[name])
        score_columns.append(arrays["score"].astype(np.float32))
        pred_columns.append(arrays["pred"].astype(np.int64))
        margin_columns.append(arrays["margin"].astype(np.int64))
        target_blocks.append(
            source_targets_from_distances(
                class_dist=arrays["class_dist"],
                pred=arrays["pred"],
                mode=block_target_mode,
                temperature=float(block_target_temperature),
            )
        )
    scores = np.stack(score_columns, axis=1).astype(np.float32)
    source_label = np.argmax(scores, axis=1).astype(np.int64)
    block_targets = np.stack(target_blocks, axis=1).astype(np.float32)
    source_pred = np.stack(pred_columns, axis=1).astype(np.int64)
    source_margin = np.stack(margin_columns, axis=1).astype(np.int64)
    meta = {
        "excluded_highpressure_base_rows": int(len(excluded_base_rows)),
        "train_base_rows": int(len(train_base_rows)),
        "train_synthetic_events": int(len(images)),
        "perturbs": [item.name for item in perturb_specs],
        "source_label_counts": {
            source_order[index]: int(np.sum(source_label == index))
            for index in range(len(source_order))
        },
        "source_pred_correct_counts": {
            source_order[index]: int(np.sum(source_pred[:, index] == parent))
            for index in range(len(source_order))
        },
        "source_margin_median": {
            source_order[index]: float(np.median(source_margin[:, index]))
            for index in range(len(source_order))
        },
        "block_target_mode": block_target_mode,
        "block_target_temperature": float(block_target_temperature),
        "train_base_row_indexes_preview": [int(item) for item in train_base_rows[:20].tolist()],
        "base_index_preview": [int(item) for item in base_index_rows[:20]],
        "perturb_preview": perturb_rows[:20],
    }
    return images, parent, source_label, block_targets, meta


def build_highpressure_images(
    *,
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    dataset_dir: Path,
    seed: int,
) -> tuple[list[EventKey], list[dict[str, str]], np.ndarray, np.ndarray, list[str]]:
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    rows = [source_events[source_order[0]][key] for key in common_event_keys]
    view_names = sorted({str(row["view_label"]) for row in rows})
    view_cache, _clean_x, _y_parent_base, _paths_base = build_view_cache(dataset_dir, view_names)
    images: list[np.ndarray] = []
    for row in rows:
        sample = int(row["sample_index"])
        view = str(row["view_label"])
        perturb = perturb_by_name([str(row["perturb"])])[0]
        query_index = int(row["base_query_index"])
        rng_seed = int(seed) + query_index * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
        rng = np.random.default_rng(rng_seed)
        images.append(apply_perturb(view_cache[view][sample], perturb, rng))
    parent = np.asarray([int(row["parent"]) for row in rows], dtype=np.int64)
    groups = [str(row["group"]) for row in rows]
    return common_event_keys, rows, np.stack(images).astype(np.float32), parent, groups


def source_oracle_selection(
    *,
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    common_keys: list[EventKey],
    parent: np.ndarray,
) -> np.ndarray:
    selected: list[int] = []
    for row_index, key in enumerate(common_keys):
        scores: list[float] = []
        target = int(parent[row_index])
        for source in source_order:
            row = source_events[source][key]
            pred = int(row["stress_pred"])
            margin = int(row["stress_margin"])
            value = float(np.log1p(max(margin, 0)))
            scores.append(value if pred == target else -1.0 - value)
        selected.append(int(np.argmax(np.asarray(scores, dtype=np.float32))))
    return np.asarray(selected, dtype=np.int64)


def summarize_parent_predictions(pred: np.ndarray, parent: np.ndarray, groups: list[str]) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for got, target, group in zip(pred.tolist(), parent.tolist(), groups, strict=False):
        grouped[str(group)][0] += int(int(got) != int(target))
        grouped[str(group)][1] += 1
    wrong_events = int(sum(value[0] for value in grouped.values()))
    total_events = int(sum(value[1] for value in grouped.values()))
    return {
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
        "per_group": {
            group: {
                "wrong": int(values[0]),
                "total": int(values[1]),
                "wrong_rate": float(values[0] / max(values[1], 1)),
            }
            for group, values in sorted(grouped.items())
        },
    }


def summarize_external_source_selection(
    *,
    selected: np.ndarray,
    rows: list[dict[str, str]],
    common_keys: list[EventKey],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    for row_index, base_row in enumerate(rows):
        source_index = int(selected[row_index])
        source = source_order[source_index]
        source_row = source_events[source][common_keys[row_index]]
        wrong = int(int(source_row["stress_pred"]) != int(base_row["parent"]))
        grouped[str(base_row["group"])][0] += wrong
        grouped[str(base_row["group"])][1] += 1
        chosen[source] += 1
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


def block_policy_predictions(block_logits: np.ndarray) -> dict[str, tuple[np.ndarray, np.ndarray | None]]:
    # block_logits: N x source_count x 3
    block_pred = np.argmax(block_logits, axis=2).astype(np.int64)
    block_top = np.max(block_logits, axis=2)
    sorted_logits = np.sort(block_logits, axis=2)
    block_margin = sorted_logits[:, :, -1] - sorted_logits[:, :, -2]
    selected_top = np.argmax(block_top, axis=1).astype(np.int64)
    selected_margin = np.argmax(block_margin, axis=1).astype(np.int64)
    pred_top = block_pred[np.arange(len(block_logits)), selected_top]
    pred_margin = block_pred[np.arange(len(block_logits)), selected_margin]
    pred_sum = np.argmax(np.sum(block_logits, axis=1), axis=1).astype(np.int64)
    pred_mean = np.argmax(np.mean(block_logits, axis=1), axis=1).astype(np.int64)

    vote_pred: list[int] = []
    for row_pred, row_margin in zip(block_pred, block_margin, strict=False):
        counts = np.bincount(row_pred.astype(np.int64), minlength=3)
        winners = np.flatnonzero(counts == np.max(counts))
        if len(winners) == 1:
            vote_pred.append(int(winners[0]))
        else:
            margin_sum = [float(np.sum(row_margin[row_pred == cls])) for cls in winners.tolist()]
            vote_pred.append(int(winners[int(np.argmax(margin_sum))]))
    return {
        "block_top_logit_max": (pred_top.astype(np.int64), selected_top),
        "block_margin_max": (pred_margin.astype(np.int64), selected_margin),
        "block_sum_logits": (pred_sum, None),
        "block_mean_logits": (pred_mean, None),
        "block_vote_margin_tie": (np.asarray(vote_pred, dtype=np.int64), None),
    }


def source_block_slice(code: np.ndarray, block_start: int, source_count: int) -> np.ndarray:
    end = int(block_start) + int(source_count) * 3
    block = code[:, int(block_start) : end]
    if block.shape[1] != int(source_count) * 3:
        raise ValueError(f"source block slice has {block.shape[1]} dims, expected {int(source_count) * 3}")
    return block.reshape((len(code), int(source_count), 3))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train a single strict-op V8 encoder with per-source parent-logit blocks from non-high-pressure synthetic events."
    )
    parser.add_argument("--source-tflite", action="append", required=True, help="name=parent_int8.tflite")
    parser.add_argument("--source-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--source-stress", action="append", required=True, help="name=highpressure/stress_events.csv")
    parser.add_argument("--base-params", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--init-model", type=Path, default=None)
    parser.add_argument("--filters", default="2,6,12")
    parser.add_argument("--parent-prefix-dim", type=int, default=4)
    parser.add_argument("--source-count", type=int, default=5)
    parser.add_argument("--first-kernel", type=int, default=3)
    parser.add_argument("--backbone-architecture", default="spacetodepth_conv")
    parser.add_argument("--activation", default="relu6")
    parser.add_argument("--pool", default="max")
    parser.add_argument("--extra-conv", action="store_true")
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--perturbs", default="all")
    parser.add_argument("--normal-margin-max", type=int, default=128)
    parser.add_argument("--max-train-base-rows", type=int, default=800)
    parser.add_argument("--block-target-mode", choices=["dist", "pred"], default="dist")
    parser.add_argument("--block-target-temperature", type=float, default=0.18)
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--learning-rate", type=float, default=0.0007)
    parser.add_argument("--l2", type=float, default=1.0e-4)
    parser.add_argument("--block-distill-weight", type=float, default=0.4)
    parser.add_argument("--selected-true-weight", type=float, default=0.2)
    parser.add_argument("--source-choice-top-weight", type=float, default=0.0)
    parser.add_argument("--source-choice-top-target", type=float, default=2.0)
    parser.add_argument("--source-choice-top-alpha", type=float, default=1.0)
    parser.add_argument("--synthetic-parent-weight", type=float, default=0.15)
    parser.add_argument("--parent-logit-anchor-weight", type=float, default=0.05)
    parser.add_argument("--freeze-backbone", action="store_true")
    parser.add_argument("--freeze-parent-output-dims", action="store_true")
    parser.add_argument("--freeze-prefix-dims", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--stress-seed", type=int, default=20260520)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument("--log-every", type=int, default=10)
    args = parser.parse_args()

    if args.parent_prefix_dim < 3:
        raise ValueError("--parent-prefix-dim must be >= 3")
    if args.freeze_prefix_dims < 0 or args.freeze_prefix_dims > args.parent_prefix_dim:
        raise ValueError("--freeze-prefix-dims must be in [0, parent_prefix_dim]")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    tf.keras.utils.set_random_seed(args.seed)

    tflite_items = [parse_named_path(item) for item in args.source_tflite]
    param_items = [parse_named_path(item) for item in args.source_params]
    stress_items = [parse_named_path(item) for item in args.source_stress]
    source_order = [name for name, _path in tflite_items]
    if [name for name, _path in param_items] != source_order or [name for name, _path in stress_items] != source_order:
        raise ValueError("source names/order must match across tflite, params, and stress inputs")
    if len(source_order) != int(args.source_count):
        raise ValueError(f"--source-count {args.source_count} does not match source count {len(source_order)}")
    code_dim = int(args.parent_prefix_dim) + len(source_order) * 3
    block_start = int(args.parent_prefix_dim)

    source_events = {name: read_events(path) for name, path in stress_items}
    source_params = {name: load_npz(path) for name, path in param_items}
    base_params = load_npz(args.base_params)

    normal_flat, normal_images = build_view_dataset(args.dataset_dir, parse_list(args.stress))
    normal_parent = np.asarray(normal_flat["y_parent"], dtype=np.int64)
    synthetic_images, synthetic_parent, source_label, block_targets, synthetic_meta = build_synthetic_training_set(
        source_tflite=tflite_items,
        source_params=source_params,
        source_order=source_order,
        source_events=source_events,
        base_params=base_params,
        dataset_dir=args.dataset_dir,
        perturbs_text=args.perturbs,
        normal_margin_max=args.normal_margin_max,
        max_train_base_rows=args.max_train_base_rows,
        seed=args.seed,
        batch_size=args.batch_size,
        block_target_mode=args.block_target_mode,
        block_target_temperature=args.block_target_temperature,
    )

    images = np.concatenate([normal_images, synthetic_images], axis=0).astype(np.float32)
    y_parent = np.concatenate([normal_parent, synthetic_parent], axis=0).astype(np.int64)
    parent_weight = np.concatenate(
        [
            parent_weights(normal_parent),
            np.full(len(synthetic_images), float(args.synthetic_parent_weight), dtype=np.float32),
        ],
        axis=0,
    ).astype(np.float32)
    block_weight = np.concatenate(
        [np.zeros(len(normal_images), dtype=np.float32), np.ones(len(synthetic_images), dtype=np.float32)],
        axis=0,
    ).astype(np.float32)
    source_class_weight = source_class_weights(source_label, np.ones(len(source_label), dtype=np.float32), len(source_order))
    selected_weight = np.concatenate(
        [
            np.zeros(len(normal_images), dtype=np.float32),
            source_class_weight[source_label].astype(np.float32),
        ],
        axis=0,
    ).astype(np.float32)
    block_target_all = np.zeros((len(images), len(source_order), 3), dtype=np.float32)
    block_target_all[len(normal_images) :] = block_targets
    source_label_all = np.concatenate(
        [np.zeros(len(normal_images), dtype=np.int64), source_label.astype(np.int64)],
        axis=0,
    )

    val_mask = np.zeros(len(images), dtype=bool)
    synthetic_start = len(normal_images)
    if args.validation_mod > 1:
        synthetic_rows = np.arange(len(synthetic_images), dtype=np.int64)
        val_mask[synthetic_start:] = (synthetic_rows % int(args.validation_mod)) == 0
    train_mask = ~val_mask
    if not np.any(train_mask):
        train_mask[:] = True
        val_mask[:] = False

    model = build_embedding_model(
        filters=parse_filters(args.filters),
        embedding_dim=code_dim,
        learning_rate=float(args.learning_rate),
        l2=float(args.l2),
        dropout=0.0,
        first_kernel=int(args.first_kernel),
        embedding_output_mode="raw",
        backbone_architecture=args.backbone_architecture,
        activation=args.activation,
        pool=args.pool,
        extra_conv=bool(args.extra_conv),
    )
    init_parent_logits = np.zeros((len(images), 3), dtype=np.float32)
    if args.init_model is not None:
        init_model = tf.keras.models.load_model(args.init_model, safe_mode=False)
        set_weights_allow_partial_output(model, init_model)
        init_parent_logits = model.predict(images, batch_size=args.batch_size, verbose=0)[:, :3].astype(np.float32)
    if args.freeze_backbone:
        for layer in model.layers:
            if layer.name != "embedding_dense":
                layer.trainable = False
    embedding_layer = model.get_layer("embedding_dense")
    trainable_names = [str(getattr(var, "path", getattr(var, "name", var))) for var in model.trainable_variables]

    optimizer = tf.keras.optimizers.Adam(float(args.learning_rate))
    parent_ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True, reduction="none")
    block_ce = tf.keras.losses.CategoricalCrossentropy(from_logits=True, reduction="none")
    selected_ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True, reduction="none")
    x_tf = tf.constant(images, dtype=tf.float32)
    y_parent_tf = tf.constant(y_parent, dtype=tf.int32)
    parent_weight_tf = tf.constant(parent_weight, dtype=tf.float32)
    block_weight_tf = tf.constant(block_weight, dtype=tf.float32)
    selected_weight_tf = tf.constant(selected_weight, dtype=tf.float32)
    block_target_tf = tf.constant(block_target_all, dtype=tf.float32)
    source_label_tf = tf.constant(source_label_all, dtype=tf.int32)
    init_parent_tf = tf.constant(init_parent_logits, dtype=tf.float32)
    train_indexes = np.where(train_mask)[0].astype(np.int64)

    train_rows: list[dict[str, Any]] = []
    rng = np.random.default_rng(args.seed)
    for epoch in range(1, int(args.epochs) + 1):
        rng.shuffle(train_indexes)
        epoch_loss: list[float] = []
        epoch_block_loss: list[float] = []
        for start in range(0, len(train_indexes), int(args.batch_size)):
            batch = train_indexes[start : start + int(args.batch_size)]
            batch_tf = tf.constant(batch, dtype=tf.int32)
            with tf.GradientTape() as tape:
                code = model(tf.gather(x_tf, batch_tf), training=True)
                parent_logits = code[:, :3]
                block_logits = tf.reshape(
                    code[:, block_start : block_start + len(source_order) * 3],
                    (-1, len(source_order), 3),
                )
                p_loss_raw = parent_ce(tf.gather(y_parent_tf, batch_tf), parent_logits)
                p_weight = tf.gather(parent_weight_tf, batch_tf)
                p_loss = tf.reduce_sum(p_loss_raw * p_weight) / tf.maximum(tf.reduce_sum(p_weight), 1.0)

                b_loss_raw = block_ce(tf.gather(block_target_tf, batch_tf), block_logits)
                b_row_loss = tf.reduce_mean(b_loss_raw, axis=1)
                b_weight = tf.gather(block_weight_tf, batch_tf)
                b_loss = tf.reduce_sum(b_row_loss * b_weight) / tf.maximum(tf.reduce_sum(b_weight), 1.0)

                selected_logits = tf.gather(
                    block_logits,
                    tf.gather(source_label_tf, batch_tf),
                    batch_dims=1,
                )
                s_loss_raw = selected_ce(tf.gather(y_parent_tf, batch_tf), selected_logits)
                s_weight = tf.gather(selected_weight_tf, batch_tf)
                s_loss = tf.reduce_sum(s_loss_raw * s_weight) / tf.maximum(tf.reduce_sum(s_weight), 1.0)

                block_top_values = tf.reduce_max(block_logits, axis=2)
                selected_top_values = tf.gather(
                    block_top_values,
                    tf.gather(source_label_tf, batch_tf),
                    batch_dims=1,
                )
                other_top_values = tf.reduce_max(
                    block_top_values
                    + tf.one_hot(
                        tf.gather(source_label_tf, batch_tf),
                        len(source_order),
                        on_value=tf.constant(-1.0e9, dtype=tf.float32),
                        off_value=tf.constant(0.0, dtype=tf.float32),
                    ),
                    axis=1,
                )
                choice_margin = selected_top_values - other_top_values
                choice_loss_raw = tf.nn.softplus(
                    float(args.source_choice_top_alpha) * (float(args.source_choice_top_target) - choice_margin)
                )
                choice_loss = tf.reduce_sum(choice_loss_raw * s_weight) / tf.maximum(tf.reduce_sum(s_weight), 1.0)

                anchor = tf.reduce_mean(tf.square(parent_logits - tf.gather(init_parent_tf, batch_tf)))
                loss = (
                    p_loss
                    + float(args.block_distill_weight) * b_loss
                    + float(args.selected_true_weight) * s_loss
                    + float(args.source_choice_top_weight) * choice_loss
                    + float(args.parent_logit_anchor_weight) * anchor
                )
            grads = tape.gradient(loss, model.trainable_variables)
            if args.freeze_parent_output_dims:
                grads = mask_parent_output_grads(
                    grads=grads,
                    variables=model.trainable_variables,
                    embedding_layer=embedding_layer,
                    parent_dim=int(args.freeze_prefix_dims),
                )
            optimizer.apply_gradients(zip(grads, model.trainable_variables))
            epoch_loss.append(float(loss.numpy()))
            epoch_block_loss.append(float(b_loss.numpy()))
        if epoch == 1 or epoch == int(args.epochs) or epoch % int(args.log_every) == 0:
            code_all = model.predict(images, batch_size=args.batch_size, verbose=0)
            normal_code = code_all[: len(normal_images)]
            normal_pred = np.argmax(normal_code[:, :3], axis=1).astype(np.int64)
            synth_code = code_all[len(normal_images) :]
            synth_blocks = source_block_slice(synth_code, block_start, len(source_order))
            selected_blocks = synth_blocks[np.arange(len(synth_blocks)), source_label]
            selected_pred = np.argmax(selected_blocks, axis=1).astype(np.int64)
            block_policy = block_policy_predictions(synth_blocks)
            val_source_policy_acc = None
            val_top_policy_acc = None
            if np.any(val_mask[synthetic_start:]):
                val_local = val_mask[synthetic_start:]
                val_source_policy_acc = float(
                    np.mean(block_policy["block_margin_max"][1][val_local] == source_label[val_local])
                )
                val_top_policy_acc = float(
                    np.mean(block_policy["block_top_logit_max"][1][val_local] == source_label[val_local])
                )
            row = {
                "epoch": int(epoch),
                "loss_mean": float(np.mean(epoch_loss)),
                "block_loss_mean": float(np.mean(epoch_block_loss)),
                "normal_parent_accuracy": float(np.mean(normal_pred == normal_parent)),
                "synthetic_selected_block_parent_accuracy": float(np.mean(selected_pred == synthetic_parent)),
                "synthetic_margin_policy_source_accuracy": float(np.mean(block_policy["block_margin_max"][1] == source_label)),
                "synthetic_margin_policy_val_source_accuracy": val_source_policy_acc,
                "synthetic_top_policy_source_accuracy": float(np.mean(block_policy["block_top_logit_max"][1] == source_label)),
                "synthetic_top_policy_val_source_accuracy": val_top_policy_acc,
            }
            train_rows.append(row)
            print(json.dumps({"source_block_train": row}, ensure_ascii=False), flush=True)
    write_csv(args.output_dir / "training_log.csv", train_rows)
    model.save(args.output_dir / "source_block_model.keras")

    normal_code = model.predict(normal_images, batch_size=args.batch_size, verbose=0)
    normal_pred = np.argmax(normal_code[:, :3], axis=1).astype(np.int64)
    normal_row: dict[str, Any] = {
        "stage": "v8_synthetic_source_blocks",
        "name": args.output_dir.name,
        "code_dim": int(code_dim),
        "source_count": int(len(source_order)),
        "parent_prefix_dim": int(args.parent_prefix_dim),
    }
    normal_row.update(
        metric_summary(
            view_order=list(normal_flat["view_names"]),
            view_labels=np.asarray(normal_flat["view_labels"]).astype(str),
            y_parent=normal_parent,
            pred=normal_pred,
        )
    )
    write_csv(args.output_dir / "candidate_results.csv", [normal_row])

    common_keys, high_rows, high_images, high_parent, high_groups = build_highpressure_images(
        source_events=source_events,
        source_order=source_order,
        dataset_dir=args.dataset_dir,
        seed=args.stress_seed,
    )
    high_code = model.predict(high_images, batch_size=args.batch_size, verbose=0)
    high_blocks = source_block_slice(high_code, block_start, len(source_order))
    policy_predictions = block_policy_predictions(high_blocks)
    policy_rows: list[dict[str, Any]] = []
    high_summaries: dict[str, Any] = {}
    for policy, (pred, selected) in policy_predictions.items():
        summary = summarize_parent_predictions(pred=pred, parent=high_parent, groups=high_groups)
        high_summaries[policy] = {
            **summary,
            "chosen_counts": (
                {source_order[index]: int(np.sum(selected == index)) for index in range(len(source_order))}
                if selected is not None
                else {}
            ),
        }
        policy_rows.append(
            {
                "policy": policy,
                "feature_source": "float_model",
                "wrong_events": summary["wrong_events"],
                "total_events": summary["total_events"],
                "wrong_rate": summary["wrong_rate"],
                "low_wrong_rate": summary["low_wrong_rate"],
                "control_wrong_rate": summary["control_wrong_rate"],
                "chosen_counts_json": json.dumps(high_summaries[policy]["chosen_counts"], ensure_ascii=False),
            }
        )
    oracle_source = source_oracle_selection(
        source_events=source_events,
        source_order=source_order,
        common_keys=common_keys,
        parent=high_parent,
    )
    oracle_block_pred = np.argmax(high_blocks[np.arange(len(high_blocks)), oracle_source], axis=1).astype(np.int64)
    oracle_block_summary = summarize_parent_predictions(pred=oracle_block_pred, parent=high_parent, groups=high_groups)
    external_oracle_summary = summarize_external_source_selection(
        selected=oracle_source,
        rows=high_rows,
        common_keys=common_keys,
        source_events=source_events,
        source_order=source_order,
    )
    high_summaries["external_source_oracle"] = external_oracle_summary
    high_summaries["oracle_source_block_pred"] = oracle_block_summary
    for name, summary in [
        ("oracle_source_block_pred", oracle_block_summary),
        ("external_source_oracle", external_oracle_summary),
    ]:
        policy_rows.append(
            {
                "policy": name,
                "feature_source": "float_model" if name == "oracle_source_block_pred" else "external_source_events",
                "wrong_events": summary["wrong_events"],
                "total_events": summary["total_events"],
                "wrong_rate": summary["wrong_rate"],
                "low_wrong_rate": summary["low_wrong_rate"],
                "control_wrong_rate": summary["control_wrong_rate"],
                "chosen_counts_json": json.dumps(external_oracle_summary.get("chosen_counts", {}), ensure_ascii=False),
            }
        )

    export_info = export_tflite(
        model,
        args.output_dir,
        normal_images[np.linspace(0, len(normal_images) - 1, min(512, len(normal_images))).astype(np.int64)],
    )
    int8_normal_code, int8_ops = tflite_raw_int8(Path(export_info["int8_tflite"]), normal_images)
    int8_normal_pred = np.argmax(int8_normal_code[:, :3], axis=1).astype(np.int64)
    tflite_row: dict[str, Any] = {
        **export_info,
        "int8_unique_ops": int8_ops,
        "strict_recommended_ops": bool(set(int8_ops).issubset(STRICT_RECOMMENDED_RAW_TFLITE_OPS)),
        "board_backbone_conservative_us": int(
            round(
                calibrated_conservative_us(
                    {
                        "filters": list(parse_filters(args.filters)),
                        "backbone_architecture": args.backbone_architecture,
                        "first_kernel": int(args.first_kernel),
                        "extra_conv": bool(args.extra_conv),
                    }
                )
            )
        ),
    }
    tflite_row.update(
        metric_summary(
            view_order=list(normal_flat["view_names"]),
            view_labels=np.asarray(normal_flat["view_labels"]).astype(str),
            y_parent=normal_parent,
            pred=int8_normal_pred,
            prefix="tflite_int8_",
        )
    )
    int8_high_code, high_ops = tflite_raw_int8(Path(export_info["int8_tflite"]), high_images)
    int8_high_blocks = source_block_slice(int8_high_code.astype(np.float32), block_start, len(source_order))
    int8_policy_predictions = block_policy_predictions(int8_high_blocks)
    int8_high_summaries: dict[str, Any] = {}
    for policy, (pred, selected) in int8_policy_predictions.items():
        summary = summarize_parent_predictions(pred=pred, parent=high_parent, groups=high_groups)
        int8_high_summaries[policy] = {
            **summary,
            "chosen_counts": (
                {source_order[index]: int(np.sum(selected == index)) for index in range(len(source_order))}
                if selected is not None
                else {}
            ),
        }
        policy_rows.append(
            {
                "policy": policy,
                "feature_source": "int8_tflite",
                "wrong_events": summary["wrong_events"],
                "total_events": summary["total_events"],
                "wrong_rate": summary["wrong_rate"],
                "low_wrong_rate": summary["low_wrong_rate"],
                "control_wrong_rate": summary["control_wrong_rate"],
                "chosen_counts_json": json.dumps(int8_high_summaries[policy]["chosen_counts"], ensure_ascii=False),
            }
        )
    int8_oracle_block_pred = np.argmax(int8_high_blocks[np.arange(len(int8_high_blocks)), oracle_source], axis=1).astype(np.int64)
    int8_oracle_block_summary = summarize_parent_predictions(pred=int8_oracle_block_pred, parent=high_parent, groups=high_groups)
    int8_high_summaries["oracle_source_block_pred"] = int8_oracle_block_summary
    policy_rows.append(
        {
            "policy": "oracle_source_block_pred",
            "feature_source": "int8_tflite",
            "wrong_events": int8_oracle_block_summary["wrong_events"],
            "total_events": int8_oracle_block_summary["total_events"],
            "wrong_rate": int8_oracle_block_summary["wrong_rate"],
            "low_wrong_rate": int8_oracle_block_summary["low_wrong_rate"],
            "control_wrong_rate": int8_oracle_block_summary["control_wrong_rate"],
            "chosen_counts_json": json.dumps(external_oracle_summary.get("chosen_counts", {}), ensure_ascii=False),
        }
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_json(args.output_dir / "tflite_summary.json", tflite_row)

    summary = {
        "source_order": source_order,
        "source_tflite": {name: str(path) for name, path in tflite_items},
        "source_params": {name: str(path) for name, path in param_items},
        "source_stress": {name: str(path) for name, path in stress_items},
        "base_params": str(args.base_params),
        "init_model": str(args.init_model) if args.init_model else "",
        "high_pressure_usage": "evaluation_only",
        "synthetic_training_usage": "non_highpressure_synthetic_source_parent_blocks",
        "runtime_feature_usage": "single_backbone_source_parent_blocks_probe",
        "train_control": {
            "freeze_backbone": bool(args.freeze_backbone),
            "freeze_parent_output_dims": bool(args.freeze_parent_output_dims),
            "freeze_prefix_dims": int(args.freeze_prefix_dims),
            "trainable_variables": trainable_names,
        },
        "synthetic": synthetic_meta,
        "normal": normal_row,
        "high_pressure_float": high_summaries,
        "high_pressure_int8": int8_high_summaries,
        "tflite": tflite_row,
        "tflite_high_ops": high_ops,
        "config": {key: (str(value) if isinstance(value, Path) else value) for key, value in vars(args).items()},
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
