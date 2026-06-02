import argparse
import csv
import json
from pathlib import Path
from typing import Any

import numpy as np


PARENT_NAMES = ["supplies", "vehicle", "weapon"]
VISUAL_TO_PARENT = np.asarray([0, 0, 1, 1, 2, 2, 2, 2], dtype=np.int64)
ROT_MIRROR_VIEWS = [
    "rot90",
    "rot180",
    "rot270",
    "mirror_lr",
    "mirror_lr_rot90",
    "mirror_lr_rot180",
    "mirror_lr_rot270",
]


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def parse_floats(text: str) -> list[float]:
    return [float(item.strip()) for item in text.split(",") if item.strip()]


def as_str_list(values: np.ndarray) -> list[str]:
    return [str(item) for item in values.tolist()]


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
        for row in rows:
            writer.writerow(row)


def load_feature_cache(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def flatten_cache(cache: dict[str, np.ndarray], view_names: list[str] | None = None) -> dict[str, Any]:
    all_views = as_str_list(np.asarray(cache["view_names"]))
    selected = all_views if view_names is None else view_names
    indexes = [all_views.index(view) for view in selected]
    y_parent = np.asarray(cache["y_parent"], dtype=np.int64)
    y_sub = np.asarray(cache["y_sub"], dtype=np.int64)
    old_gap = np.asarray(cache["old_gap"], dtype=np.float64)[indexes]
    old_pred = np.asarray(cache["old_pred"], dtype=np.int64)[indexes]
    n = len(y_parent)
    view_labels = np.asarray([view for view in selected for _ in range(n)])
    return {
        "view_names": selected,
        "paths": as_str_list(np.asarray(cache["paths"])),
        "sample_index": np.tile(np.arange(n, dtype=np.int64), len(indexes)),
        "view_labels": view_labels,
        "old_gap": old_gap.reshape(len(indexes) * n, old_gap.shape[-1]),
        "old_pred": old_pred.reshape(len(indexes) * n),
        "y_parent": np.tile(y_parent, len(indexes)),
        "y_sub": np.tile(y_sub, len(indexes)),
    }


def zfit(values: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = np.mean(values, axis=0)
    std = np.std(values, axis=0) + 1.0e-6
    return (values - mean) / std, mean, std


def l2_normalize(values: np.ndarray) -> np.ndarray:
    norm = np.linalg.norm(values, axis=1, keepdims=True)
    return values / np.maximum(norm, 1.0e-8)


def pca_fit(values: np.ndarray, dim: int, whiten: bool) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = np.mean(values, axis=0)
    centered = values - mean
    _u, s, vt = np.linalg.svd(centered, full_matrices=False)
    components = vt[:dim].T
    scale = np.ones(dim, dtype=np.float64)
    if whiten:
        denom = np.maximum(s[:dim] / max(1.0, np.sqrt(len(values) - 1)), 1.0e-6)
        scale = 1.0 / denom
    return mean, components, scale


def pca_apply(values: np.ndarray, mean: np.ndarray, components: np.ndarray, scale: np.ndarray) -> np.ndarray:
    return (values - mean) @ components * scale


def orbit_reliability_weights(features: np.ndarray, sample_index: np.ndarray, view_labels: np.ndarray) -> np.ndarray:
    clean_or_d4 = (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    x = features[clean_or_d4]
    samples = sample_index[clean_or_d4]
    within_rows: list[np.ndarray] = []
    centers: list[np.ndarray] = []
    for sample in np.unique(samples):
        group = x[samples == sample]
        center = np.mean(group, axis=0)
        centers.append(center)
        within_rows.append(np.mean((group - center) ** 2, axis=0))
    within = np.mean(np.stack(within_rows), axis=0) + 1.0e-6
    between = np.var(np.stack(centers), axis=0) + 1.0e-6
    weights = np.sqrt(np.clip(between / within, 0.05, 20.0))
    return weights / np.mean(weights)


def transform_feature_set(flat: dict[str, Any], spec: str) -> tuple[np.ndarray, dict[str, Any]]:
    raw = np.asarray(flat["old_gap"], dtype=np.float64)
    view_labels = np.asarray(flat["view_labels"])
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    z, mean, std = zfit(raw)
    if spec == "gap_zscore_l2":
        return l2_normalize(z), {"kind": spec, "mean": mean, "std": std}
    if spec == "gap_zscore":
        return z, {"kind": spec, "mean": mean, "std": std}
    if spec == "gap_orbit_diag_l2":
        weights = orbit_reliability_weights(z, sample_index, view_labels)
        return l2_normalize(z * weights), {"kind": spec, "mean": mean, "std": std, "weights": weights}
    if spec.startswith("gap_pca_l2_"):
        dim = int(spec.rsplit("_", 1)[1])
        p_mean, components, scale = pca_fit(z, dim, whiten=False)
        return l2_normalize(pca_apply(z, p_mean, components, scale)), {
            "kind": spec,
            "mean": mean,
            "std": std,
            "pca_mean": p_mean,
            "components": components,
            "scale": scale,
        }
    if spec.startswith("gap_whiten_l2_"):
        dim = int(spec.rsplit("_", 1)[1])
        p_mean, components, scale = pca_fit(z, dim, whiten=True)
        return l2_normalize(pca_apply(z, p_mean, components, scale)), {
            "kind": spec,
            "mean": mean,
            "std": std,
            "pca_mean": p_mean,
            "components": components,
            "scale": scale,
        }
    raise ValueError(f"unknown transform spec: {spec}")


def kmeans(features: np.ndarray, k: int, seed: int, iterations: int = 35) -> tuple[np.ndarray, np.ndarray]:
    if len(features) <= k:
        labels = np.arange(len(features), dtype=np.int64)
        return features.copy(), labels
    rng = np.random.default_rng(seed)
    first = int(rng.integers(0, len(features)))
    centers = [features[first]]
    dist = np.sum((features - centers[0]) ** 2, axis=1)
    for _ in range(1, k):
        index = int(np.argmax(dist))
        centers.append(features[index])
        dist = np.minimum(dist, np.sum((features - centers[-1]) ** 2, axis=1))
    centers_arr = np.stack(centers).astype(np.float64)
    labels = np.zeros(len(features), dtype=np.int64)
    for _ in range(iterations):
        distances = squared_distances(features, centers_arr)
        labels = np.argmin(distances, axis=1).astype(np.int64)
        next_centers = centers_arr.copy()
        for cluster in range(k):
            mask = labels == cluster
            if np.any(mask):
                next_centers[cluster] = np.mean(features[mask], axis=0)
        if np.allclose(next_centers, centers_arr, atol=1.0e-7):
            break
        centers_arr = next_centers
    return centers_arr, labels


def squared_distances(values: np.ndarray, prototypes: np.ndarray) -> np.ndarray:
    x2 = np.sum(values * values, axis=1, keepdims=True)
    p2 = np.sum(prototypes * prototypes, axis=1)
    return x2 + p2[None, :] - 2.0 * values @ prototypes.T


def build_prototypes(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    y_sub: np.ndarray,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    source: str,
    k_per_subclass: int,
    seed: int,
) -> dict[str, np.ndarray]:
    proto_rows: list[np.ndarray] = []
    proto_parent: list[int] = []
    proto_sub: list[int] = []
    proto_cluster: list[int] = []
    proto_sample: list[int] = []
    proto_view: list[str] = []
    for subclass in sorted(np.unique(y_sub).astype(int).tolist()):
        mask = y_sub == subclass
        group = embeddings[mask]
        group_samples = sample_index[mask]
        group_views = view_labels[mask]
        group_parent = int(VISUAL_TO_PARENT[subclass])
        k = min(k_per_subclass, len(group))
        centers, cluster_labels = kmeans(group, k, seed + subclass * 1009 + k)
        for cluster in range(k):
            members = np.where(cluster_labels == cluster)[0]
            if len(members) == 0:
                continue
            if source == "kmeans":
                proto = centers[cluster]
                proto_sample_id = -1
                proto_view_id = "centroid"
            elif source == "medoid":
                local_dist = np.sum((group[members] - centers[cluster]) ** 2, axis=1)
                local_index = int(members[int(np.argmin(local_dist))])
                proto = group[local_index]
                proto_sample_id = int(group_samples[local_index])
                proto_view_id = str(group_views[local_index])
            else:
                raise ValueError(f"unknown prototype source: {source}")
            proto_rows.append(proto)
            proto_parent.append(group_parent)
            proto_sub.append(subclass)
            proto_cluster.append(cluster)
            proto_sample.append(proto_sample_id)
            proto_view.append(proto_view_id)
    return {
        "prototypes": np.stack(proto_rows).astype(np.float64),
        "prototype_parent": np.asarray(proto_parent, dtype=np.int64),
        "prototype_subclass": np.asarray(proto_sub, dtype=np.int64),
        "prototype_cluster": np.asarray(proto_cluster, dtype=np.int64),
        "prototype_sample_index": np.asarray(proto_sample, dtype=np.int64),
        "prototype_view_label": np.asarray(proto_view),
    }


def predict_closed(embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    dist = squared_distances(embeddings, prototypes)
    by_class: list[np.ndarray] = []
    for parent in range(len(PARENT_NAMES)):
        mask = prototype_parent == parent
        by_class.append(np.min(dist[:, mask], axis=1) if np.any(mask) else np.full(len(embeddings), np.inf))
    class_dist = np.stack(by_class, axis=1)
    pred = np.argmin(class_dist, axis=1).astype(np.int64)
    sorted_dist = np.sort(class_dist, axis=1)
    margin = sorted_dist[:, 1] - sorted_dist[:, 0]
    return pred, margin


def predict_with_exclusion(
    *,
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    prototype_sample_index: np.ndarray,
    prototype_view_label: np.ndarray,
    strict_original: bool,
) -> tuple[np.ndarray, np.ndarray]:
    pred = np.zeros(len(embeddings), dtype=np.int64)
    margin = np.zeros(len(embeddings), dtype=np.float64)
    for index, embedding in enumerate(embeddings):
        dist = np.sum((prototypes - embedding) ** 2, axis=1)
        if strict_original:
            exclude = prototype_sample_index == int(sample_index[index])
        else:
            exclude = (prototype_sample_index == int(sample_index[index])) & (prototype_view_label == str(view_labels[index]))
        if np.any(exclude):
            dist = dist.copy()
            dist[exclude] = np.inf
        class_dist = []
        for parent in range(len(PARENT_NAMES)):
            mask = prototype_parent == parent
            class_dist.append(float(np.min(dist[mask])) if np.any(mask) else float("inf"))
        class_dist_arr = np.asarray(class_dist, dtype=np.float64)
        pred[index] = int(np.argmin(class_dist_arr))
        sorted_dist = np.sort(class_dist_arr)
        margin[index] = float(sorted_dist[1] - sorted_dist[0])
    return pred, margin


def quantize(values: np.ndarray, scale: float) -> np.ndarray:
    return np.clip(np.rint(values * scale), -128, 127).astype(np.int8)


def predict_int8(embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    x = embeddings.astype(np.int32)
    p = prototypes.astype(np.int32)
    dist = np.sum((x[:, None, :] - p[None, :, :]) ** 2, axis=2)
    by_class: list[np.ndarray] = []
    for parent in range(len(PARENT_NAMES)):
        mask = prototype_parent == parent
        by_class.append(np.min(dist[:, mask], axis=1) if np.any(mask) else np.full(len(embeddings), np.iinfo(np.int32).max))
    class_dist = np.stack(by_class, axis=1)
    pred = np.argmin(class_dist, axis=1).astype(np.int64)
    sorted_dist = np.sort(class_dist, axis=1)
    margin = sorted_dist[:, 1].astype(np.int64) - sorted_dist[:, 0].astype(np.int64)
    return pred, margin


def per_view_metrics(view_order: list[str], view_labels: np.ndarray, y_parent: np.ndarray, pred: np.ndarray) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for view in view_order:
        mask = view_labels == view
        correct = pred[mask] == y_parent[mask]
        rows.append(
            {
                "stress": view,
                "accuracy": float(np.mean(correct)),
                "correct": int(np.sum(correct)),
                "wrong": int(np.sum(~correct)),
            }
        )
    return rows


def metric_summary(
    *,
    view_order: list[str],
    view_labels: np.ndarray,
    y_parent: np.ndarray,
    pred: np.ndarray,
    prefix: str = "",
) -> dict[str, Any]:
    per_view = per_view_metrics(view_order, view_labels, y_parent, pred)
    by_name = {str(row["stress"]): row for row in per_view}
    rot_rows = [by_name[view] for view in ROT_MIRROR_VIEWS if view in by_name]
    stress_rows = [row for row in per_view if row["stress"] != "clean"]
    fixed_stress_rows = [row for row in stress_rows if row["stress"] not in set(ROT_MIRROR_VIEWS)]
    clean = by_name["clean"]
    rotmirror_min_accuracy = float(min(float(row["accuracy"]) for row in rot_rows)) if rot_rows else 1.0
    rotmirror_all_correct = all(int(row["wrong"]) == 0 for row in rot_rows) if rot_rows else True
    stress_min_accuracy = float(min(float(row["accuracy"]) for row in stress_rows)) if stress_rows else 1.0
    stress_mean_accuracy = float(np.mean([float(row["accuracy"]) for row in stress_rows])) if stress_rows else 1.0
    stress_all_correct = all(int(row["wrong"]) == 0 for row in stress_rows) if stress_rows else True
    out = {
        f"{prefix}clean_correct": int(clean["correct"]),
        f"{prefix}clean_total": int(clean["correct"]) + int(clean["wrong"]),
        f"{prefix}clean_accuracy": float(clean["accuracy"]),
        f"{prefix}clean_all_correct": int(clean["wrong"]) == 0,
        f"{prefix}rotmirror_min_accuracy": rotmirror_min_accuracy,
        f"{prefix}rotmirror_all_correct": rotmirror_all_correct,
        f"{prefix}stress_min_accuracy": stress_min_accuracy,
        f"{prefix}stress_mean_accuracy": stress_mean_accuracy,
        f"{prefix}stress_all_correct": stress_all_correct,
        f"{prefix}fixed_stress_min_accuracy": float(min(float(row["accuracy"]) for row in fixed_stress_rows)) if fixed_stress_rows else 1.0,
        f"{prefix}fixed_stress_all_correct": all(int(row["wrong"]) == 0 for row in fixed_stress_rows),
    }
    out[f"{prefix}per_view_json" if prefix else "per_view_json"] = json.dumps(per_view, ensure_ascii=False)
    return out


def evaluate_prototype_candidate(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    transform_name: str,
    source: str,
    k_per_subclass: int,
    seed: int,
    quant_scales: list[float],
) -> tuple[dict[str, Any], dict[str, np.ndarray]]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    view_order = list(flat["view_names"])
    proto = build_prototypes(
        embeddings=embeddings,
        y_parent=y_parent,
        y_sub=y_sub,
        sample_index=sample_index,
        view_labels=view_labels,
        source=source,
        k_per_subclass=k_per_subclass,
        seed=seed,
    )
    pred, margin = predict_closed(embeddings, proto["prototypes"], proto["prototype_parent"])
    no_self_pred, no_self_margin = predict_with_exclusion(
        embeddings=embeddings,
        prototypes=proto["prototypes"],
        prototype_parent=proto["prototype_parent"],
        sample_index=sample_index,
        view_labels=view_labels,
        prototype_sample_index=proto["prototype_sample_index"],
        prototype_view_label=proto["prototype_view_label"],
        strict_original=False,
    )
    approx_loo_pred, approx_loo_margin = predict_with_exclusion(
        embeddings=embeddings,
        prototypes=proto["prototypes"],
        prototype_parent=proto["prototype_parent"],
        sample_index=sample_index,
        view_labels=view_labels,
        prototype_sample_index=proto["prototype_sample_index"],
        prototype_view_label=proto["prototype_view_label"],
        strict_original=True,
    )
    row: dict[str, Any] = {
        "name": "v8_phaseA_embedding_prototype",
        "transform": transform_name,
        "prototype_source": source,
        "k_per_subclass": k_per_subclass,
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(len(proto["prototypes"])),
        "estimated_distance_macs": int(len(proto["prototypes"]) * embeddings.shape[1]),
        "estimated_float_table_bytes": int(len(proto["prototypes"]) * embeddings.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(proto["prototypes"]) * embeddings.shape[1]),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=no_self_pred, prefix="no_self_"))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=approx_loo_pred, prefix="approx_loo_"))
    # Backward-compatible aliases for older reports. For centroid/kmeans prototypes this is not
    # a true strict LOO because the held-out original may still influence the centroid.
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=approx_loo_pred, prefix="strict_loo_"))
    row["no_self_margin_min"] = float(np.min(no_self_margin))
    row["approx_loo_margin_min"] = float(np.min(approx_loo_margin))
    row["strict_loo_margin_min"] = float(np.min(approx_loo_margin))
    row["strict_loo_is_true_rebuild"] = False
    row["strict_loo_eval_kind"] = "deprecated_sample_exclusion_approx"

    best_int8: dict[str, Any] | None = None
    best_int8_arrays: dict[str, np.ndarray] = {}
    for scale in quant_scales:
        z_q = quantize(embeddings, scale)
        p_q = quantize(proto["prototypes"], scale)
        int8_pred, int8_margin = predict_int8(z_q, p_q, proto["prototype_parent"])
        int8_row: dict[str, Any] = {
            "int8_scale": float(scale),
            "int8_flip_count": int(np.sum(int8_pred != pred)),
            "int8_margin_min": int(np.min(int8_margin)),
            "int8_margin_mean": float(np.mean(int8_margin)),
        }
        int8_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
        if best_int8 is None or int8_score(int8_row) > int8_score(best_int8):
            best_int8 = int8_row
            best_int8_arrays = {"embedding_int8": z_q, "prototypes_int8": p_q, "int8_pred": int8_pred}
    assert best_int8 is not None
    row.update(best_int8)
    payload = {
        "embedding_float": embeddings.astype(np.float32),
        "parent": y_parent.astype(np.int64),
        "subclass": y_sub.astype(np.int64),
        "sample_index": sample_index.astype(np.int64),
        "view_labels": view_labels.astype(str),
        "paths": np.asarray(flat["paths"]),
        "pred": pred.astype(np.int64),
        "margin": margin.astype(np.float32),
        "no_self_pred": no_self_pred.astype(np.int64),
        "approx_loo_pred": approx_loo_pred.astype(np.int64),
        "strict_loo_pred": approx_loo_pred.astype(np.int64),
        **proto,
        **best_int8_arrays,
    }
    return row, payload


