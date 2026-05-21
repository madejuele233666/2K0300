import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from build_v8_source_logit_teacher import class_distances, load_npz, row_key_map
from evaluate_v8_embedding_prototypes import write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_sources(text: str) -> list[Path]:
    return [Path(item.strip()) for item in text.split(",") if item.strip()]


def build_source_block_qanchor_teacher(
    *,
    base_npz: Path,
    source_npz: list[Path],
    output_dir: Path,
    target_abs_p99: float,
    margin_scale_percentile: float,
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
    if not source_npz:
        raise ValueError("at least one --source-npz is required")

    blocks: list[np.ndarray] = []
    source_summaries: list[dict[str, Any]] = []
    for path in source_npz:
        source = load_npz(path)
        source_by_key = row_key_map(source)
        source_parent = np.asarray(source["parent"], dtype=np.int64)
        source_embedding = np.asarray(source["embedding_int8"], dtype=np.int8)
        source_prototypes = np.asarray(source["prototypes_int8"], dtype=np.int8)
        source_proto_parent = np.asarray(source["prototype_parent"], dtype=np.int64)
        source_margin = np.asarray(source["int8_margin"], dtype=np.int64)
        source_pred = np.asarray(source["int8_pred"], dtype=np.int64)
        dist = class_distances(
            source_embedding,
            source_prototypes,
            source_proto_parent,
            batch_size=batch_size,
        ).astype(np.float32)
        scale = float(np.percentile(np.maximum(source_margin, 1), float(margin_scale_percentile)))
        scale = max(scale, 1.0)

        source_block = np.zeros((len(base_sample), 3), dtype=np.int8)
        aligned = 0
        missing = 0
        skipped_parent_mismatch = 0
        wrong_rows = 0
        for base_index, key in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
            source_index = source_by_key.get((int(key[0]), str(key[1])))
            if source_index is None:
                missing += 1
                continue
            aligned += 1
            if int(source_parent[source_index]) != int(base_parent[base_index]):
                skipped_parent_mismatch += 1
                continue
            wrong_rows += int(source_pred[source_index] != base_parent[base_index])
            class_delta = dist[source_index] - float(dist[source_index, int(base_parent[base_index])])
            quant = np.rint(class_delta / scale * float(target_abs_p99))
            source_block[base_index] = np.clip(quant, 0, 127).astype(np.int8)
        if missing or skipped_parent_mismatch:
            raise ValueError(
                f"{path} alignment failed: missing={missing}, parent_mismatch={skipped_parent_mismatch}"
            )
        blocks.append(source_block)
        source_summaries.append(
            {
                "source": str(path),
                "aligned_rows": int(aligned),
                "missing_rows": int(missing),
                "parent_mismatch_rows": int(skipped_parent_mismatch),
                "source_embedding_dim": int(source_embedding.shape[1]),
                "block_dim": 3,
                "margin_scale_percentile": float(margin_scale_percentile),
                "margin_scale": float(scale),
                "wrong_rows": int(wrong_rows),
                "source_margin_min": int(np.min(source_margin)),
                "source_margin_mean": float(np.mean(source_margin)),
            }
        )

    target = np.concatenate([base_embedding.astype(np.int8), *blocks], axis=1).astype(np.int8)
    weights = np.ones(len(base_sample), dtype=np.float32)
    if base_low_margin_extra_weight:
        weights[base_margin <= int(base_low_margin_threshold)] += float(base_low_margin_extra_weight)

    rows: list[dict[str, Any]] = []
    for index in range(len(base_sample)):
        rows.append(
            {
                "row_index": int(index),
                "sample_index": int(base_sample[index]),
                "view_label": str(base_view[index]),
                "parent": int(base_parent[index]),
                "base_margin": int(base_margin[index]),
                "weight": float(weights[index]),
                "target_json": json.dumps([int(value) for value in target[index].tolist()]),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "qanchor_teacher.npz",
        sample_index=base_sample.astype(np.int64),
        view_labels=base_view.astype(str),
        parent=base_parent.astype(np.int64),
        embedding_int8=target.astype(np.int8),
        qanchor_weight=weights.astype(np.float32),
        base_embedding_dim=np.asarray(base_embedding.shape[1], dtype=np.int64),
        source_block_dim=np.asarray(sum(block.shape[1] for block in blocks), dtype=np.int64),
        source_block_count=np.asarray(len(blocks), dtype=np.int64),
        source_base_npz=np.asarray(str(base_npz)),
        source_npz=np.asarray([str(path) for path in source_npz]),
        high_pressure_usage=np.asarray("none"),
    )
    write_csv(output_dir / "source_block_qanchor_rows.csv", rows)
    summary = {
        "output": str(output_dir / "qanchor_teacher.npz"),
        "base_npz": str(base_npz),
        "source_npz": [str(path) for path in source_npz],
        "high_pressure_usage": "none",
        "row_count": int(len(base_sample)),
        "base_embedding_dim": int(base_embedding.shape[1]),
        "source_block_dim": int(sum(block.shape[1] for block in blocks)),
        "target_dim": int(target.shape[1]),
        "target_abs_p99": float(target_abs_p99),
        "margin_scale_percentile": float(margin_scale_percentile),
        "base_low_margin_threshold": int(base_low_margin_threshold),
        "base_low_margin_extra_weight": float(base_low_margin_extra_weight),
        "weight_min": float(np.min(weights)),
        "weight_max": float(np.max(weights)),
        "weight_mean": float(np.mean(weights)),
        "sources": source_summaries,
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build D4+source-class-block qanchor teacher for V8.")
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--source-npz", required=True, help="Comma-separated non-base source params npz paths.")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--target-abs-p99", type=float, default=48.0)
    parser.add_argument("--margin-scale-percentile", type=float, default=90.0)
    parser.add_argument("--base-low-margin-threshold", type=int, default=16)
    parser.add_argument("--base-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--batch-size", type=int, default=256)
    args = parser.parse_args()

    build_source_block_qanchor_teacher(
        base_npz=args.base_npz,
        source_npz=parse_sources(args.source_npz),
        output_dir=args.output_dir,
        target_abs_p99=args.target_abs_p99,
        margin_scale_percentile=args.margin_scale_percentile,
        base_low_margin_threshold=args.base_low_margin_threshold,
        base_low_margin_extra_weight=args.base_low_margin_extra_weight,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
