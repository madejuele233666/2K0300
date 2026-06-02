import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import (
    PARENT_NAMES,
    ROT_MIRROR_VIEWS,
    VISUAL_TO_PARENT,
    int8_score,
    kmeans,
    metric_summary,
    parse_csv,
    parse_floats,
    parse_ints,
    predict_closed,
    predict_int8,
    predict_with_exclusion,
    quantize,
    squared_distances,
    write_csv,
)


def ordered_unique(values: np.ndarray) -> list[str]:
    out: list[str] = []
    for value in values.astype(str).tolist():
        if value not in out:
            out.append(value)
    return out


def extended_row_score(row: dict[str, Any]) -> tuple[Any, ...]:
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
        float(row["approx_loo_stress_min_accuracy"]),
        -int(row["prototype_count"]),
        -int(row["estimated_distance_macs"]),
        float(row["margin_min"]),
        int(row["int8_margin_min"]),
    )


def load_embedding_params(path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    with np.load(path, allow_pickle=True) as data:
        required = ["embedding_float", "parent", "subclass", "sample_index", "view_labels", "paths"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing required arrays: {missing}")
        embeddings = np.asarray(data["embedding_float"], dtype=np.float32)
        view_labels = np.asarray(data["view_labels"]).astype(str)
        flat = {
            "view_names": ordered_unique(view_labels),
            "paths": np.asarray(data["paths"]).astype(str),
            "sample_index": np.asarray(data["sample_index"], dtype=np.int64),
            "view_labels": view_labels,
            "y_parent": np.asarray(data["parent"], dtype=np.int64),
            "y_sub": np.asarray(data["subclass"], dtype=np.int64),
        }
    return embeddings, flat


def kcenter(features: np.ndarray, k: int, seed: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    if len(features) <= k:
        indexes = np.arange(len(features), dtype=np.int64)
        return features.copy(), indexes.copy(), indexes
    rng = np.random.default_rng(seed)
    first = int(rng.integers(0, len(features)))
    selected = [first]
    dist = np.sum((features - features[first]) ** 2, axis=1)
    for _ in range(1, k):
        index = int(np.argmax(dist))
        selected.append(index)
        dist = np.minimum(dist, np.sum((features - features[index]) ** 2, axis=1))
    centers = features[np.asarray(selected, dtype=np.int64)]
    labels = np.argmin(squared_distances(features, centers), axis=1).astype(np.int64)
    return centers.astype(np.float64), labels, np.asarray(selected, dtype=np.int64)


def quantization_error(values: np.ndarray, scale: float) -> np.ndarray:
    clipped = np.clip(np.rint(values * scale), -128, 127) / scale
    return np.sum((values - clipped) ** 2, axis=1)


def build_prototypes_ext(
    *,
    embeddings: np.ndarray,
    y_sub: np.ndarray,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    source: str,
    k_per_subclass: int,
    seed: int,
    quant_stable_scale: float,
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
        source_seed = seed + subclass * 1009 + k
        if source == "kcenter":
            centers, cluster_labels, center_indexes = kcenter(group, k, source_seed)
        else:
            centers, cluster_labels = kmeans(group, k, source_seed)
            center_indexes = np.full(k, -1, dtype=np.int64)
        for cluster in range(k):
            members = np.where(cluster_labels == cluster)[0]
            if len(members) == 0:
                continue
            if source == "kmeans":
                proto = centers[cluster]
                proto_sample_id = -1
                proto_view_id = "centroid"
            elif source in {"medoid", "quant_medoid"}:
                local_dist = np.sum((group[members] - centers[cluster]) ** 2, axis=1)
                if source == "quant_medoid":
                    qerr = quantization_error(group[members], quant_stable_scale)
                    local_dist = local_dist + 0.05 * qerr
                local_index = int(members[int(np.argmin(local_dist))])
                proto = group[local_index]
                proto_sample_id = int(group_samples[local_index])
                proto_view_id = str(group_views[local_index])
            elif source == "kcenter":
                local_index = int(center_indexes[cluster])
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


def evaluate_candidate_ext(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    transform_name: str,
    source: str,
    k_per_subclass: int,
    seed: int,
    quant_scales: list[float],
    quant_stable_scale: float,
) -> tuple[dict[str, Any], dict[str, np.ndarray]]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    proto = build_prototypes_ext(
        embeddings=embeddings,
        y_sub=y_sub,
        sample_index=sample_index,
        view_labels=view_labels,
        source=source,
        k_per_subclass=k_per_subclass,
        seed=seed,
        quant_stable_scale=quant_stable_scale,
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
        "name": "v8_extended_embedding_prototype",
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
    row["no_self_margin_min"] = float(np.min(no_self_margin))
    row["approx_loo_margin_min"] = float(np.min(approx_loo_margin))

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
        **proto,
        **best_int8_arrays,
    }
    return row, payload


def class_distances(embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> np.ndarray:
    dist = squared_distances(embeddings, prototypes)
    by_parent: list[np.ndarray] = []
    for parent in range(len(PARENT_NAMES)):
        mask = prototype_parent == parent
        by_parent.append(np.min(dist[:, mask], axis=1) if np.any(mask) else np.full(len(embeddings), np.inf))
    return np.stack(by_parent, axis=1)


def boundary_events(payload: dict[str, np.ndarray], low_margin_quantile: float) -> list[dict[str, Any]]:
    y_parent = np.asarray(payload["parent"], dtype=np.int64)
    pred = np.asarray(payload["pred"], dtype=np.int64)
    int8_pred = np.asarray(payload["int8_pred"], dtype=np.int64)
    approx_loo_pred = np.asarray(payload["approx_loo_pred"], dtype=np.int64)
    margin = np.asarray(payload["margin"], dtype=np.float64)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    paths = np.asarray(payload["paths"]).astype(str)
    prototypes = np.asarray(payload["prototypes"], dtype=np.float64)
    prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
    class_dist = class_distances(np.asarray(payload["embedding_float"], dtype=np.float64), prototypes, prototype_parent)
    correct_dist = class_dist[np.arange(len(y_parent)), y_parent]
    wrong_dist = np.min(np.where(np.arange(len(PARENT_NAMES))[None, :] == y_parent[:, None], np.inf, class_dist), axis=1)
    threshold = float(np.quantile(margin, low_margin_quantile))
    event_mask = (pred != y_parent) | (int8_pred != pred) | (approx_loo_pred != y_parent) | (margin <= threshold)
    rows: list[dict[str, Any]] = []
    for index in np.where(event_mask)[0]:
        sample = int(sample_index[index])
        rows.append(
            {
                "event_index": int(index),
                "sample_index": sample,
                "path": str(paths[sample]),
                "view": str(view_labels[index]),
                "parent": PARENT_NAMES[int(y_parent[index])],
                "pred": PARENT_NAMES[int(pred[index])],
                "int8_pred": PARENT_NAMES[int(int8_pred[index])],
                "approx_loo_pred": PARENT_NAMES[int(approx_loo_pred[index])],
                "is_wrong": bool(pred[index] != y_parent[index]),
                "is_int8_flip": bool(int8_pred[index] != pred[index]),
                "is_approx_loo_wrong": bool(approx_loo_pred[index] != y_parent[index]),
                "margin": float(margin[index]),
                "correct_parent_dist": float(correct_dist[index]),
                "nearest_wrong_parent_dist": float(wrong_dist[index]),
                "low_margin_threshold": threshold,
            }
        )
    return rows


def true_rebuild_loo(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    row: dict[str, Any],
    seed: int,
    quant_stable_scale: float,
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
    events: list[dict[str, Any]] = []
    for sample in np.unique(sample_index):
        train_mask = sample_index != int(sample)
        query_mask = sample_index == int(sample)
        proto = build_prototypes_ext(
            embeddings=embeddings[train_mask],
            y_sub=y_sub[train_mask],
            sample_index=sample_index[train_mask],
            view_labels=view_labels[train_mask],
            source=source,
            k_per_subclass=k_per_subclass,
            seed=seed,
            quant_stable_scale=quant_stable_scale,
        )
        pred, margin = predict_closed(embeddings[query_mask], proto["prototypes"], proto["prototype_parent"])
        loo_pred[query_mask] = pred
        loo_margin[query_mask] = margin
        int8_pred, _int8_margin = predict_int8(
            quantize(embeddings[query_mask], int8_scale),
            quantize(proto["prototypes"], int8_scale),
            proto["prototype_parent"],
        )
        loo_int8_pred[query_mask] = int8_pred
    summary = metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=loo_pred, prefix="true_rebuild_loo_")
    summary.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=loo_int8_pred, prefix="true_rebuild_loo_int8_"))
    summary["true_rebuild_loo_margin_min"] = float(np.min(loo_margin))
    summary["true_rebuild_loo_margin_mean"] = float(np.mean(loo_margin))
    summary["true_rebuild_loo_eval_kind"] = "rebuild_leave_one_original_out"
    for index in np.where((loo_pred != y_parent) | (loo_int8_pred != y_parent))[0]:
        sample = int(sample_index[index])
        events.append(
            {
                "sample_index": sample,
                "path": str(paths[sample]),
                "view": str(view_labels[index]),
                "parent": PARENT_NAMES[int(y_parent[index])],
                "true_rebuild_loo_pred": PARENT_NAMES[int(loo_pred[index])],
                "true_rebuild_loo_int8_pred": PARENT_NAMES[int(loo_int8_pred[index])],
                "margin": float(loo_margin[index]),
            }
        )
    return summary, events


def main() -> None:
    parser = argparse.ArgumentParser(description="Run V8 extended prototype budget and boundary diagnostics.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sources", default="kmeans,medoid,kcenter,quant_medoid")
    parser.add_argument("--k-values", default="16,24,32,48,64,96,128,192")
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--quant-stable-scale", type=float, default=32.0)
    parser.add_argument("--seed", type=int, default=20260519)
    parser.add_argument("--true-loo-top", type=int, default=0)
    parser.add_argument("--boundary-low-margin-quantile", type=float, default=0.01)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    embeddings, flat = load_embedding_params(args.params_npz)
    sources = parse_csv(args.sources)
    k_values = parse_ints(args.k_values)
    quant_scales = parse_floats(args.quant_scales)
    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    for source in sources:
        for k_value in k_values:
            row, payload = evaluate_candidate_ext(
                embeddings=embeddings,
                flat=flat,
                transform_name=args.params_npz.parent.name,
                source=source,
                k_per_subclass=k_value,
                seed=args.seed,
                quant_scales=quant_scales,
                quant_stable_scale=args.quant_stable_scale,
            )
            row["stage"] = "v8_extended_prototype_sweep"
            rows.append(row)
            payloads.append(payload)
            print(
                json.dumps(
                    {
                        "source": source,
                        "k": k_value,
                        "prototype_count": row["prototype_count"],
                        "clean": row["clean_accuracy"],
                        "stress_min": row["stress_min_accuracy"],
                        "int8_stress_min": row["int8_stress_min_accuracy"],
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: extended_row_score(item[0]), reverse=True)
    rows_sorted = [row for row, _payload in sorted_pairs]
    write_csv(args.output_dir / "extended_candidate_results.csv", rows_sorted)
    best_payload = sorted_pairs[0][1] if sorted_pairs else {}
    if rows_sorted:
        write_csv(args.output_dir / "best_stress_summary.csv", json.loads(str(rows_sorted[0]["per_view_json"])))
        write_csv(args.output_dir / "best_boundary_events.csv", boundary_events(best_payload, args.boundary_low_margin_quantile))
        np.savez_compressed(args.output_dir / "best_extended_v8_prototype_params.npz", **best_payload)

    true_loo_rows: list[dict[str, Any]] = []
    true_loo_events: list[dict[str, Any]] = []
    for rank, (row, _payload) in enumerate(sorted_pairs[: max(0, args.true_loo_top)], start=1):
        loo_summary, events = true_rebuild_loo(
            embeddings=embeddings,
            flat=flat,
            row=row,
            seed=args.seed,
            quant_stable_scale=args.quant_stable_scale,
        )
        out_row = dict(row)
        out_row["true_loo_rank"] = rank
        out_row.update(loo_summary)
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
        print(json.dumps({"true_rebuild_loo_rank": rank, **loo_summary}, ensure_ascii=False), flush=True)
    if true_loo_rows:
        write_csv(args.output_dir / "true_rebuild_loo_top.csv", true_loo_rows)
        write_csv(args.output_dir / "true_rebuild_loo_events.csv", true_loo_events)

    summary = {
        "params_npz": str(args.params_npz),
        "candidate_count": len(rows_sorted),
        "true_loo_top": args.true_loo_top,
        "true_loo_top_results": true_loo_rows,
        "best": rows_sorted[0] if rows_sorted else None,
        "top20": rows_sorted[:20],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"best": summary["best"], "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
