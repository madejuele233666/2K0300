import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf

from analyze_v8_learned_source_gate import (
    build_features,
    build_model,
    class_distances,
    load_npz,
    parse_named_path,
    softmax_numpy,
    source_score_matrix,
    split_by_sample,
    view_family,
    write_csv,
    write_json,
)


def build_labels(
    *,
    score_matrix: np.ndarray,
    sample_index: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    label_mode: str,
) -> np.ndarray:
    if label_mode == "row_winner":
        return np.argmax(score_matrix, axis=1).astype(np.int64)

    if label_mode.startswith("sample_family_"):
        group_keys = [
            (int(sample), int(parent_value), view_family(str(view)))
            for sample, parent_value, view in zip(
                sample_index.tolist(),
                parent.tolist(),
                view_labels.astype(str).tolist(),
                strict=False,
            )
        ]
        aggregate_mode = label_mode.removeprefix("sample_family_")
    elif label_mode.startswith("sample_"):
        group_keys = [
            (int(sample), int(parent_value))
            for sample, parent_value in zip(sample_index.tolist(), parent.tolist(), strict=False)
        ]
        aggregate_mode = label_mode.removeprefix("sample_")
    else:
        raise ValueError(f"unknown label mode: {label_mode}")

    grouped: dict[tuple[Any, ...], list[int]] = defaultdict(list)
    for row_index, key in enumerate(group_keys):
        grouped[key].append(row_index)

    group_choice: dict[tuple[Any, ...], int] = {}
    for key, indexes in grouped.items():
        scores = score_matrix[np.asarray(indexes, dtype=np.int64)]
        if aggregate_mode == "min":
            aggregate = np.min(scores, axis=0)
        elif aggregate_mode == "correct_count":
            correct = scores >= 0.0
            aggregate = np.sum(correct, axis=0).astype(np.float64) * 1.0e6
            aggregate += np.mean(np.maximum(scores, 0.0), axis=0)
        else:
            raise ValueError(f"unknown label aggregate mode: {aggregate_mode}")
        group_choice[key] = int(np.argmax(aggregate))

    return np.asarray([group_choice[key] for key in group_keys], dtype=np.int64)


