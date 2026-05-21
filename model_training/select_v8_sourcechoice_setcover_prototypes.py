import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import PARENT_NAMES, metric_summary, write_csv
from prune_merge_v8_logit_prototypes import evaluate_int8, load_state, row_for_state, state_payload


PARENT_COUNT = len(PARENT_NAMES)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_ints(value: str) -> list[int]:
    return [int(item.strip()) for item in value.split(",") if item.strip()]


def unique_view_order(view_labels: np.ndarray) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for item in view_labels.tolist():
        name = str(item)
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out


def subset_state(state: dict[str, np.ndarray], keep: np.ndarray) -> dict[str, np.ndarray]:
    return {key: value[keep].copy() for key, value in state.items()}


def full_class_distances(
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    out = np.full((len(x_all), PARENT_COUNT), np.iinfo(np.int64).max, dtype=np.int64)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(PARENT_COUNT)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            out[start : start + len(x), parent] = np.min(dist[:, indexes], axis=1)
    return out


def load_source_labels(
    *,
    teacher_npz: Path,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    parent: np.ndarray,
    prototype_sample_index: np.ndarray,
    prototype_view_label: np.ndarray,
    prototype_parent: np.ndarray,
    allow_missing: bool = False,
    allow_evaluation_only: bool = False,
) -> tuple[np.ndarray, np.ndarray, dict[str, Any]]:
    with np.load(teacher_npz, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent", "source_label", "source_names", "high_pressure_usage"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{teacher_npz} missing source gate teacher arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_parent = np.asarray(data["parent"], dtype=np.int64)
        teacher_label = np.asarray(data["source_label"], dtype=np.int64)
        source_names = np.asarray(data["source_names"]).astype(str)
        high_pressure_usage = str(np.asarray(data["high_pressure_usage"]).item())
    if high_pressure_usage != "none" and not (allow_evaluation_only and "evaluation_only" in high_pressure_usage):
        raise ValueError(f"source labels must be normal-only, got high_pressure_usage={high_pressure_usage}")
    by_key = {
        (int(sample), str(view)): (int(label), int(row_parent))
        for sample, view, label, row_parent in zip(
            teacher_sample.tolist(),
            teacher_view.tolist(),
            teacher_label.tolist(),
            teacher_parent.tolist(),
            strict=False,
        )
    }

    def map_rows(samples: np.ndarray, views: np.ndarray, parents: np.ndarray, label_name: str) -> np.ndarray:
        out = np.full(len(samples), -1, dtype=np.int64)
        missing: list[tuple[int, str]] = []
        parent_mismatch: list[tuple[int, str, int, int]] = []
        for index, (sample, view, row_parent) in enumerate(
            zip(samples.tolist(), views.tolist(), parents.tolist(), strict=False)
        ):
            found = by_key.get((int(sample), str(view)))
            if found is None:
                if allow_missing:
                    continue
                missing.append((int(sample), str(view)))
                continue
            label, teacher_parent_value = found
            if int(teacher_parent_value) != int(row_parent):
                parent_mismatch.append((int(sample), str(view), int(row_parent), int(teacher_parent_value)))
                continue
            out[index] = int(label)
        if missing:
            preview = ", ".join(f"{sample}:{view}" for sample, view in missing[:10])
            raise ValueError(f"{label_name} rows missing from source teacher: {len(missing)}, first {preview}")
        if parent_mismatch:
            preview = ", ".join(
                f"{sample}:{view}:{row_parent}!={teacher_parent_value}"
                for sample, view, row_parent, teacher_parent_value in parent_mismatch[:10]
            )
            raise ValueError(f"{label_name} parent mismatch in source teacher: {len(parent_mismatch)}, first {preview}")
        return out

    row_labels = map_rows(sample_index, view_labels, parent, "embedding")
    proto_labels = map_rows(prototype_sample_index, prototype_view_label, prototype_parent, "prototype")
    summary = {
        "source_gate_teacher_npz": str(teacher_npz),
        "high_pressure_usage": high_pressure_usage,
        "source_names": source_names.tolist(),
        "allow_missing": bool(allow_missing),
        "allow_evaluation_only": bool(allow_evaluation_only),
        "missing_row_labels": int(np.sum(row_labels < 0)),
        "missing_prototype_labels": int(np.sum(proto_labels < 0)),
        "row_label_counts": {
            str(source_names[int(label)]): int(np.sum(row_labels == label))
            for label in sorted(set(row_source for row_source in row_labels.tolist() if row_source >= 0))
        },
        "prototype_label_counts": {
            str(source_names[int(label)]): int(np.sum(proto_labels == label))
            for label in sorted(set(proto_source for proto_source in proto_labels.tolist() if proto_source >= 0))
        },
    }
    return row_labels, proto_labels, summary


def greedy_sourcechoice_cover(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    row_source_label: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    prototype_source_label: np.ndarray,
    class_dist: np.ndarray,
    parent: int,
    source_label: int,
    parent_margin_target: int,
    source_margin_target: int,
    batch_size: int,
) -> tuple[list[int], dict[str, Any]]:
    query_indexes = np.where((y_parent == parent) & (row_source_label == source_label))[0]
    proto_indexes = np.where((prototype_parent == parent) & (prototype_source_label == source_label))[0]
    if len(query_indexes) == 0:
        return [], {
            "query_count": 0,
            "prototype_count": int(len(proto_indexes)),
            "selected_count": 0,
            "parent_margin_target": int(parent_margin_target),
            "source_margin_target": int(source_margin_target),
        }
    if len(proto_indexes) == 0:
        raise ValueError(f"parent {parent} source {source_label} has queries but no prototypes")

    wrong_parent_dist = np.min(
        class_dist[query_indexes][:, [item for item in range(PARENT_COUNT) if item != parent]],
        axis=1,
    )
    parent_threshold = wrong_parent_dist - int(parent_margin_target)

    other_source_indexes = np.where((prototype_parent == parent) & (prototype_source_label != source_label))[0]
    if len(other_source_indexes) == 0:
        source_threshold = np.full(len(query_indexes), np.iinfo(np.int64).max, dtype=np.int64)
    else:
        x_all = embeddings[query_indexes].astype(np.int32)
        other = prototypes[other_source_indexes].astype(np.int32)
        other_best: list[np.ndarray] = []
        for start in range(0, len(x_all), batch_size):
            x = x_all[start : start + batch_size]
            dist = np.sum((x[:, None, :] - other[None, :, :]) ** 2, axis=2).astype(np.int64)
            other_best.append(np.min(dist, axis=1))
        source_threshold = np.concatenate(other_best).astype(np.int64) - int(source_margin_target)

    threshold = np.minimum(parent_threshold, source_threshold)
    coverage: list[np.ndarray] = []
    x_all = embeddings[query_indexes].astype(np.int32)
    p_all = prototypes[proto_indexes].astype(np.int32)
    for start in range(0, len(p_all), batch_size):
        p = p_all[start : start + batch_size]
        dist = np.sum((x_all[:, None, :] - p[None, :, :]) ** 2, axis=2).astype(np.int64)
        ok = dist < threshold[:, None]
        coverage.extend(np.flatnonzero(ok[:, local]).astype(np.int64) for local in range(ok.shape[1]))

    uncovered = np.ones(len(query_indexes), dtype=bool)
    selected: list[int] = []
    selected_local: set[int] = set()
    while np.any(uncovered):
        best_local = -1
        best_gain = -1
        for local, covered_queries in enumerate(coverage):
            if local in selected_local or len(covered_queries) == 0:
                continue
            gain = int(np.sum(uncovered[covered_queries]))
            if gain > best_gain:
                best_gain = gain
                best_local = local
        if best_local < 0 or best_gain <= 0:
            missing = query_indexes[np.flatnonzero(uncovered)[:10]]
            raise ValueError(
                f"parent {parent} source {source_label} cannot cover {int(np.sum(uncovered))} rows "
                f"at parent/source targets {parent_margin_target}/{source_margin_target}; "
                f"first query indexes: {missing.tolist()}"
            )
        selected_local.add(best_local)
        selected.append(int(proto_indexes[best_local]))
        uncovered[coverage[best_local]] = False

    return selected, {
        "query_count": int(len(query_indexes)),
        "prototype_count": int(len(proto_indexes)),
        "selected_count": int(len(selected)),
        "parent_margin_target": int(parent_margin_target),
        "source_margin_target": int(source_margin_target),
    }


def build_sourcechoice_setcover_tables(
    *,
    input_npz: Path,
    source_gate_teacher_npz: Path,
    output_dir: Path,
    name: str,
    parent_margin_targets: list[int],
    source_margin_targets: list[int],
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

    rows: list[dict[str, Any]] = []
    payloads: dict[str, tuple[dict[str, Any], dict[str, np.ndarray], Any]] = {}
    diagnostics: dict[str, Any] = {"source_label_summary": source_label_summary}
    labels = [label for label in sorted(set(row_source_label.tolist())) if int(label) >= 0]
    for parent_margin_target in parent_margin_targets:
        for source_margin_target in source_margin_targets:
            selected: list[int] = []
            per_cluster: dict[str, Any] = {}
            try:
                for parent in range(PARENT_COUNT):
                    for source_label in labels:
                        cluster_selected, info = greedy_sourcechoice_cover(
                            embeddings=embeddings,
                            y_parent=y_parent,
                            row_source_label=row_source_label,
                            prototypes=state["prototypes_int8"],
                            prototype_parent=proto_parent,
                            prototype_source_label=proto_source_label,
                            class_dist=class_dist,
                            parent=parent,
                            source_label=source_label,
                            parent_margin_target=parent_margin_target,
                            source_margin_target=source_margin_target,
                            batch_size=batch_size,
                        )
                        selected.extend(cluster_selected)
                        per_cluster[f"{parent}:{source_label}"] = info
            except ValueError as exc:
                source = f"sourcechoice_pm{parent_margin_target}_sm{source_margin_target}"
                diagnostics[source] = {
                    "failed": True,
                    "error": str(exc),
                    "parent_margin_target": int(parent_margin_target),
                    "source_margin_target": int(source_margin_target),
                }
                continue
            keep = np.asarray(sorted(set(selected)), dtype=np.int64)
            covered_state = subset_state(state, keep)
            result = evaluate_int8(
                embeddings,
                y_parent,
                covered_state["prototypes_int8"],
                covered_state["prototype_parent"],
                max(parent_margin_targets + source_margin_targets + [8]),
            )
            source = f"sourcechoice_pm{parent_margin_target}_sm{source_margin_target}"
            row = row_for_state(
                name=name,
                source=source,
                base=base,
                state=covered_state,
                eval_result=result,
                extra={
                    "sourcechoice_parent_margin_target": int(parent_margin_target),
                    "sourcechoice_source_margin_target": int(source_margin_target),
                    "sourcechoice_cluster_json": json.dumps(per_cluster, ensure_ascii=False),
                    "source_gate_teacher_npz": str(source_gate_teacher_npz),
                },
            )
            rows.append(row)
            payloads[source] = (row, covered_state, result)
            diagnostics[source] = {
                "failed": False,
                "per_cluster": per_cluster,
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
        raise SystemExit("no source-choice set-cover candidate succeeded")

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
        description="Select V8 prototype subsets with a normal-only source-choice preserving set-cover objective."
    )
    parser.add_argument("input_npz", type=Path)
    parser.add_argument("--source-gate-teacher-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--parent-margin-targets", default="0,1,2,3")
    parser.add_argument("--source-margin-targets", default="0,1,2,3")
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--allow-missing-source-labels", action="store_true")
    parser.add_argument("--allow-evaluation-only-teacher", action="store_true")
    args = parser.parse_args()
    build_sourcechoice_setcover_tables(
        input_npz=args.input_npz,
        source_gate_teacher_npz=args.source_gate_teacher_npz,
        output_dir=args.output_dir,
        name=args.name or args.input_npz.parent.name,
        parent_margin_targets=parse_ints(args.parent_margin_targets),
        source_margin_targets=parse_ints(args.source_margin_targets),
        batch_size=args.batch_size,
        allow_missing_source_labels=bool(args.allow_missing_source_labels),
        allow_evaluation_only_teacher=bool(args.allow_evaluation_only_teacher),
    )


if __name__ == "__main__":
    main()
