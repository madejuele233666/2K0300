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


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    out: list[np.ndarray] = []
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            class_dist[:, parent] = np.min(dist[:, indexes], axis=1)
        out.append(class_dist)
    return np.concatenate(out).astype(np.int64)


def one_hot(values: np.ndarray, count: int) -> np.ndarray:
    out = np.zeros((len(values), count), dtype=np.float32)
    valid = (values >= 0) & (values < count)
    out[np.where(valid)[0], values[valid].astype(np.int64)] = 1.0
    return out


def feature_tokens(mode: str) -> set[str]:
    return {token for token in mode.split("+") if token}


def build_source_arrays(
    *,
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    keys: list[NormalKey],
    score_mode: str,
    batch_size: int,
) -> dict[str, dict[str, np.ndarray]]:
    out: dict[str, dict[str, np.ndarray]] = {}
    for source in source_order:
        payload = payloads[source]
        key_map = normal_key_map(payload)
        indexes = np.asarray([key_map[key] for key in keys], dtype=np.int64)
        code = np.asarray(payload["embedding_int8"], dtype=np.int8)[indexes]
        parent = np.asarray(payload["parent"], dtype=np.int64)[indexes]
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)[indexes]
        margin = np.asarray(payload["int8_margin"], dtype=np.int64)[indexes]
        prototypes = np.asarray(payload["prototypes_int8"], dtype=np.int8)
        prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
        dist = class_distances(
            code,
            prototypes,
            prototype_parent,
            batch_size=batch_size,
        )
        dim = int(code.shape[1])
        score = transformed_margin(margin, dim, score_mode)
        label_score = np.where(pred == parent, score, -1.0 - np.abs(score)).astype(np.float64)
        out[source] = {
            "code": code,
            "parent": parent,
            "pred": pred,
            "margin": margin,
            "score": score.astype(np.float64),
            "label_score": label_score,
            "dist": dist,
            "dim": np.asarray(dim, dtype=np.int64),
        }
    return out


def build_stress_arrays(
    *,
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    keys: list[EventKey],
    score_mode: str,
    batch_size: int,
) -> dict[str, dict[str, np.ndarray]]:
    out: dict[str, dict[str, np.ndarray]] = {}
    for source in source_order:
        rows = [source_events[source][key] for key in keys]
        code = np.stack([parse_feature(row) for row in rows], axis=0).astype(np.int8)
        pred = np.asarray([int(row["stress_pred"]) for row in rows], dtype=np.int64)
        parent = np.asarray([int(row["parent"]) for row in rows], dtype=np.int64)
        margin = np.asarray([int(row.get("stress_margin", row.get("primary_margin", 0))) for row in rows], dtype=np.int64)
        payload = payloads[source]
        prototypes = np.asarray(payload["prototypes_int8"], dtype=np.int8)
        prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
        dist = class_distances(
            code,
            prototypes,
            prototype_parent,
            batch_size=batch_size,
        )
        dim = int(code.shape[1])
        score = transformed_margin(margin, dim, score_mode)
        out[source] = {
            "code": code,
            "parent": parent,
            "pred": pred,
            "margin": margin,
            "score": score.astype(np.float64),
            "dist": dist,
            "dim": np.asarray(dim, dtype=np.int64),
        }
    return out


def build_features(
    *,
    arrays: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    view_labels: np.ndarray,
    view_order: list[str],
    family_order: list[str],
    mode: str,
) -> np.ndarray:
    tokens = feature_tokens(mode)
    parts: list[np.ndarray] = []
    for source in source_order:
        item = arrays[source]
        dim = int(item["dim"])
        if "code" in tokens:
            parts.append(item["code"].astype(np.float32) / 128.0)
        if "dist" in tokens:
            parts.append(np.log1p(np.maximum(item["dist"].astype(np.float32), 0.0)) / math.sqrt(float(max(dim, 1))))
        if "margin" in tokens:
            margin = np.log1p(np.maximum(item["margin"].astype(np.float32), 0.0))
            parts.append((margin / math.sqrt(float(max(dim, 1))))[:, None])
        if "pred" in tokens:
            parts.append(one_hot(item["pred"].astype(np.int64), 3))
        if "score" in tokens:
            parts.append(item["score"].astype(np.float32)[:, None])
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


