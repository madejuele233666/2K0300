import argparse
import itertools
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import metric_summary, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def save_npz(path: Path, payload: dict[str, np.ndarray]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(path, **payload)


def view_order_from_labels(view_labels: np.ndarray) -> list[str]:
    out: list[str] = []
    for item in view_labels.astype(str).tolist():
        if item not in out:
            out.append(item)
    if "clean" in out:
        out.remove("clean")
        out.insert(0, "clean")
    return out


def enumerate_weights(
    *,
    dim: int,
    max_weight: int,
    min_sum: int,
    max_sum: int,
    min_positive_dims: int,
    include_all_ones: bool,
) -> list[np.ndarray]:
    rows: list[tuple[int, ...]] = []
    if include_all_ones:
        rows.append(tuple([1] * dim))
    for values in itertools.product(range(max_weight + 1), repeat=dim):
        weight_sum = sum(values)
        if weight_sum < min_sum or weight_sum > max_sum:
            continue
        if sum(1 for item in values if item > 0) < min_positive_dims:
            continue
        rows.append(tuple(int(item) for item in values))
    unique = sorted(set(rows), key=lambda item: (sum(item), item))
    return [np.asarray(item, dtype=np.int32) for item in unique]


def class_distances_weighted(
    *,
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    weights: np.ndarray,
    batch_size: int,
) -> np.ndarray:
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    w = weights.astype(np.int32).reshape(1, 1, -1)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    rows: list[np.ndarray] = []
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum(((x[:, None, :] - p_all[None, :, :]) ** 2) * w, axis=2).astype(np.int64)
        cls = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes):
                cls[:, parent] = np.min(dist[:, indexes], axis=1)
        rows.append(cls)
    return np.concatenate(rows, axis=0)


