import argparse
import json
from pathlib import Path

import numpy as np

from run_v7_delta_merge_phase1 import PARENT_NAMES
from run_v7_delta_merge_phase2 import zfit
from run_v7_phase3_stress_aware_search import (
    ROT_MIRROR_VIEWS,
    raw_adapter_features,
    write_csv,
)


DEFAULT_PROFILE_NAMES = [
    "rot_old_wrong_plus_clean",
    "rot_old_wrong_plus_all_cleanrot",
    "all_old_wrong_plus_clean",
    "all_old_wrong_plus_cleanrot",
]


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def as_str_list(values: np.ndarray) -> list[str]:
    return [str(item) for item in values.tolist()]


def flatten_cache(cache: dict[str, np.ndarray], view_names: list[str]) -> dict[str, object]:
    all_views = as_str_list(cache["view_names"])
    indexes = [all_views.index(view) for view in view_names]
    n = len(cache["y_parent"])
    return {
        "views": view_names,
        "paths": as_str_list(cache["paths"]),
        "sample_index": np.tile(np.arange(n, dtype=np.int64), len(indexes)),
        "view_labels": np.asarray([view for view in view_names for _ in range(n)]),
        "y_parent": np.tile(cache["y_parent"].astype(np.int64), len(indexes)),
        "old_pred": cache["old_pred"][indexes].reshape(len(indexes) * n).astype(np.int64),
        "old_gap": cache["old_gap"][indexes].reshape(len(indexes) * n, cache["old_gap"].shape[-1]).astype(np.float64),
        "old_logits": cache["old_logits"][indexes].reshape(len(indexes) * n, cache["old_logits"].shape[-1]).astype(np.float64),
    }


def profile_mask(profile: str, flat: dict[str, object]) -> np.ndarray:
    view_labels = np.asarray(flat["view_labels"])
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    old_wrong = old_pred != y_parent
    rot = np.isin(view_labels, ROT_MIRROR_VIEWS)
    clean = view_labels == "clean"
    cleanrot = clean | rot
    if profile == "rot_old_wrong_plus_clean":
        return (old_wrong & rot) | clean
    if profile == "rot_old_wrong_plus_all_cleanrot":
        return (old_wrong & rot) | cleanrot
    if profile == "all_old_wrong_plus_clean":
        return old_wrong | clean
    if profile == "all_old_wrong_plus_cleanrot":
        return old_wrong | cleanrot
    if profile == "all_old_wrong":
        return old_wrong
    if profile == "all_views":
        return np.ones(len(old_wrong), dtype=bool)
    raise ValueError(f"unknown prototype profile: {profile}")


def class_distance_matrix(eval_features: np.ndarray, proto_features: np.ndarray, proto_labels: np.ndarray) -> np.ndarray:
    x2 = np.sum(eval_features * eval_features, axis=1, keepdims=True)
    p2 = np.sum(proto_features * proto_features, axis=1)
    dist = x2 + p2[None, :] - 2.0 * eval_features @ proto_features.T
    per_class: list[np.ndarray] = []
    for class_id in range(len(PARENT_NAMES)):
        mask = proto_labels == class_id
        if np.any(mask):
            per_class.append(np.min(dist[:, mask], axis=1))
        else:
            per_class.append(np.full(len(eval_features), np.inf))
    return np.stack(per_class, axis=1)


