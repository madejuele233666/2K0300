import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from build_v8_source_logit_teacher import class_distances, load_npz, row_key_map, softmax
from evaluate_v8_embedding_prototypes import write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_sources(text: str) -> list[Path]:
    return [Path(item.strip()) for item in text.split(",") if item.strip()]


def pca_project(values: np.ndarray, side_dim: int, target_abs_p99: float) -> tuple[np.ndarray, dict[str, Any]]:
    centered = values.astype(np.float64) - np.mean(values.astype(np.float64), axis=0, keepdims=True)
    if centered.shape[1] == 0:
        raise ValueError("source margin matrix has no columns")
    _u, singular, vt = np.linalg.svd(centered, full_matrices=False)
    components = vt[:side_dim]
    projected = centered @ components.T
    if projected.shape[1] < side_dim:
        projected = np.pad(projected, ((0, 0), (0, side_dim - projected.shape[1])), mode="constant")
    scale = float(np.percentile(np.abs(projected), 99)) if projected.size else 1.0
    scale = max(scale, 1.0e-6)
    projected_q = np.rint(projected / scale * float(target_abs_p99))
    projected_q = np.clip(projected_q, -127, 127).astype(np.int8)
    denom = float(np.sum(singular * singular)) if singular.size else 1.0
    summary = {
        "side_dim": int(side_dim),
        "target_abs_p99": float(target_abs_p99),
        "projection_scale": float(scale),
        "singular_values": [float(item) for item in singular[: min(len(singular), side_dim)].tolist()],
        "explained_fraction": float(np.sum(singular[:side_dim] * singular[:side_dim]) / max(denom, 1.0e-9)),
    }
    return projected_q, summary


def winner_simplex_project(confidence: np.ndarray, side_dim: int, target_abs_p99: float) -> tuple[np.ndarray, dict[str, Any]]:
    if confidence.ndim != 2 or confidence.shape[1] < 2:
        raise ValueError("winner_simplex requires at least two source columns")
    source_count = int(confidence.shape[1])
    winners = np.argmax(confidence, axis=1).astype(np.int64)
    one_hot = np.eye(source_count, dtype=np.float64)[winners]
    centered = one_hot - np.mean(np.eye(source_count, dtype=np.float64), axis=0, keepdims=True)
    _u, singular, vt = np.linalg.svd(centered, full_matrices=False)
    components = vt[:side_dim]
    projected = centered @ components.T
    if projected.shape[1] < side_dim:
        projected = np.pad(projected, ((0, 0), (0, side_dim - projected.shape[1])), mode="constant")
    scale = float(np.max(np.abs(projected))) if projected.size else 1.0
    scale = max(scale, 1.0e-6)
    projected_q = np.rint(projected / scale * float(target_abs_p99))
    projected_q = np.clip(projected_q, -127, 127).astype(np.int8)
    summary = {
        "side_dim": int(side_dim),
        "target_abs_p99": float(target_abs_p99),
        "projection_scale": float(scale),
        "singular_values": [float(item) for item in singular[: min(len(singular), side_dim)].tolist()],
        "winner_counts": {
            str(index): int(np.sum(winners == index))
            for index in range(source_count)
        },
    }
    return projected_q, summary


