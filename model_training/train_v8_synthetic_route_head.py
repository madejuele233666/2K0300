import argparse
import csv
import json
import os
from collections import defaultdict
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf

from analyze_v8_synthetic_source_event_gate import (
    classify_features,
    event_key,
    label_scores,
    load_npz,
    parse_list,
    parse_named_path,
    read_events,
    select_training_rows,
    summarize_selection,
)
from evaluate_v8_embedding_prototypes import metric_summary, write_csv
from stress_test_v8_low_margin import apply_perturb, build_view_cache, perturb_by_name, tflite_raw_int8
from train_v8_end_to_end_embedding import build_embedding_model, build_view_dataset, parse_filters
from train_v8_parent_classifier import export_tflite, predict_tflite, set_weights_allow_partial_output


DEFAULT_STRESS = (
    "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,"
    "cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,"
    "cam_blur3a0_noise0p02,cam_blur5a45_noise0p04"
)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def one_hot(values: np.ndarray, count: int) -> np.ndarray:
    out = np.zeros((len(values), count), dtype=np.float32)
    out[np.arange(len(values)), values.astype(np.int64)] = 1.0
    return out


def source_class_weights(labels: np.ndarray, weights: np.ndarray, source_count: int) -> np.ndarray:
    active = weights > 0.0
    counts = np.bincount(labels[active].astype(np.int64), minlength=source_count).astype(np.float32)
    present = counts > 0.0
    out = np.ones(source_count, dtype=np.float32)
    if np.any(present):
        out[present] = float(np.mean(counts[present])) / np.maximum(counts[present], 1.0)
    return out.astype(np.float32)


def parent_weights(labels: np.ndarray) -> np.ndarray:
    counts = np.bincount(labels.astype(np.int64), minlength=3).astype(np.float32)
    present = counts > 0.0
    out = np.ones(3, dtype=np.float32)
    if np.any(present):
        out[present] = float(np.mean(counts[present])) / np.maximum(counts[present], 1.0)
    return out[labels.astype(np.int64)].astype(np.float32)


def same_tf_variable(left: Any, right: Any) -> bool:
    if left is right:
        return True
    left_path = getattr(left, "path", None)
    right_path = getattr(right, "path", None)
    if left_path is not None and right_path is not None and left_path == right_path:
        return True
    return getattr(left, "name", None) == getattr(right, "name", None) and tuple(left.shape) == tuple(right.shape)


def mask_parent_output_grads(
    *,
    grads: list[tf.Tensor | None],
    variables: list[tf.Variable],
    embedding_layer: tf.keras.layers.Layer,
    parent_dim: int,
) -> list[tf.Tensor | None]:
    kernel = getattr(embedding_layer, "kernel", None)
    bias = getattr(embedding_layer, "bias", None)
    masked: list[tf.Tensor | None] = []
    for grad, var in zip(grads, variables, strict=False):
        if grad is None:
            masked.append(None)
            continue
        if kernel is not None and same_tf_variable(var, kernel):
            mask_np = np.ones(tuple(var.shape), dtype=np.float32)
            mask_np[:, :parent_dim] = 0.0
            masked.append(grad * tf.constant(mask_np, dtype=grad.dtype))
            continue
        if bias is not None and same_tf_variable(var, bias):
            mask_np = np.ones(tuple(var.shape), dtype=np.float32)
            mask_np[:parent_dim] = 0.0
            masked.append(grad * tf.constant(mask_np, dtype=grad.dtype))
            continue
        masked.append(grad)
    return masked


