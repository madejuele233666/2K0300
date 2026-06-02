import argparse
import csv
import json
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import PARENT_NAMES, metric_summary, write_csv


PARENT_COUNT = len(PARENT_NAMES)


@dataclass
class EvalResult:
    pred: np.ndarray
    margin: np.ndarray
    class_dist: np.ndarray
    nearest_by_parent: np.ndarray
    usage_correct: np.ndarray
    usage_wrong_low_margin: np.ndarray
    margin_min: int
    margin_mean: float
    all_correct: bool
    source_decision_margin: np.ndarray
    source_decision_active: np.ndarray
    source_decision_target: np.ndarray
    source_decision_margin_min: int
    source_decision_margin_mean: float
    source_decision_le_target: int


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_ints(value: str) -> list[int]:
    return [int(item.strip()) for item in value.split(",") if item.strip()]


def parse_strings(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def unique_view_order(view_labels: np.ndarray) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for item in view_labels.tolist():
        name = str(item)
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out


def ensure_1d_str(values: np.ndarray, length: int, fill: str) -> np.ndarray:
    if values.shape == ():
        return np.asarray([str(values.item())] * length)
    return values.astype(str)


def load_state(path: Path) -> tuple[dict[str, np.ndarray], dict[str, np.ndarray]]:
    with np.load(path, allow_pickle=True) as data:
        base = {key: data[key] for key in data.files}
    required = [
        "embedding_int8",
        "parent",
        "subclass",
        "sample_index",
        "view_labels",
        "paths",
        "prototypes_int8",
        "prototype_parent",
    ]
    missing = [key for key in required if key not in base]
    if missing:
        raise ValueError(f"{path} is missing required arrays: {missing}")

    proto_count = int(len(base["prototypes_int8"]))
    state = {
        "prototypes_int8": np.asarray(base["prototypes_int8"], dtype=np.int8),
        "prototype_parent": np.asarray(base["prototype_parent"], dtype=np.int64),
        "prototype_subclass": np.asarray(base.get("prototype_subclass", np.full(proto_count, -1)), dtype=np.int64),
        "prototype_cluster": np.asarray(base.get("prototype_cluster", np.arange(proto_count)), dtype=np.int64),
        "prototype_sample_index": np.asarray(base.get("prototype_sample_index", np.full(proto_count, -1)), dtype=np.int64),
        "prototype_view_label": ensure_1d_str(
            np.asarray(base.get("prototype_view_label", np.asarray(["unknown"] * proto_count))),
            proto_count,
            "unknown",
        ),
        "prototype_source_kind": ensure_1d_str(
            np.asarray(base.get("prototype_source_kind", np.asarray(["unknown"] * proto_count))),
            proto_count,
            "unknown",
        ),
    }
    return base, state


def clone_state(state: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    return {key: value.copy() for key, value in state.items()}


def subset_state(state: dict[str, np.ndarray], keep: np.ndarray) -> dict[str, np.ndarray]:
    return {key: value[keep].copy() for key, value in state.items()}


def remove_one(state: dict[str, np.ndarray], index: int) -> dict[str, np.ndarray]:
    keep = np.ones(len(state["prototypes_int8"]), dtype=bool)
    keep[index] = False
    return subset_state(state, keep)


def merge_mean_pair(state: dict[str, np.ndarray], keep_index: int, drop_index: int, label: str) -> dict[str, np.ndarray]:
    out = remove_one(state, drop_index)
    mapped_keep = keep_index - (1 if drop_index < keep_index else 0)
    a = state["prototypes_int8"][keep_index].astype(np.int16)
    b = state["prototypes_int8"][drop_index].astype(np.int16)
    merged = np.clip(np.rint((a + b) / 2.0), -128, 127).astype(np.int8)
    out["prototypes_int8"][mapped_keep] = merged
    out["prototype_subclass"][mapped_keep] = (
        state["prototype_subclass"][keep_index]
        if state["prototype_subclass"][keep_index] == state["prototype_subclass"][drop_index]
        else -1
    )
    out["prototype_cluster"][mapped_keep] = -1
    out["prototype_sample_index"][mapped_keep] = -1
    out["prototype_view_label"][mapped_keep] = label
    out["prototype_source_kind"][mapped_keep] = label
    return out


def evaluate_int8(
    embeddings: np.ndarray,
    y_parent: np.ndarray,
    proto: np.ndarray,
    proto_parent: np.ndarray,
    low_margin_threshold: int,
    source_wrong_parent: np.ndarray | None = None,
    source_target_margin: np.ndarray | None = None,
    source_weight: np.ndarray | None = None,
    source_margin_override: float = -1.0,
) -> EvalResult:
    x = embeddings.astype(np.int32)
    p = proto.astype(np.int32)
    dist = np.sum((x[:, None, :] - p[None, :, :]) ** 2, axis=2)
    class_dist_rows: list[np.ndarray] = []
    nearest_rows: list[np.ndarray] = []
    for parent in range(PARENT_COUNT):
        indexes = np.where(proto_parent == parent)[0]
        if len(indexes) == 0:
            class_dist_rows.append(np.full(len(x), np.iinfo(np.int32).max, dtype=np.int64))
            nearest_rows.append(np.full(len(x), -1, dtype=np.int64))
            continue
        local = dist[:, indexes]
        local_argmin = np.argmin(local, axis=1)
        class_dist_rows.append(local[np.arange(len(x)), local_argmin].astype(np.int64))
        nearest_rows.append(indexes[local_argmin].astype(np.int64))
    class_dist = np.stack(class_dist_rows, axis=1)
    nearest_by_parent = np.stack(nearest_rows, axis=1)
    pred = np.argmin(class_dist, axis=1).astype(np.int64)
    order = np.argsort(class_dist, axis=1)
    margin = class_dist[np.arange(len(x)), order[:, 1]] - class_dist[np.arange(len(x)), order[:, 0]]
    nearest_correct = nearest_by_parent[np.arange(len(x)), y_parent]
    usage_correct = np.bincount(nearest_correct[nearest_correct >= 0], minlength=len(proto))
    wrong_parent = np.where(order[:, 0] == y_parent, order[:, 1], order[:, 0])
    nearest_wrong = nearest_by_parent[np.arange(len(x)), wrong_parent]
    low = margin <= int(low_margin_threshold)
    usage_wrong_low_margin = np.bincount(nearest_wrong[low & (nearest_wrong >= 0)], minlength=len(proto))
    source_margin = np.zeros(len(x), dtype=np.int64)
    source_active = np.zeros(len(x), dtype=bool)
    source_target = np.zeros(len(x), dtype=np.float32)
    source_margin_min = 0
    source_margin_mean = 0.0
    source_le_target = 0
    if source_wrong_parent is not None and source_target_margin is not None and source_weight is not None:
        wrong = np.asarray(source_wrong_parent, dtype=np.int64)
        target = np.asarray(source_target_margin, dtype=np.float32)
        weight = np.asarray(source_weight, dtype=np.float32)
        if len(wrong) != len(x) or len(target) != len(x) or len(weight) != len(x):
            raise ValueError("source-decision arrays must match embedding row count")
        source_active = (wrong >= 0) & (weight > 0.0)
        source_target = (
            np.full(len(x), float(source_margin_override), dtype=np.float32)
            if float(source_margin_override) >= 0.0
            else target
        )
        source_margin = (
            class_dist[np.arange(len(x)), np.clip(wrong, 0, PARENT_COUNT - 1)]
            - class_dist[np.arange(len(x)), y_parent]
        ).astype(np.int64)
        active_margin = source_margin[source_active]
        if len(active_margin):
            source_margin_min = int(np.min(active_margin))
            source_margin_mean = float(np.mean(active_margin))
            source_le_target = int(np.sum(source_active & (source_margin <= source_target)))
    return EvalResult(
        pred=pred,
        margin=margin.astype(np.int64),
        class_dist=class_dist,
        nearest_by_parent=nearest_by_parent,
        usage_correct=usage_correct.astype(np.int64),
        usage_wrong_low_margin=usage_wrong_low_margin.astype(np.int64),
        margin_min=int(np.min(margin)),
        margin_mean=float(np.mean(margin)),
        all_correct=bool(np.all(pred == y_parent)),
        source_decision_margin=source_margin.astype(np.int64),
        source_decision_active=source_active,
        source_decision_target=source_target.astype(np.float32),
        source_decision_margin_min=source_margin_min,
        source_decision_margin_mean=source_margin_mean,
        source_decision_le_target=source_le_target,
    )


def source_decision_arrays(base: dict[str, np.ndarray]) -> tuple[np.ndarray | None, np.ndarray | None, np.ndarray | None]:
    required = ["source_decision_wrong_parent", "source_decision_target_margin", "source_decision_weight"]
    if not all(key in base for key in required):
        return None, None, None
    return (
        np.asarray(base["source_decision_wrong_parent"], dtype=np.int64),
        np.asarray(base["source_decision_target_margin"], dtype=np.float32),
        np.asarray(base["source_decision_weight"], dtype=np.float32),
    )


def evaluate_state(
    *,
    base: dict[str, np.ndarray],
    state: dict[str, np.ndarray],
    low_margin_threshold: int,
    source_margin_override: float,
) -> EvalResult:
    wrong, target, weight = source_decision_arrays(base)
    return evaluate_int8(
        np.asarray(base["embedding_int8"], dtype=np.int8),
        np.asarray(base["parent"], dtype=np.int64),
        state["prototypes_int8"],
        state["prototype_parent"],
        low_margin_threshold,
        source_wrong_parent=wrong,
        source_target_margin=target,
        source_weight=weight,
        source_margin_override=source_margin_override,
    )


def row_for_state(
    *,
    name: str,
    source: str,
    base: dict[str, np.ndarray],
    state: dict[str, np.ndarray],
    eval_result: EvalResult,
    extra: dict[str, Any] | None = None,
) -> dict[str, Any]:
    view_labels = np.asarray(base["view_labels"]).astype(str)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    view_order = unique_view_order(view_labels)
    proto_count = int(len(state["prototypes_int8"]))
    feature_dim = int(state["prototypes_int8"].shape[1])
    row: dict[str, Any] = {
        "stage": "v8_parent_logits_prune_merge",
        "name": name,
        "feature_source": str(np.asarray(base.get("feature_source", "int8_tflite")).item()),
        "prototype_source": source,
        "k_per_subclass": "",
        "feature_dim": feature_dim,
        "prototype_count": proto_count,
        "estimated_distance_macs": int(proto_count * feature_dim),
        "estimated_float_table_bytes": int(proto_count * feature_dim * 4),
        "estimated_int8_table_bytes": int(proto_count * feature_dim),
        "margin_min": float(eval_result.margin_min),
        "margin_mean": float(eval_result.margin_mean),
        "int8_scale": float(np.asarray(base.get("int8_scale", 1.0)).item()),
        "int8_flip_count": 0,
        "int8_margin_min": int(eval_result.margin_min),
        "int8_margin_mean": float(eval_result.margin_mean),
        "tflite_unique_ops": "",
        "low_margin_le_1": int(np.sum(eval_result.margin <= 1)),
        "low_margin_le_2": int(np.sum(eval_result.margin <= 2)),
        "low_margin_le_4": int(np.sum(eval_result.margin <= 4)),
        "low_margin_le_8": int(np.sum(eval_result.margin <= 8)),
        "source_decision_active_rows": int(np.sum(eval_result.source_decision_active)),
        "source_decision_margin_min": int(eval_result.source_decision_margin_min),
        "source_decision_margin_mean": float(eval_result.source_decision_margin_mean),
        "source_decision_margin_le_target": int(eval_result.source_decision_le_target),
    }
    if extra:
        row.update(extra)
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=eval_result.pred))
    row.update(
        metric_summary(
            view_order=view_order,
            view_labels=view_labels,
            y_parent=y_parent,
            pred=eval_result.pred,
            prefix="int8_",
        )
    )
    return row


