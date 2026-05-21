import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv
from build_v8_source_logit_teacher import class_distances, load_npz, parse_sources, row_key_map


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def build_source_decision_margin_teacher(
    *,
    base_npz: Path,
    source_npz: list[Path],
    output_dir: Path,
    margin_scale_percentile: float,
    min_sources: int,
    require_correct_source: bool,
    source_margin_min: int,
    target_margin: float,
    base_low_margin_threshold: int,
    base_low_margin_extra_weight: float,
    weakness_weight: float,
    max_weight: float,
    batch_size: int,
) -> None:
    base = load_npz(base_npz)
    base_sample = np.asarray(base["sample_index"], dtype=np.int64)
    base_view = np.asarray(base["view_labels"]).astype(str)
    base_parent = np.asarray(base["parent"], dtype=np.int64)
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)

    aggregate_margin = np.zeros((len(base_sample), 3), dtype=np.float64)
    support = np.zeros(len(base_sample), dtype=np.int64)
    source_summaries: list[dict[str, Any]] = []

    for path in source_npz:
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
        skipped_low_margin = 0
        skipped_parent_mismatch = 0
        missing = 0
        for base_index, key in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
            source_index = source_by_key.get((int(key[0]), str(key[1])))
            if source_index is None:
                missing += 1
                continue
            aligned += 1
            parent = int(base_parent[base_index])
            if int(source_parent[source_index]) != parent:
                skipped_parent_mismatch += 1
                continue
            if require_correct_source and int(source_pred[source_index]) != parent:
                skipped_wrong += 1
                continue
            if int(source_margin[source_index]) < int(source_margin_min):
                skipped_low_margin += 1
                continue
            dist = class_dist[source_index].astype(np.float64)
            margins = dist - float(dist[parent])
            margins[parent] = np.inf
            aggregate_margin[base_index] += np.maximum(margins, 0.0) / scale
            support[base_index] += 1
            used += 1
        source_summaries.append(
            {
                "source": str(path),
                "aligned_rows": int(aligned),
                "used_rows": int(used),
                "missing_base_rows": int(missing),
                "skipped_wrong_rows": int(skipped_wrong),
                "skipped_low_margin_rows": int(skipped_low_margin),
                "skipped_parent_mismatch": int(skipped_parent_mismatch),
                "source_margin_min": int(source_margin_min),
                "margin_scale_percentile": float(margin_scale_percentile),
                "margin_scale": float(scale),
            }
        )

    active = support >= int(min_sources)
    wrong_parent = np.full(len(base_sample), -1, dtype=np.int64)
    normalized_margin = np.zeros(len(base_sample), dtype=np.float32)
    for index in np.where(active)[0]:
        parent = int(base_parent[index])
        margins = aggregate_margin[index] / float(max(int(support[index]), 1))
        margins[parent] = np.inf
        wrong_parent[index] = int(np.argmin(margins))
        normalized_margin[index] = float(margins[wrong_parent[index]])

    active_indexes = np.where(active & (wrong_parent >= 0))[0]
    if len(active_indexes):
        weakness = 1.0 / (1.0 + normalized_margin[active_indexes].astype(np.float32))
    else:
        weakness = np.zeros((0,), dtype=np.float32)
    weights = np.ones(len(active_indexes), dtype=np.float32)
    weights += weakness * float(weakness_weight)
    if base_low_margin_extra_weight:
        weights[base_margin[active_indexes] <= int(base_low_margin_threshold)] += float(base_low_margin_extra_weight)
    if max_weight > 0:
        weights = np.minimum(weights, float(max_weight)).astype(np.float32)
    target = np.full(len(active_indexes), float(target_margin), dtype=np.float32)

    rows: list[dict[str, Any]] = []
    for out_index, index in enumerate(active_indexes.tolist()):
        rows.append(
            {
                "row_index": int(index),
                "sample_index": int(base_sample[index]),
                "view_label": str(base_view[index]),
                "parent": int(base_parent[index]),
                "wrong_parent": int(wrong_parent[index]),
                "support": int(support[index]),
                "base_margin": int(base_margin[index]),
                "normalized_aggregate_margin": float(normalized_margin[index]),
                "target_margin": float(target[out_index]),
                "weight": float(weights[out_index]),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "source_decision_margin_teacher.npz",
        sample_index=base_sample[active_indexes].astype(np.int64),
        view_labels=base_view[active_indexes].astype(str),
        parent=base_parent[active_indexes].astype(np.int64),
        wrong_parent=wrong_parent[active_indexes].astype(np.int64),
        support=support[active_indexes].astype(np.int64),
        base_margin=base_margin[active_indexes].astype(np.int64),
        normalized_aggregate_margin=normalized_margin[active_indexes].astype(np.float32),
        target_margin=target.astype(np.float32),
        weight=weights.astype(np.float32),
        source_base_npz=np.asarray(str(base_npz)),
        source_npz=np.asarray([str(path) for path in source_npz]),
        high_pressure_usage=np.asarray("none"),
    )
    write_csv(output_dir / "source_decision_margin_rows.csv", rows)
    summary = {
        "output": str(output_dir / "source_decision_margin_teacher.npz"),
        "base_npz": str(base_npz),
        "source_npz": [str(path) for path in source_npz],
        "high_pressure_usage": "none",
        "row_count": int(len(rows)),
        "base_row_count": int(len(base_sample)),
        "active_fraction": float(len(rows) / max(len(base_sample), 1)),
        "min_sources": int(min_sources),
        "require_correct_source": bool(require_correct_source),
        "source_margin_min": int(source_margin_min),
        "target_margin": float(target_margin),
        "base_low_margin_threshold": int(base_low_margin_threshold),
        "base_low_margin_extra_weight": float(base_low_margin_extra_weight),
        "weakness_weight": float(weakness_weight),
        "weight_min": float(np.min(weights)) if len(weights) else 0.0,
        "weight_max": float(np.max(weights)) if len(weights) else 0.0,
        "weight_mean": float(np.mean(weights)) if len(weights) else 0.0,
        "support_counts": {
            str(int(value)): int(np.sum(support[active_indexes] == value))
            for value in sorted(set(support[active_indexes].tolist()))
        },
        "wrong_parent_counts": {
            str(int(value)): int(np.sum(wrong_parent[active_indexes] == value))
            for value in sorted(set(wrong_parent[active_indexes].tolist()))
        },
        "normalized_aggregate_margin_min": float(np.min(normalized_margin[active_indexes])) if len(active_indexes) else 0.0,
        "normalized_aggregate_margin_mean": float(np.mean(normalized_margin[active_indexes])) if len(active_indexes) else 0.0,
        "source_summary": source_summaries,
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build normal-only retained-source decision-margin teacher for V8.")
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--source-npz", required=True, help="Comma-separated retained source params npz paths.")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--margin-scale-percentile", type=float, default=90.0)
    parser.add_argument("--min-sources", type=int, default=2)
    parser.add_argument("--allow-wrong-source", action="store_true")
    parser.add_argument("--source-margin-min", type=int, default=16)
    parser.add_argument("--target-margin", type=float, default=8.0)
    parser.add_argument("--base-low-margin-threshold", type=int, default=16)
    parser.add_argument("--base-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--weakness-weight", type=float, default=1.0)
    parser.add_argument("--max-weight", type=float, default=4.0)
    parser.add_argument("--batch-size", type=int, default=256)
    args = parser.parse_args()

    build_source_decision_margin_teacher(
        base_npz=args.base_npz,
        source_npz=parse_sources(args.source_npz),
        output_dir=args.output_dir,
        margin_scale_percentile=args.margin_scale_percentile,
        min_sources=args.min_sources,
        require_correct_source=not args.allow_wrong_source,
        source_margin_min=args.source_margin_min,
        target_margin=args.target_margin,
        base_low_margin_threshold=args.base_low_margin_threshold,
        base_low_margin_extra_weight=args.base_low_margin_extra_weight,
        weakness_weight=args.weakness_weight,
        max_weight=args.max_weight,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