def build_route_probe_model(
    *,
    filters: tuple[int, int, int],
    code_dim: int,
    source_count: int,
    route_start_dim: int,
    learning_rate: float,
    l2: float,
    first_kernel: int,
    backbone_architecture: str,
    activation: str,
    pool: str,
    extra_conv: bool,
    separate_route_head: bool,
    route_hidden_dim: int,
) -> tf.keras.Model:
    if not separate_route_head:
        return build_embedding_model(
            filters=filters,
            embedding_dim=code_dim,
            learning_rate=learning_rate,
            l2=l2,
            dropout=0.0,
            first_kernel=first_kernel,
            embedding_output_mode="raw",
            backbone_architecture=backbone_architecture,
            activation=activation,
            pool=pool,
            extra_conv=extra_conv,
        )
    if code_dim != route_start_dim + source_count:
        raise ValueError("--separate-route-head requires code_dim == route_start_dim + source_count")
    regularizer = tf.keras.regularizers.l2(l2) if l2 > 0 else None
    parent_model = build_embedding_model(
        filters=filters,
        embedding_dim=route_start_dim,
        learning_rate=learning_rate,
        l2=l2,
        dropout=0.0,
        first_kernel=first_kernel,
        embedding_output_mode="raw",
        backbone_architecture=backbone_architecture,
        activation=activation,
        pool=pool,
        extra_conv=extra_conv,
    )
    gap = parent_model.get_layer("gap").output
    route_x: tf.Tensor = gap
    if route_hidden_dim > 0:
        route_x = tf.keras.layers.Dense(route_hidden_dim, kernel_regularizer=regularizer, name="route_hidden_dense")(
            route_x
        )
        if activation == "relu6":
            route_x = tf.keras.layers.ReLU(max_value=6.0, name="route_hidden_relu6")(route_x)
        else:
            route_x = tf.keras.layers.Activation(activation, name="route_hidden_activation")(route_x)
    route_logits = tf.keras.layers.Dense(source_count, kernel_regularizer=regularizer, name="route_dense")(route_x)
    outputs = tf.keras.layers.Concatenate(name="embedding_with_route")([parent_model.output, route_logits])
    model = tf.keras.Model(parent_model.input, outputs, name="v8_pure_embedding_route_probe")
    model.optimizer = tf.keras.optimizers.Adam(learning_rate=learning_rate)  # type: ignore[attr-defined]
    return model


def build_synthetic_training_set(
    *,
    source_tflite: list[tuple[str, Path]],
    source_params: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    source_events: dict[str, dict[tuple[str, int, str, int], dict[str, str]]],
    base_params: dict[str, np.ndarray],
    dataset_dir: Path,
    perturbs_text: str,
    normal_margin_max: int,
    max_train_base_rows: int,
    seed: int,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict[str, Any]]:
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
    perturb_specs = perturb_by_name(parse_list(perturbs_text))
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

    source_arrays: dict[str, dict[str, np.ndarray]] = {}
    for name, tflite_path in source_tflite:
        chunks: list[np.ndarray] = []
        for start in range(0, len(images), int(batch_size)):
            features, _ops = tflite_raw_int8(tflite_path, images[start : start + int(batch_size)])
            chunks.append(features)
        features_all = np.concatenate(chunks, axis=0).astype(np.int8)
        source_arrays[name] = classify_features(features=features_all, parents=parent, payload=source_params[name])
    scores = label_scores(source_arrays, source_order, parent)
    labels = np.argmax(scores, axis=1).astype(np.int64)
    meta = {
        "excluded_highpressure_base_rows": int(len(excluded_base_rows)),
        "train_base_rows": int(len(train_base_rows)),
        "train_synthetic_events": int(len(images)),
        "perturbs": [item.name for item in perturb_specs],
        "source_label_counts": {
            source_order[index]: int(np.sum(labels == index))
            for index in range(len(source_order))
        },
        "train_base_row_indexes_preview": [int(item) for item in train_base_rows[:20].tolist()],
        "base_index": [int(item) for item in base_index_rows[:20]],
        "perturb_preview": perturb_rows[:20],
    }
    return images, parent, labels, meta