def state_payload(base: dict[str, np.ndarray], state: dict[str, np.ndarray], eval_result: EvalResult) -> dict[str, np.ndarray]:
    payload: dict[str, np.ndarray] = {}
    for key, value in base.items():
        if key.startswith("prototype_") or key in {"prototypes", "prototypes_int8", "pred", "int8_pred", "margin", "int8_margin"}:
            continue
        payload[key] = value
    payload["pred"] = eval_result.pred.astype(np.int64)
    payload["int8_pred"] = eval_result.pred.astype(np.int64)
    payload["margin"] = eval_result.margin.astype(np.float32)
    payload["int8_margin"] = eval_result.margin.astype(np.int64)
    if np.any(eval_result.source_decision_active):
        payload["source_decision_margin"] = eval_result.source_decision_margin.astype(np.int64)
    payload["prototypes_int8"] = state["prototypes_int8"].astype(np.int8)
    payload["prototypes"] = state["prototypes_int8"].astype(np.float32)
    for key, value in state.items():
        if key == "prototypes_int8":
            continue
        payload[key] = value
    payload["prototype_cluster"] = np.arange(len(state["prototypes_int8"]), dtype=np.int64)
    payload["tie_break_policy"] = np.asarray("argmin_parent_order")
    return payload


def accept_candidate(
    *,
    current: EvalResult,
    candidate: EvalResult,
    require_margin_at_least: int,
    source_decision_preserve: bool,
    source_decision_max_le_target_increase: int,
    source_decision_allow_min_margin_drop: int,
) -> bool:
    if not candidate.all_correct:
        return False
    if candidate.margin_min < require_margin_at_least:
        return False
    if source_decision_preserve and bool(np.any(current.source_decision_active)):
        if candidate.source_decision_le_target > current.source_decision_le_target + int(source_decision_max_le_target_increase):
            return False
        if candidate.source_decision_margin_min < current.source_decision_margin_min - int(source_decision_allow_min_margin_drop):
            return False
    return candidate.margin_min >= current.margin_min