def build_source_margin_qanchor_teacher(
    *,
    base_npz: Path,
    source_npz: list[Path],
    output_dir: Path,
    side_dim: int,
    side_mode: str,
    target_abs_p99: float,
    margin_scale_percentile: float,
    min_sources: int,
    require_correct_source: bool,
    confidence_temperature: float,
    base_low_margin_threshold: int,
    base_low_margin_extra_weight: float,
    batch_size: int,
) -> None:
    base = load_npz(base_npz)
    base_sample = np.asarray(base["sample_index"], dtype=np.int64)
    base_view = np.asarray(base["view_labels"]).astype(str)
    base_parent = np.asarray(base["parent"], dtype=np.int64)
    base_embedding = np.asarray(base["embedding_int8"], dtype=np.int8)
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)
    if side_dim <= 0:
        raise ValueError("--side-dim must be positive")

    source_scores = np.zeros((len(base_sample), len(source_npz)), dtype=np.float32)
    support = np.zeros(len(base_sample), dtype=np.int64)
    source_summaries: list[dict[str, Any]] = []
    for source_index, path in enumerate(source_npz):
        source = load_npz(path)
        source_by_key = row_key_map(source)
        source_parent = np.asarray(source["parent"], dtype=np.int64)
        source_embedding = np.asarray(source["embedding_int8"], dtype=np.int8)
        source_prototypes = np.asarray(source["prototypes_int8"], dtype=np.int8)
        source_proto_parent = np.asarray(source["prototype_parent"], dtype=np.int64)
        source_margin = np.asarray(source["int8_margin"], dtype=np.int64)
        class_dist = class_distances(
            source_embedding,
            source_prototypes,
            source_proto_parent,
            batch_size=batch_size,
        )
        source_pred = np.argmin(class_dist, axis=1).astype(np.int64)
        scale = float(np.percentile(np.maximum(source_margin, 1), float(margin_scale_percentile)))
        scale = max(scale, 1.0)
        aligned = 0
        used = 0
        missing = 0
        skipped_wrong = 0
        skipped_parent_mismatch = 0
        for base_index, key in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
            source_row = source_by_key.get((int(key[0]), str(key[1])))
            if source_row is None:
                missing += 1
                continue
            aligned += 1
            if int(source_parent[source_row]) != int(base_parent[base_index]):
                skipped_parent_mismatch += 1
                continue
            if require_correct_source and int(source_pred[source_row]) != int(base_parent[base_index]):
                skipped_wrong += 1
                continue
            score = np.log1p(float(max(int(source_margin[source_row]), 0))) / np.log1p(scale)
            source_scores[base_index, source_index] = float(score)
            support[base_index] += 1
            used += 1
        source_summaries.append(
            {
                "source": str(path),
                "aligned_rows": int(aligned),
                "used_rows": int(used),
                "missing_base_rows": int(missing),
                "skipped_wrong_rows": int(skipped_wrong),
                "skipped_parent_mismatch": int(skipped_parent_mismatch),
                "margin_scale_percentile": float(margin_scale_percentile),
                "margin_scale": float(scale),
            }
        )

    active = support >= int(min_sources)
    if not np.any(active):
        raise ValueError("no active rows after source alignment")
    confidence = softmax(source_scores[active].astype(np.float64) / max(float(confidence_temperature), 1.0e-6))
    if side_mode == "confidence_pca":
        side_q, side_summary = pca_project(
            confidence.astype(np.float32),
            side_dim=side_dim,
            target_abs_p99=target_abs_p99,
        )
    elif side_mode == "winner_simplex":
        side_q, side_summary = winner_simplex_project(
            confidence.astype(np.float32),
            side_dim=side_dim,
            target_abs_p99=target_abs_p99,
        )
    else:
        raise ValueError(f"unknown side_mode: {side_mode}")
    target = np.concatenate([base_embedding[active].astype(np.int8), side_q.astype(np.int8)], axis=1)
    weights = np.ones(np.sum(active), dtype=np.float32)
    active_base_margin = base_margin[active]
    if base_low_margin_extra_weight:
        weights[active_base_margin <= int(base_low_margin_threshold)] += float(base_low_margin_extra_weight)

    rows: list[dict[str, Any]] = []
    active_indexes = np.where(active)[0]
    source_names = [path.parent.name for path in source_npz]
    for local_index, base_index in enumerate(active_indexes.tolist()):
        conf = confidence[local_index]
        rows.append(
            {
                "row_index": int(base_index),
                "sample_index": int(base_sample[base_index]),
                "view_label": str(base_view[base_index]),
                "parent": int(base_parent[base_index]),
                "support": int(support[base_index]),
                "base_margin": int(base_margin[base_index]),
                "weight": float(weights[local_index]),
                "top_source": str(source_names[int(np.argmax(conf))]) if source_names else "",
                "confidence_json": json.dumps([float(v) for v in conf.tolist()]),
                "side_embedding_json": json.dumps([int(v) for v in side_q[local_index].tolist()]),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "qanchor_teacher.npz",
        sample_index=base_sample[active].astype(np.int64),
        view_labels=base_view[active].astype(str),
        parent=base_parent[active].astype(np.int64),
        embedding_int8=target.astype(np.int8),
        qanchor_weight=weights.astype(np.float32),
        support=support[active].astype(np.int64),
        source_margin_scores=source_scores[active].astype(np.float32),
        source_confidence=confidence.astype(np.float32),
        source_names=np.asarray(source_names).astype(str),
        source_base_npz=np.asarray(str(base_npz)),
        source_npz=np.asarray([str(path) for path in source_npz]).astype(str),
        high_pressure_usage=np.asarray("none"),
    )
    write_csv(output_dir / "source_margin_qanchor_rows.csv", rows)
    write_csv(output_dir / "source_summary.csv", source_summaries)
    summary = {
        "output": str(output_dir / "qanchor_teacher.npz"),
        "base_npz": str(base_npz),
        "source_npz": [str(path) for path in source_npz],
        "high_pressure_usage": "none",
        "row_count": int(np.sum(active)),
        "base_row_count": int(len(base_sample)),
        "active_fraction": float(np.mean(active)),
        "target_dim": int(base_embedding.shape[1] + side_dim),
        "base_dim": int(base_embedding.shape[1]),
        "side_dim": int(side_dim),
        "side_mode": str(side_mode),
        "min_sources": int(min_sources),
        "require_correct_source": bool(require_correct_source),
        "confidence_temperature": float(confidence_temperature),
        "weight_min": float(np.min(weights)),
        "weight_max": float(np.max(weights)),
        "weight_mean": float(np.mean(weights)),
        "support_counts": {
            str(int(value)): int(np.sum(support[active] == value))
            for value in sorted(set(support[active].tolist()))
        },
        "top_source_counts": {
            source_names[index]: int(np.sum(np.argmax(confidence, axis=1) == index))
            for index in range(len(source_names))
        },
        "side_projection": side_summary,
        "pca": side_summary if side_mode == "confidence_pca" else {},
        "source_summary": source_summaries,
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build normal-only source-margin side-dim qanchor teacher for V8.")
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--source-npz", required=True, help="Comma-separated retained source params npz paths.")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--side-dim", type=int, default=2)
    parser.add_argument("--side-mode", choices=["confidence_pca", "winner_simplex"], default="confidence_pca")
    parser.add_argument("--target-abs-p99", type=float, default=48.0)
    parser.add_argument("--margin-scale-percentile", type=float, default=90.0)
    parser.add_argument("--min-sources", type=int, default=2)
    parser.add_argument("--allow-wrong-source", action="store_true")
    parser.add_argument("--confidence-temperature", type=float, default=1.0)
    parser.add_argument("--base-low-margin-threshold", type=int, default=16)
    parser.add_argument("--base-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()
    build_source_margin_qanchor_teacher(
        base_npz=args.base_npz,
        source_npz=parse_sources(args.source_npz),
        output_dir=args.output_dir,
        side_dim=args.side_dim,
        side_mode=args.side_mode,
        target_abs_p99=args.target_abs_p99,
        margin_scale_percentile=args.margin_scale_percentile,
        min_sources=args.min_sources,
        require_correct_source=not args.allow_wrong_source,
        confidence_temperature=args.confidence_temperature,
        base_low_margin_threshold=args.base_low_margin_threshold,
        base_low_margin_extra_weight=args.base_low_margin_extra_weight,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