def build_highpressure_images(
    *,
    source_events: dict[str, dict[tuple[str, int, str, int], dict[str, str]]],
    source_order: list[str],
    dataset_dir: Path,
    seed: int,
) -> tuple[list[tuple[str, int, str, int]], list[dict[str, str]], np.ndarray, np.ndarray, list[str]]:
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    rows = [source_events[source_order[0]][key] for key in common_event_keys]
    view_names = sorted({str(row["view_label"]) for row in rows})
    view_cache, _clean_x, _y_parent_base, _paths_base = build_view_cache(dataset_dir, view_names)
    images: list[np.ndarray] = []
    for row in rows:
        sample = int(row["sample_index"])
        view = str(row["view_label"])
        perturb_name = str(row["perturb"])
        perturb = perturb_by_name([perturb_name])[0]
        query_index = int(row["base_query_index"])
        rng_seed = int(seed) + query_index * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
        rng = np.random.default_rng(rng_seed)
        images.append(apply_perturb(view_cache[view][sample], perturb, rng))
    parent = np.asarray([int(row["parent"]) for row in rows], dtype=np.int64)
    groups = [str(row["group"]) for row in rows]
    return common_event_keys, rows, np.stack(images).astype(np.float32), parent, groups


def summarize_parent_predictions(pred: np.ndarray, parent: np.ndarray, groups: list[str]) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for got, target, group in zip(pred.tolist(), parent.tolist(), groups, strict=False):
        grouped[str(group)][0] += int(int(got) != int(target))
        grouped[str(group)][1] += 1
    per_group = [
        {
            "group": group,
            "wrong": int(values[0]),
            "total": int(values[1]),
            "wrong_rate": float(values[0] / max(values[1], 1)),
        }
        for group, values in sorted(grouped.items())
    ]
    wrong = int(sum(row["wrong"] for row in per_group))
    total = int(sum(row["total"] for row in per_group))
    return {
        "wrong_events": wrong,
        "total_events": total,
        "wrong_rate": float(wrong / max(total, 1)),
        "low_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "low"), None),
        "control_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "control"), None),
        "per_group": per_group,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Train a single-backbone V8 parent + synthetic source-route head probe.")
    parser.add_argument("--source-tflite", action="append", required=True, help="name=parent_int8.tflite")
    parser.add_argument("--source-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--source-stress", action="append", required=True, help="name=highpressure/stress_events.csv")
    parser.add_argument("--base-params", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--init-model", type=Path, default=None)
    parser.add_argument("--filters", default="2,6,12")
    parser.add_argument("--code-dim", type=int, default=8)
    parser.add_argument("--source-count", type=int, default=5)
    parser.add_argument("--route-start-dim", type=int, default=3)
    parser.add_argument("--freeze-prefix-dims", type=int, default=3)
    parser.add_argument("--first-kernel", type=int, default=3)
    parser.add_argument("--backbone-architecture", default="spacetodepth_conv")
    parser.add_argument("--activation", default="relu6")
    parser.add_argument("--pool", default="max")
    parser.add_argument("--extra-conv", action="store_true")
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--perturbs", default="all")
    parser.add_argument("--normal-margin-max", type=int, default=128)
    parser.add_argument("--max-train-base-rows", type=int, default=800)
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--learning-rate", type=float, default=0.0005)
    parser.add_argument("--l2", type=float, default=1.0e-4)
    parser.add_argument("--route-weight", type=float, default=0.5)
    parser.add_argument("--synthetic-parent-weight", type=float, default=0.25)
    parser.add_argument("--parent-logit-anchor-weight", type=float, default=0.05)
    parser.add_argument("--route-logit-l2", type=float, default=0.0)
    parser.add_argument("--freeze-backbone", action="store_true")
    parser.add_argument("--freeze-parent-output-dims", action="store_true")
    parser.add_argument("--separate-route-head", action="store_true")
    parser.add_argument("--route-hidden-dim", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--stress-seed", type=int, default=20260520)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument("--log-every", type=int, default=10)
    args = parser.parse_args()

    if args.route_start_dim < 3:
        raise ValueError("--route-start-dim must be >= 3 so parent logits remain at code[:3]")
    if args.freeze_prefix_dims < 0 or args.freeze_prefix_dims > args.code_dim:
        raise ValueError("--freeze-prefix-dims must be in [0, code_dim]")
    if args.code_dim < args.route_start_dim + args.source_count:
        raise ValueError("--code-dim must fit route_start_dim plus source route logits")
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

    source_events = {name: read_events(path) for name, path in stress_items}
    source_params = {name: load_npz(path) for name, path in param_items}
    base_params = load_npz(args.base_params)

    normal_flat, normal_images = build_view_dataset(args.dataset_dir, parse_list(args.stress))
    normal_parent = np.asarray(normal_flat["y_parent"], dtype=np.int64)
    synthetic_images, synthetic_parent, source_label, synthetic_meta = build_synthetic_training_set(
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
    )

    images = np.concatenate([normal_images, synthetic_images], axis=0).astype(np.float32)
    y_parent = np.concatenate([normal_parent, synthetic_parent], axis=0).astype(np.int64)
    route_label = np.concatenate(
        [np.zeros(len(normal_images), dtype=np.int64), source_label.astype(np.int64)],
        axis=0,
    )
    route_weight = np.concatenate(
        [np.zeros(len(normal_images), dtype=np.float32), np.ones(len(synthetic_images), dtype=np.float32)],
        axis=0,
    )
    parent_weight = np.concatenate(
        [
            parent_weights(normal_parent),
            np.full(len(synthetic_images), float(args.synthetic_parent_weight), dtype=np.float32),
        ],
        axis=0,
    ).astype(np.float32)
    route_class_weight = source_class_weights(route_label, route_weight, len(source_order))
    route_weight = route_weight * route_class_weight[route_label]

    val_mask = np.zeros(len(images), dtype=bool)
    synthetic_start = len(normal_images)
    if args.validation_mod > 1:
        synthetic_rows = np.arange(len(synthetic_images), dtype=np.int64)
        val_mask[synthetic_start:] = (synthetic_rows % int(args.validation_mod)) == 0
    train_mask = ~val_mask
    if not np.any(train_mask):
        train_mask[:] = True
        val_mask[:] = False

    model = build_route_probe_model(
        filters=parse_filters(args.filters),
        code_dim=args.code_dim,
        source_count=len(source_order),
        route_start_dim=int(args.route_start_dim),
        learning_rate=args.learning_rate,
        l2=args.l2,
        first_kernel=args.first_kernel,
        backbone_architecture=args.backbone_architecture,
        activation=args.activation,
        pool=args.pool,
        extra_conv=args.extra_conv,
        separate_route_head=bool(args.separate_route_head),
        route_hidden_dim=int(args.route_hidden_dim),
    )
    init_parent_logits = np.zeros((len(images), 3), dtype=np.float32)
    if args.init_model is not None:
        init_model = tf.keras.models.load_model(args.init_model, safe_mode=False)
        set_weights_allow_partial_output(model, init_model)
        # Use the freshly built model after partial weight copy. Older saved Lambda
        # layers can fail direct deserialization-time prediction because their
        # closure references are not portable across scripts.
        init_parent_logits = model.predict(images, batch_size=args.batch_size, verbose=0)[:, :3].astype(np.float32)
    if args.freeze_backbone:
        for layer in model.layers:
            if args.separate_route_head:
                layer.trainable = layer.name.startswith("route_")
            elif layer.name != "embedding_dense":
                layer.trainable = False
    embedding_layer = model.get_layer("embedding_dense")
    trainable_names = [str(getattr(var, "path", getattr(var, "name", var))) for var in model.trainable_variables]

    optimizer = tf.keras.optimizers.Adam(float(args.learning_rate))
    parent_ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True, reduction="none")
    route_ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True, reduction="none")
    x_tf = tf.constant(images, dtype=tf.float32)
    y_parent_tf = tf.constant(y_parent, dtype=tf.int32)
    route_label_tf = tf.constant(route_label, dtype=tf.int32)
    parent_weight_tf = tf.constant(parent_weight, dtype=tf.float32)
    route_weight_tf = tf.constant(route_weight, dtype=tf.float32)
    init_parent_tf = tf.constant(init_parent_logits, dtype=tf.float32)
    train_indexes = np.where(train_mask)[0].astype(np.int64)

    train_rows: list[dict[str, Any]] = []
    rng = np.random.default_rng(args.seed)
    for epoch in range(1, int(args.epochs) + 1):
        rng.shuffle(train_indexes)
        epoch_loss: list[float] = []
        epoch_route_acc: list[float] = []
        for start in range(0, len(train_indexes), int(args.batch_size)):
            batch = train_indexes[start : start + int(args.batch_size)]
            batch_tf = tf.constant(batch, dtype=tf.int32)
            with tf.GradientTape() as tape:
                code = model(tf.gather(x_tf, batch_tf), training=True)
                parent_logits = code[:, :3]
                route_logits = code[:, int(args.route_start_dim) : int(args.route_start_dim) + len(source_order)]
                p_loss = parent_ce(tf.gather(y_parent_tf, batch_tf), parent_logits)
                p_loss = tf.reduce_sum(p_loss * tf.gather(parent_weight_tf, batch_tf)) / tf.maximum(
                    tf.reduce_sum(tf.gather(parent_weight_tf, batch_tf)),
                    1.0,
                )
                r_loss_raw = route_ce(tf.gather(route_label_tf, batch_tf), route_logits)
                r_loss = tf.reduce_sum(r_loss_raw * tf.gather(route_weight_tf, batch_tf)) / tf.maximum(
                    tf.reduce_sum(tf.gather(route_weight_tf, batch_tf)),
                    1.0,
                )
                anchor = tf.reduce_mean(tf.square(parent_logits - tf.gather(init_parent_tf, batch_tf)))
                route_l2 = tf.reduce_mean(tf.square(route_logits))
                loss = (
                    p_loss
                    + float(args.route_weight) * r_loss
                    + float(args.parent_logit_anchor_weight) * anchor
                    + float(args.route_logit_l2) * route_l2
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
            route_active = tf.gather(route_weight_tf, batch_tf) > 0.0
            if bool(tf.reduce_any(route_active).numpy()):
                route_pred = tf.argmax(route_logits, axis=1, output_type=tf.int32)
                route_ok = tf.boolean_mask(route_pred == tf.gather(route_label_tf, batch_tf), route_active)
                epoch_route_acc.append(float(tf.reduce_mean(tf.cast(route_ok, tf.float32)).numpy()))
        if epoch == 1 or epoch == int(args.epochs) or epoch % int(args.log_every) == 0:
            code_all = model.predict(images, batch_size=args.batch_size, verbose=0)
            normal_code = code_all[: len(normal_images)]
            normal_pred = np.argmax(normal_code[:, :3], axis=1).astype(np.int64)
            synth_code = code_all[len(normal_images) :]
            synth_route_pred = np.argmax(
                synth_code[:, int(args.route_start_dim) : int(args.route_start_dim) + len(source_order)],
                axis=1,
            ).astype(np.int64)
            val_source_acc = None
            if np.any(val_mask[synthetic_start:]):
                val_local = val_mask[synthetic_start:]
                val_source_acc = float(np.mean(synth_route_pred[val_local] == source_label[val_local]))
            row = {
                "epoch": int(epoch),
                "loss_mean": float(np.mean(epoch_loss)),
                "normal_parent_accuracy": float(np.mean(normal_pred == normal_parent)),
                "synthetic_source_train_accuracy_batch_mean": float(np.mean(epoch_route_acc)) if epoch_route_acc else 0.0,
                "synthetic_source_accuracy": float(np.mean(synth_route_pred == source_label)),
                "synthetic_source_val_accuracy": val_source_acc,
            }
            train_rows.append(row)
            print(json.dumps({"route_head_train": row}, ensure_ascii=False), flush=True)
    write_csv(args.output_dir / "training_log.csv", train_rows)

    normal_code = model.predict(normal_images, batch_size=args.batch_size, verbose=0)
    normal_pred = np.argmax(normal_code[:, :3], axis=1).astype(np.int64)
    normal_row: dict[str, Any] = {
        "stage": "v8_synthetic_route_head",
        "name": args.output_dir.name,
        "code_dim": int(args.code_dim),
        "source_count": int(len(source_order)),
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
    route_selected = np.argmax(
        high_code[:, int(args.route_start_dim) : int(args.route_start_dim) + len(source_order)],
        axis=1,
    ).astype(np.int64)
    route_summary = summarize_selection(
        selected=route_selected,
        base_rows=high_rows,
        common_keys=common_keys,
        source_events=source_events,
        source_order=source_order,
    )
    parent_summary = summarize_parent_predictions(
        pred=np.argmax(high_code[:, :3], axis=1).astype(np.int64),
        parent=high_parent,
        groups=high_groups,
    )

    export_info = export_tflite(model, args.output_dir, normal_images[np.linspace(0, len(normal_images) - 1, min(512, len(normal_images))).astype(np.int64)])
    int8_pred, int8_ops = predict_tflite(Path(export_info["int8_tflite"]), normal_images)
    tflite_row: dict[str, Any] = {
        **export_info,
        "int8_unique_ops": int8_ops,
    }
    tflite_row.update(
        metric_summary(
            view_order=list(normal_flat["view_names"]),
            view_labels=np.asarray(normal_flat["view_labels"]).astype(str),
            y_parent=normal_parent,
            pred=int8_pred,
            prefix="tflite_int8_",
        )
    )
    write_json(args.output_dir / "tflite_summary.json", tflite_row)

    summary = {
        "source_order": source_order,
        "source_tflite": {name: str(path) for name, path in tflite_items},
        "source_params": {name: str(path) for name, path in param_items},
        "source_stress": {name: str(path) for name, path in stress_items},
        "base_params": str(args.base_params),
        "init_model": str(args.init_model) if args.init_model else "",
        "high_pressure_usage": "evaluation_only",
        "synthetic_training_usage": "non_highpressure_synthetic_route_labels",
        "runtime_feature_usage": "single_backbone_route_logits_probe",
        "train_control": {
            "freeze_backbone": bool(args.freeze_backbone),
            "freeze_parent_output_dims": bool(args.freeze_parent_output_dims),
            "route_start_dim": int(args.route_start_dim),
            "freeze_prefix_dims": int(args.freeze_prefix_dims),
            "separate_route_head": bool(args.separate_route_head),
            "route_hidden_dim": int(args.route_hidden_dim),
            "route_logit_l2": float(args.route_logit_l2),
            "trainable_variables": trainable_names,
        },
        "synthetic": synthetic_meta,
        "normal": normal_row,
        "route_high_pressure": route_summary,
        "parent_high_pressure": parent_summary,
        "tflite": tflite_row,
        "config": {key: (str(value) if isinstance(value, Path) else value) for key, value in vars(args).items()},
    }
    write_json(args.output_dir / "summary.json", summary)
    write_csv(
        args.output_dir / "policy_summary.csv",
        [
            {
                "policy": "single_backbone_route_head",
                "wrong_events": route_summary["wrong_events"],
                "total_events": route_summary["total_events"],
                "wrong_rate": route_summary["wrong_rate"],
                "low_wrong_rate": route_summary["low_wrong_rate"],
                "control_wrong_rate": route_summary["control_wrong_rate"],
                "normal_clean_accuracy": normal_row.get("clean_accuracy"),
                "normal_stress_min_accuracy": normal_row.get("stress_min_accuracy"),
                "tflite_int8_clean_accuracy": tflite_row.get("tflite_int8_clean_accuracy"),
                "tflite_int8_stress_min_accuracy": tflite_row.get("tflite_int8_stress_min_accuracy"),
                "ops": json.dumps(int8_ops),
            }
        ],
    )
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