def greedy_remove(
    *,
    label: str,
    base: dict[str, np.ndarray],
    start_state: dict[str, np.ndarray],
    candidates: list[int],
    low_margin_threshold: int,
    max_attempts: int,
    require_margin_at_least: int,
    source_decision_preserve: bool,
    source_decision_margin_override: float,
    source_decision_max_le_target_increase: int,
    source_decision_allow_min_margin_drop: int,
) -> tuple[dict[str, np.ndarray], EvalResult, list[dict[str, Any]]]:
    state = clone_state(start_state)
    current = evaluate_state(
        base=base,
        state=state,
        low_margin_threshold=low_margin_threshold,
        source_margin_override=source_decision_margin_override,
    )
    history: list[dict[str, Any]] = []
    original_ids = np.arange(len(state["prototypes_int8"]), dtype=np.int64)
    attempted = 0
    for original_index in candidates:
        if attempted >= max_attempts:
            break
        matches = np.where(original_ids == int(original_index))[0]
        if len(matches) == 0:
            continue
        local_index = int(matches[0])
        attempted += 1
        trial_state = remove_one(state, local_index)
        trial_ids = np.delete(original_ids, local_index)
        trial = evaluate_state(
            base=base,
            state=trial_state,
            low_margin_threshold=low_margin_threshold,
            source_margin_override=source_decision_margin_override,
        )
        if accept_candidate(
            current=current,
            candidate=trial,
            require_margin_at_least=require_margin_at_least,
            source_decision_preserve=source_decision_preserve,
            source_decision_max_le_target_increase=source_decision_max_le_target_increase,
            source_decision_allow_min_margin_drop=source_decision_allow_min_margin_drop,
        ):
            history.append(
                {
                    "label": label,
                    "accepted": True,
                    "original_index": int(original_index),
                    "attempt": int(attempted),
                    "prototype_count": int(len(trial_state["prototypes_int8"])),
                    "margin_min": int(trial.margin_min),
                    "margin_mean": float(trial.margin_mean),
                    "source_decision_margin_min": int(trial.source_decision_margin_min),
                    "source_decision_le_target": int(trial.source_decision_le_target),
                }
            )
            state = trial_state
            original_ids = trial_ids
            current = trial
        else:
            history.append(
                {
                    "label": label,
                    "accepted": False,
                    "original_index": int(original_index),
                    "attempt": int(attempted),
                    "margin_min": int(trial.margin_min),
                    "all_correct": bool(trial.all_correct),
                    "source_decision_margin_min": int(trial.source_decision_margin_min),
                    "source_decision_le_target": int(trial.source_decision_le_target),
                }
            )
    return state, current, history


