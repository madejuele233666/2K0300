import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import metric_summary, write_csv
from search_v8_diag_metric_weights import (
    class_distances_weighted,
    load_npz,
    low_margin_counts,
    make_payload,
    predict_from_class_dist,
    save_npz,
    view_order_from_labels,
    write_json,
)


def eval_weights(
    *,
    base: dict[str, np.ndarray],
    weights: np.ndarray,
    batch_size: int,
    view_order: list[str],
) -> tuple[dict[str, Any], dict[str, np.ndarray]]:
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    class_dist = class_distances_weighted(
        embeddings=embeddings,
        prototypes=prototypes,
        prototype_parent=prototype_parent,
        weights=weights,
        batch_size=batch_size,
    )
    pred, margin = predict_from_class_dist(class_dist)
    name = "diag_mask_" + "".join(str(int(item)) for item in weights.tolist())
    proto_count = int(len(prototypes))
    weight_sum = int(np.sum(weights))
    row: dict[str, Any] = {
        "stage": "v8_diag_metric_greedy_mask",
        "name": name,
        "feature_source": str(np.asarray(base.get("feature_source", np.asarray("unknown"))).reshape(-1)[0]),
        "prototype_source": name,
        "k_per_subclass": "",
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": proto_count,
        "metric_weights_json": json.dumps([int(item) for item in weights.tolist()], separators=(",", ":")),
        "metric_weight_sum": weight_sum,
        "estimated_distance_macs": int(proto_count * weight_sum),
        "estimated_float_table_bytes": int(proto_count * embeddings.shape[1] * 4),
        "estimated_int8_table_bytes": int(proto_count * embeddings.shape[1] + embeddings.shape[1] * 4),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "int8_scale": float(np.asarray(base.get("int8_scale", np.asarray(1.0))).reshape(-1)[0]),
        "int8_flip_count": 0,
        "int8_margin_min": int(np.min(margin)),
        "int8_margin_mean": float(np.mean(margin)),
        "tflite_unique_ops": "",
        **low_margin_counts(margin),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred, prefix="int8_"))
    payload = make_payload(base=base, pred=pred, margin=margin, weights=weights, source_name=name)
    return row, payload


def all_normal_correct(row: dict[str, Any]) -> bool:
    return bool(row["int8_clean_all_correct"]) and bool(row["int8_rotmirror_all_correct"]) and bool(row["int8_stress_all_correct"])


def row_rank(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        all_normal_correct(row),
        int(row["int8_margin_min"]),
        -int(row["low_margin_le_8"]),
        float(row["int8_margin_mean"]),
        -int(row["estimated_distance_macs"]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Greedily drop diagonal metric dimensions while preserving V8 normal int8 replay."
    )
    parser.add_argument("params_npz", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--train-config", type=Path, default=None)
    parser.add_argument("--target-sum", type=int, required=True)
    parser.add_argument("--min-margin", type=int, default=1)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    base = load_npz(args.params_npz)
    required = ["embedding_int8", "parent", "view_labels", "prototypes_int8", "prototype_parent"]
    missing = [key for key in required if key not in base]
    if missing:
        raise ValueError(f"{args.params_npz} missing required arrays: {missing}")
    dim = int(np.asarray(base["embedding_int8"]).shape[1])
    weights = np.ones(dim, dtype=np.int32)
    view_order = view_order_from_labels(np.asarray(base["view_labels"]).astype(str))
    rows: list[dict[str, Any]] = []
    saved: list[dict[str, Any]] = []

    row, payload = eval_weights(base=base, weights=weights, batch_size=int(args.batch_size), view_order=view_order)
    rows.append(row)
    best_payload = payload
    best_row = row
    accepted_rows: list[dict[str, Any]] = [row]

    while int(np.sum(weights)) > int(args.target_sum):
        candidates: list[tuple[dict[str, Any], dict[str, np.ndarray], np.ndarray, int]] = []
        for dim_index in np.where(weights > 0)[0].tolist():
            trial_weights = weights.copy()
            trial_weights[int(dim_index)] -= 1
            trial_row, trial_payload = eval_weights(
                base=base,
                weights=trial_weights,
                batch_size=int(args.batch_size),
                view_order=view_order,
            )
            trial_row["dropped_dim"] = int(dim_index)
            rows.append(trial_row)
            candidates.append((trial_row, trial_payload, trial_weights, int(dim_index)))
        candidates.sort(key=lambda item: row_rank(item[0]), reverse=True)
        next_row, next_payload, next_weights, dropped_dim = candidates[0]
        if not all_normal_correct(next_row) or int(next_row["int8_margin_min"]) < int(args.min_margin):
            break
        weights = next_weights
        accepted = dict(next_row)
        accepted["accepted_drop_dim"] = int(dropped_dim)
        accepted_rows.append(accepted)
        best_row = next_row
        best_payload = next_payload

    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows_sorted = sorted(rows, key=row_rank, reverse=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    write_csv(args.output_dir / "accepted_path.csv", accepted_rows)

    best_path = args.output_dir / "best_diag_mask_parent_logits_params.npz"
    save_npz(best_path, best_payload)
    for index, accepted in enumerate(accepted_rows):
        weights_arr = np.asarray(json.loads(str(accepted["metric_weights_json"])), dtype=np.int32)
        row_payload = eval_weights(base=base, weights=weights_arr, batch_size=int(args.batch_size), view_order=view_order)[1]
        path = args.output_dir / f"{accepted['prototype_source']}_parent_logits_params.npz"
        save_npz(path, row_payload)
        saved.append({"rank": index + 1, "prototype_source": accepted["prototype_source"], "params_npz": str(path)})

    if args.train_config is not None and args.train_config.exists():
        shutil.copy2(args.train_config, args.output_dir / "train_config.json")

    summary = {
        "params_npz": str(args.params_npz),
        "output_dir": str(args.output_dir),
        "target_sum": int(args.target_sum),
        "stopped_weight_sum": int(best_row["metric_weight_sum"]),
        "best": best_row,
        "best_params_npz": str(best_path),
        "accepted_path": accepted_rows,
        "saved": saved,
        "selection": "normal_only_greedy_drop_by_int8_margin_then_low_margin_count",
        "high_pressure_usage": "none",
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
