import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import (
    PARENT_NAMES,
    ROT_MIRROR_VIEWS,
    metric_summary,
    parse_csv,
    parse_floats,
    parse_ints,
    predict_closed,
    predict_int8,
    quantize,
    squared_distances,
    write_csv,
)
from run_v8_extended_prototype_sweep import build_prototypes_ext, load_embedding_params


def load_existing_prototypes(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        required = ["prototypes", "prototype_parent"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing existing prototype arrays: {missing}")
        count = len(data["prototypes"])
        return {
            "prototypes": np.asarray(data["prototypes"], dtype=np.float64),
            "prototype_parent": np.asarray(data["prototype_parent"], dtype=np.int64),
            "prototype_subclass": np.asarray(
                data["prototype_subclass"] if "prototype_subclass" in data.files else np.full(count, -1),
                dtype=np.int64,
            ),
            "prototype_cluster": np.asarray(
                data["prototype_cluster"] if "prototype_cluster" in data.files else np.arange(count),
                dtype=np.int64,
            ),
            "prototype_sample_index": np.asarray(
                data["prototype_sample_index"] if "prototype_sample_index" in data.files else np.full(count, -1),
                dtype=np.int64,
            ),
            "prototype_view_label": np.asarray(
                data["prototype_view_label"] if "prototype_view_label" in data.files else np.full(count, "existing")
            ).astype(str),
        }


def class_distances_float(
    embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray
) -> np.ndarray:
    dist = squared_distances(embeddings, prototypes)
    by_parent: list[np.ndarray] = []
    for parent in range(len(PARENT_NAMES)):
        mask = prototype_parent == parent
        by_parent.append(np.min(dist[:, mask], axis=1) if np.any(mask) else np.full(len(embeddings), np.inf))
    return np.stack(by_parent, axis=1)


def class_distances_int8(embeddings_q: np.ndarray, prototypes_q: np.ndarray, prototype_parent: np.ndarray) -> np.ndarray:
    x = embeddings_q.astype(np.int32)
    p = prototypes_q.astype(np.int32)
    diff = x[:, None, :] - p[None, :, :]
    dist = np.sum(diff * diff, axis=2)
    by_parent: list[np.ndarray] = []
    max_value = np.iinfo(np.int32).max
    for parent in range(len(PARENT_NAMES)):
        mask = prototype_parent == parent
        by_parent.append(np.min(dist[:, mask], axis=1) if np.any(mask) else np.full(len(embeddings_q), max_value))
    return np.stack(by_parent, axis=1)


def pred_and_margin(class_dist: np.ndarray, y_parent: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    pred = np.argmin(class_dist, axis=1).astype(np.int64)
    correct = class_dist[np.arange(len(y_parent)), y_parent]
    wrong = np.min(np.where(np.arange(len(PARENT_NAMES))[None, :] == y_parent[:, None], np.inf, class_dist), axis=1)
    return pred, wrong - correct


def int8_candidate_distances(embeddings_q: np.ndarray, candidates_q: np.ndarray) -> np.ndarray:
    x = embeddings_q.astype(np.int32)
    p = candidates_q.astype(np.int32)
    diff = x[:, None, :] - p[None, :, :]
    return np.sum(diff * diff, axis=2)


def choose_int8_scale(
    *,
    embeddings: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    y_parent: np.ndarray,
    quant_scales: list[float],
) -> dict[str, Any]:
    best: dict[str, Any] | None = None
    for scale in quant_scales:
        z_q = quantize(embeddings, scale)
        p_q = quantize(prototypes, scale)
        pred, margin = predict_int8(z_q, p_q, prototype_parent)
        row = {
            "int8_scale": float(scale),
            "int8_wrong_count": int(np.sum(pred != y_parent)),
            "int8_margin_min": int(np.min(margin)),
            "int8_margin_mean": float(np.mean(margin)),
            "embedding_int8": z_q,
            "prototypes_int8": p_q,
            "int8_pred": pred,
            "int8_margin": margin,
        }
        score = (-row["int8_wrong_count"], row["int8_margin_min"], row["int8_margin_mean"])
        if best is None or score > best["_score"]:
            row["_score"] = score
            best = row
    assert best is not None
    best.pop("_score", None)
    return best


def view_path(paths: np.ndarray, sample_index: np.ndarray, index: int) -> str:
    return str(paths[int(sample_index[index])])


def add_candidate(
    rows: list[dict[str, Any]],
    seen: set[tuple[str, int, int]],
    *,
    kind: str,
    embedding: np.ndarray,
    parent: int,
    subclass: int,
    source_index: int,
    source_sample: int,
    source_view: str,
    source_path: str,
    priority: float,
) -> None:
    key = (kind, int(parent), int(source_index))
    if key in seen:
        return
    seen.add(key)
    rows.append(
        {
            "embedding": np.asarray(embedding, dtype=np.float64),
            "parent": int(parent),
            "subclass": int(subclass),
            "source_index": int(source_index),
            "source_sample_index": int(source_sample),
            "source_view": str(source_view),
            "source_path": str(source_path),
            "kind": kind,
            "priority": float(priority),
        }
    )


def build_candidate_pool(
    *,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    y_sub: np.ndarray,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    paths: np.ndarray,
    pred: np.ndarray,
    int8_pred: np.ndarray,
    margin: np.ndarray,
    low_margin_top: int,
    defense_per_event: int,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, int, int]] = set()
    wrong_indexes = np.where((pred != y_parent) | (int8_pred != y_parent))[0]
    correct_indexes = np.where((pred == y_parent) & (int8_pred == y_parent))[0]
    low_indexes = correct_indexes[np.argsort(margin[correct_indexes])[: max(0, low_margin_top)]]
    event_indexes = np.unique(np.concatenate([wrong_indexes, low_indexes])).astype(np.int64)

    for index in wrong_indexes:
        add_candidate(
            rows,
            seen,
            kind="wrong_exact",
            embedding=embeddings[index],
            parent=int(y_parent[index]),
            subclass=int(y_sub[index]),
            source_index=int(index),
            source_sample=int(sample_index[index]),
            source_view=str(view_labels[index]),
            source_path=view_path(paths, sample_index, int(index)),
            priority=1000.0 - float(margin[index]),
        )

    for rank, index in enumerate(low_indexes):
        add_candidate(
            rows,
            seen,
            kind="low_margin_exact",
            embedding=embeddings[index],
            parent=int(y_parent[index]),
            subclass=int(y_sub[index]),
            source_index=int(index),
            source_sample=int(sample_index[index]),
            source_view=str(view_labels[index]),
            source_path=view_path(paths, sample_index, int(index)),
            priority=500.0 - float(rank),
        )

    orbit_views = np.asarray(["clean", *ROT_MIRROR_VIEWS])
    for sample in np.unique(sample_index[event_indexes]):
        mask = sample_index == int(sample)
        orbit_mask = mask & np.isin(view_labels, orbit_views)
        if not np.any(orbit_mask):
            orbit_mask = mask
        indexes = np.where(orbit_mask)[0]
        first = int(np.where(mask)[0][0])
        add_candidate(
            rows,
            seen,
            kind="orbit_mean",
            embedding=np.mean(embeddings[indexes], axis=0),
            parent=int(y_parent[first]),
            subclass=int(y_sub[first]),
            source_index=int(sample),
            source_sample=int(sample),
            source_view="clean_d4_mean",
            source_path=str(paths[int(sample)]),
            priority=250.0,
        )

    # Defense candidates are nearest same-parent points around each conflict. They can thicken
    # the correct local region without adding an exact wrong-view prototype.
    for event_index in event_indexes:
        parent = int(y_parent[event_index])
        pool = np.where(y_parent == parent)[0]
        dist = np.sum((embeddings[pool] - embeddings[event_index]) ** 2, axis=1)
        order = pool[np.argsort(dist)]
        added = 0
        for neighbor in order:
            if int(neighbor) == int(event_index):
                continue
            add_candidate(
                rows,
                seen,
                kind="same_parent_defense",
                embedding=embeddings[neighbor],
                parent=int(y_parent[neighbor]),
                subclass=int(y_sub[neighbor]),
                source_index=int(neighbor),
                source_sample=int(sample_index[neighbor]),
                source_view=str(view_labels[neighbor]),
                source_path=view_path(paths, sample_index, int(neighbor)),
                priority=100.0 - float(added),
            )
            added += 1
            if added >= defense_per_event:
                break
    return rows


def describe_candidate(candidate: dict[str, Any], rank: int, score: float, stats: dict[str, Any]) -> dict[str, Any]:
    return {
        "rank": int(rank),
        "score": float(score),
        "kind": candidate["kind"],
        "parent": PARENT_NAMES[int(candidate["parent"])],
        "subclass": int(candidate["subclass"]),
        "source_index": int(candidate["source_index"]),
        "source_sample_index": int(candidate["source_sample_index"]),
        "source_view": candidate["source_view"],
        "source_path": candidate["source_path"],
        **stats,
    }


def evaluate_row(
    *,
    stage: str,
    base_source: str,
    base_k: int,
    reserved_count: int,
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    view_order: list[str],
    view_labels: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    class_dist: np.ndarray,
    int8_class_dist: np.ndarray,
    int8_scale: float,
    selected_trace: list[dict[str, Any]],
) -> dict[str, Any]:
    pred, margin = pred_and_margin(class_dist, y_parent)
    int8_pred, int8_margin = pred_and_margin(int8_class_dist, y_parent)
    row: dict[str, Any] = {
        "stage": stage,
        "base_source": base_source,
        "base_k_per_subclass": int(base_k),
        "reserved_count": int(reserved_count),
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(len(prototypes)),
        "estimated_distance_macs": int(len(prototypes) * embeddings.shape[1]),
        "estimated_float_table_bytes": int(len(prototypes) * embeddings.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(prototypes) * embeddings.shape[1]),
        "wrong_count": int(np.sum(pred != y_parent)),
        "int8_wrong_count": int(np.sum(int8_pred != y_parent)),
        "int8_scale": float(int8_scale),
        "margin_min": float(np.min(margin)),
        "margin_p01": float(np.quantile(margin, 0.01)),
        "margin_mean": float(np.mean(margin)),
        "int8_margin_min": int(np.min(int8_margin)),
        "int8_margin_p01": float(np.quantile(int8_margin, 0.01)),
        "int8_margin_mean": float(np.mean(int8_margin)),
        "selected_trace_json": json.dumps(selected_trace, ensure_ascii=False),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
    return row


def compile_for_base(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    existing_proto: dict[str, np.ndarray] | None,
    base_source: str,
    base_k: int,
    quant_scales: list[float],
    quant_stable_scale: float,
    seed: int,
    low_margin_top: int,
    defense_per_event: int,
    max_reserved: int,
    snapshot_budgets: set[int],
    reject_new_wrong: bool,
    target_margin: float,
) -> tuple[list[dict[str, Any]], dict[str, np.ndarray], list[dict[str, Any]]]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    paths = np.asarray(flat["paths"]).astype(str)
    view_order = list(flat["view_names"])

    if base_source == "existing":
        if existing_proto is None:
            raise ValueError("base_source=existing requires existing_proto")
        proto = existing_proto
        display_base_k = int(base_k) if int(base_k) > 0 else max(1, int(round(len(proto["prototypes"]) / 8.0)))
    else:
        proto = build_prototypes_ext(
            embeddings=embeddings,
            y_sub=y_sub,
            sample_index=sample_index,
            view_labels=view_labels,
            source=base_source,
            k_per_subclass=base_k,
            seed=seed,
            quant_stable_scale=quant_stable_scale,
        )
        display_base_k = int(base_k)
    prototypes = np.asarray(proto["prototypes"], dtype=np.float64)
    prototype_parent = np.asarray(proto["prototype_parent"], dtype=np.int64)
    prototype_subclass = np.asarray(proto["prototype_subclass"], dtype=np.int64)
    prototype_cluster = np.asarray(proto["prototype_cluster"], dtype=np.int64)
    prototype_sample_index = np.asarray(proto["prototype_sample_index"], dtype=np.int64)
    prototype_view_label = np.asarray(proto["prototype_view_label"]).astype(str)
    prototype_source_kind = np.asarray([base_source for _ in range(len(prototypes))], dtype=object)
    prototype_source_index = np.full(len(prototypes), -1, dtype=np.int64)

    pred, margin = predict_closed(embeddings, prototypes, prototype_parent)
    int8_choice = choose_int8_scale(
        embeddings=embeddings,
        prototypes=prototypes,
        prototype_parent=prototype_parent,
        y_parent=y_parent,
        quant_scales=quant_scales,
    )
    int8_scale = float(int8_choice["int8_scale"])
    embeddings_q = np.asarray(int8_choice["embedding_int8"], dtype=np.int8)
    int8_pred = np.asarray(int8_choice["int8_pred"], dtype=np.int64)
    class_dist = class_distances_float(embeddings, prototypes, prototype_parent)
    int8_class_dist = class_distances_int8(embeddings_q, np.asarray(int8_choice["prototypes_int8"], dtype=np.int8), prototype_parent)
    _base_pred, base_margin = pred_and_margin(class_dist, y_parent)
    _base_int8_pred, base_int8_margin = pred_and_margin(int8_class_dist, y_parent)

    candidates = build_candidate_pool(
        embeddings=embeddings,
        y_parent=y_parent,
        y_sub=y_sub,
        sample_index=sample_index,
        view_labels=view_labels,
        paths=paths,
        pred=pred,
        int8_pred=int8_pred,
        margin=margin,
        low_margin_top=low_margin_top,
        defense_per_event=defense_per_event,
    )
    if candidates:
        candidate_embeddings = np.stack([candidate["embedding"] for candidate in candidates]).astype(np.float64)
        candidate_parent = np.asarray([candidate["parent"] for candidate in candidates], dtype=np.int64)
        candidate_float_dist = squared_distances(embeddings, candidate_embeddings)
        candidate_int8_dist = int8_candidate_distances(embeddings_q, quantize(candidate_embeddings, int8_scale))
    else:
        candidate_embeddings = np.zeros((0, embeddings.shape[1]), dtype=np.float64)
        candidate_parent = np.zeros(0, dtype=np.int64)
        candidate_float_dist = np.zeros((len(embeddings), 0), dtype=np.float64)
        candidate_int8_dist = np.zeros((len(embeddings), 0), dtype=np.int64)

    rows: list[dict[str, Any]] = []
    selected_trace: list[dict[str, Any]] = []
    used: set[int] = set()
    rows.append(
        evaluate_row(
            stage="base",
            base_source=base_source,
            base_k=display_base_k,
            reserved_count=0,
            embeddings=embeddings,
            y_parent=y_parent,
            view_order=view_order,
            view_labels=view_labels,
            prototypes=prototypes,
            prototype_parent=prototype_parent,
            class_dist=class_dist,
            int8_class_dist=int8_class_dist,
            int8_scale=int8_scale,
            selected_trace=selected_trace,
        )
    )

    for step in range(1, max_reserved + 1):
        cur_pred, cur_margin = pred_and_margin(class_dist, y_parent)
        cur_int8_pred, cur_int8_margin = pred_and_margin(int8_class_dist, y_parent)
        cur_wrong = cur_pred != y_parent
        cur_int8_wrong = cur_int8_pred != y_parent
        risk_mask = cur_wrong | cur_int8_wrong | (cur_margin < target_margin)
        best_candidate: tuple[float, int, np.ndarray, np.ndarray, dict[str, Any]] | None = None
        for cand_idx, candidate in enumerate(candidates):
            if cand_idx in used:
                continue
            parent = int(candidate_parent[cand_idx])
            new_class_dist = class_dist.copy()
            new_class_dist[:, parent] = np.minimum(new_class_dist[:, parent], candidate_float_dist[:, cand_idx])
            new_int8_class_dist = int8_class_dist.copy()
            new_int8_class_dist[:, parent] = np.minimum(new_int8_class_dist[:, parent], candidate_int8_dist[:, cand_idx])
            new_pred, new_margin = pred_and_margin(new_class_dist, y_parent)
            new_int8_pred, new_int8_margin = pred_and_margin(new_int8_class_dist, y_parent)
            new_wrong = new_pred != y_parent
            new_int8_wrong = new_int8_pred != y_parent
            introduced_float = int(np.sum((~cur_wrong) & new_wrong))
            introduced_int8 = int(np.sum((~cur_int8_wrong) & new_int8_wrong))
            if reject_new_wrong and (introduced_float > 0 or introduced_int8 > 0):
                continue
            fixed_float = int(np.sum(cur_wrong & (~new_wrong)))
            fixed_int8 = int(np.sum(cur_int8_wrong & (~new_int8_wrong)))
            risk_improved = int(np.sum(risk_mask & (new_margin > cur_margin + 1.0e-9)))
            int8_risk_improved = int(np.sum(risk_mask & (new_int8_margin > cur_int8_margin)))
            wrong_delta = int(np.sum(cur_wrong) - np.sum(new_wrong))
            int8_wrong_delta = int(np.sum(cur_int8_wrong) - np.sum(new_int8_wrong))
            min_margin_gain = float(np.min(new_margin) - np.min(cur_margin))
            int8_min_margin_gain = int(np.min(new_int8_margin) - np.min(cur_int8_margin))
            score = (
                fixed_float * 1_000_000.0
                + fixed_int8 * 1_000_000.0
                + wrong_delta * 500_000.0
                + int8_wrong_delta * 500_000.0
                + risk_improved * 100.0
                + int8_risk_improved * 25.0
                + min_margin_gain * 10.0
                + int8_min_margin_gain * 0.01
                + float(candidate["priority"]) * 0.001
                - introduced_float * 10_000_000.0
                - introduced_int8 * 10_000_000.0
            )
            stats = {
                "fixed_float_wrong": fixed_float,
                "fixed_int8_wrong": fixed_int8,
                "introduced_float_wrong": introduced_float,
                "introduced_int8_wrong": introduced_int8,
                "risk_improved": risk_improved,
                "int8_risk_improved": int8_risk_improved,
                "wrong_count_after": int(np.sum(new_wrong)),
                "int8_wrong_count_after": int(np.sum(new_int8_wrong)),
                "margin_min_after": float(np.min(new_margin)),
                "int8_margin_min_after": int(np.min(new_int8_margin)),
            }
            if best_candidate is None or score > best_candidate[0]:
                best_candidate = (score, cand_idx, new_class_dist, new_int8_class_dist, stats)
        if best_candidate is None or best_candidate[0] <= 0:
            break
        score, cand_idx, class_dist, int8_class_dist, stats = best_candidate
        used.add(cand_idx)
        candidate = candidates[cand_idx]
        selected_trace.append(describe_candidate(candidate, len(selected_trace) + 1, score, stats))
        prototypes = np.vstack([prototypes, candidate_embeddings[cand_idx]])
        prototype_parent = np.concatenate([prototype_parent, [int(candidate["parent"])]])
        prototype_subclass = np.concatenate([prototype_subclass, [int(candidate["subclass"])]])
        prototype_cluster = np.concatenate([prototype_cluster, [int(display_base_k * 1000 + step)]])
        prototype_sample_index = np.concatenate([prototype_sample_index, [int(candidate["source_sample_index"])]])
        prototype_view_label = np.concatenate([prototype_view_label, [str(candidate["source_view"])]])
        prototype_source_kind = np.concatenate([prototype_source_kind, [str(candidate["kind"])]])
        prototype_source_index = np.concatenate([prototype_source_index, [int(candidate["source_index"])]])
        if step in snapshot_budgets or step == max_reserved:
            rows.append(
                evaluate_row(
                    stage="compiled",
                    base_source=base_source,
                    base_k=display_base_k,
                    reserved_count=step,
                    embeddings=embeddings,
                    y_parent=y_parent,
                    view_order=view_order,
                    view_labels=view_labels,
                    prototypes=prototypes,
                    prototype_parent=prototype_parent,
                    class_dist=class_dist,
                    int8_class_dist=int8_class_dist,
                    int8_scale=int8_scale,
                    selected_trace=selected_trace,
                )
            )
        cur_pred, _cur_margin = pred_and_margin(class_dist, y_parent)
        cur_int8_pred, _cur_int8_margin = pred_and_margin(int8_class_dist, y_parent)
        if np.sum(cur_pred != y_parent) == 0 and np.sum(cur_int8_pred != y_parent) == 0 and step not in snapshot_budgets:
            rows.append(
                evaluate_row(
                    stage="compiled_closed",
                    base_source=base_source,
                    base_k=display_base_k,
                    reserved_count=step,
                    embeddings=embeddings,
                    y_parent=y_parent,
                    view_order=view_order,
                    view_labels=view_labels,
                    prototypes=prototypes,
                    prototype_parent=prototype_parent,
                    class_dist=class_dist,
                    int8_class_dist=int8_class_dist,
                    int8_scale=int8_scale,
                    selected_trace=selected_trace,
                )
            )

    final_pred, final_margin = pred_and_margin(class_dist, y_parent)
    final_int8_pred, final_int8_margin = pred_and_margin(int8_class_dist, y_parent)
    final_payload = {
        "embedding_float": embeddings.astype(np.float32),
        "embedding_int8": embeddings_q.astype(np.int8),
        "parent": y_parent.astype(np.int64),
        "subclass": y_sub.astype(np.int64),
        "sample_index": sample_index.astype(np.int64),
        "view_labels": view_labels.astype(str),
        "paths": paths.astype(str),
        "pred": final_pred.astype(np.int64),
        "int8_pred": final_int8_pred.astype(np.int64),
        "margin": final_margin.astype(np.float32),
        "int8_margin": final_int8_margin.astype(np.int64),
        "prototypes": prototypes.astype(np.float32),
        "prototypes_int8": quantize(prototypes, int8_scale).astype(np.int8),
        "prototype_parent": prototype_parent.astype(np.int64),
        "prototype_subclass": prototype_subclass.astype(np.int64),
        "prototype_cluster": prototype_cluster.astype(np.int64),
        "prototype_sample_index": prototype_sample_index.astype(np.int64),
        "prototype_view_label": prototype_view_label.astype(str),
        "prototype_source_kind": prototype_source_kind.astype(str),
        "prototype_source_index": prototype_source_index.astype(np.int64),
        "base_source": np.asarray(base_source),
        "base_k_per_subclass": np.asarray(display_base_k, dtype=np.int64),
        "base_margin": base_margin.astype(np.float32),
        "base_int8_margin": base_int8_margin.astype(np.int64),
        "selected_candidate_trace_json": np.asarray(json.dumps(selected_trace, ensure_ascii=False)),
        "int8_scale": np.asarray(int8_scale, dtype=np.float32),
        "tie_break_policy": np.asarray("argmin_parent_order"),
    }
    return rows, final_payload, selected_trace


def row_score(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        -int(row["wrong_count"]),
        -int(row["int8_wrong_count"]),
        bool(row["clean_all_correct"]),
        bool(row["rotmirror_all_correct"]),
        bool(row["stress_all_correct"]),
        bool(row["int8_clean_all_correct"]),
        bool(row["int8_rotmirror_all_correct"]),
        bool(row["int8_stress_all_correct"]),
        -int(row["prototype_count"]),
        float(row["margin_min"]),
        int(row["int8_margin_min"]),
        -int(row["estimated_distance_macs"]),
    )


def boundary_events(payload: dict[str, np.ndarray], low_margin_quantile: float) -> list[dict[str, Any]]:
    y_parent = np.asarray(payload["parent"], dtype=np.int64)
    pred = np.asarray(payload["pred"], dtype=np.int64)
    int8_pred = np.asarray(payload["int8_pred"], dtype=np.int64)
    margin = np.asarray(payload["margin"], dtype=np.float64)
    int8_margin = np.asarray(payload["int8_margin"], dtype=np.int64)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    paths = np.asarray(payload["paths"]).astype(str)
    threshold = float(np.quantile(margin, low_margin_quantile))
    event_mask = (pred != y_parent) | (int8_pred != y_parent) | (margin <= threshold)
    rows: list[dict[str, Any]] = []
    for index in np.where(event_mask)[0]:
        sample = int(sample_index[index])
        rows.append(
            {
                "event_type": "final_wrong_or_low_margin",
                "event_index": int(index),
                "sample_index": sample,
                "path": str(paths[sample]),
                "view": str(view_labels[index]),
                "parent": PARENT_NAMES[int(y_parent[index])],
                "pred": PARENT_NAMES[int(pred[index])],
                "int8_pred": PARENT_NAMES[int(int8_pred[index])],
                "is_wrong": bool(pred[index] != y_parent[index]),
                "is_int8_wrong": bool(int8_pred[index] != y_parent[index]),
                "margin": float(margin[index]),
                "int8_margin": int(int8_margin[index]),
                "low_margin_threshold": threshold,
            }
        )
    try:
        selected = json.loads(str(np.asarray(payload["selected_candidate_trace_json"]).item()))
    except Exception:
        selected = []
    for item in selected:
        rows.append({"event_type": "selected_candidate", **item})
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Compile V8 closed-set boundary prototypes from a trained embedding.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sources", default="existing,kmeans,medoid,quant_medoid")
    parser.add_argument("--k-values", default="32,48,64,96")
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--quant-stable-scale", type=float, default=32.0)
    parser.add_argument("--seed", type=int, default=20260519)
    parser.add_argument("--low-margin-top", type=int, default=128)
    parser.add_argument("--defense-per-event", type=int, default=2)
    parser.add_argument("--max-reserved", type=int, default=32)
    parser.add_argument("--snapshot-budgets", default="0,4,8,16,32")
    parser.add_argument("--target-margin", type=float, default=0.02)
    parser.add_argument("--allow-new-wrong", action="store_true")
    parser.add_argument("--boundary-low-margin-quantile", type=float, default=0.01)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    embeddings, flat = load_embedding_params(args.params_npz)
    embeddings = np.asarray(embeddings, dtype=np.float64)
    existing_proto = load_existing_prototypes(args.params_npz)
    sources = parse_csv(args.sources)
    k_values = parse_ints(args.k_values)
    quant_scales = parse_floats(args.quant_scales)
    snapshot_budgets = set(parse_ints(args.snapshot_budgets))

    all_rows: list[dict[str, Any]] = []
    payloads: list[tuple[dict[str, Any], dict[str, np.ndarray]]] = []
    for source in sources:
        effective_k_values = [0] if source == "existing" else k_values
        for k_value in effective_k_values:
            rows, payload, selected_trace = compile_for_base(
                embeddings=embeddings,
                flat=flat,
                existing_proto=existing_proto,
                base_source=source,
                base_k=k_value,
                quant_scales=quant_scales,
                quant_stable_scale=args.quant_stable_scale,
                seed=args.seed,
                low_margin_top=args.low_margin_top,
                defense_per_event=args.defense_per_event,
                max_reserved=args.max_reserved,
                snapshot_budgets=snapshot_budgets,
                reject_new_wrong=not args.allow_new_wrong,
                target_margin=args.target_margin,
            )
            all_rows.extend(rows)
            for row in rows:
                payloads.append((row, payload))
            best_local = max(rows, key=row_score)
            print(
                json.dumps(
                    {
                        "source": source,
                        "k": k_value,
                        "best_reserved": best_local["reserved_count"],
                        "prototype_count": best_local["prototype_count"],
                        "wrong_count": best_local["wrong_count"],
                        "int8_wrong_count": best_local["int8_wrong_count"],
                        "selected": len(selected_trace),
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

    sorted_rows = sorted(all_rows, key=row_score, reverse=True)
    write_csv(args.output_dir / "compiled_candidate_results.csv", sorted_rows)
    best_row = sorted_rows[0] if sorted_rows else None
    best_payload: dict[str, np.ndarray] | None = None
    if best_row is not None:
        # Re-run the winning base only to the exact winning reserved count. The broad
        # sweep may continue selecting margin-improving candidates after the best row.
        best_source = str(best_row["base_source"])
        best_k = 0 if best_source == "existing" else int(best_row["base_k_per_subclass"])
        best_reserved = int(best_row["reserved_count"])
        _best_rows, best_payload, _selected = compile_for_base(
            embeddings=embeddings,
            flat=flat,
            existing_proto=existing_proto,
            base_source=best_source,
            base_k=best_k,
            quant_scales=quant_scales,
            quant_stable_scale=args.quant_stable_scale,
            seed=args.seed,
            low_margin_top=args.low_margin_top,
            defense_per_event=args.defense_per_event,
            max_reserved=best_reserved,
            snapshot_budgets={best_reserved},
            reject_new_wrong=not args.allow_new_wrong,
            target_margin=args.target_margin,
        )
    if best_payload is not None:
        np.savez_compressed(args.output_dir / "best_compiled_v8_prototype_params.npz", **best_payload)
        write_csv(args.output_dir / "compiled_boundary_events.csv", boundary_events(best_payload, args.boundary_low_margin_quantile))
        try:
            selected_trace = json.loads(str(np.asarray(best_payload["selected_candidate_trace_json"]).item()))
        except Exception:
            selected_trace = []
        (args.output_dir / "selected_candidate_trace.json").write_text(
            json.dumps(selected_trace, indent=2, ensure_ascii=False), encoding="utf-8"
        )

    summary = {
        "params_npz": str(args.params_npz),
        "candidate_row_count": len(sorted_rows),
        "best": best_row,
        "top20": sorted_rows[:20],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"best": best_row, "candidate_row_count": len(sorted_rows)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