def build_prune_candidates(eval_result: EvalResult, mode: str) -> list[int]:
    indexes = np.arange(len(eval_result.usage_correct), dtype=np.int64)
    if mode == "unused":
        candidates = indexes[eval_result.usage_correct == 0]
        order = np.lexsort((candidates, -eval_result.usage_wrong_low_margin[candidates]))
        return candidates[order].astype(int).tolist()
    if mode == "low_margin_wrong":
        candidates = indexes[eval_result.usage_wrong_low_margin > 0]
        order = np.lexsort((eval_result.usage_correct[candidates], -eval_result.usage_wrong_low_margin[candidates]))
        return candidates[order].astype(int).tolist()
    raise ValueError(f"unknown prune candidate mode: {mode}")


def build_duplicate_candidates(state: dict[str, np.ndarray], eval_result: EvalResult) -> list[int]:
    buckets: dict[tuple[int, int, int, int], list[int]] = {}
    proto = state["prototypes_int8"].astype(np.int16)
    parents = state["prototype_parent"].astype(np.int64)
    for index, row in enumerate(proto):
        key = (int(parents[index]), int(row[0]), int(row[1]), int(row[2]))
        buckets.setdefault(key, []).append(index)
    remove: list[int] = []
    for indexes in buckets.values():
        if len(indexes) <= 1:
            continue
        indexes_sorted = sorted(indexes, key=lambda item: (eval_result.usage_correct[item], -eval_result.usage_wrong_low_margin[item], item))
        remove.extend(indexes_sorted[:-1])
    return remove


