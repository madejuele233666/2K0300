import argparse
import csv
import json
from pathlib import Path
from typing import Any

import numpy as np


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


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
        writer.writerows(rows)


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def row_key_map(payload: dict[str, np.ndarray]) -> dict[tuple[int, str], int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {(int(sample_id), str(view_label)): int(index) for index, (sample_id, view_label) in enumerate(zip(sample, view, strict=False))}


def align_source(
    base: dict[str, np.ndarray],
    source: dict[str, np.ndarray],
    source_name: str,
    *,
    allow_missing_source_rows: bool,
) -> tuple[np.ndarray, np.ndarray, int]:
    source_by_key = row_key_map(source)
    base_sample = np.asarray(base["sample_index"], dtype=np.int64)
    base_view = np.asarray(base["view_labels"]).astype(str)
    base_parent = np.asarray(base["parent"], dtype=np.int64)
    source_parent = np.asarray(source["parent"], dtype=np.int64)
    base_indexes: list[int] = []
    source_indexes: list[int] = []
    missing: list[tuple[int, str]] = []
    parent_mismatch: list[tuple[int, str]] = []
    for row_index, (sample, view) in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
        key = (int(sample), str(view))
        source_index = source_by_key.get(key)
        if source_index is None:
            missing.append(key)
            continue
        if int(base_parent[row_index]) != int(source_parent[source_index]):
            parent_mismatch.append(key)
        base_indexes.append(int(row_index))
        source_indexes.append(int(source_index))
    if missing and not allow_missing_source_rows:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing[:10])
        raise ValueError(f"{source_name} is missing {len(missing)} base rows, first {preview}")
    if parent_mismatch:
        preview = ", ".join(f"{sample}:{view}" for sample, view in parent_mismatch[:10])
        raise ValueError(f"{source_name} parent mismatch on {len(parent_mismatch)} rows, first {preview}")
    if not base_indexes:
        raise ValueError(f"{source_name} has no rows in common with base")
    return np.asarray(base_indexes, dtype=np.int64), np.asarray(source_indexes, dtype=np.int64), len(missing)


