import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import PARENT_NAMES, write_csv
from prune_merge_v8_logit_prototypes import evaluate_int8, load_state, row_for_state, state_payload
from select_v8_setcover_prototypes import (
    full_class_distances,
    greedy_parent_cover,
    subset_state,
    unique_view_order,
)
from select_v8_sourcechoice_setcover_prototypes import load_source_labels


PARENT_COUNT = len(PARENT_NAMES)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_ints(value: str) -> list[int]:
    return [int(item.strip()) for item in value.split(",") if item.strip()]


def same_source_usage(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    row_source_label: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    prototype_source_label: np.ndarray,
    batch_size: int,
) -> np.ndarray:
    usage = np.zeros(len(prototypes), dtype=np.int64)
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    labels = [label for label in sorted(set(row_source_label.tolist())) if int(label) >= 0]
    for parent in range(PARENT_COUNT):
        for source_label in labels:
            query_indexes = np.where((y_parent == parent) & (row_source_label == source_label))[0]
            proto_indexes = np.where((prototype_parent == parent) & (prototype_source_label == source_label))[0]
            if len(query_indexes) == 0 or len(proto_indexes) == 0:
                continue
            proto = p_all[proto_indexes]
            nearest_chunks: list[np.ndarray] = []
            for start in range(0, len(query_indexes), batch_size):
                idx = query_indexes[start : start + batch_size]
                x = x_all[idx]
                dist = np.sum((x[:, None, :] - proto[None, :, :]) ** 2, axis=2).astype(np.int64)
                nearest_chunks.append(proto_indexes[np.argmin(dist, axis=1)])
            nearest = np.concatenate(nearest_chunks).astype(np.int64)
            usage += np.bincount(nearest, minlength=len(prototypes)).astype(np.int64)
    return usage


def source_anchor_indexes(
    *,
    usage: np.ndarray,
    prototype_parent: np.ndarray,
    prototype_source_label: np.ndarray,
    cap_per_cluster: int,
) -> tuple[list[int], dict[str, Any]]:
    if cap_per_cluster <= 0:
        return [], {"cap_per_cluster": int(cap_per_cluster), "selected_count": 0, "clusters": []}
    selected: list[int] = []
    clusters: list[dict[str, Any]] = []
    labels = [label for label in sorted(set(prototype_source_label.tolist())) if int(label) >= 0]
    for parent in range(PARENT_COUNT):
        for source_label in labels:
            indexes = np.where((prototype_parent == parent) & (prototype_source_label == source_label) & (usage > 0))[0]
            order = sorted(indexes.tolist(), key=lambda index: (-int(usage[index]), int(index)))
            keep = order[: int(cap_per_cluster)]
            selected.extend(keep)
            clusters.append(
                {
                    "parent": int(parent),
                    "source_label": int(source_label),
                    "available": int(len(indexes)),
                    "selected": int(len(keep)),
                    "usage_sum_selected": int(np.sum(usage[np.asarray(keep, dtype=np.int64)])) if keep else 0,
                }
            )
    return selected, {
        "cap_per_cluster": int(cap_per_cluster),
        "selected_count": int(len(selected)),
        "clusters": clusters,
    }