def build_teacher(
    *,
    normal_params: list[tuple[str, Path]],
    base_params_npz: Path,
    output_dir: Path,
    feature_mode: str,
    score_mode: str,
    hidden_dim: int,
    epochs: int,
    learning_rate: float,
    batch_size: int,
    seed: int,
    validation_mod: int,
    normalize_p90: bool,
    cascade_rule: str,
    cascade_quantile: float,
    max_rows: int,
    label_mode: str,
) -> None:
    if cascade_rule not in {"conf", "prob_margin", "base_adv"}:
        raise ValueError(f"unknown cascade rule: {cascade_rule}")
    if cascade_quantile < 0.0 or cascade_quantile > 1.0:
        raise ValueError(f"cascade quantile must be in [0,1], got {cascade_quantile}")
    source_order = [name for name, _path in normal_params]
    normal_payloads = {name: load_npz(path) for name, path in normal_params}
    base_payload = load_npz(base_params_npz)
    base_sample = np.asarray(base_payload["sample_index"], dtype=np.int64)
    base_view = np.asarray(base_payload["view_labels"]).astype(str)
    base_parent = np.asarray(base_payload["parent"], dtype=np.int64)
    base_codes = np.asarray(base_payload["embedding_int8"], dtype=np.int8)
    prototypes = np.asarray(base_payload["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base_payload["prototype_parent"], dtype=np.int64)

    base_pred, base_margin, base_class_dist = class_distances(
        base_codes,
        prototypes,
        prototype_parent,
        batch_size=batch_size,
    )
    if not np.all(base_pred == base_parent):
        raise ValueError("base normal int8 replay is not 100%; refuse to build guard teacher")

    score_matrix, present = source_score_matrix(
        base_payload=base_payload,
        payloads=normal_payloads,
        source_order=source_order,
        score_mode=score_mode,
        normalize_p90=normalize_p90,
    )
    valid = np.any(present, axis=1)
    labels = build_labels(
        score_matrix=score_matrix,
        sample_index=base_sample,
        parent=base_parent,
        view_labels=base_view,
        label_mode=label_mode,
    )
    train_mask, val_mask = split_by_sample(base_sample, validation_mod)
    view_order = list(dict.fromkeys(base_view.tolist()))
    family_order = list(dict.fromkeys(view_family(view) for view in base_view.tolist()))
    x_normal = build_features(
        codes=base_codes,
        pred=base_pred,
        margin=base_margin,
        class_dist=base_class_dist,
        view_labels=base_view,
        view_order=view_order,
        family_order=family_order,
        mode=feature_mode,
    )
    train_indexes = train_mask & valid
    val_indexes = val_mask & valid
    mean = np.mean(x_normal[train_indexes], axis=0, keepdims=True)
    std = np.maximum(np.std(x_normal[train_indexes], axis=0, keepdims=True), 1.0e-6)
    x_train_all = (x_normal - mean) / std
    class_counts = np.bincount(labels[valid], minlength=len(source_order)).astype(np.float64)
    class_weight = {
        index: float(np.mean(class_counts[class_counts > 0]) / max(class_counts[index], 1.0))
        for index in range(len(source_order))
    }
    model = build_model(
        input_dim=x_train_all.shape[1],
        output_dim=len(source_order),
        hidden_dim=hidden_dim,
        learning_rate=learning_rate,
        seed=seed,
    )
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
        epochs=epochs,
        batch_size=batch_size,
        verbose=0,
        class_weight=class_weight,
        callbacks=callbacks,
    )
    train_pred = np.argmax(model.predict(x_train_all[train_indexes], batch_size=batch_size, verbose=0), axis=1)
    train_acc = float(np.mean(train_pred == labels[train_indexes]))
    val_acc: float | None = None
    if np.any(val_indexes):
        val_pred = np.argmax(model.predict(x_train_all[val_indexes], batch_size=batch_size, verbose=0), axis=1)
        val_acc = float(np.mean(val_pred == labels[val_indexes]))

    logits = model.predict(x_train_all, batch_size=batch_size, verbose=0)
    probs = softmax_numpy(logits)
    pred_source = np.argmax(probs, axis=1).astype(np.int64)
    conf = np.max(probs, axis=1)
    prob_margin = np.partition(probs, -2, axis=1)[:, -1] - np.partition(probs, -2, axis=1)[:, -2]
    base_adv = probs[np.arange(len(probs)), pred_source] - probs[:, 0]

    ref_indexes = val_indexes if np.any(val_indexes) else train_indexes
    ref_probs = probs[ref_indexes]
    ref_pred = pred_source[ref_indexes]
    ref_labels = labels[ref_indexes]
    ref_conf = np.max(ref_probs, axis=1)
    ref_prob_margin = np.partition(ref_probs, -2, axis=1)[:, -1] - np.partition(ref_probs, -2, axis=1)[:, -2]
    ref_base_adv = ref_probs[np.arange(len(ref_probs)), ref_pred] - ref_probs[:, 0]
    nonbase_label_mask = ref_labels != 0
    correct_nonbase_mask = nonbase_label_mask & (ref_pred == ref_labels)
    ref_adv_mask = correct_nonbase_mask if np.any(correct_nonbase_mask) else nonbase_label_mask
    if cascade_rule == "conf":
        threshold = float(np.quantile(ref_conf, cascade_quantile))
        gate_mask = (pred_source != 0) & (conf >= threshold)
        gate_value = conf
    elif cascade_rule == "prob_margin":
        threshold = float(np.quantile(ref_prob_margin, cascade_quantile))
        gate_mask = (pred_source != 0) & (prob_margin >= threshold)
        gate_value = prob_margin
    else:
        threshold = float(np.quantile(ref_base_adv[ref_adv_mask], cascade_quantile)) if np.any(ref_adv_mask) else 0.0
        gate_mask = (pred_source != 0) & (base_adv >= threshold)
        gate_value = base_adv

    wrong_parent_order = np.argsort(base_class_dist, axis=1)
    nearest_wrong_parent = wrong_parent_order[:, 1].astype(np.int64)
    support = np.sum(present, axis=1).astype(np.int64)
    row_indexes = np.where(gate_mask & valid)[0]
    indexed_rows: list[dict[str, Any]] = []
    for row_index in row_indexes.tolist():
        source_index = int(pred_source[row_index])
        weight = float(max(gate_value[row_index], 0.0))
        order_key = (
            int(base_margin[row_index]),
            -weight,
            -float(conf[row_index]),
            -int(support[row_index]),
            int(row_index),
        )
        indexed_rows.append(
            {
                "order_key": order_key,
                "query_index": int(row_index),
                "sample_index": int(base_sample[row_index]),
                "view_label": str(base_view[row_index]),
                "parent": int(base_parent[row_index]),
                "teacher_wrong_parent": int(nearest_wrong_parent[row_index]),
                "teacher_vote_count": int(support[row_index]),
                "teacher_margin_mean": float(weight),
                "student_int8_margin": int(base_margin[row_index]),
                "weight": weight,
                "pred_source": source_order[source_index],
                "pred_source_index": source_index,
                "label_source": source_order[int(labels[row_index])],
                "label_source_index": int(labels[row_index]),
                "gate_confidence": float(conf[row_index]),
                "gate_prob_margin": float(prob_margin[row_index]),
                "gate_base_advantage": float(base_adv[row_index]),
            }
        )
    indexed_rows.sort(key=lambda row: row["order_key"])
    if max_rows > 0:
        indexed_rows = indexed_rows[: int(max_rows)]
    if not indexed_rows:
        raise ValueError("no learned-gate guard rows selected")

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "learned_gate_guard_teacher.npz",
        query_index=np.asarray([row["query_index"] for row in indexed_rows], dtype=np.int64),
        sample_index=np.asarray([row["sample_index"] for row in indexed_rows], dtype=np.int64),
        view_labels=np.asarray([row["view_label"] for row in indexed_rows]).astype(str),
        parent=np.asarray([row["parent"] for row in indexed_rows], dtype=np.int64),
        teacher_wrong_parent=np.asarray([row["teacher_wrong_parent"] for row in indexed_rows], dtype=np.int64),
        teacher_vote_count=np.asarray([row["teacher_vote_count"] for row in indexed_rows], dtype=np.int64),
        teacher_margin_mean=np.asarray([row["teacher_margin_mean"] for row in indexed_rows], dtype=np.float32),
        student_int8_margin=np.asarray([row["student_int8_margin"] for row in indexed_rows], dtype=np.int64),
        weight=np.asarray([row["weight"] for row in indexed_rows], dtype=np.float32),
        pred_source=np.asarray([row["pred_source"] for row in indexed_rows]).astype(str),
        pred_source_index=np.asarray([row["pred_source_index"] for row in indexed_rows], dtype=np.int64),
        label_source=np.asarray([row["label_source"] for row in indexed_rows]).astype(str),
        label_source_index=np.asarray([row["label_source_index"] for row in indexed_rows], dtype=np.int64),
        gate_confidence=np.asarray([row["gate_confidence"] for row in indexed_rows], dtype=np.float32),
        gate_prob_margin=np.asarray([row["gate_prob_margin"] for row in indexed_rows], dtype=np.float32),
        gate_base_advantage=np.asarray([row["gate_base_advantage"] for row in indexed_rows], dtype=np.float32),
        source_order=np.asarray(source_order).astype(str),
        base_npz=np.asarray(str(base_params_npz)),
        high_pressure_usage=np.asarray("none"),
    )
    csv_rows = [{key: value for key, value in row.items() if key != "order_key"} for row in indexed_rows]
    write_csv(output_dir / "learned_gate_guard_rows.csv", csv_rows)
    summary = {
        "output": str(output_dir / "learned_gate_guard_teacher.npz"),
        "base_params_npz": str(base_params_npz),
        "normal_params": {name: str(path) for name, path in normal_params},
        "high_pressure_usage": "none",
        "normal_training_usage": f"normal-only {label_mode} source labels and normal confidence quantile",
        "feature_mode": feature_mode,
        "score_mode": score_mode,
        "label_mode": label_mode,
        "hidden_dim": int(hidden_dim),
        "normalize_p90": bool(normalize_p90),
        "cascade_rule": cascade_rule,
        "cascade_quantile": float(cascade_quantile),
        "cascade_threshold": float(threshold),
        "row_count": int(len(indexed_rows)),
        "raw_selected_rows": int(np.sum(gate_mask & valid)),
        "max_rows": int(max_rows),
        "train_source_label_acc": train_acc,
        "val_source_label_acc": val_acc,
        "epochs_ran": int(len(history.history["loss"])),
        "source_order": source_order,
        "normal_label_counts": {source_order[index]: int(class_counts[index]) for index in range(len(source_order))},
        "pred_source_counts": {
            source_order[index]: int(np.sum([row["pred_source_index"] == index for row in indexed_rows]))
            for index in range(len(source_order))
        },
        "student_int8_margin_min": int(min(row["student_int8_margin"] for row in indexed_rows)),
        "student_int8_margin_p50": float(np.percentile([row["student_int8_margin"] for row in indexed_rows], 50)),
        "student_int8_margin_max": int(max(row["student_int8_margin"] for row in indexed_rows)),
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build normal-only region guards from a learned source-gate fallback policy."
    )
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--feature-mode", default="code+dist+margin+pred")
    parser.add_argument("--score-mode", default="log")
    parser.add_argument("--hidden-dim", type=int, default=64)
    parser.add_argument("--epochs", type=int, default=120)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--validation-mod", type=int, default=5)
    parser.add_argument("--cascade-rule", choices=["conf", "prob_margin", "base_adv"], default="base_adv")
    parser.add_argument("--cascade-quantile", type=float, default=0.5)
    parser.add_argument("--max-rows", type=int, default=0)
    parser.add_argument(
        "--label-mode",
        default="row_winner",
        help="Normal-only label mode: row_winner,sample_min,sample_correct_count,sample_family_min,sample_family_correct_count.",
    )
    parser.add_argument("--no-normalize-p90", action="store_false", dest="normalize_p90")
    parser.set_defaults(normalize_p90=True)
    args = parser.parse_args()
    build_teacher(
        normal_params=[parse_named_path(item) for item in args.normal_params],
        base_params_npz=args.base_params_npz,
        output_dir=args.output_dir,
        feature_mode=args.feature_mode,
        score_mode=args.score_mode,
        hidden_dim=args.hidden_dim,
        epochs=args.epochs,
        learning_rate=args.learning_rate,
        batch_size=args.batch_size,
        seed=args.seed,
        validation_mod=args.validation_mod,
        normalize_p90=bool(args.normalize_p90),
        cascade_rule=args.cascade_rule,
        cascade_quantile=args.cascade_quantile,
        max_rows=args.max_rows,
        label_mode=args.label_mode,
    )


if __name__ == "__main__":
    main()
