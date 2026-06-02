import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import PARENT_NAMES, metric_summary
from prune_merge_v8_logit_prototypes import evaluate_int8, load_state, row_for_state, state_payload


PARENT_COUNT = len(PARENT_NAMES)


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


def greedy_parent_cover(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    class_dist: np.ndarray,
    parent: int,
    margin_target: int,
    batch_size: int,
) -> tuple[list[int], dict[str, Any]]:
    query_indexes = np.where(y_parent == parent)[0]
    proto_indexes = np.where(prototype_parent == parent)[0]
    wrong_dist = np.min(class_dist[query_indexes][:, [item for item in range(PARENT_COUNT) if item != parent]], axis=1)
    threshold = wrong_dist - int(margin_target)
    if len(query_indexes) == 0:
        return [], {"query_count": 0, "prototype_count": int(len(proto_indexes)), "covered": 0}
    if len(proto_indexes) == 0:
        raise ValueError(f"parent {parent} has no prototypes")

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
                f"parent {parent} cannot cover {int(np.sum(uncovered))} rows at margin target {margin_target}; "
                f"first query indexes: {missing.tolist()}"
            )
        selected_local.add(best_local)
        selected.append(int(proto_indexes[best_local]))
        uncovered[coverage[best_local]] = False

    return selected, {
        "query_count": int(len(query_indexes)),
        "prototype_count": int(len(proto_indexes)),
        "selected_count": int(len(selected)),
        "margin_target": int(margin_target),
    }


def build_setcover_tables(
    *,
    input_npz: Path,
    output_dir: Path,
    name: str,
    margin_targets: list[int],
    batch_size: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    base, state = load_state(input_npz)
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
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
    for margin_target in margin_targets:
        selected: list[int] = []
        per_parent: dict[str, Any] = {}
        for parent in range(PARENT_COUNT):
            parent_selected, info = greedy_parent_cover(
                embeddings=embeddings,
                y_parent=y_parent,
                prototypes=state["prototypes_int8"],
                prototype_parent=state["prototype_parent"],
                class_dist=class_dist,
                parent=parent,
                margin_target=margin_target,
                batch_size=batch_size,
            )
            selected.extend(parent_selected)
            per_parent[str(parent)] = info
        keep = np.asarray(sorted(set(selected)), dtype=np.int64)
        covered_state = subset_state(state, keep)
        result = evaluate_int8(
            embeddings,
            y_parent,
            covered_state["prototypes_int8"],
            covered_state["prototype_parent"],
            max(margin_targets + [8]),
        )
        source = f"setcover_m{margin_target}"
        row = row_for_state(
            name=name,
            source=source,
            base=base,
            state=covered_state,
            eval_result=result,
            extra={
                "setcover_margin_target": int(margin_target),
                "setcover_parent_json": json.dumps(per_parent, ensure_ascii=False),
            },
        )
        rows.append(row)
        payloads[source] = (row, covered_state, result)
        diagnostics[source] = {"per_parent": per_parent, "row": row}

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
    parser = argparse.ArgumentParser(description="Select V8 prototype subsets with a normal-only greedy set-cover objective.")
    parser.add_argument("input_npz", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--margin-targets", default="0,4,8")
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()
    build_setcover_tables(
        input_npz=args.input_npz,
        output_dir=args.output_dir,
        name=args.name or args.input_npz.parent.name,
        margin_targets=parse_ints(args.margin_targets),
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