def predict_from_class_dist(class_dist: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(class_dist, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (
        class_dist[np.arange(len(class_dist)), order[:, 1]]
        - class_dist[np.arange(len(class_dist)), order[:, 0]]
    ).astype(np.int64)
    return pred, margin


def low_margin_counts(margin: np.ndarray) -> dict[str, int]:
    return {
        "low_margin_le_1": int(np.sum(margin <= 1)),
        "low_margin_le_2": int(np.sum(margin <= 2)),
        "low_margin_le_4": int(np.sum(margin <= 4)),
        "low_margin_le_8": int(np.sum(margin <= 8)),
    }


def make_payload(
    *,
    base: dict[str, np.ndarray],
    pred: np.ndarray,
    margin: np.ndarray,
    weights: np.ndarray,
    source_name: str,
) -> dict[str, np.ndarray]:
    payload = dict(base)
    payload["pred"] = pred.astype(np.int64)
    payload["int8_pred"] = pred.astype(np.int64)
    payload["margin"] = margin.astype(np.float32)
    payload["int8_margin"] = margin.astype(np.int64)
    payload["metric_weights_int32"] = weights.astype(np.int32)
    payload["distance_metric"] = np.asarray("diag_weighted_int8_l2")
    payload["prototype_source_kind"] = np.asarray([source_name] * len(base["prototypes_int8"]))
    return payload


def candidate_name(weights: np.ndarray) -> str:
    return "diag_w_" + "_".join(str(int(item)) for item in weights.tolist())


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Search normal-only integer diagonal relevance weights for a V8 int8 prototype payload."
    )
    parser.add_argument("params_npz", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--train-config", type=Path, default=None)
    parser.add_argument("--max-weight", type=int, default=4)
    parser.add_argument("--min-sum", type=int, default=1)
    parser.add_argument("--max-sum", type=int, default=0, help="0 means feature_dim + 1")
    parser.add_argument("--min-positive-dims", type=int, default=1)
    parser.add_argument("--save-top", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    base = load_npz(args.params_npz)
    required = ["embedding_int8", "parent", "view_labels", "prototypes_int8", "prototype_parent"]
    missing = [key for key in required if key not in base]
    if missing:
        raise ValueError(f"{args.params_npz} missing required arrays: {missing}")

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    dim = int(embeddings.shape[1])
    max_sum = int(args.max_sum) if int(args.max_sum) > 0 else dim + 1
    weights_list = enumerate_weights(
        dim=dim,
        max_weight=int(args.max_weight),
        min_sum=int(args.min_sum),
        max_sum=max_sum,
        min_positive_dims=int(args.min_positive_dims),
        include_all_ones=True,
    )
    view_order = view_order_from_labels(view_labels)
    rows: list[dict[str, Any]] = []
    payloads: list[tuple[dict[str, Any], dict[str, np.ndarray]]] = []

    for weights in weights_list:
        class_dist = class_distances_weighted(
            embeddings=embeddings,
            prototypes=prototypes,
            prototype_parent=prototype_parent,
            weights=weights,
            batch_size=int(args.batch_size),
        )
        pred, margin = predict_from_class_dist(class_dist)
        name = candidate_name(weights)
        proto_count = int(len(prototypes))
        weight_sum = int(np.sum(weights))
        row: dict[str, Any] = {
            "stage": "v8_diag_metric_weight_search",
            "name": name,
            "feature_source": str(np.asarray(base.get("feature_source", np.asarray("unknown"))).reshape(-1)[0]),
            "prototype_source": name,
            "k_per_subclass": "",
            "feature_dim": dim,
            "prototype_count": proto_count,
            "metric_weights_json": json.dumps([int(item) for item in weights.tolist()], separators=(",", ":")),
            "metric_weight_sum": weight_sum,
            "estimated_distance_macs": int(proto_count * weight_sum),
            "estimated_float_table_bytes": int(proto_count * dim * 4),
            "estimated_int8_table_bytes": int(proto_count * dim + dim * 4),
            "margin_min": float(np.min(margin)),
            "margin_mean": float(np.mean(margin)),
            "int8_scale": float(np.asarray(base.get("int8_scale", np.asarray(1.0))).reshape(-1)[0]),
            "int8_flip_count": 0,
            "int8_margin_min": int(np.min(margin)),
            "int8_margin_mean": float(np.mean(margin)),
            "tflite_unique_ops": str(np.asarray(base.get("tflite_unique_ops", np.asarray(""))).reshape(-1)[0])
            if "tflite_unique_ops" in base
            else "",
            **low_margin_counts(margin),
        }
        row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
        row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred, prefix="int8_"))
        rows.append(row)
        payloads.append((row, make_payload(base=base, pred=pred, margin=margin, weights=weights, source_name=name)))

    def rank_key(item: tuple[dict[str, Any], dict[str, np.ndarray]]) -> tuple[Any, ...]:
        row = item[0]
        all_correct = bool(row["int8_clean_all_correct"]) and bool(row["int8_rotmirror_all_correct"]) and bool(row["int8_stress_all_correct"])
        return (
            all_correct,
            int(row["int8_margin_min"]),
            -int(row["low_margin_le_8"]),
            float(row["int8_margin_mean"]),
            -int(row["estimated_distance_macs"]),
        )

    payloads_sorted = sorted(payloads, key=rank_key, reverse=True)
    rows_sorted = [row for row, _payload in payloads_sorted]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)

    save_count = max(1, int(args.save_top))
    saved: list[dict[str, Any]] = []
    for index, (row, payload) in enumerate(payloads_sorted[:save_count]):
        path = args.output_dir / f"{row['prototype_source']}_parent_logits_params.npz"
        save_npz(path, payload)
        saved.append({"rank": index + 1, "prototype_source": row["prototype_source"], "params_npz": str(path)})
    best_row, best_payload = payloads_sorted[0]
    best_path = args.output_dir / "best_diag_metric_parent_logits_params.npz"
    save_npz(best_path, best_payload)

    if args.train_config is not None and args.train_config.exists():
        shutil.copy2(args.train_config, args.output_dir / "train_config.json")

    summary = {
        "params_npz": str(args.params_npz),
        "output_dir": str(args.output_dir),
        "candidate_count": len(rows_sorted),
        "best": best_row,
        "best_params_npz": str(best_path),
        "saved": saved,
        "selection": "normal_only_sorted_by_int8_margin_then_low_margin_count",
        "high_pressure_usage": "none",
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