def per_view_metrics(
    *,
    view_order: list[str],
    view_labels: np.ndarray,
    y_parent: np.ndarray,
    pred: np.ndarray,
    gate: np.ndarray,
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for view in view_order:
        mask = view_labels == view
        correct = pred[mask] == y_parent[mask]
        rows.append(
            {
                "stress": view,
                "accuracy": float(np.mean(correct)),
                "correct": int(np.sum(correct)),
                "wrong": int(np.sum(~correct)),
                "gate_count": int(np.sum(gate[mask])),
            }
        )
    return rows


def row_score(row: dict[str, object]) -> tuple[object, ...]:
    return (
        bool(row["clean_all_correct"]),
        bool(row["rotmirror_all_correct"]),
        bool(row["stress_all_correct"]),
        int(row["clean_correct"]),
        float(row["rotmirror_min_accuracy"]),
        float(row["stress_min_accuracy"]),
        float(row["stress_mean_accuracy"]),
        -int(row["prototype_count"]),
        -int(row["gate_count"]),
        -int(row["feature_dim"]),
    )


def evaluate_candidate(
    *,
    profile: str,
    feature_name: str,
    flat: dict[str, object],
    view_order: list[str],
    threshold_grid: int,
) -> tuple[list[dict[str, object]], dict[str, np.ndarray]]:
    old_gap = np.asarray(flat["old_gap"], dtype=np.float64)
    old_logits = np.asarray(flat["old_logits"], dtype=np.float64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    raw = raw_adapter_features(feature_name, old_gap, old_logits)
    z, mean, std = zfit(raw)
    proto_mask = profile_mask(profile, flat)
    proto_features = z[proto_mask]
    proto_labels = y_parent[proto_mask]
    if len(proto_features) == 0:
        return [], {}
    dist_by_class = class_distance_matrix(z, proto_features, proto_labels)
    proto_pred = np.argmin(dist_by_class, axis=1).astype(np.int64)
    sorted_dist = np.sort(dist_by_class, axis=1)
    nearest_dist = sorted_dist[:, 0]
    proto_conf = sorted_dist[:, 1] - sorted_dist[:, 0]
    conf_values = np.unique(np.quantile(proto_conf, np.linspace(0.0, 1.0, threshold_grid)))
    dist_values = np.unique(np.quantile(nearest_dist, np.linspace(0.0, 1.0, threshold_grid)))
    rows: list[dict[str, object]] = []
    best_payload: dict[str, np.ndarray] = {}
    best_row: dict[str, object] | None = None
    rot_views = [view for view in ROT_MIRROR_VIEWS if view in set(view_order)]
    for conf_threshold in conf_values:
        for dist_threshold in dist_values:
            gate = (proto_pred != old_pred) & (proto_conf >= conf_threshold) & (nearest_dist <= dist_threshold)
            pred = np.where(gate, proto_pred, old_pred)
            per_view = per_view_metrics(
                view_order=view_order,
                view_labels=view_labels,
                y_parent=y_parent,
                pred=pred,
                gate=gate,
            )
            per_view_by_name = {str(item["stress"]): item for item in per_view}
            stress_rows = [item for item in per_view if item["stress"] != "clean"]
            rot_rows = [per_view_by_name[view] for view in rot_views]
            row = {
                "name": "phase6_prototype_rescue",
                "prototype_profile": profile,
                "feature_name": feature_name,
                "feature_dim": int(raw.shape[1]),
                "prototype_count": int(len(proto_features)),
                "conf_threshold": float(conf_threshold),
                "dist_threshold": float(dist_threshold),
                "clean_correct": int(per_view_by_name["clean"]["correct"]),
                "clean_total": int(per_view_by_name["clean"]["correct"]) + int(per_view_by_name["clean"]["wrong"]),
                "clean_accuracy": float(per_view_by_name["clean"]["accuracy"]),
                "clean_all_correct": int(per_view_by_name["clean"]["wrong"]) == 0,
                "rotmirror_min_accuracy": float(min(float(item["accuracy"]) for item in rot_rows)),
                "rotmirror_all_correct": all(int(item["wrong"]) == 0 for item in rot_rows),
                "stress_min_accuracy": float(min(float(item["accuracy"]) for item in stress_rows)),
                "stress_mean_accuracy": float(np.mean([float(item["accuracy"]) for item in stress_rows])),
                "stress_all_correct": all(int(item["wrong"]) == 0 for item in stress_rows),
                "gate_count": int(np.sum(gate)),
                "per_view_json": json.dumps(per_view, ensure_ascii=False),
                "estimated_distance_macs": int(len(proto_features) * raw.shape[1]),
                "estimated_float_table_bytes": int(len(proto_features) * raw.shape[1] * 4),
                "estimated_int8_table_bytes": int(len(proto_features) * raw.shape[1]),
            }
            rows.append(row)
            if best_row is None or row_score(row) > row_score(best_row):
                best_row = row
                best_payload = {
                    "feature_mean": mean.astype(np.float32),
                    "feature_std": std.astype(np.float32),
                    "prototype_features": proto_features.astype(np.float32),
                    "prototype_labels": proto_labels.astype(np.int64),
                    "prototype_view_labels": view_labels[proto_mask].astype(str),
                    "prototype_sample_index": np.asarray(flat["sample_index"], dtype=np.int64)[proto_mask],
                    "proto_pred": proto_pred.astype(np.int64),
                    "proto_conf": proto_conf.astype(np.float32),
                    "nearest_dist": nearest_dist.astype(np.float32),
                    "gate": gate.astype(bool),
                    "pred": pred.astype(np.int64),
                    "best_row_json": json.dumps(row, ensure_ascii=False),
                }
    return rows, best_payload


def main() -> None:
    parser = argparse.ArgumentParser(description="V7 phase6 nearest-prototype rescue over frozen old backbone features.")
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--profiles", default=",".join(DEFAULT_PROFILE_NAMES))
    parser.add_argument("--features", default="old_gap")
    parser.add_argument("--threshold-grid", type=int, default=121)
    args = parser.parse_args()

    with np.load(args.feature_cache, allow_pickle=True) as data:
        cache = {key: data[key] for key in data.files}
    view_order = as_str_list(cache["view_names"])
    flat = flatten_cache(cache, view_order)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    all_rows: list[dict[str, object]] = []
    best_row: dict[str, object] | None = None
    best_payload: dict[str, np.ndarray] = {}
    for feature_name in parse_csv(args.features):
        for profile in parse_csv(args.profiles):
            rows, payload = evaluate_candidate(
                profile=profile,
                feature_name=feature_name,
                flat=flat,
                view_order=view_order,
                threshold_grid=args.threshold_grid,
            )
            all_rows.extend(rows)
            for row in rows:
                if best_row is None or row_score(row) > row_score(best_row):
                    best_row = row
                    best_payload = payload
    rows_sorted = sorted(all_rows, key=row_score, reverse=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    summary = {
        "feature_cache": str(args.feature_cache),
        "profiles": parse_csv(args.profiles),
        "features": parse_csv(args.features),
        "threshold_grid": args.threshold_grid,
        "best": best_row,
        "top20": rows_sorted[:20],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    if best_row is None:
        return

    per_view = json.loads(str(best_row["per_view_json"]))
    write_csv(args.output_dir / "best_stress_summary.csv", per_view)
    gate = np.asarray(best_payload["gate"], dtype=bool)
    pred = np.asarray(best_payload["pred"], dtype=np.int64)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    old_pred = np.asarray(flat["old_pred"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    paths = list(flat["paths"])  # type: ignore[arg-type]
    sample_rows = []
    for index in np.where(gate | (pred != y_parent))[0].tolist():
        sample_rows.append(
            {
                "stress": str(view_labels[index]),
                "sample_index": int(sample_index[index]),
                "file": Path(str(paths[int(sample_index[index])])).name,
                "parent": PARENT_NAMES[int(y_parent[index])],
                "old_pred": PARENT_NAMES[int(old_pred[index])],
                "pred": PARENT_NAMES[int(pred[index])],
                "correct": bool(pred[index] == y_parent[index]),
                "gate": bool(gate[index]),
            }
        )
    write_csv(args.output_dir / "best_sample_events.csv", sample_rows)
    np.savez_compressed(
        args.output_dir / "best_v7_phase6_prototype_params.npz",
        **best_payload,
        feature_name=np.asarray(str(best_row["feature_name"])),
        prototype_profile=np.asarray(str(best_row["prototype_profile"])),
        conf_threshold=np.asarray(float(best_row["conf_threshold"]), dtype=np.float32),
        dist_threshold=np.asarray(float(best_row["dist_threshold"]), dtype=np.float32),
        view_order=np.asarray(view_order),
    )
    print(json.dumps({"best": best_row}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