def build_sourcechoice_anchor_setcover_tables(
    *,
    input_npz: Path,
    source_gate_teacher_npz: Path,
    output_dir: Path,
    name: str,
    parent_margin_targets: list[int],
    anchor_caps: list[int],
    batch_size: int,
    allow_missing_source_labels: bool,
    allow_evaluation_only_teacher: bool,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    base, state = load_state(input_npz)
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    proto_parent = np.asarray(state["prototype_parent"], dtype=np.int64)
    proto_sample = np.asarray(state["prototype_sample_index"], dtype=np.int64)
    proto_view = np.asarray(state["prototype_view_label"]).astype(str)
    row_source_label, proto_source_label, source_label_summary = load_source_labels(
        teacher_npz=source_gate_teacher_npz,
        sample_index=sample_index,
        view_labels=view_labels,
        parent=y_parent,
        prototype_sample_index=proto_sample,
        prototype_view_label=proto_view,
        prototype_parent=proto_parent,
        allow_missing=allow_missing_source_labels,
        allow_evaluation_only=allow_evaluation_only_teacher,
    )
    class_dist = full_class_distances(
        embeddings,
        state["prototypes_int8"],
        proto_parent,
        batch_size=batch_size,
    )
    usage = same_source_usage(
        embeddings=embeddings,
        y_parent=y_parent,
        row_source_label=row_source_label,
        prototypes=state["prototypes_int8"],
        prototype_parent=proto_parent,
        prototype_source_label=proto_source_label,
        batch_size=batch_size,
    )

    rows: list[dict[str, Any]] = []
    payloads: dict[str, tuple[dict[str, Any], dict[str, np.ndarray], Any]] = {}
    diagnostics: dict[str, Any] = {
        "source_label_summary": source_label_summary,
        "same_source_usage_nonzero": int(np.sum(usage > 0)),
        "same_source_usage_sum": int(np.sum(usage)),
    }
    for parent_margin_target in parent_margin_targets:
        parent_selected: list[int] = []
        parent_cover: dict[str, Any] = {}
        try:
            for parent in range(PARENT_COUNT):
                selected, info = greedy_parent_cover(
                    embeddings=embeddings,
                    y_parent=y_parent,
                    prototypes=state["prototypes_int8"],
                    prototype_parent=proto_parent,
                    class_dist=class_dist,
                    parent=parent,
                    margin_target=parent_margin_target,
                    batch_size=batch_size,
                )
                parent_selected.extend(selected)
                parent_cover[str(parent)] = info
        except ValueError as exc:
            diagnostics[f"pm{parent_margin_target}"] = {
                "failed": True,
                "error": str(exc),
                "parent_margin_target": int(parent_margin_target),
            }
            continue
        for cap in anchor_caps:
            anchors, anchor_info = source_anchor_indexes(
                usage=usage,
                prototype_parent=proto_parent,
                prototype_source_label=proto_source_label,
                cap_per_cluster=cap,
            )
            keep = np.asarray(sorted(set(parent_selected + anchors)), dtype=np.int64)
            covered_state = subset_state(state, keep)
            result = evaluate_int8(
                embeddings,
                y_parent,
                covered_state["prototypes_int8"],
                covered_state["prototype_parent"],
                max(parent_margin_targets + [8]),
            )
            source = f"pm{parent_margin_target}_anchor{cap}"
            row = row_for_state(
                name=name,
                source=source,
                base=base,
                state=covered_state,
                eval_result=result,
                extra={
                    "sourcechoice_parent_margin_target": int(parent_margin_target),
                    "sourcechoice_anchor_cap": int(cap),
                    "sourcechoice_parent_selected_count": int(len(set(parent_selected))),
                    "sourcechoice_anchor_added_count": int(len(set(anchors) - set(parent_selected))),
                    "sourcechoice_anchor_info_json": json.dumps(anchor_info, ensure_ascii=False),
                    "sourcechoice_parent_cover_json": json.dumps(parent_cover, ensure_ascii=False),
                    "source_gate_teacher_npz": str(source_gate_teacher_npz),
                },
            )
            rows.append(row)
            payloads[source] = (row, covered_state, result)
            diagnostics[source] = {
                "failed": False,
                "parent_cover": parent_cover,
                "anchor_info": anchor_info,
                "row": row,
            }

    if not rows:
        write_json(
            output_dir / "summary.json",
            {
                "input_npz": str(input_npz),
                "source_gate_teacher_npz": str(source_gate_teacher_npz),
                "candidate_count": 0,
                "diagnostics": diagnostics,
                "view_order": unique_view_order(view_labels),
            },
        )
        raise SystemExit("no source-choice anchor set-cover candidate succeeded")

    def row_score(row: dict[str, Any]) -> tuple[float, int, int, float, int]:
        acc = min(
            float(row["clean_accuracy"]),
            float(row["rotmirror_min_accuracy"]),
            float(row["stress_min_accuracy"]),
            float(row["int8_clean_accuracy"]),
            float(row["int8_rotmirror_min_accuracy"]),
            float(row["int8_stress_min_accuracy"]),
        )
        return (
            acc,
            int(row["int8_margin_min"]),
            -int(row["prototype_count"]),
            float(row["int8_margin_mean"]),
            -int(row["estimated_distance_macs"]),
        )

    rows_sorted = sorted(rows, key=row_score, reverse=True)
    write_csv(output_dir / "candidate_results.csv", rows_sorted)
    best = rows_sorted[0]
    _row, best_state, best_result = payloads[str(best["prototype_source"])]
    np.savez_compressed(output_dir / "best_parent_logits_memory_params.npz", **state_payload(base, best_state, best_result))
    for source, (_source_row, source_state, source_result) in payloads.items():
        np.savez_compressed(output_dir / f"{source}_parent_logits_memory_params.npz", **state_payload(base, source_state, source_result))

    row_payload = {
        "input_npz": str(input_npz),
        "source_gate_teacher_npz": str(source_gate_teacher_npz),
        "candidate_count": int(len(rows_sorted)),
        "best": best,
        "top": rows_sorted,
        "diagnostics": diagnostics,
        "view_order": unique_view_order(view_labels),
    }
    write_json(output_dir / "summary.json", row_payload)
    config_src = input_npz.parent / "train_config.json"
    if config_src.exists():
        shutil.copy2(config_src, output_dir / "train_config.json")
    print(json.dumps({"output_dir": str(output_dir), "best": best, "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Select V8 source-choice anchor subsets on top of normal-only parent set-cover."
    )
    parser.add_argument("input_npz", type=Path)
    parser.add_argument("--source-gate-teacher-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--parent-margin-targets", default="2,3")
    parser.add_argument("--anchor-caps", default="0,4,8,16,24,32")
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--allow-missing-source-labels", action="store_true")
    parser.add_argument("--allow-evaluation-only-teacher", action="store_true")
    args = parser.parse_args()
    build_sourcechoice_anchor_setcover_tables(
        input_npz=args.input_npz,
        source_gate_teacher_npz=args.source_gate_teacher_npz,
        output_dir=args.output_dir,
        name=args.name or args.input_npz.parent.name,
        parent_margin_targets=parse_ints(args.parent_margin_targets),
        anchor_caps=parse_ints(args.anchor_caps),
        batch_size=args.batch_size,
        allow_missing_source_labels=bool(args.allow_missing_source_labels),
        allow_evaluation_only_teacher=bool(args.allow_evaluation_only_teacher),
    )


if __name__ == "__main__":
    main()