def class_distances_int8(embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> np.ndarray:
    x = embeddings.astype(np.int32)
    p = prototypes.astype(np.int32)
    dist = np.sum((x[:, None, :] - p[None, :, :]) ** 2, axis=2)
    by_class: list[np.ndarray] = []
    for parent in range(3):
        mask = prototype_parent == parent
        if np.any(mask):
            by_class.append(np.min(dist[:, mask], axis=1).astype(np.float32))
        else:
            by_class.append(np.full(len(embeddings), np.finfo(np.float32).max, dtype=np.float32))
    return np.stack(by_class, axis=1).astype(np.float32)


def robust_scale_columns(values: np.ndarray) -> np.ndarray:
    x = values.astype(np.float32)
    scale = np.percentile(np.abs(x), 95, axis=0).astype(np.float32)
    scale = np.maximum(scale, 1.0)
    return x / scale[None, :]


def pca_int8(values: np.ndarray, extra_dim: int, target_abs_p99: float) -> tuple[np.ndarray, dict[str, Any]]:
    if extra_dim <= 0:
        return np.zeros((len(values), 0), dtype=np.int8), {
            "extra_dim": 0,
            "scale": [],
            "explained_variance_ratio": [],
        }
    x = values.astype(np.float32)
    center = np.mean(x, axis=0, keepdims=True)
    centered = x - center
    _u, s, vh = np.linalg.svd(centered, full_matrices=False)
    components = vh[:extra_dim].astype(np.float32)
    score = centered @ components.T
    scale = np.percentile(np.abs(score), 99, axis=0).astype(np.float32)
    scale = np.maximum(scale, 1.0)
    quant = np.clip(np.rint(score / scale[None, :] * float(target_abs_p99)), -128, 127).astype(np.int8)
    denom = float(np.sum(s**2)) if len(s) else 1.0
    explained = ((s[:extra_dim] ** 2) / max(denom, 1.0)).astype(np.float64)
    return quant, {
        "extra_dim": int(extra_dim),
        "source_dim": int(values.shape[1]),
        "target_abs_p99": float(target_abs_p99),
        "scale": [float(v) for v in scale.tolist()],
        "explained_variance_ratio": [float(v) for v in explained.tolist()],
    }


def build_composite_qanchor(
    *,
    base_npz: Path,
    pca_source_npzs: list[Path],
    output_dir: Path,
    extra_dim: int,
    target_abs_p99: float,
    source_feature_mode: str,
    base_low_margin_threshold: int,
    base_low_margin_extra_weight: float,
    source_low_margin_threshold: int,
    source_low_margin_extra_weight: float,
    allow_missing_source_rows: bool,
) -> None:
    base = load_npz(base_npz)
    if not pca_source_npzs:
        raise ValueError("at least one --pca-source-npz is required")

    aligned_base_sets: list[set[int]] = []
    aligned: list[dict[str, Any]] = []
    for source_path in pca_source_npzs:
        source = load_npz(source_path)
        base_idx, source_idx, missing = align_source(
            base,
            source,
            str(source_path),
            allow_missing_source_rows=allow_missing_source_rows,
        )
        aligned_base_sets.append(set(int(value) for value in base_idx.tolist()))
        aligned.append(
            {
                "path": source_path,
                "payload": source,
                "base_indexes": base_idx,
                "source_indexes": source_idx,
                "missing": int(missing),
            }
        )

    common_base = sorted(set.intersection(*aligned_base_sets))
    if not common_base:
        raise ValueError("sources have no common rows with base")
    common_base_index = np.asarray(common_base, dtype=np.int64)
    base_embedding = np.asarray(base["embedding_int8"], dtype=np.int8)[common_base_index]
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)[common_base_index]
    base_pred = np.asarray(base["int8_pred"], dtype=np.int64)[common_base_index]
    parent = np.asarray(base["parent"], dtype=np.int64)[common_base_index]

    source_feature_blocks: list[np.ndarray] = []
    source_summaries: list[dict[str, Any]] = []
    source_margin_stack: list[np.ndarray] = []
    source_pred_stack: list[np.ndarray] = []
    for item in aligned:
        source = item["payload"]
        source_pos_by_base = {
            int(base_row): int(source_row)
            for base_row, source_row in zip(
                np.asarray(item["base_indexes"], dtype=np.int64).tolist(),
                np.asarray(item["source_indexes"], dtype=np.int64).tolist(),
                strict=False,
            )
        }
        source_indexes = np.asarray([source_pos_by_base[int(row)] for row in common_base], dtype=np.int64)
        source_embedding = np.asarray(source["embedding_int8"], dtype=np.int8)[source_indexes]
        source_margin = np.asarray(source["int8_margin"], dtype=np.int64)[source_indexes]
        source_pred = np.asarray(source["int8_pred"], dtype=np.int64)[source_indexes]
        source_margin_stack.append(source_margin.astype(np.int64))
        source_pred_stack.append(source_pred.astype(np.int64))

        blocks: list[np.ndarray] = []
        if source_feature_mode in {"embedding", "embedding_class_delta", "embedding_class_delta_margin"}:
            blocks.append(robust_scale_columns(source_embedding.astype(np.float32)))
        if source_feature_mode in {"class_delta", "embedding_class_delta", "class_delta_margin", "embedding_class_delta_margin"}:
            class_dist = class_distances_int8(
                source_embedding,
                np.asarray(source["prototypes_int8"], dtype=np.int8),
                np.asarray(source["prototype_parent"], dtype=np.int64),
            )
            true_dist = class_dist[np.arange(len(parent)), parent]
            class_delta = class_dist - true_dist[:, None]
            blocks.append(robust_scale_columns(class_delta.astype(np.float32)))
        if source_feature_mode in {"class_delta_margin", "embedding_class_delta_margin"}:
            blocks.append(robust_scale_columns(source_margin.astype(np.float32)[:, None]))
        if not blocks:
            raise ValueError(f"unknown source_feature_mode: {source_feature_mode}")
        feature_block = np.concatenate(blocks, axis=1).astype(np.float32)
        source_feature_blocks.append(feature_block)
        source_summaries.append(
            {
                "path": str(item["path"]),
                "source_rows": int(len(np.asarray(source["sample_index"]))),
                "missing_base_rows": int(item["missing"]),
                "common_rows": int(len(common_base_index)),
                "embedding_dim": int(source_embedding.shape[1]),
                "feature_dim": int(feature_block.shape[1]),
                "wrong_rows": int(np.sum(source_pred != parent)),
                "margin_min": int(np.min(source_margin)),
                "margin_p10": float(np.percentile(source_margin, 10)),
                "margin_mean": float(np.mean(source_margin)),
            }
        )

    source_feature = np.concatenate(source_feature_blocks, axis=1).astype(np.float32)
    source_margin_min = np.min(np.stack(source_margin_stack, axis=1), axis=1)
    source_pred_wrong_count = np.sum(np.stack(source_pred_stack, axis=1) != parent[:, None], axis=1)

    pca_embedding, pca_summary = pca_int8(source_feature, extra_dim, target_abs_p99)
    target = np.concatenate([base_embedding, pca_embedding], axis=1).astype(np.int8)

    weights = np.ones(len(target), dtype=np.float32)
    if base_low_margin_extra_weight:
        weights[base_margin <= int(base_low_margin_threshold)] += float(base_low_margin_extra_weight)
    if source_low_margin_extra_weight:
        weights[source_margin_min <= int(source_low_margin_threshold)] += float(source_low_margin_extra_weight)

    rows = []
    view_labels = np.asarray(base["view_labels"]).astype(str)[common_base_index]
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)[common_base_index]
    for index in range(len(target)):
        rows.append(
            {
                "row_index": int(index),
                "sample_index": int(sample_index[index]),
                "view_label": str(view_labels[index]),
                "parent": int(parent[index]),
                "base_margin": int(base_margin[index]),
                "source_margin_min": int(source_margin_min[index]),
                "base_pred": int(base_pred[index]),
                "source_pred_wrong_count": int(source_pred_wrong_count[index]),
                "weight": float(weights[index]),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "qanchor_teacher.npz",
        sample_index=sample_index.astype(np.int64),
        view_labels=view_labels.astype(str),
        parent=parent.astype(np.int64),
        embedding_int8=target.astype(np.int8),
        qanchor_weight=weights.astype(np.float32),
        base_embedding_dim=np.asarray(base_embedding.shape[1], dtype=np.int64),
        pca_embedding_dim=np.asarray(extra_dim, dtype=np.int64),
        source_feature_mode=np.asarray(str(source_feature_mode)),
        source_base_npz=np.asarray(str(base_npz)),
        source_pca_npz=np.asarray(str(pca_source_npzs[0])),
        source_pca_npzs=np.asarray([str(path) for path in pca_source_npzs]),
        high_pressure_usage=np.asarray("none"),
    )
    write_csv(output_dir / "qanchor_rows.csv", rows)
    summary = {
        "output": str(output_dir / "qanchor_teacher.npz"),
        "base_npz": str(base_npz),
        "pca_source_npz": str(pca_source_npzs[0]),
        "pca_source_npzs": [str(path) for path in pca_source_npzs],
        "high_pressure_usage": "none",
        "source_feature_mode": str(source_feature_mode),
        "row_count": int(len(target)),
        "base_source_row_count": int(len(np.asarray(base["sample_index"]))),
        "source_missing_base_rows": int(sum(int(item["missing"]) for item in aligned)),
        "allow_missing_source_rows": bool(allow_missing_source_rows),
        "base_embedding_dim": int(base_embedding.shape[1]),
        "source_feature_dim": int(source_feature.shape[1]),
        "pca_embedding_dim": int(extra_dim),
        "target_embedding_dim": int(target.shape[1]),
        "sources": source_summaries,
        "base_low_margin_threshold": int(base_low_margin_threshold),
        "base_low_margin_extra_weight": float(base_low_margin_extra_weight),
        "source_low_margin_threshold": int(source_low_margin_threshold),
        "source_low_margin_extra_weight": float(source_low_margin_extra_weight),
        "weight_min": float(np.min(weights)),
        "weight_max": float(np.max(weights)),
        "weight_mean": float(np.mean(weights)),
        "weighted_rows": int(np.sum(weights > 1.0)),
        "base_wrong_rows": int(np.sum(base_pred != parent)),
        "source_wrong_rows": int(np.sum(source_pred_wrong_count > 0)),
        "pca": pca_summary,
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a normal-only composite qanchor teacher for V8 shared-head parent logits.")
    parser.add_argument("--base-npz", type=Path, required=True)
    parser.add_argument("--pca-source-npz", type=Path, action="append", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--extra-dim", type=int, default=2)
    parser.add_argument("--target-abs-p99", type=float, default=96.0)
    parser.add_argument(
        "--source-feature-mode",
        choices=["embedding", "class_delta", "embedding_class_delta", "class_delta_margin", "embedding_class_delta_margin"],
        default="embedding",
    )
    parser.add_argument("--base-low-margin-threshold", type=int, default=16)
    parser.add_argument("--base-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--source-low-margin-threshold", type=int, default=16)
    parser.add_argument("--source-low-margin-extra-weight", type=float, default=1.0)
    parser.add_argument("--allow-missing-source-rows", action="store_true")
    args = parser.parse_args()

    build_composite_qanchor(
        base_npz=args.base_npz,
        pca_source_npzs=args.pca_source_npz,
        output_dir=args.output_dir,
        extra_dim=args.extra_dim,
        target_abs_p99=args.target_abs_p99,
        source_feature_mode=args.source_feature_mode,
        base_low_margin_threshold=args.base_low_margin_threshold,
        base_low_margin_extra_weight=args.base_low_margin_extra_weight,
        source_low_margin_threshold=args.source_low_margin_threshold,
        source_low_margin_extra_weight=args.source_low_margin_extra_weight,
        allow_missing_source_rows=args.allow_missing_source_rows,
    )


if __name__ == "__main__":
    main()