def build_pair_candidates(
    state: dict[str, np.ndarray],
    eval_result: EvalResult,
    max_distance: int,
    max_pairs: int,
) -> list[tuple[int, int, int]]:
    proto = state["prototypes_int8"].astype(np.int16)
    parents = state["prototype_parent"].astype(np.int64)
    pairs: list[tuple[int, int, int, int, int]] = []
    for parent in range(PARENT_COUNT):
        indexes = np.where(parents == parent)[0]
        group = proto[indexes]
        for local_i in range(len(indexes)):
            diff = group[local_i + 1 :] - group[local_i]
            if len(diff) == 0:
                continue
            dist = np.sum(diff * diff, axis=1)
            hits = np.where(dist <= max_distance)[0]
            for hit in hits:
                i = int(indexes[local_i])
                j = int(indexes[local_i + 1 + int(hit)])
                use_i = int(eval_result.usage_correct[i])
                use_j = int(eval_result.usage_correct[j])
                wrong = int(eval_result.usage_wrong_low_margin[i] + eval_result.usage_wrong_low_margin[j])
                pairs.append((int(dist[int(hit)]), -(wrong), use_i + use_j, i, j))
    pairs.sort()
    return [(i, j, d) for d, _wrong, _usage, i, j in pairs[:max_pairs]]


def greedy_merge(
    *,
    label: str,
    base: dict[str, np.ndarray],
    start_state: dict[str, np.ndarray],
    pairs: list[tuple[int, int, int]],
    mode: str,
    low_margin_threshold: int,
    max_attempts: int,
    require_margin_at_least: int,
    source_decision_preserve: bool,
    source_decision_margin_override: float,
    source_decision_max_le_target_increase: int,
    source_decision_allow_min_margin_drop: int,
) -> tuple[dict[str, np.ndarray], EvalResult, list[dict[str, Any]]]:
    state = clone_state(start_state)
    current = evaluate_state(
        base=base,
        state=state,
        low_margin_threshold=low_margin_threshold,
        source_margin_override=source_decision_margin_override,
    )
    original_ids = np.arange(len(state["prototypes_int8"]), dtype=np.int64)
    history: list[dict[str, Any]] = []
    attempted = 0
    for original_i, original_j, distance in pairs:
        if attempted >= max_attempts:
            break
        hit_i = np.where(original_ids == int(original_i))[0]
        hit_j = np.where(original_ids == int(original_j))[0]
        if len(hit_i) == 0 or len(hit_j) == 0:
            continue
        local_i = int(hit_i[0])
        local_j = int(hit_j[0])
        if state["prototype_parent"][local_i] != state["prototype_parent"][local_j]:
            continue
        attempted += 1
        if mode == "medoid":
            use_i = current.usage_correct[local_i]
            use_j = current.usage_correct[local_j]
            drop = local_i if use_i <= use_j else local_j
            trial_state = remove_one(state, drop)
            trial_ids = np.delete(original_ids, drop)
        elif mode == "mean":
            keep = local_i
            drop = local_j
            trial_state = merge_mean_pair(state, keep, drop, label)
            trial_ids = np.delete(original_ids, drop)
        else:
            raise ValueError(f"unknown merge mode: {mode}")
        trial = evaluate_state(
            base=base,
            state=trial_state,
            low_margin_threshold=low_margin_threshold,
            source_margin_override=source_decision_margin_override,
        )
        if accept_candidate(
            current=current,
            candidate=trial,
            require_margin_at_least=require_margin_at_least,
            source_decision_preserve=source_decision_preserve,
            source_decision_max_le_target_increase=source_decision_max_le_target_increase,
            source_decision_allow_min_margin_drop=source_decision_allow_min_margin_drop,
        ):
            history.append(
                {
                    "label": label,
                    "accepted": True,
                    "original_i": int(original_i),
                    "original_j": int(original_j),
                    "distance": int(distance),
                    "attempt": int(attempted),
                    "prototype_count": int(len(trial_state["prototypes_int8"])),
                    "margin_min": int(trial.margin_min),
                    "margin_mean": float(trial.margin_mean),
                    "source_decision_margin_min": int(trial.source_decision_margin_min),
                    "source_decision_le_target": int(trial.source_decision_le_target),
                }
            )
            state = trial_state
            original_ids = trial_ids
            current = trial
        else:
            history.append(
                {
                    "label": label,
                    "accepted": False,
                    "original_i": int(original_i),
                    "original_j": int(original_j),
                    "distance": int(distance),
                    "attempt": int(attempted),
                    "margin_min": int(trial.margin_min),
                    "all_correct": bool(trial.all_correct),
                    "source_decision_margin_min": int(trial.source_decision_margin_min),
                    "source_decision_le_target": int(trial.source_decision_le_target),
                }
            )
    return state, current, history


