import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import PARENT_NAMES, kmeans
from prune_merge_v8_logit_prototypes import evaluate_int8, load_state, row_for_state, state_payload


PARENT_COUNT = len(PARENT_NAMES)


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


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_ints(value: str) -> list[int]:
    return [int(item.strip()) for item in value.split(",") if item.strip()]


def full_class_distances(
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int,
) -> np.ndarray:
    out = np.full((len(embeddings), PARENT_COUNT), np.iinfo(np.int64).max, dtype=np.int64)
    x_all = embeddings.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        for parent in range(PARENT_COUNT):
            indexes = np.where(prototype_parent == parent)[0]
            if len(indexes) > 0:
                out[start : start + len(x), parent] = np.min(dist[:, indexes], axis=1)
    return out


def unique_rows(values: np.ndarray) -> np.ndarray:
    if len(values) == 0:
        return values
    _unique, indexes = np.unique(values, axis=0, return_index=True)
    return values[np.sort(indexes)]


def build_parent_candidates(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    parent: int,
    center_count: int,
    seed: int,
) -> dict[str, np.ndarray]:
    query_indexes = np.where(y_parent == parent)[0]
    exact = embeddings[query_indexes].astype(np.int16)
    center_rows = np.zeros((0, embeddings.shape[1]), dtype=np.int16)
    if center_count > 0:
        centers, _labels = kmeans(exact.astype(np.float64), min(center_count, len(exact)), seed + parent * 1009, iterations=35)
        center_rows = np.clip(np.rint(centers), -128, 127).astype(np.int16)
    candidate_rows = unique_rows(np.vstack([center_rows, exact])).astype(np.int8)
    source_kind = np.asarray(
        ["center"] * len(unique_rows(center_rows).astype(np.int8)) + ["exact"] * len(exact),
        dtype=object,
    )
    # Rebuild metadata after deduplication so array lengths match candidate_rows.
    source_kind_out: list[str] = []
    sample_out: list[int] = []
    view_out: list[str] = []
    seen: set[tuple[int, ...]] = set()
    for row in center_rows.astype(np.int8):
        key = tuple(int(item) for item in row.tolist())
        if key in seen:
            continue
        seen.add(key)
        source_kind_out.append("center")
        sample_out.append(-1)
        view_out.append("center")
    for idx, row in zip(query_indexes.tolist(), exact.astype(np.int8), strict=False):
        key = tuple(int(item) for item in row.tolist())
        if key in seen:
            continue
        seen.add(key)
        source_kind_out.append("exact")
        sample_out.append(int(sample_index[idx]))
        view_out.append(str(view_labels[idx]))
    candidate_rows = np.asarray([list(key) for key in seen], dtype=np.int8)
    # Preserve the insertion order encoded in metadata.
    ordered_rows: list[np.ndarray] = []
    seen_order: set[tuple[int, ...]] = set()
    for row in center_rows.astype(np.int8):
        key = tuple(int(item) for item in row.tolist())
        if key not in seen_order:
            ordered_rows.append(row)
            seen_order.add(key)
    for row in exact.astype(np.int8):
        key = tuple(int(item) for item in row.tolist())
        if key not in seen_order:
            ordered_rows.append(row)
            seen_order.add(key)
    candidate_rows = np.stack(ordered_rows).astype(np.int8)
    return {
        "prototypes": candidate_rows,
        "source_kind": np.asarray(source_kind_out, dtype=object),
        "sample_index": np.asarray(sample_out, dtype=np.int64),
        "view_label": np.asarray(view_out, dtype=object),
    }


def subset_candidates(candidates: dict[str, np.ndarray], keep: np.ndarray) -> dict[str, np.ndarray]:
    return {key: value[keep].copy() for key, value in candidates.items()}


def reject_cross_parent_steals(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    candidates: dict[str, np.ndarray],
    class_dist: np.ndarray,
    parent: int,
    batch_size: int,
) -> tuple[dict[str, np.ndarray], dict[str, int]]:
    candidate_rows = np.asarray(candidates["prototypes"], dtype=np.int8)
    if len(candidate_rows) == 0:
        return candidates, {
            "candidate_count_before_safety": 0,
            "candidate_count_after_safety": 0,
            "rejected_cross_parent_count": 0,
        }

    other_indexes = np.where(y_parent != parent)[0]
    if len(other_indexes) == 0:
        return candidates, {
            "candidate_count_before_safety": int(len(candidate_rows)),
            "candidate_count_after_safety": int(len(candidate_rows)),
            "rejected_cross_parent_count": 0,
        }

    original_wrong = np.empty(len(other_indexes), dtype=np.int64)
    for local, row_index in enumerate(other_indexes.tolist()):
        true_parent = int(y_parent[row_index])
        wrong_parent_cols = [item for item in range(PARENT_COUNT) if item != true_parent]
        original_wrong[local] = int(np.min(class_dist[row_index, wrong_parent_cols]))

    x_other = embeddings[other_indexes].astype(np.int32)
    p_all = candidate_rows.astype(np.int32)
    keep = np.ones(len(candidate_rows), dtype=bool)
    for start in range(0, len(p_all), batch_size):
        p = p_all[start : start + batch_size]
        dist = np.sum((x_other[:, None, :] - p[None, :, :]) ** 2, axis=2).astype(np.int64)
        steals = np.any(dist < original_wrong[:, None], axis=0)
        keep[start : start + len(p)] = ~steals

    safe = subset_candidates(candidates, keep)
    return safe, {
        "candidate_count_before_safety": int(len(candidate_rows)),
        "candidate_count_after_safety": int(len(safe["prototypes"])),
        "rejected_cross_parent_count": int(np.sum(~keep)),
    }


