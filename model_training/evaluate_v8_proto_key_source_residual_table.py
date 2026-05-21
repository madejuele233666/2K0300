import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from analyze_v8_proto_source_gate import (
    build_keys,
    class_distances,
    learn_mapping,
    load_npz,
    parse_feature,
    parse_named_path,
)
from estimate_v8_board_time import calibrated_conservative_us
from evaluate_v8_embedding_prototypes import metric_summary, write_csv
from evaluate_v8_source_gated_residual_table import (
    classify_global_plus_source_residual,
    compile_residual_tables,
    evaluate_stress_rows,
    read_csv_rows,
    unique_order,
)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def labels_from_keys(
    *,
    keys: list[str],
    mapping: dict[str, str],
    fallback: str,
    source_to_index: dict[str, int],
) -> tuple[np.ndarray, dict[str, int], int]:
    labels: list[int] = []
    counts = {name: 0 for name in source_to_index}
    fallback_count = 0
    for key in keys:
        source = mapping.get(key, fallback)
        fallback_count += int(key not in mapping)
        counts[source] += 1
        labels.append(int(source_to_index[source]))
    return np.asarray(labels, dtype=np.int64), counts, fallback_count


def evaluate_once(
    *,
    params_npz: Path,
    train_config: Path,
    stress_events_csv: Path,
    normal_params: list[tuple[str, Path]],
    output_dir: Path,
    key_mode: str,
    score_mode: str,
    score_quantile: float,
    min_support: int,
    base_subset: str,
    target_margin: int,
    max_iterations: int,
    batch_size: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(train_config, output_dir / "train_config.json")
    base = load_npz(params_npz)
    config = json.loads(train_config.read_text(encoding="utf-8"))
    source_order = [name for name, _path in normal_params]
    source_to_index = {name: index for index, name in enumerate(source_order)}
    normal_payloads = {name: load_npz(path) for name, path in normal_params}

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)

    normal_pred, normal_margin, normal_nearest = class_distances(
        embeddings,
        prototypes,
        prototype_parent,
        batch_size=batch_size,
    )
    normal_keys = build_keys(
        mode=key_mode,
        nearest_proto=normal_nearest,
        pred=normal_pred,
        margin=normal_margin,
        prototype_parent=prototype_parent,
    )
    mapping, fallback, mapping_rows = learn_mapping(
        keys=normal_keys,
        base_payload=base,
        payloads=normal_payloads,
        source_order=source_order,
        score_mode=score_mode,
        score_quantile=score_quantile,
        min_support=min_support,
    )
    normal_labels, normal_label_counts, normal_fallback_count = labels_from_keys(
        keys=normal_keys,
        mapping=mapping,
        fallback=fallback,
        source_to_index=source_to_index,
    )

    (
        base_prototypes,
        base_parent,
        residual_tables,
        residual_parent,
        _residual_sample,
        _residual_view,
        compile_summary,
    ) = compile_residual_tables(
        embeddings=embeddings,
        parent=parent,
        view_labels=view_labels,
        labels=normal_labels,
        source_count=len(source_order),
        base_subset=base_subset,
        target_margin=target_margin,
        max_iterations=max_iterations,
    )
    normal_replay_pred, normal_replay_margin = classify_global_plus_source_residual(
        features=embeddings,
        labels=normal_labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )
    runtime_effective_prototypes = int(compile_summary["runtime_effective_prototypes"])
    feature_dim = int(embeddings.shape[1])
    estimated_distance_macs = int(runtime_effective_prototypes * feature_dim)
    backbone_conservative = calibrated_conservative_us(config)
    table_us = max(20.0, float(estimated_distance_macs) * 0.02)
    board_total_conservative = backbone_conservative + table_us
    view_order = unique_order(view_labels)
    normal_row: dict[str, Any] = {
        "stage": "v8_proto_key_source_residual_table",
        "name": f"proto_key_source_residual_{key_mode}_{score_mode}_q{score_quantile:g}_m{min_support}_{base_subset}_t{target_margin}",
        "prototype_source": "proto_key_global_base_plus_source_residual",
        "key_mode": key_mode,
        "score_mode": score_mode,
        "score_quantile": float(score_quantile),
        "min_support": int(min_support),
        "fallback_source": fallback,
        "mapping_size": int(len(mapping)),
        "normal_fallback_count": int(normal_fallback_count),
        "feature_dim": feature_dim,
        "base_prototypes": int(len(base_prototypes)),
        "max_source_residual": int(compile_summary["max_source_residual"]),
        "runtime_effective_prototypes": runtime_effective_prototypes,
        "total_stored_prototypes": int(compile_summary["total_stored_prototypes"]),
        "prototype_count": runtime_effective_prototypes,
        "estimated_distance_macs": estimated_distance_macs,
        "int8_margin_min": int(np.min(normal_replay_margin)),
        "int8_margin_mean": float(np.mean(normal_replay_margin)),
        "residual_table_sizes_json": json.dumps(compile_summary["residual_table_sizes"]),
        "normal_source_counts_json": json.dumps(normal_label_counts, ensure_ascii=False),
        "board_backbone_conservative_us": int(round(backbone_conservative)),
        "board_table_worst_us": int(round(table_us)),
        "board_total_conservative_us": int(round(board_total_conservative)),
        "under_2ms_conservative": bool(board_total_conservative <= 2000.0),
        "high_pressure_usage": "evaluation_only",
        "selection_usage": "normal_proto_key_source_routing_only",
    }
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=normal_replay_pred))
    normal_row.update(
        metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=normal_replay_pred, prefix="int8_")
    )

    stress_rows = read_csv_rows(stress_events_csv)
    stress_features = np.stack([parse_feature(row) for row in stress_rows], axis=0).astype(np.int8)
    stress_pred, stress_margin, stress_nearest = class_distances(
        stress_features,
        prototypes,
        prototype_parent,
        batch_size=batch_size,
    )
    stress_keys = build_keys(
        mode=key_mode,
        nearest_proto=stress_nearest,
        pred=stress_pred,
        margin=stress_margin,
        prototype_parent=prototype_parent,
    )
    stress_labels, stress_label_counts, stress_fallback_count = labels_from_keys(
        keys=stress_keys,
        mapping=mapping,
        fallback=fallback,
        source_to_index=source_to_index,
    )
    stress_summary, stress_out = evaluate_stress_rows(
        stress_rows=stress_rows,
        features=stress_features,
        labels=stress_labels,
        base_prototypes=base_prototypes,
        base_parent=base_parent,
        residual_tables=residual_tables,
        residual_parent=residual_parent,
    )

    write_csv(output_dir / "candidate_results.csv", [normal_row])
    write_csv(output_dir / "proto_key_mapping.csv", mapping_rows)
    write_csv(output_dir / "stress_events.csv", stress_out)
    write_json(
        output_dir / "summary.json",
        {
            "params_npz": str(params_npz),
            "train_config": str(train_config),
            "stress_events_csv": str(stress_events_csv),
            "normal_params": {name: str(path) for name, path in normal_params},
            "source_order": source_order,
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_proto_key_source_routing_only",
            "key_mode": key_mode,
            "score_mode": score_mode,
            "score_quantile": float(score_quantile),
            "min_support": int(min_support),
            "base_subset": base_subset,
            "target_margin": int(target_margin),
            "compile": compile_summary,
            "normal": normal_row,
            "stress": {
                **stress_summary,
                "stress_source_counts": stress_label_counts,
                "stress_fallback_count": int(stress_fallback_count),
            },
        },
    )
    print(json.dumps({"normal": normal_row, "stress": stress_summary}, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate a shared D4 base plus source residual table selected by normal-only D4 proto-key routing."
    )
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--train-config", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--key-mode", default="proto_parent+pred+margin_bucket")
    parser.add_argument("--score-mode", default="raw")
    parser.add_argument("--score-quantile", type=float, default=5.0)
    parser.add_argument("--min-support", type=int, default=1)
    parser.add_argument("--base-subset", choices=["clean", "clean_rotmirror"], default="clean")
    parser.add_argument("--target-margin", type=int, default=0)
    parser.add_argument("--max-iterations", type=int, default=12)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()
    evaluate_once(
        params_npz=args.params_npz,
        train_config=args.train_config,
        stress_events_csv=args.stress_events_csv,
        normal_params=[parse_named_path(item) for item in args.normal_params],
        output_dir=args.output_dir,
        key_mode=args.key_mode,
        score_mode=args.score_mode,
        score_quantile=float(args.score_quantile),
        min_support=int(args.min_support),
        base_subset=args.base_subset,
        target_margin=int(args.target_margin),
        max_iterations=int(args.max_iterations),
        batch_size=int(args.batch_size),
    )


if __name__ == "__main__":
    main()