def build_model(input_dim: int, output_dim: int, hidden_dim: int, learning_rate: float, seed: int) -> tf.keras.Model:
    tf.keras.utils.set_random_seed(seed)
    inputs = tf.keras.Input((input_dim,), name="multisource_event_features")
    x = inputs
    if hidden_dim > 0:
        x = tf.keras.layers.Dense(hidden_dim, activation="relu", name="hidden")(x)
    outputs = tf.keras.layers.Dense(output_dim, name="source_logits")(x)
    model = tf.keras.Model(inputs, outputs, name="v8_multisource_event_gate_probe")
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=learning_rate),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
        metrics=[tf.keras.metrics.SparseCategoricalAccuracy(name="acc")],
    )
    return model


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
        description="Train a normal-only multisource event gate and evaluate source choice on high-pressure rows."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--feature-modes",
        default="margin,dist+margin+pred,code+dist+margin+pred,code+dist+margin+pred+family",
    )
    parser.add_argument("--score-modes", default="raw,log,per_sqrt_dim")
    parser.add_argument(
        "--label-modes",
        default="row_winner",
        help="Normal-only label modes: row_winner,sample_min,sample_correct_count,sample_family_min,sample_family_correct_count.",
    )
    parser.add_argument("--hidden-dims", default="0,16,32,64")
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--validation-mod", type=int, default=5)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source order mismatch: {source_order} != {normal_order}")

    source_events = {name: read_events(path) for name, path in source_items}
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_event_keys:
        raise ValueError("source event files have no common high-pressure events")
    base_rows = [source_events[source_order[0]][key] for key in common_event_keys]
    stress_view_labels = np.asarray([row["view_label"] for row in base_rows]).astype(str)

    normal_payloads = {name: load_npz(path) for name, path in normal_items}
    normal_maps = {name: normal_key_map(payload) for name, payload in normal_payloads.items()}
    normal_keys = sorted(set.intersection(*(set(mapping) for mapping in normal_maps.values())))
    if not normal_keys:
        raise ValueError("normal params have no common sample/view rows")
    base_normal = normal_payloads[source_order[0]]
    base_map = normal_maps[source_order[0]]
    base_indexes = np.asarray([base_map[key] for key in normal_keys], dtype=np.int64)
    normal_sample = np.asarray([key[0] for key in normal_keys], dtype=np.int64)
    normal_view_labels = np.asarray([key[1] for key in normal_keys]).astype(str)
    normal_parent = np.asarray(base_normal["parent"], dtype=np.int64)[base_indexes]
    view_order = list(dict.fromkeys(normal_view_labels.tolist()))
    family_order = list(dict.fromkeys(view_family(view) for view in normal_view_labels.tolist()))
    train_mask, val_mask = split_by_sample(normal_sample, args.validation_mod)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    for score_mode in parse_list(args.score_modes):
        normal_arrays = build_source_arrays(
            payloads=normal_payloads,
            source_order=source_order,
            keys=normal_keys,
            score_mode=score_mode,
            batch_size=args.batch_size,
        )
        stress_arrays = build_stress_arrays(
            source_events=source_events,
            payloads=normal_payloads,
            source_order=source_order,
            keys=common_event_keys,
            score_mode=score_mode,
            batch_size=args.batch_size,
        )
        score_matrix = np.stack([normal_arrays[source]["label_score"] for source in source_order], axis=1)
        for label_mode in parse_list(args.label_modes):
            labels = build_labels(
                score_matrix=score_matrix,
                sample_index=normal_sample,
                parent=normal_parent,
                view_labels=normal_view_labels,
                label_mode=label_mode,
            )
            class_counts = np.bincount(labels, minlength=len(source_order)).astype(np.float64)
            present = class_counts > 0
            class_weight = {
                index: float(np.mean(class_counts[present]) / max(class_counts[index], 1.0))
                for index in range(len(source_order))
            }
            for feature_mode in parse_list(args.feature_modes):
                x_normal = build_features(
                    arrays=normal_arrays,
                    source_order=source_order,
                    view_labels=normal_view_labels,
                    view_order=view_order,
                    family_order=family_order,
                    mode=feature_mode,
                )
                x_stress = build_features(
                    arrays=stress_arrays,
                    source_order=source_order,
                    view_labels=stress_view_labels,
                    view_order=view_order,
                    family_order=family_order,
                    mode=feature_mode,
                )
                mean = np.mean(x_normal[train_mask], axis=0, keepdims=True)
                std = np.maximum(np.std(x_normal[train_mask], axis=0, keepdims=True), 1.0e-6)
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
                    callbacks: list[tf.keras.callbacks.Callback] = [
                        tf.keras.callbacks.EarlyStopping(
                            monitor="val_loss" if np.any(val_mask) else "loss",
                            patience=10,
                            restore_best_weights=True,
                        )
                    ]
                    history = model.fit(
                        x_train_all[train_mask],
                        labels[train_mask],
                        validation_data=(x_train_all[val_mask], labels[val_mask]) if np.any(val_mask) else None,
                        epochs=args.epochs,
                        batch_size=args.batch_size,
                        verbose=0,
                        class_weight=class_weight,
                        callbacks=callbacks,
                    )
                    train_pred = np.argmax(
                        model.predict(x_train_all[train_mask], batch_size=args.batch_size, verbose=0),
                        axis=1,
                    )
                    train_acc = float(np.mean(train_pred == labels[train_mask]))
                    val_acc = None
                    if np.any(val_mask):
                        val_pred = np.argmax(
                            model.predict(x_train_all[val_mask], batch_size=args.batch_size, verbose=0),
                            axis=1,
                        )
                        val_acc = float(np.mean(val_pred == labels[val_mask]))
                    stress_logits = model.predict(x_stress_all, batch_size=args.batch_size, verbose=0)
                    selected = np.argmax(stress_logits, axis=1).astype(np.int64)
                    summary = summarize_selection(
                        selected=selected,
                        base_rows=base_rows,
                        common_keys=common_event_keys,
                        source_events=source_events,
                        source_order=source_order,
                    )
                    policy_rows.append(
                        {
                            "policy": (
                                f"multisource_event_gate:{label_mode}:{feature_mode}:"
                                f"{score_mode}:hidden{hidden_dim}"
                            ),
                            "selection_label_usage": "none",
                            "normal_training_usage": f"multisource normal {label_mode} labels",
                            "runtime_feature_usage": "all_source_embeddings_diagnostic",
                            "label_mode": label_mode,
                            "feature_mode": feature_mode,
                            "score_mode": score_mode,
                            "hidden_dim": int(hidden_dim),
                            "input_dim": int(x_train_all.shape[1]),
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
        "source_order": source_order,
        "common_normal_rows": int(len(normal_keys)),
        "common_high_pressure_events": int(len(common_event_keys)),
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "multisource normal label-mode gate only",
        "runtime_feature_usage": "all_source_embeddings_diagnostic_not_deployable",
        "normal_parent_counts": {
            str(parent): int(np.sum(normal_parent == parent))
            for parent in sorted(set(normal_parent.tolist()))
        },
        "view_order": view_order,
        "family_order": family_order,
        "settings": int(len(policy_rows)),
        "label_modes": parse_list(args.label_modes),
        "top_policies": policy_rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