def true_rebuild_loo(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    row: dict[str, Any],
    seed: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    paths = np.asarray(flat["paths"]).astype(str)
    source = str(row["prototype_source"])
    k_per_subclass = int(row["k_per_subclass"])
    int8_scale = float(row["int8_scale"])
    loo_pred = np.zeros(len(embeddings), dtype=np.int64)
    loo_margin = np.zeros(len(embeddings), dtype=np.float64)
    loo_int8_pred = np.zeros(len(embeddings), dtype=np.int64)
    loo_int8_margin = np.zeros(len(embeddings), dtype=np.int64)

    for sample in np.unique(sample_index):
        train_mask = sample_index != int(sample)
        query_mask = sample_index == int(sample)
        proto = build_prototypes(
            embeddings=embeddings[train_mask],
            y_parent=y_parent[train_mask],
            y_sub=y_sub[train_mask],
            sample_index=sample_index[train_mask],
            view_labels=view_labels[train_mask],
            source=source,
            k_per_subclass=k_per_subclass,
            seed=seed,
        )
        pred, margin = predict_closed(embeddings[query_mask], proto["prototypes"], proto["prototype_parent"])
        loo_pred[query_mask] = pred
        loo_margin[query_mask] = margin
        int8_pred, int8_margin = predict_int8(
            quantize(embeddings[query_mask], int8_scale),
            quantize(proto["prototypes"], int8_scale),
            proto["prototype_parent"],
        )
        loo_int8_pred[query_mask] = int8_pred
        loo_int8_margin[query_mask] = int8_margin

    summary = metric_summary(
        view_order=view_order,
        view_labels=view_labels,
        y_parent=y_parent,
        pred=loo_pred,
        prefix="true_rebuild_loo_",
    )
    summary.update(
        metric_summary(
            view_order=view_order,
            view_labels=view_labels,
            y_parent=y_parent,
            pred=loo_int8_pred,
            prefix="true_rebuild_loo_int8_",
        )
    )
    summary["true_rebuild_loo_margin_min"] = float(np.min(loo_margin))
    summary["true_rebuild_loo_margin_mean"] = float(np.mean(loo_margin))
    summary["true_rebuild_loo_int8_margin_min"] = int(np.min(loo_int8_margin))
    summary["true_rebuild_loo_int8_margin_mean"] = float(np.mean(loo_int8_margin))
    summary["true_rebuild_loo_eval_kind"] = "rebuild_leave_one_original_out"

    events: list[dict[str, Any]] = []
    event_indexes = np.where((loo_pred != y_parent) | (loo_int8_pred != y_parent))[0]
    for index in event_indexes:
        sample = int(sample_index[index])
        events.append(
            {
                "sample_index": sample,
                "path": str(paths[sample]),
                "view": str(view_labels[index]),
                "parent": PARENT_NAMES[int(y_parent[index])],
                "true_rebuild_loo_pred": PARENT_NAMES[int(loo_pred[index])],
                "true_rebuild_loo_int8_pred": PARENT_NAMES[int(loo_int8_pred[index])],
                "is_true_rebuild_loo_wrong": bool(loo_pred[index] != y_parent[index]),
                "is_true_rebuild_loo_int8_wrong": bool(loo_int8_pred[index] != y_parent[index]),
                "margin": float(loo_margin[index]),
                "int8_margin": int(loo_int8_margin[index]),
            }
        )
    return summary, events


def int8_score(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        bool(row["int8_clean_all_correct"]),
        bool(row["int8_rotmirror_all_correct"]),
        bool(row["int8_stress_all_correct"]),
        int(row["int8_clean_correct"]),
        float(row["int8_rotmirror_min_accuracy"]),
        float(row["int8_stress_min_accuracy"]),
        -int(row["int8_flip_count"]),
        int(row["int8_margin_min"]),
    )


def row_score(row: dict[str, Any]) -> tuple[Any, ...]:
    loo_stress = float(row.get("true_rebuild_loo_stress_min_accuracy", row.get("approx_loo_stress_min_accuracy", row["strict_loo_stress_min_accuracy"])))
    return (
        bool(row["clean_all_correct"]),
        bool(row["rotmirror_all_correct"]),
        bool(row["stress_all_correct"]),
        bool(row["int8_clean_all_correct"]),
        bool(row["int8_rotmirror_all_correct"]),
        bool(row["int8_stress_all_correct"]),
        int(row["clean_correct"]),
        float(row["rotmirror_min_accuracy"]),
        float(row["stress_min_accuracy"]),
        float(row["fixed_stress_min_accuracy"]),
        float(row["no_self_stress_min_accuracy"]),
        loo_stress,
        -int(row["prototype_count"]),
        -int(row["estimated_distance_macs"]),
        float(row["margin_min"]),
        int(row["int8_margin_min"]),
    )


def default_transform_specs(feature_dim: int) -> list[str]:
    specs = ["gap_zscore_l2", "gap_zscore", "gap_orbit_diag_l2"]
    for dim in [16, min(24, feature_dim), feature_dim]:
        if dim <= feature_dim and f"gap_pca_l2_{dim}" not in specs:
            specs.append(f"gap_pca_l2_{dim}")
        if dim <= feature_dim and f"gap_whiten_l2_{dim}" not in specs:
            specs.append(f"gap_whiten_l2_{dim}")
    return specs


def run_transform_sweep(
    *,
    flat: dict[str, Any],
    output_dir: Path,
    transform_specs: list[str],
    prototype_sources: list[str],
    k_values: list[int],
    quant_scales: list[float],
    seed: int,
    name: str,
    true_loo_top: int = 0,
) -> tuple[list[dict[str, Any]], dict[str, np.ndarray]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    for spec in transform_specs:
        embeddings, transform_payload = transform_feature_set(flat, spec)
        for source in prototype_sources:
            for k_value in k_values:
                row, payload = evaluate_prototype_candidate(
                    embeddings=embeddings,
                    flat=flat,
                    transform_name=spec,
                    source=source,
                    k_per_subclass=k_value,
                    seed=seed,
                    quant_scales=quant_scales,
                )
                row["stage"] = name
                rows.append(row)
                payload.update({f"transform_{key}": value for key, value in transform_payload.items() if isinstance(value, np.ndarray)})
                payload["transform_kind"] = np.asarray(spec)
                payloads.append(payload)
    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: row_score(item[0]), reverse=True)
    true_loo_rows: list[dict[str, Any]] = []
    true_loo_events: list[dict[str, Any]] = []
    for rank, (row, payload) in enumerate(sorted_pairs[: max(0, true_loo_top)], start=1):
        loo_summary, events = true_rebuild_loo(
            embeddings=np.asarray(payload["embedding_float"], dtype=np.float64),
            flat=flat,
            row=row,
            seed=seed,
        )
        row["true_loo_rank"] = rank
        row.update(loo_summary)
        out_row = dict(row)
        true_loo_rows.append(out_row)
        for event in events:
            event.update(
                {
                    "true_loo_rank": rank,
                    "prototype_source": row["prototype_source"],
                    "k_per_subclass": row["k_per_subclass"],
                    "prototype_count": row["prototype_count"],
                    "int8_scale": row["int8_scale"],
                }
            )
            true_loo_events.append(event)
    rows_sorted = [row for row, _payload in sorted_pairs]
    best_payload = sorted_pairs[0][1] if sorted_pairs else {}
    write_csv(output_dir / "candidate_results.csv", rows_sorted)
    if true_loo_rows:
        write_csv(output_dir / "true_rebuild_loo_top.csv", true_loo_rows)
        write_csv(output_dir / "true_rebuild_loo_events.csv", true_loo_events)
    if rows_sorted:
        write_csv(output_dir / "best_stress_summary.csv", json.loads(str(rows_sorted[0]["per_view_json"])))
        np.savez_compressed(output_dir / "best_v8_embedding_prototype_params.npz", **best_payload)
    summary = {
        "stage": name,
        "candidate_count": len(rows_sorted),
        "true_loo_top": int(max(0, true_loo_top)),
        "true_loo_top_results": true_loo_rows,
        "best": rows_sorted[0] if rows_sorted else None,
        "top20": rows_sorted[:20],
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    return rows_sorted, best_payload


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate V8 embedding/prototype compression on a frozen feature cache.")
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--transforms", default="")
    parser.add_argument("--prototype-sources", default="medoid,kmeans")
    parser.add_argument("--k-values", default="1,2,4,8,16")
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--seed", type=int, default=20260519)
    parser.add_argument("--true-loo-top", type=int, default=0)
    args = parser.parse_args()

    cache = load_feature_cache(args.feature_cache)
    flat = flatten_cache(cache)
    feature_dim = int(np.asarray(flat["old_gap"]).shape[1])
    transforms = parse_csv(args.transforms) if args.transforms else default_transform_specs(feature_dim)
    rows, _payload = run_transform_sweep(
        flat=flat,
        output_dir=args.output_dir,
        transform_specs=transforms,
        prototype_sources=parse_csv(args.prototype_sources),
        k_values=parse_ints(args.k_values),
        quant_scales=parse_floats(args.quant_scales),
        seed=args.seed,
        name="a0_no_training_compression",
        true_loo_top=args.true_loo_top,
    )
    print(json.dumps({"best": rows[0] if rows else None, "candidate_count": len(rows)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
