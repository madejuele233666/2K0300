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

from estimate_v8_board_time import calibrated_conservative_us
from train_v8_end_to_end_embedding import build_embedding_model, build_view_dataset, parse_filters
from stress_test_v8_low_margin import apply_perturb, build_view_cache, perturb_by_name


STRICT_RECOMMENDED_RAW_TFLITE_OPS = {
    "SPACE_TO_DEPTH",
    "CONV_2D",
    "MAX_POOL_2D",
    "MEAN",
    "FULLY_CONNECTED",
    "DELEGATE",
}


EventKey = tuple[str, int, str, int]


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


def parse_csv_items(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


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


def load_source_gate_teacher(path: Path, flat: dict[str, Any]) -> dict[str, np.ndarray]:
    row_count = len(np.asarray(flat["sample_index"]))
    label = np.zeros(row_count, dtype=np.int64)
    weight = np.zeros(row_count, dtype=np.float32)
    target = np.zeros((row_count, 1), dtype=np.float32)
    flat_sample = np.asarray(flat["sample_index"], dtype=np.int64)
    flat_view = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(flat_sample, flat_view, strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "source_label", "target_probs", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_label = np.asarray(data["source_label"], dtype=np.int64)
        teacher_target = np.asarray(data["target_probs"], dtype=np.float32)
        teacher_weight = np.asarray(data["weight"], dtype=np.float32)
        source_names = (
            np.asarray(data["source_names"]).astype(str)
            if "source_names" in data.files
            else np.asarray([f"source{index}" for index in range(teacher_target.shape[1])]).astype(str)
        )
    if teacher_target.ndim != 2 or len(teacher_label) != len(teacher_target):
        raise ValueError(f"invalid target/label shapes: {teacher_target.shape}, {teacher_label.shape}")
    if len(teacher_weight) != len(teacher_label) or len(teacher_sample) != len(teacher_label):
        raise ValueError("teacher array lengths do not match")
    target = np.zeros((row_count, teacher_target.shape[1]), dtype=np.float32)
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample, teacher_view, strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        label[row_index] = int(teacher_label[index])
        weight[row_index] = float(teacher_weight[index])
        target[row_index] = teacher_target[index]
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"teacher rows missing from flat dataset: {len(missing_keys)}, first {preview}")
    return {
        "label": label,
        "weight": weight,
        "target": target,
        "source_names": source_names.astype(str),
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


def representative_dataset(samples: np.ndarray):
    def gen():
        for index in range(len(samples)):
            yield [samples[index : index + 1].astype(np.float32)]

    return gen


def export_tflite(model: tf.keras.Model, output_dir: Path, rep_samples: np.ndarray) -> dict[str, Any]:
    float_path = output_dir / "source_gate_float.tflite"
    int8_path = output_dir / "source_gate_int8.tflite"
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    float_path.write_bytes(converter.convert())
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset(rep_samples)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    int8_path.write_bytes(converter.convert())
    return {
        "float_tflite": str(float_path),
        "int8_tflite": str(int8_path),
        "float_tflite_bytes": int(float_path.stat().st_size),
        "int8_tflite_bytes": int(int8_path.stat().st_size),
    }


def predict_tflite(path: Path, images: np.ndarray, batch_size: int) -> tuple[np.ndarray, list[str]]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_index = int(input_detail["index"])
    output_index = int(output_detail["index"])
    in_scale, in_zero = input_detail.get("quantization", (0.0, 0))
    out_scale, out_zero = output_detail.get("quantization", (0.0, 0))
    preds: list[int] = []
    for start in range(0, len(images), batch_size):
        for image in images[start : start + batch_size]:
            value = image[None, ...].astype(np.float32)
            if input_detail["dtype"] == np.int8:
                value = np.clip(np.rint(value / float(in_scale) + int(in_zero)), -128, 127).astype(np.int8)
            interpreter.set_tensor(input_index, value)
            interpreter.invoke()
            logits = interpreter.get_tensor(output_index)
            if output_detail["dtype"] == np.int8:
                logits = (logits.astype(np.float32) - int(out_zero)) * float(out_scale)
            preds.append(int(np.argmax(logits[0])))
    ops = sorted(set(str(item["op_name"]) for item in interpreter._get_ops_details()))  # noqa: SLF001
    return np.asarray(preds, dtype=np.int64), ops


def soft_ce_loss(target: tf.Tensor, logits: tf.Tensor) -> tf.Tensor:
    return tf.keras.losses.categorical_crossentropy(target, logits, from_logits=True)


def build_high_pressure_images(
    *,
    dataset_dir: Path,
    rows: list[dict[str, str]],
    seed: int,
) -> np.ndarray:
    selected_view_names = sorted(set(str(row["view_label"]) for row in rows))
    view_cache, _clean_x, _y_parent, _paths = build_view_cache(dataset_dir, selected_view_names)
    perturb_cache = {item.name: item for item in perturb_by_name(sorted(set(str(row["perturb"]) for row in rows)))}
    images: list[np.ndarray] = []
    for row in rows:
        view = str(row["view_label"])
        sample = int(row["sample_index"])
        query_index = int(row["base_query_index"])
        perturb = perturb_cache[str(row["perturb"])]
        rng_seed = seed + query_index * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
        rng = np.random.default_rng(rng_seed)
        images.append(apply_perturb(view_cache[view][sample], perturb, rng))
    return np.stack(images).astype(np.float32)


def summarize_selection(
    *,
    selected: np.ndarray,
    base_rows: list[dict[str, str]],
    common_keys: list[EventKey],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_names: list[str],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    trace_rows: list[dict[str, Any]] = []
    for row_index, row in enumerate(base_rows):
        source = source_names[int(selected[row_index])]
        key = common_keys[row_index]
        pred = int(source_events[source][key]["stress_pred"])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
        if len(trace_rows) < 300:
            trace_rows.append(
                {
                    "group": str(row["group"]),
                    "sample_index": int(row["sample_index"]),
                    "view_label": str(row["view_label"]),
                    "perturb": str(row["perturb"]),
                    "parent": parent,
                    "chosen_source": source,
                    "stress_pred": pred,
                    "wrong": bool(wrong),
                }
            )
    wrong_events = int(sum(value[0] for value in grouped.values()))
    total_events = int(sum(value[1] for value in grouped.values()))
    return (
        {
            "wrong_events": wrong_events,
            "total_events": total_events,
            "wrong_rate": float(wrong_events / max(total_events, 1)),
            "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
            "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
            "chosen_counts": dict(chosen),
        },
        trace_rows,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Train a standalone normal-only V8 source/orbit gate.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--source-gate-teacher-npz", type=Path, required=True)
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--filters", default="2,6,12")
    parser.add_argument("--backbone-architecture", default="spacetodepth_conv")
    parser.add_argument("--first-kernel", type=int, default=3)
    parser.add_argument("--activation", choices=["relu", "relu6"], default="relu")
    parser.add_argument("--pool", choices=["max", "avg"], default="max")
    parser.add_argument("--extra-conv", action="store_true")
    parser.add_argument("--dropout", type=float, default=0.0)
    parser.add_argument("--l2", type=float, default=1.0e-5)
    parser.add_argument("--learning-rate", type=float, default=0.003)
    parser.add_argument("--epochs", type=int, default=160)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument("--class-balance", choices=["none", "inverse_label"], default="none")
    parser.add_argument("--max-class-balance-weight", type=float, default=4.0)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--highstress-seed", type=int, default=20260520)
    parser.add_argument(
        "--stress",
        default=(
            "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
            "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,"
            "cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,"
            "cam_blur3a0_noise0p02,cam_blur5a45_noise0p04,cam_blur3a45,cam_blur3a135,cam_noise0p03,"
            "cam_blur3a45_noise0p02,cam_blur3a135_noise0p02,cam_bright0p06,cam_contrast0p12,"
            "cam_bright0p04_contrast0p10"
        ),
    )
    args = parser.parse_args()

    tf.keras.utils.set_random_seed(args.seed)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stress_names = parse_csv_items(args.stress)
    flat, images = build_view_dataset(args.dataset_dir, stress_names)
    teacher = load_source_gate_teacher(args.source_gate_teacher_npz, flat)
    teacher_source_names = [str(name) for name in np.asarray(teacher["source_names"]).astype(str).tolist()]
    source_count = len(teacher_source_names)
    source_items = [parse_named_path(item) for item in args.source]
    source_names = [name for name, _path in source_items]
    if len(source_names) != source_count:
        raise ValueError(f"--source count {len(source_names)} does not match teacher source count {source_count}")
    labels = np.asarray(teacher["label"], dtype=np.int64)
    weights = np.asarray(teacher["weight"], dtype=np.float32)
    target = np.asarray(teacher["target"], dtype=np.float32)
    active = weights > 0.0
    class_weight = np.ones(source_count, dtype=np.float32)
    if args.class_balance == "inverse_label":
        counts = np.bincount(labels[active], minlength=source_count).astype(np.float32)
        nonzero = counts > 0
        mean_count = float(np.mean(counts[nonzero])) if np.any(nonzero) else 1.0
        class_weight[nonzero] = mean_count / np.maximum(counts[nonzero], 1.0)
        class_weight = np.minimum(class_weight, float(args.max_class_balance_weight)).astype(np.float32)
        weights = weights * class_weight[labels]
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    train_mask, val_mask = split_by_sample(sample_index, args.validation_mod)
    train_mask &= active
    val_mask &= active

    config = {
        **{key: (str(value) if isinstance(value, Path) else value) for key, value in vars(args).items()},
        "filters": list(parse_filters(args.filters)),
        "source_names": source_names,
        "teacher_source_names": teacher_source_names,
        "source_count": source_count,
        "class_weights": {source_names[index]: float(class_weight[index]) for index in range(source_count)},
        "active_rows": int(np.sum(active)),
        "train_rows": int(np.sum(train_mask)),
        "val_rows": int(np.sum(val_mask)),
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "standalone source gate teacher only",
    }
    write_json(args.output_dir / "train_config.json", config)

    model = build_embedding_model(
        filters=parse_filters(args.filters),
        embedding_dim=source_count,
        learning_rate=args.learning_rate,
        l2=args.l2,
        dropout=args.dropout,
        first_kernel=args.first_kernel,
        embedding_output_mode="raw",
        backbone_architecture=args.backbone_architecture,
        activation=args.activation,
        pool=args.pool,
        extra_conv=args.extra_conv,
    )
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=args.learning_rate),
        loss=soft_ce_loss,
        metrics=[tf.keras.metrics.CategoricalAccuracy(name="source_acc")],
    )
    callbacks: list[tf.keras.callbacks.Callback] = [
        tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=20, restore_best_weights=True)
    ]
    history = model.fit(
        images[train_mask],
        target[train_mask],
        sample_weight=weights[train_mask],
        validation_data=(images[val_mask], target[val_mask], weights[val_mask]) if np.any(val_mask) else None,
        epochs=args.epochs,
        batch_size=args.batch_size,
        verbose=2,
        callbacks=callbacks,
    )
    model.save(args.output_dir / "source_gate_model.keras")

    normal_logits = model.predict(images, batch_size=args.batch_size, verbose=0)
    normal_pred = np.argmax(normal_logits, axis=1).astype(np.int64)
    normal_metrics = {
        "train_source_label_acc": float(np.mean(normal_pred[train_mask] == labels[train_mask])),
        "val_source_label_acc": float(np.mean(normal_pred[val_mask] == labels[val_mask])) if np.any(val_mask) else None,
        "all_active_source_label_acc": float(np.mean(normal_pred[active] == labels[active])),
        "pred_counts": {source_names[index]: int(np.sum(normal_pred[active] == index)) for index in range(source_count)},
        "label_counts": {source_names[index]: int(np.sum(labels[active] == index)) for index in range(source_count)},
        "epochs_ran": int(len(history.history["loss"])),
    }

    rng = np.random.default_rng(args.seed)
    rep_idx = rng.choice(len(images), size=min(512, len(images)), replace=False)
    export_info = export_tflite(model, args.output_dir, images[rep_idx])
    tflite_pred, int8_ops = predict_tflite(Path(export_info["int8_tflite"]), images, args.batch_size)
    tflite_metrics = {
        **export_info,
        "int8_unique_ops": int8_ops,
        "strict_recommended_ops": bool(set(int8_ops).issubset(STRICT_RECOMMENDED_RAW_TFLITE_OPS)),
        "int8_train_source_label_acc": float(np.mean(tflite_pred[train_mask] == labels[train_mask])),
        "int8_val_source_label_acc": float(np.mean(tflite_pred[val_mask] == labels[val_mask])) if np.any(val_mask) else None,
        "int8_all_active_source_label_acc": float(np.mean(tflite_pred[active] == labels[active])),
        "gate_conservative_us": int(
            round(
                calibrated_conservative_us(
                    {
                        "filters": list(parse_filters(args.filters)),
                        "backbone_architecture": args.backbone_architecture,
                        "first_kernel": args.first_kernel,
                        "extra_conv": args.extra_conv,
                    }
                )
            )
        ),
    }

    source_events = {name: read_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    base_rows = [source_events[source_names[0]][key] for key in common_keys]
    high_images = build_high_pressure_images(dataset_dir=args.dataset_dir, rows=base_rows, seed=args.highstress_seed)
    float_high_pred = np.argmax(model.predict(high_images, batch_size=args.batch_size, verbose=0), axis=1).astype(np.int64)
    int8_high_pred, high_ops = predict_tflite(Path(export_info["int8_tflite"]), high_images, args.batch_size)
    float_summary, float_trace = summarize_selection(
        selected=float_high_pred,
        base_rows=base_rows,
        common_keys=common_keys,
        source_events=source_events,
        source_names=source_names,
    )
    int8_summary, int8_trace = summarize_selection(
        selected=int8_high_pred,
        base_rows=base_rows,
        common_keys=common_keys,
        source_events=source_events,
        source_names=source_names,
    )
    write_csv(args.output_dir / "float_highstress_trace_sample.csv", float_trace)
    write_csv(args.output_dir / "int8_highstress_trace_sample.csv", int8_trace)
    high_summary = {
        "common_events": int(len(common_keys)),
        "high_pressure_usage": "evaluation_only",
        "float_gate": {**float_summary, "chosen_counts_json": json.dumps(float_summary["chosen_counts"], ensure_ascii=False)},
        "int8_gate": {**int8_summary, "chosen_counts_json": json.dumps(int8_summary["chosen_counts"], ensure_ascii=False)},
        "int8_high_unique_ops": high_ops,
    }
    summary = {
        "config": config,
        "normal": normal_metrics,
        "tflite": tflite_metrics,
        "highstress": high_summary,
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