def margin_event_rows(
    *,
    base: dict[str, np.ndarray],
    state: dict[str, np.ndarray],
    eval_result: EvalResult,
    threshold: int,
    limit: int,
) -> list[dict[str, Any]]:
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    proto_parent = state["prototype_parent"].astype(np.int64)
    proto_sample = state["prototype_sample_index"].astype(np.int64)
    proto_view = state["prototype_view_label"].astype(str)
    proto_source = state["prototype_source_kind"].astype(str)
    order = np.argsort(eval_result.class_dist, axis=1)
    low = np.where(eval_result.margin <= threshold)[0]
    low = low[np.argsort(eval_result.margin[low])][:limit]
    rows: list[dict[str, Any]] = []
    for query in low:
        correct_parent = int(y_parent[query])
        wrong_parent = int(order[query, 1] if order[query, 0] == correct_parent else order[query, 0])
        correct_proto = int(eval_result.nearest_by_parent[query, correct_parent])
        wrong_proto = int(eval_result.nearest_by_parent[query, wrong_parent])
        rows.append(
            {
                "query_index": int(query),
                "sample_index": int(sample_index[query]),
                "view_label": str(view_labels[query]),
                "parent": correct_parent,
                "pred": int(eval_result.pred[query]),
                "margin": int(eval_result.margin[query]),
                "correct_dist": int(eval_result.class_dist[query, correct_parent]),
                "wrong_parent": wrong_parent,
                "wrong_dist": int(eval_result.class_dist[query, wrong_parent]),
                "correct_proto": correct_proto,
                "correct_proto_sample": int(proto_sample[correct_proto]) if correct_proto >= 0 else -1,
                "correct_proto_view": str(proto_view[correct_proto]) if correct_proto >= 0 else "",
                "correct_proto_source": str(proto_source[correct_proto]) if correct_proto >= 0 else "",
                "wrong_proto": wrong_proto,
                "wrong_proto_parent": int(proto_parent[wrong_proto]) if wrong_proto >= 0 else -1,
                "wrong_proto_sample": int(proto_sample[wrong_proto]) if wrong_proto >= 0 else -1,
                "wrong_proto_view": str(proto_view[wrong_proto]) if wrong_proto >= 0 else "",
                "wrong_proto_source": str(proto_source[wrong_proto]) if wrong_proto >= 0 else "",
            }
        )
    return rows