def greedy_cover_parent(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    candidates: np.ndarray,
    class_dist: np.ndarray,
    parent: int,
    margin_target: int,
    batch_size: int,
) -> tuple[list[int], dict[str, Any]]:
    query_indexes = np.where(y_parent == parent)[0]
    wrong_parents = [item for item in range(PARENT_COUNT) if item != parent]
    wrong_dist = np.min(class_dist[query_indexes][:, wrong_parents], axis=1)
    threshold = wrong_dist - int(margin_target)
    x_all = embeddings[query_indexes].astype(np.int32)
    p_all = candidates.astype(np.int32)
    coverage: list[np.ndarray] = []
    for start in range(0, len(p_all), batch_size):
        p = p_all[start : start + batch_size]
        dist = np.sum((x_all[:, None, :] - p[None, :, :]) ** 2, axis=2).astype(np.int64)
        ok = dist < threshold[:, None]
        coverage.extend(np.flatnonzero(ok[:, local]).astype(np.int64) for local in range(ok.shape[1]))
    uncovered = np.ones(len(query_indexes), dtype=bool)
    selected: list[int] = []
    used: set[int] = set()
    while np.any(uncovered):
        best_index = -1
        best_gain = -1
        for index, covered in enumerate(coverage):
            if index in used or len(covered) == 0:
                continue
            gain = int(np.sum(uncovered[covered]))
            if gain > best_gain:
                best_index = index
                best_gain = gain
        if best_index < 0 or best_gain <= 0:
            raise ValueError(
                f"parent {parent} cannot cover {int(np.sum(uncovered))} rows at margin target {margin_target}"
            )
        used.add(best_index)
        selected.append(best_index)
        uncovered[coverage[best_index]] = False
    return selected, {
        "query_count": int(len(query_indexes)),
        "candidate_count": int(len(candidates)),
        "selected_count": int(len(selected)),
        "margin_target": int(margin_target),
    }


