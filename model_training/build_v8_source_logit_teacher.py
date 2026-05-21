import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def parse_sources(text: str) -> list[Path]:
    return [Path(item.strip()) for item in text.split(",") if item.strip()]


def row_key_map(payload: dict[str, np.ndarray]) -> dict[tuple[int, str], int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def class_distances(
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    class_dist_rows: list[np.ndarray] = []
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            class_dist[:, parent] = np.min(dist[:, indexes], axis=1)
        class_dist_rows.append(class_dist)
    return np.concatenate(class_dist_rows).astype(np.int64)


def softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - np.max(logits, axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-9)


def build_source_logit_teacher(
    *,
    base_npz: Path,
    source_npz: list[Path],
    output_dir: Path,
    aggregate_mode: str,
    margin_scale_percentile: float,
    min_sources: int,
    require_correct_source: bool,
    target_temperature: float,
    base_low_margin_threshold: int,
    base_low_margin_extra_weight: float,
    batch_size: int,
) -> None:
    base = load_npz(base_npz)
    base_sample = np.asarray(base["sample_index"], dtype=np.int64)
    base_view = np.asarray(base["view_labels"]).astype(str)
    base_parent = np.asarray(base["parent"], dtype=np.int64)
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)

    aggregate_logits = np.zeros((len(base_sample), 3), dtype=np.float32)
    winner_logits = np.zeros((len(base_sample), 3), dtype=np.float32)
    winner_score = np.full(len(base_sample), -np.inf, dtype=np.float32)
    winner_source = np.full(len(base_sample), -1, dtype=np.int64)
    support = np.zeros(len(base_sample), dtype=np.int64)
    source_summaries: list[dict[str, Any]] = []
    rows_by_source: list[dict[str, Any]] = []

    if aggregate_mode not in {"mean", "winner", "class_margin_sum"}:
        raise ValueError(f"unknown aggregate_mode: {aggregate_mode}")

    for source_id, path in enumerate(source_npz):
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
        skipped_wrong = 0
        skipped_parent_mismatch = 0
        missing = 0
        for base_index, key in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
            source_index = source_by_key.get((int(key[0]), str(key[1])))
            if source_index is None:
                missing += 1
                continue
            aligned += 1
            if int(source_parent[source_index]) != int(base_parent[base_index]):
                skipped_parent_mismatch += 1
                continue
            if require_correct_source and int(source_pred[source_index]) != int(base_parent[base_index]):
                skipped_wrong += 1
                continue
            dist = class_dist[source_index].astype(np.float32)
            if aggregate_mode == "class_margin_sum":
                logits = np.zeros(3, dtype=np.float32)
                for parent in range(3):
                    other = [item for item in range(3) if item != parent]
                    logits[parent] = float(np.min(dist[other]) - dist[parent]) / float(scale)
            else:
                logits = -(dist - np.min(dist)) / float(scale)
            aggregate_logits[base_index] += logits.astype(np.float32)
            score = np.log1p(float(max(int(source_margin[source_index]), 0))) / np.log1p(scale)
            if float(score) > float(winner_score[base_index]):
                winner_score[base_index] = float(score)
                winner_logits[base_index] = logits.astype(np.float32)
                winner_source[base_index] = int(source_id)
            support[base_index] += 1
            used += 1
        rows_by_source.append(
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
        source_summaries.append(rows_by_source[-1])

    active = support >= int(min_sources)
    target_logits = np.zeros_like(aggregate_logits, dtype=np.float32)
    if aggregate_mode == "mean":
        target_logits[active] = aggregate_logits[active] / support[active, None].astype(np.float32)
    elif aggregate_mode == "winner":
        target_logits[active] = winner_logits[active]
    elif aggregate_mode == "class_margin_sum":
        target_logits[active] = aggregate_logits[active]
    if target_temperature != 1.0:
        target_logits[active] = target_logits[active] / float(target_temperature)
    target_probs = softmax(target_logits.astype(np.float64)).astype(np.float32)
    weights = active.astype(np.float32)
    if base_low_margin_extra_weight:
        weights[(active) & (base_margin <= int(base_low_margin_threshold))] += float(base_low_margin_extra_weight)

    rows: list[dict[str, Any]] = []
    for index in np.where(active)[0]:
        rows.append(
            {
                "row_index": int(index),
                "sample_index": int(base_sample[index]),
                "view_label": str(base_view[index]),
                "parent": int(base_parent[index]),
                "support": int(support[index]),
                "base_margin": int(base_margin[index]),
                "weight": float(weights[index]),
                "target_prob_parent": float(target_probs[index, int(base_parent[index])]),
                "winner_source": int(winner_source[index]),
                "winner_score": float(winner_score[index]) if np.isfinite(winner_score[index]) else 0.0,
                "target_logits_json": json.dumps([float(v) for v in target_logits[index].tolist()]),
                "target_probs_json": json.dumps([float(v) for v in target_probs[index].tolist()]),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "source_logit_teacher.npz",
        sample_index=base_sample[active].astype(np.int64),
        view_labels=base_view[active].astype(str),
        parent=base_parent[active].astype(np.int64),
        target_logits=target_logits[active].astype(np.float32),
        target_probs=target_probs[active].astype(np.float32),
        weight=weights[active].astype(np.float32),
        support=support[active].astype(np.int64),
        source_base_npz=np.asarray(str(base_npz)),
        source_npz=np.asarray([str(path) for path in source_npz]),
        aggregate_mode=np.asarray(str(aggregate_mode)),
        winner_source=winner_source[active].astype(np.int64),
        winner_score=winner_score[active].astype(np.float32),
        high_pressure_usage=np.asarray("none"),
    )
    write_csv(output_dir / "source_logit_rows.csv", rows)
    write_csv(output_dir / "source_summary.csv", rows_by_source)
    summary = {
        "output": str(output_dir / "source_logit_teacher.npz"),
        "base_npz": str(base_npz),
        "source_npz": [str(path) for path in source_npz],
        "high_pressure_usage": "none",
        "row_count": int(len(rows)),
        "base_row_count": int(len(base_sample)),
        "active_fraction": float(len(rows) / max(len(base_sample), 1)),
        "aggregate_mode": str(aggregate_mode),
        "min_sources": int(min_sources),
        "require_correct_source": bool(require_correct_source),
        "target_temperature": float(target_temperature),
        "base_low_margin_threshold": int(base_low_margin_threshold),
        "base_low_margin_extra_weight": float(base_low_margin_extra_weight),
        "weight_min": float(np.min(weights[active])) if np.any(active) else 0.0,
        "weight_max": float(np.max(weights[active])) if np.any(active) else 0.0,
        "weight_mean": float(np.mean(weights[active])) if np.any(active) else 0.0,
        "support_counts": {
            str(int(value)): int(np.sum(support[active] == value))
            for value in sorted(set(support[active].tolist()))
        },
        "target_prob_parent_mean": float(np.mean(target_probs[active, base_parent[active]])) if np.any(active) else 0.0,
        "target_prob_parent_min": float(np.min(target_probs[active, base_parent[active]])) if np.any(active) else 0.0,
        "winner_source_counts": {
            str(int(value)): int(np.sum(winner_source[active] == value))
            for value in sorted(set(winner_source[active].tolist()))
        } if aggregate_mode in {"winner", "class_margin_sum"} and np.any(active) else {},
        "source_summary": source_summaries,
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build normal-only source class-distance logit teacher for V8.")
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--source-npz", required=True, help="Comma-separated retained source params npz paths.")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--aggregate-mode", choices=["mean", "winner", "class_margin_sum"], default="mean")
    parser.add_argument("--margin-scale-percentile", type=float, default=90.0)
    parser.add_argument("--min-sources", type=int, default=2)
    parser.add_argument("--allow-wrong-source", action="store_true")
    parser.add_argument("--target-temperature", type=float, default=1.0)
    parser.add_argument("--base-low-margin-threshold", type=int, default=16)
    parser.add_argument("--base-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--batch-size", type=int, default=256)
    args = parser.parse_args()

    build_source_logit_teacher(
        base_npz=args.base_npz,
        source_npz=parse_sources(args.source_npz),
        output_dir=args.output_dir,
        aggregate_mode=args.aggregate_mode,
        margin_scale_percentile=args.margin_scale_percentile,
        min_sources=args.min_sources,
        require_correct_source=not args.allow_wrong_source,
        target_temperature=args.target_temperature,
        base_low_margin_threshold=args.base_low_margin_threshold,
        base_low_margin_extra_weight=args.base_low_margin_extra_weight,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