def write_event_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def row_score(row: dict[str, Any]) -> tuple[Any, ...]:
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
        int(float(row["int8_margin_min"])),
        -int(row.get("low_margin_le_1") or 0),
        -int(row.get("low_margin_le_2") or 0),
        -int(row.get("low_margin_le_4") or 0),
        -int(row.get("low_margin_le_8") or 0),
        float(row["int8_margin_mean"]),
        -int(row["prototype_count"]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Prune/merge V8 parent-logit int8 prototype tables.")
    parser.add_argument("input_npz", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--low-margin-thresholds", default="1,2,4,8")
    parser.add_argument("--merge-distance-thresholds", default="0,1,2,4,8")
    parser.add_argument("--event-threshold", type=int, default=8)
    parser.add_argument("--event-limit", type=int, default=400)
    parser.add_argument("--max-prune-attempts", type=int, default=1200)
    parser.add_argument("--max-merge-attempts", type=int, default=800)
    parser.add_argument("--max-pairs-per-threshold", type=int, default=2000)
    parser.add_argument("--require-margin-at-least", type=int, default=1)
    parser.add_argument("--save-payload-sources", default="")
    parser.add_argument("--source-decision-preserve", action="store_true")
    parser.add_argument(
        "--source-decision-compiler-margin",
        type=float,
        default=-1.0,
        help="Use this source-decision margin threshold; negative means use per-row target from the input payload.",
    )
    parser.add_argument("--source-decision-max-le-target-increase", type=int, default=0)
    parser.add_argument("--source-decision-allow-min-margin-drop", type=int, default=0)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base, base_state = load_state(args.input_npz)
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(base["parent"], dtype=np.int64)
    run_name = args.name or args.input_npz.parent.name
    thresholds = parse_ints(args.low_margin_thresholds)
    merge_thresholds = parse_ints(args.merge_distance_thresholds)
    base_eval = evaluate_state(
        base=base,
        state=base_state,
        low_margin_threshold=max(thresholds + [args.event_threshold]),
        source_margin_override=args.source_decision_compiler_margin,
    )
    rows: list[dict[str, Any]] = []
    payloads: list[tuple[dict[str, Any], dict[str, np.ndarray], EvalResult]] = []
    histories: dict[str, list[dict[str, Any]]] = {}

    base_row = row_for_state(
        name=run_name,
        source="base",
        base=base,
        state=base_state,
        eval_result=base_eval,
        extra={
            "prune_merge_history_json": "[]",
            "low_margin_le_1": int(np.sum(base_eval.margin <= 1)),
            "low_margin_le_2": int(np.sum(base_eval.margin <= 2)),
            "low_margin_le_4": int(np.sum(base_eval.margin <= 4)),
            "low_margin_le_8": int(np.sum(base_eval.margin <= 8)),
        },
    )
    rows.append(base_row)
    payloads.append((base_row, base_state, base_eval))
    write_event_csv(
        args.output_dir / "base_margin_events.csv",
        margin_event_rows(
            base=base,
            state=base_state,
            eval_result=base_eval,
            threshold=args.event_threshold,
            limit=args.event_limit,
        ),
    )

    for threshold in thresholds:
        eval_for_threshold = evaluate_state(
            base=base,
            state=base_state,
            low_margin_threshold=threshold,
            source_margin_override=args.source_decision_compiler_margin,
        )
        for mode in ["unused", "low_margin_wrong"]:
            candidates = build_prune_candidates(eval_for_threshold, mode)
            state, result, history = greedy_remove(
                label=f"prune_{mode}_le{threshold}",
                base=base,
                start_state=base_state,
                candidates=candidates,
                low_margin_threshold=threshold,
                max_attempts=args.max_prune_attempts,
                require_margin_at_least=args.require_margin_at_least,
                source_decision_preserve=args.source_decision_preserve,
                source_decision_margin_override=args.source_decision_compiler_margin,
                source_decision_max_le_target_increase=args.source_decision_max_le_target_increase,
                source_decision_allow_min_margin_drop=args.source_decision_allow_min_margin_drop,
            )
            histories[f"prune_{mode}_le{threshold}"] = history
            row = row_for_state(
                name=run_name,
                source=f"prune_{mode}_le{threshold}",
                base=base,
                state=state,
                eval_result=result,
                extra={
                    "accepted_ops": int(sum(1 for item in history if item["accepted"])),
                    "attempted_ops": int(len(history)),
                    "source_decision_preserve": bool(args.source_decision_preserve),
                    "prune_merge_history_json": json.dumps(history[-50:], ensure_ascii=False),
                },
            )
            rows.append(row)
            payloads.append((row, state, result))

        unused_candidates = build_prune_candidates(eval_for_threshold, "unused")
        unused_state, unused_result, unused_history = greedy_remove(
            label=f"cascade_unused_le{threshold}",
            base=base,
            start_state=base_state,
            candidates=unused_candidates,
            low_margin_threshold=threshold,
            max_attempts=args.max_prune_attempts,
            require_margin_at_least=args.require_margin_at_least,
            source_decision_preserve=args.source_decision_preserve,
            source_decision_margin_override=args.source_decision_compiler_margin,
            source_decision_max_le_target_increase=args.source_decision_max_le_target_increase,
            source_decision_allow_min_margin_drop=args.source_decision_allow_min_margin_drop,
        )
        next_eval = evaluate_state(
            base=base,
            state=unused_state,
            low_margin_threshold=threshold,
            source_margin_override=args.source_decision_compiler_margin,
        )
        low_candidates = build_prune_candidates(next_eval, "low_margin_wrong")
        low_state, low_result, low_history = greedy_remove(
            label=f"cascade_low_margin_wrong_le{threshold}",
            base=base,
            start_state=unused_state,
            candidates=low_candidates,
            low_margin_threshold=threshold,
            max_attempts=args.max_prune_attempts,
            require_margin_at_least=args.require_margin_at_least,
            source_decision_preserve=args.source_decision_preserve,
            source_decision_margin_override=args.source_decision_compiler_margin,
            source_decision_max_le_target_increase=args.source_decision_max_le_target_increase,
            source_decision_allow_min_margin_drop=args.source_decision_allow_min_margin_drop,
        )
        history = unused_history + low_history
        histories[f"prune_unused_then_low_margin_wrong_le{threshold}"] = history
        row = row_for_state(
            name=run_name,
            source=f"prune_unused_then_low_margin_wrong_le{threshold}",
            base=base,
            state=low_state,
            eval_result=low_result,
            extra={
                "accepted_ops": int(sum(1 for item in history if item["accepted"])),
                "attempted_ops": int(len(history)),
                "source_decision_preserve": bool(args.source_decision_preserve),
                "prune_merge_history_json": json.dumps(history[-50:], ensure_ascii=False),
            },
        )
        rows.append(row)
        payloads.append((row, low_state, low_result))

    duplicate_candidates = build_duplicate_candidates(base_state, base_eval)
    if duplicate_candidates:
        state, result, history = greedy_remove(
            label="prune_duplicate_same_parent_code",
            base=base,
            start_state=base_state,
            candidates=duplicate_candidates,
            low_margin_threshold=args.event_threshold,
            max_attempts=args.max_prune_attempts,
            require_margin_at_least=args.require_margin_at_least,
            source_decision_preserve=args.source_decision_preserve,
            source_decision_margin_override=args.source_decision_compiler_margin,
            source_decision_max_le_target_increase=args.source_decision_max_le_target_increase,
            source_decision_allow_min_margin_drop=args.source_decision_allow_min_margin_drop,
        )
        histories["prune_duplicate_same_parent_code"] = history
        row = row_for_state(
            name=run_name,
            source="prune_duplicate_same_parent_code",
            base=base,
            state=state,
            eval_result=result,
            extra={
                "accepted_ops": int(sum(1 for item in history if item["accepted"])),
                "attempted_ops": int(len(history)),
                "source_decision_preserve": bool(args.source_decision_preserve),
                "prune_merge_history_json": json.dumps(history[-50:], ensure_ascii=False),
            },
        )
        rows.append(row)
        payloads.append((row, state, result))

    for distance in merge_thresholds:
        pairs = build_pair_candidates(
            base_state,
            base_eval,
            max_distance=distance,
            max_pairs=args.max_pairs_per_threshold,
        )
        for mode in ["medoid", "mean"]:
            state, result, history = greedy_merge(
                label=f"merge_{mode}_d{distance}",
                base=base,
                start_state=base_state,
                pairs=pairs,
                mode=mode,
                low_margin_threshold=args.event_threshold,
                max_attempts=args.max_merge_attempts,
                require_margin_at_least=args.require_margin_at_least,
                source_decision_preserve=args.source_decision_preserve,
                source_decision_margin_override=args.source_decision_compiler_margin,
                source_decision_max_le_target_increase=args.source_decision_max_le_target_increase,
                source_decision_allow_min_margin_drop=args.source_decision_allow_min_margin_drop,
            )
            histories[f"merge_{mode}_d{distance}"] = history
            row = row_for_state(
                name=run_name,
                source=f"merge_{mode}_d{distance}",
                base=base,
                state=state,
                eval_result=result,
                extra={
                    "accepted_ops": int(sum(1 for item in history if item["accepted"])),
                    "attempted_ops": int(len(history)),
                    "candidate_pairs": int(len(pairs)),
                    "source_decision_preserve": bool(args.source_decision_preserve),
                    "prune_merge_history_json": json.dumps(history[-50:], ensure_ascii=False),
                },
            )
            rows.append(row)
            payloads.append((row, state, result))

    rows_sorted = sorted(rows, key=row_score, reverse=True)
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    payload_by_source = {row["prototype_source"]: (row, state, result) for row, state, result in payloads}
    saved_payloads: dict[str, str] = {}
    for source in parse_strings(args.save_payload_sources):
        if source not in payload_by_source:
            raise ValueError(f"requested payload source not found: {source}")
        row, state, result = payload_by_source[source]
        payload = state_payload(base, state, result)
        safe_source = source.replace("/", "_").replace(" ", "_")
        path = args.output_dir / f"{safe_source}_parent_logits_params.npz"
        np.savez_compressed(path, **payload)
        saved_payloads[source] = str(path)
    best_row = rows_sorted[0]
    _row, best_state, best_eval = payload_by_source[str(best_row["prototype_source"])]
    best_payload = state_payload(base, best_state, best_eval)
    np.savez_compressed(args.output_dir / "best_pruned_merged_parent_logits_params.npz", **best_payload)
    np.savez_compressed(args.output_dir / "best_parent_logits_memory_params.npz", **best_payload)
    write_event_csv(
        args.output_dir / "best_margin_events.csv",
        margin_event_rows(
            base=base,
            state=best_state,
            eval_result=best_eval,
            threshold=args.event_threshold,
            limit=args.event_limit,
        ),
    )
    config_src = args.input_npz.parent / "train_config.json"
    if config_src.exists():
        shutil.copy2(config_src, args.output_dir / "train_config.json")
    write_json(
        args.output_dir / "summary.json",
        {
            "input_npz": str(args.input_npz),
            "candidate_count": len(rows_sorted),
            "best": best_row,
            "top10": rows_sorted[:10],
            "base": base_row,
            "saved_payloads": saved_payloads,
            "history_counts": {
                key: {
                    "attempted": len(value),
                    "accepted": int(sum(1 for item in value if item["accepted"])),
                }
                for key, value in histories.items()
            },
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "best": best_row, "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
