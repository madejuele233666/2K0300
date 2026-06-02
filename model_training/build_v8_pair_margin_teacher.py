import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import ROT_MIRROR_VIEWS, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def subset_mask(view_labels: np.ndarray, subset: str) -> np.ndarray:
    if subset == "clean":
        return view_labels == "clean"
    if subset == "clean_rotmirror":
        return (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    if subset == "all":
        return np.ones(len(view_labels), dtype=bool)
    raise ValueError(f"unknown subset: {subset}")


def quantize_payload(values: np.ndarray, scale: float) -> np.ndarray:
    return np.clip(np.rint(values.astype(np.float32) * float(scale)), -128, 127).astype(np.int8)


def make_neighborhood_deltas(
    *,
    dim: int,
    radius: int,
    samples: int,
    seed: int,
) -> np.ndarray:
    if radius <= 0:
        return np.zeros((1, dim), dtype=np.int16)
    choices = np.arange(-radius, radius + 1, dtype=np.int16)
    full_count = int(len(choices) ** dim)
    if samples <= 0 or samples >= full_count:
        grids = np.meshgrid(*([choices] * dim), indexing="ij")
        return np.stack([grid.reshape(-1) for grid in grids], axis=1).astype(np.int16)
    rng = np.random.default_rng(seed)
    deltas = {tuple([0] * dim)}
    while len(deltas) < samples:
        deltas.add(tuple(rng.integers(-radius, radius + 1, size=dim).tolist()))
    return np.asarray(sorted(deltas), dtype=np.int16)


def fixed_pair_neighborhood_margin(
    embeddings: np.ndarray,
    correct_proto: np.ndarray,
    wrong_proto: np.ndarray,
    deltas: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    margins: list[np.ndarray] = []
    x_all = embeddings.astype(np.int16)
    c_all = correct_proto.astype(np.int16)
    w_all = wrong_proto.astype(np.int16)
    delta_all = deltas.astype(np.int16)
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        c = c_all[start : start + batch_size]
        w = w_all[start : start + batch_size]
        best = np.full(len(x), np.iinfo(np.int64).max, dtype=np.int64)
        for delta in delta_all:
            q = np.clip(x + delta[None, :], -128, 127).astype(np.int16)
            correct_dist = np.sum((q - c) ** 2, axis=1).astype(np.int64)
            wrong_dist = np.sum((q - w) ** 2, axis=1).astype(np.int64)
            best = np.minimum(best, wrong_dist - correct_dist)
        margins.append(best.astype(np.int64))
    return np.concatenate(margins).astype(np.int64)


def teacher_weights(
    *,
    wrong_before: np.ndarray,
    true_margin: np.ndarray,
    neighborhood_margin: np.ndarray,
    target_int8_margin: int,
    neighborhood_target_int8_margin: int,
    mode: str,
    wrong_weight: float,
    margin_weight: float,
    neighborhood_weight: float,
    max_weight: float,
) -> np.ndarray:
    if mode == "simple":
        weights = np.ones(len(true_margin), dtype=np.float32)
        weights[wrong_before] = 2.0
        return weights
    if mode != "budget":
        raise ValueError(f"unknown weight mode: {mode}")
    target = max(float(target_int8_margin), 1.0)
    neighborhood_target = max(float(neighborhood_target_int8_margin), 1.0)
    weights = np.ones(len(true_margin), dtype=np.float32)
    weights += wrong_before.astype(np.float32) * float(wrong_weight)
    weights += np.clip((target - true_margin.astype(np.float32)) / target, 0.0, 1.0) * float(margin_weight)
    weights += (
        np.clip((neighborhood_target - neighborhood_margin.astype(np.float32)) / neighborhood_target, 0.0, 1.0)
        * float(neighborhood_weight)
    )
    if max_weight > 0:
        weights = np.minimum(weights, float(max_weight))
    return weights.astype(np.float32)


def nearest_by_parent(
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    parent_count = 3
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    dist_rows: list[np.ndarray] = []
    nearest_rows: list[np.ndarray] = []
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(parent_count)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), parent_count), np.iinfo(np.int64).max, dtype=np.int64)
        nearest = np.full((len(x), parent_count), -1, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            local = dist[:, indexes]
            arg = np.argmin(local, axis=1)
            class_dist[:, parent] = local[np.arange(len(x)), arg]
            nearest[:, parent] = indexes[arg]
        pred = np.argmin(class_dist, axis=1).astype(np.int64)
        sorted_dist = np.sort(class_dist, axis=1)
        pred_rows.append(pred)
        margin_rows.append((sorted_dist[:, 1] - sorted_dist[:, 0]).astype(np.int64))
        dist_rows.append(class_dist)
        nearest_rows.append(nearest)
    return (
        np.concatenate(pred_rows).astype(np.int64),
        np.concatenate(margin_rows).astype(np.int64),
        np.concatenate(dist_rows).astype(np.int64),
        np.concatenate(nearest_rows).astype(np.int64),
    )


def build_pair_margin_teacher(
    *,
    params_npz: Path,
    output_dir: Path,
    base_subset: str,
    target_int8_margin: int,
    include_neighborhood: bool,
    neighborhood_radius: int,
    neighborhood_samples: int,
    neighborhood_target_int8_margin: int,
    weight_mode: str,
    wrong_weight: float,
    margin_weight: float,
    neighborhood_weight: float,
    max_weight: float,
    seed: int,
    int8_scale: float,
    batch_size: int,
    max_events: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with np.load(params_npz, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}

    y_parent = np.asarray(payload["parent"], dtype=np.int64)
    y_sub = np.asarray(payload["subclass"], dtype=np.int64)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    paths = np.asarray(payload.get("paths", np.asarray([]))).astype(str)
    if "embedding_int8" in payload:
        embeddings_int8 = np.asarray(payload["embedding_int8"], dtype=np.int8)
    else:
        embeddings_int8 = quantize_payload(np.asarray(payload["embedding_float"], dtype=np.float32), int8_scale)

    selected = subset_mask(view_labels, base_subset)
    prototype_parent = y_parent[selected].astype(np.int64)
    prototype_sample = sample_index[selected].astype(np.int64)
    prototype_view = view_labels[selected].astype(str)
    if "prototypes_int8" in payload and base_subset == "all" and len(payload["prototypes_int8"]) == len(payload["prototype_parent"]):
        prototypes_int8 = np.asarray(payload["prototypes_int8"], dtype=np.int8)
        prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
        prototype_sample = np.asarray(payload.get("prototype_sample_index", np.full(len(prototypes_int8), -1)), dtype=np.int64)
        prototype_view = np.asarray(payload.get("prototype_view_label", np.asarray([""] * len(prototypes_int8)))).astype(str)
    else:
        prototypes_int8 = embeddings_int8[selected].astype(np.int8)

    pred, sorted_margin, class_dist, nearest = nearest_by_parent(
        embeddings_int8,
        prototypes_int8,
        prototype_parent,
        batch_size=batch_size,
    )
    correct_dist = class_dist[np.arange(len(y_parent)), y_parent]
    wrong_dist = np.full(len(y_parent), np.iinfo(np.int64).max, dtype=np.int64)
    wrong_parent = np.full(len(y_parent), -1, dtype=np.int64)
    for parent in range(3):
        mask = y_parent != parent
        better = mask & (class_dist[:, parent] < wrong_dist)
        wrong_dist[better] = class_dist[better, parent]
        wrong_parent[better] = parent
    true_margin = (wrong_dist - correct_dist).astype(np.int64)
    correct_proto_all = prototypes_int8[nearest[np.arange(len(y_parent)), y_parent]].astype(np.int8)
    wrong_proto_all = prototypes_int8[nearest[np.arange(len(y_parent)), wrong_parent]].astype(np.int8)
    if include_neighborhood:
        deltas = make_neighborhood_deltas(
            dim=int(embeddings_int8.shape[1]),
            radius=neighborhood_radius,
            samples=neighborhood_samples,
            seed=seed,
        )
        neighborhood_margin = fixed_pair_neighborhood_margin(
            embeddings_int8,
            correct_proto_all,
            wrong_proto_all,
            deltas,
            batch_size=batch_size,
        )
    else:
        deltas = np.zeros((1, int(embeddings_int8.shape[1])), dtype=np.int16)
        neighborhood_margin = true_margin.copy()
    risk_mask = (pred != y_parent) | (true_margin <= int(target_int8_margin))
    if include_neighborhood:
        risk_mask |= neighborhood_margin <= int(neighborhood_target_int8_margin)
    risk_indexes = np.where(risk_mask)[0]
    order = np.lexsort(
        (
            risk_indexes,
            neighborhood_margin[risk_indexes],
            true_margin[risk_indexes],
            pred[risk_indexes] == y_parent[risk_indexes],
        )
    )
    risk_indexes = risk_indexes[order]
    if max_events > 0:
        risk_indexes = risk_indexes[:max_events]

    correct_proto_index = nearest[risk_indexes, y_parent[risk_indexes]]
    wrong_proto_index = nearest[risk_indexes, wrong_parent[risk_indexes]]
    correct_proto_sample_out = prototype_sample[correct_proto_index].astype(np.int64) if len(risk_indexes) else np.zeros((0,), dtype=np.int64)
    wrong_proto_sample_out = prototype_sample[wrong_proto_index].astype(np.int64) if len(risk_indexes) else np.zeros((0,), dtype=np.int64)
    correct_proto_view_out = prototype_view[correct_proto_index].astype(str) if len(risk_indexes) else np.asarray([], dtype=str)
    wrong_proto_view_out = prototype_view[wrong_proto_index].astype(str) if len(risk_indexes) else np.asarray([], dtype=str)
    wrong_before = pred[risk_indexes] != y_parent[risk_indexes]
    rows: list[dict[str, Any]] = []
    for out_index, index in enumerate(risk_indexes.tolist()):
        cpi = int(correct_proto_index[out_index])
        wpi = int(wrong_proto_index[out_index])
        rows.append(
            {
                "query_index": int(index),
                "sample_index": int(sample_index[index]),
                "path": str(paths[int(sample_index[index])]) if len(paths) > int(sample_index[index]) else "",
                "view_label": str(view_labels[index]),
                "parent": int(y_parent[index]),
                "subclass": int(y_sub[index]),
                "pred_before": int(pred[index]),
                "wrong_parent": int(wrong_parent[index]),
                "wrong_before": bool(wrong_before[out_index]),
                "sorted_int8_margin": int(sorted_margin[index]),
                "true_int8_margin": int(true_margin[index]),
                "neighborhood_int8_margin": int(neighborhood_margin[index]),
                "correct_dist": int(correct_dist[index]),
                "wrong_dist": int(wrong_dist[index]),
                "correct_proto_index": cpi,
                "correct_proto_sample": int(prototype_sample[cpi]) if cpi >= 0 else -1,
                "correct_proto_view": str(prototype_view[cpi]) if cpi >= 0 else "",
                "wrong_proto_index": wpi,
                "wrong_proto_sample": int(prototype_sample[wpi]) if wpi >= 0 else -1,
                "wrong_proto_view": str(prototype_view[wpi]) if wpi >= 0 else "",
            }
        )
    write_csv(output_dir / "pair_margin_events.csv", rows)

    if len(risk_indexes) > 0:
        weights = teacher_weights(
            wrong_before=wrong_before,
            true_margin=true_margin[risk_indexes],
            neighborhood_margin=neighborhood_margin[risk_indexes],
            target_int8_margin=target_int8_margin,
            neighborhood_target_int8_margin=neighborhood_target_int8_margin,
            mode=weight_mode,
            wrong_weight=wrong_weight,
            margin_weight=margin_weight,
            neighborhood_weight=neighborhood_weight,
            max_weight=max_weight,
        )
        correct_proto = prototypes_int8[correct_proto_index].astype(np.int8)
        wrong_proto = prototypes_int8[wrong_proto_index].astype(np.int8)
    else:
        weights = np.zeros((0,), dtype=np.float32)
        correct_proto = np.zeros((0, embeddings_int8.shape[1]), dtype=np.int8)
        wrong_proto = np.zeros((0, embeddings_int8.shape[1]), dtype=np.int8)
    np.savez_compressed(
        output_dir / "pair_margin_teacher.npz",
        sample_index=sample_index[risk_indexes].astype(np.int64),
        view_labels=view_labels[risk_indexes].astype(str),
        query_index=risk_indexes.astype(np.int64),
        parent=y_parent[risk_indexes].astype(np.int64),
        true_int8_margin=true_margin[risk_indexes].astype(np.int64),
        sorted_int8_margin=sorted_margin[risk_indexes].astype(np.int64),
        neighborhood_int8_margin=neighborhood_margin[risk_indexes].astype(np.int64),
        correct_proto_index=correct_proto_index.astype(np.int64),
        correct_proto_sample=correct_proto_sample_out,
        correct_proto_view=correct_proto_view_out,
        wrong_proto_index=wrong_proto_index.astype(np.int64),
        wrong_proto_sample=wrong_proto_sample_out,
        wrong_proto_view=wrong_proto_view_out,
        correct_proto_int8=correct_proto,
        wrong_proto_int8=wrong_proto,
        wrong_parent=wrong_parent[risk_indexes].astype(np.int64),
        weight=weights,
        source_params_npz=np.asarray(str(params_npz)),
        base_subset=np.asarray(base_subset),
        target_int8_margin=np.asarray(int(target_int8_margin), dtype=np.int64),
        include_neighborhood=np.asarray(bool(include_neighborhood)),
        neighborhood_radius=np.asarray(int(neighborhood_radius), dtype=np.int64),
        neighborhood_samples=np.asarray(int(len(deltas)), dtype=np.int64),
        neighborhood_target_int8_margin=np.asarray(int(neighborhood_target_int8_margin), dtype=np.int64),
        weight_mode=np.asarray(str(weight_mode)),
        int8_scale=np.asarray(float(int8_scale), dtype=np.float32),
    )

    by_view: dict[str, int] = {}
    for view in view_labels[risk_indexes].tolist():
        by_view[str(view)] = by_view.get(str(view), 0) + 1
    write_json(
        output_dir / "summary.json",
        {
            "source_params_npz": str(params_npz),
            "base_subset": base_subset,
            "target_int8_margin": int(target_int8_margin),
            "include_neighborhood": bool(include_neighborhood),
            "neighborhood_radius": int(neighborhood_radius),
            "neighborhood_delta_count": int(len(deltas)),
            "neighborhood_target_int8_margin": int(neighborhood_target_int8_margin),
            "weight_mode": weight_mode,
            "int8_scale": float(int8_scale),
            "embedding_count": int(len(embeddings_int8)),
            "prototype_count": int(len(prototypes_int8)),
            "event_count": int(len(risk_indexes)),
            "wrong_event_count": int(np.sum(wrong_before)),
            "true_margin_min": int(np.min(true_margin)) if len(true_margin) else 0,
            "neighborhood_margin_min": int(np.min(neighborhood_margin)) if len(neighborhood_margin) else 0,
            "selected_true_margin_min": int(np.min(true_margin[risk_indexes])) if len(risk_indexes) else 0,
            "selected_true_margin_max": int(np.max(true_margin[risk_indexes])) if len(risk_indexes) else 0,
            "selected_neighborhood_margin_min": int(np.min(neighborhood_margin[risk_indexes])) if len(risk_indexes) else 0,
            "selected_neighborhood_margin_max": int(np.max(neighborhood_margin[risk_indexes])) if len(risk_indexes) else 0,
            "weight_min": float(np.min(weights)) if len(weights) else 0.0,
            "weight_max": float(np.max(weights)) if len(weights) else 0.0,
            "weight_mean": float(np.mean(weights)) if len(weights) else 0.0,
            "by_view_top20": dict(sorted(by_view.items(), key=lambda item: (-item[1], item[0]))[:20]),
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Export fixed nearest-correct/nearest-wrong int8 pair teachers for V8 margin training.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--base-subset", choices=["clean", "clean_rotmirror", "all"], default="clean_rotmirror")
    parser.add_argument("--target-int8-margin", type=int, default=8)
    parser.add_argument("--include-neighborhood", action="store_true")
    parser.add_argument("--neighborhood-radius", type=int, default=1)
    parser.add_argument("--neighborhood-samples", type=int, default=0)
    parser.add_argument("--neighborhood-target-int8-margin", type=int, default=8)
    parser.add_argument("--weight-mode", choices=["simple", "budget"], default="simple")
    parser.add_argument("--wrong-weight", type=float, default=2.0)
    parser.add_argument("--margin-weight", type=float, default=1.0)
    parser.add_argument("--neighborhood-weight", type=float, default=1.0)
    parser.add_argument("--max-weight", type=float, default=4.0)
    parser.add_argument("--seed", type=int, default=20260520)
    parser.add_argument("--int8-scale", type=float, default=64.0)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--max-events", type=int, default=0)
    args = parser.parse_args()
    build_pair_margin_teacher(
        params_npz=args.params_npz,
        output_dir=args.output_dir,
        base_subset=args.base_subset,
        target_int8_margin=args.target_int8_margin,
        include_neighborhood=args.include_neighborhood,
        neighborhood_radius=args.neighborhood_radius,
        neighborhood_samples=args.neighborhood_samples,
        neighborhood_target_int8_margin=args.neighborhood_target_int8_margin,
        weight_mode=args.weight_mode,
        wrong_weight=args.wrong_weight,
        margin_weight=args.margin_weight,
        neighborhood_weight=args.neighborhood_weight,
        max_weight=args.max_weight,
        seed=args.seed,
        int8_scale=args.int8_scale,
        batch_size=args.batch_size,
        max_events=args.max_events,
    )


if __name__ == "__main__":
    main()