def run(
    *,
    input_npz: Path,
    output_dir: Path,
    name: str,
    center_counts: list[int],
    margin_targets: list[int],
    seed: int,
    batch_size: int,
    reject_cross_parent: bool,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    base, state = load_state(input_npz)
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    class_dist = full_class_distances(
        embeddings,
        state["prototypes_int8"],
        state["prototype_parent"],
        batch_size=batch_size,
    )
    rows: list[dict[str, Any]] = []
    payloads: dict[str, tuple[dict[str, Any], dict[str, np.ndarray], Any]] = {}
    diagnostics: dict[str, Any] = {}
    for center_count in center_counts:
        parent_candidates = {
            parent: build_parent_candidates(
                embeddings=embeddings,
                y_parent=y_parent,
                sample_index=sample_index,
                view_labels=view_labels,
                parent=parent,
                center_count=center_count,
                seed=seed,
            )
            for parent in range(PARENT_COUNT)
        }
        safety_info: dict[str, Any] = {}
        if reject_cross_parent:
            safe_parent_candidates: dict[int, dict[str, np.ndarray]] = {}
            for parent, candidates in parent_candidates.items():
                safe_candidates, info = reject_cross_parent_steals(
                    embeddings=embeddings,
                    y_parent=y_parent,
                    candidates=candidates,
                    class_dist=class_dist,
                    parent=parent,
                    batch_size=batch_size,
                )
                safe_parent_candidates[parent] = safe_candidates
                safety_info[str(parent)] = info
            parent_candidates = safe_parent_candidates
        for margin_target in margin_targets:
            proto_rows: list[np.ndarray] = []
            proto_parent: list[int] = []
            proto_subclass: list[int] = []
            proto_cluster: list[int] = []
            proto_sample: list[int] = []
            proto_view: list[str] = []
            proto_kind: list[str] = []
            per_parent: dict[str, Any] = {}
            for parent in range(PARENT_COUNT):
                candidates = parent_candidates[parent]
                selected, info = greedy_cover_parent(
                    embeddings=embeddings,
                    y_parent=y_parent,
                    candidates=np.asarray(candidates["prototypes"], dtype=np.int8),
                    class_dist=class_dist,
                    parent=parent,
                    margin_target=margin_target,
                    batch_size=batch_size,
                )
                if str(parent) in safety_info:
                    info = {**info, **safety_info[str(parent)]}
                per_parent[str(parent)] = info
                for local in selected:
                    proto_rows.append(np.asarray(candidates["prototypes"][local], dtype=np.int8))
                    proto_parent.append(parent)
                    proto_subclass.append(-1)
                    proto_cluster.append(len(proto_rows) - 1)
                    proto_sample.append(int(candidates["sample_index"][local]))
                    proto_view.append(str(candidates["view_label"][local]))
                    proto_kind.append(str(candidates["source_kind"][local]))
            covered_state = {
                "prototypes_int8": np.stack(proto_rows).astype(np.int8),
                "prototype_parent": np.asarray(proto_parent, dtype=np.int64),
                "prototype_subclass": np.asarray(proto_subclass, dtype=np.int64),
                "prototype_cluster": np.asarray(proto_cluster, dtype=np.int64),
                "prototype_sample_index": np.asarray(proto_sample, dtype=np.int64),
                "prototype_view_label": np.asarray(proto_view, dtype=object),
                "prototype_source_kind": np.asarray(proto_kind, dtype=object),
            }
            result = evaluate_int8(
                embeddings,
                y_parent,
                covered_state["prototypes_int8"],
                covered_state["prototype_parent"],
                max(margin_targets + [8]),
            )
            source_prefix = "center_setcover_safe" if reject_cross_parent else "center_setcover"
            source = f"{source_prefix}_k{center_count}_m{margin_target}"
            row = row_for_state(
                name=name,
                source=source,
                base=base,
                state=covered_state,
                eval_result=result,
                extra={
                    "center_count_per_parent": int(center_count),
                    "setcover_margin_target": int(margin_target),
                    "cross_parent_reject": bool(reject_cross_parent),
                    "setcover_parent_json": json.dumps(per_parent, ensure_ascii=False),
                },
            )
            rows.append(row)
            payloads[source] = (row, covered_state, result)
            diagnostics[source] = {"row": row, "per_parent": per_parent}

    def score(row: dict[str, Any]) -> tuple[float, int, int, float]:
        acc = min(
            float(row["clean_accuracy"]),
            float(row["rotmirror_min_accuracy"]),
            float(row["stress_min_accuracy"]),
            float(row["int8_clean_accuracy"]),
            float(row["int8_rotmirror_min_accuracy"]),
            float(row["int8_stress_min_accuracy"]),
        )
        return (acc, int(row["int8_margin_min"]), -int(row["prototype_count"]), float(row["int8_margin_mean"]))

    rows_sorted = sorted(rows, key=score, reverse=True)
    write_csv(output_dir / "candidate_results.csv", rows_sorted)
    best = rows_sorted[0]
    _best_row, best_state, best_result = payloads[str(best["prototype_source"])]
    np.savez_compressed(output_dir / "best_parent_logits_memory_params.npz", **state_payload(base, best_state, best_result))
    for source, (_row, source_state, source_result) in payloads.items():
        np.savez_compressed(output_dir / f"{source}_parent_logits_memory_params.npz", **state_payload(base, source_state, source_result))
    config_src = input_npz.parent / "train_config.json"
    if config_src.exists():
        shutil.copy2(config_src, output_dir / "train_config.json")
    write_json(
        output_dir / "summary.json",
        {
            "input_npz": str(input_npz),
            "candidate_count": int(len(rows_sorted)),
            "best": best,
            "top": rows_sorted,
            "diagnostics": diagnostics,
        },
    )
    print(json.dumps({"output_dir": str(output_dir), "best": best, "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(description="Select V8 prototypes using normal-only kmeans-center plus exact fallback set-cover.")
    parser.add_argument("input_npz", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--center-counts", default="128,256,512")
    parser.add_argument("--margin-targets", default="0,4,8")
    parser.add_argument("--seed", type=int, default=20260521)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument(
        "--allow-cross-parent-steal",
        action="store_true",
        help="Disable the conservative normal-only safety filter for generated center candidates.",
    )
    args = parser.parse_args()
    run(
        input_npz=args.input_npz,
        output_dir=args.output_dir,
        name=args.name or args.input_npz.parent.name,
        center_counts=parse_ints(args.center_counts),
        margin_targets=parse_ints(args.margin_targets),
        seed=args.seed,
        batch_size=args.batch_size,
        reject_cross_parent=not args.allow_cross_parent_steal,
    )


if __name__ == "__main__":
    main()
