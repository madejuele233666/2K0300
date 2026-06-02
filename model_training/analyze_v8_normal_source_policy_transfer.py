import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np


EventKey = tuple[str, int, str, int]
NormalKey = tuple[int, str]


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


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def parse_list(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def read_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def view_family(view: str) -> str:
    if view == "clean":
        return "clean"
    if view.startswith("rot") or view.startswith("mirror"):
        return "d4"
    if "noise" in view and "blur" in view:
        return "blur_noise"
    if "blur" in view:
        return "blur"
    if "noise" in view:
        return "noise"
    if "bright" in view:
        return "brightness"
    if "contrast" in view:
        return "contrast"
    if "shift" in view:
        return "shift"
    return "other"


def transformed_margin(margin: np.ndarray, dim: int, mode: str) -> np.ndarray:
    values = np.maximum(margin.astype(np.float64), 0.0)
    if mode == "raw":
        return values
    if mode == "per_sqrt_dim":
        return values / math.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return values / float(max(dim, 1))
    if mode == "log":
        return np.log1p(values)
    raise ValueError(f"unknown score mode: {mode}")


def aggregate_choice(scores: np.ndarray, mode: str) -> int:
    if mode == "min":
        aggregate = np.min(scores, axis=0)
    elif mode == "correct_count":
        correct = scores >= 0.0
        aggregate = np.sum(correct, axis=0).astype(np.float64) * 1.0e6
        aggregate += np.mean(np.maximum(scores, 0.0), axis=0)
    else:
        raise ValueError(f"unknown aggregate mode: {mode}")
    return int(np.argmax(aggregate))


def choose_with_fallback(
    *,
    score_matrix: np.ndarray,
    primary_indexes: list[int],
    fallback_indexes: list[int],
    fallback_label: int,
    aggregate_mode: str,
) -> int:
    if primary_indexes:
        return aggregate_choice(score_matrix[np.asarray(primary_indexes, dtype=np.int64)], aggregate_mode)
    if fallback_indexes:
        return aggregate_choice(score_matrix[np.asarray(fallback_indexes, dtype=np.int64)], aggregate_mode)
    return int(fallback_label)


def build_score_matrix(
    *,
    normal_payloads: dict[str, dict[str, np.ndarray]],
    source_order: list[str],
    normal_keys: list[NormalKey],
    score_mode: str,
) -> tuple[np.ndarray, np.ndarray]:
    score_columns: list[np.ndarray] = []
    parent_reference: np.ndarray | None = None
    for source in source_order:
        payload = normal_payloads[source]
        key_map = normal_key_map(payload)
        indexes = np.asarray([key_map[key] for key in normal_keys], dtype=np.int64)
        parent = np.asarray(payload["parent"], dtype=np.int64)[indexes]
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)[indexes]
        margin = np.asarray(payload["int8_margin"], dtype=np.int64)[indexes]
        code_dim = int(np.asarray(payload["embedding_int8"]).shape[1])
        score = transformed_margin(margin, code_dim, score_mode)
        label_score = np.where(pred == parent, score, -1.0 - np.abs(score)).astype(np.float64)
        score_columns.append(label_score)
        if parent_reference is None:
            parent_reference = parent
        elif not np.array_equal(parent_reference, parent):
            raise ValueError(f"parent mismatch for source {source}")
    if parent_reference is None:
        raise ValueError("no normal payloads")
    return np.stack(score_columns, axis=1), parent_reference


def build_policy_maps(
    *,
    score_matrix: np.ndarray,
    normal_keys: list[NormalKey],
    parent: np.ndarray,
    label_mode: str,
) -> tuple[dict[tuple[Any, ...], int], str]:
    samples = np.asarray([key[0] for key in normal_keys], dtype=np.int64)
    views = np.asarray([key[1] for key in normal_keys]).astype(str)
    row_labels = np.argmax(score_matrix, axis=1).astype(np.int64)
    if label_mode == "row_winner":
        return {
            (int(sample), str(view)): int(label)
            for sample, view, label in zip(samples.tolist(), views.tolist(), row_labels.tolist(), strict=False)
        }, "sample_view"

    if label_mode.startswith("row_peer_") or label_mode.startswith("row_other_family_"):
        if label_mode.startswith("row_peer_"):
            aggregate_mode = label_mode.removeprefix("row_peer_")
            selector = "peer"
        else:
            aggregate_mode = label_mode.removeprefix("row_other_family_")
            selector = "other_family"

        grouped: dict[tuple[int, int], list[int]] = defaultdict(list)
        for row_index, (sample, parent_value) in enumerate(zip(samples.tolist(), parent.tolist(), strict=False)):
            grouped[(int(sample), int(parent_value))].append(row_index)

        policy: dict[tuple[Any, ...], int] = {}
        for row_index, (sample, parent_value, view) in enumerate(
            zip(samples.tolist(), parent.tolist(), views.tolist(), strict=False)
        ):
            group_key = (int(sample), int(parent_value))
            candidates = [index for index in grouped[group_key] if index != row_index]
            if selector == "other_family":
                family = view_family(str(view))
                candidates = [index for index in candidates if view_family(str(views[index])) != family]
            fallback = [index for index in grouped[group_key] if index != row_index]
            policy[(int(sample), str(view))] = choose_with_fallback(
                score_matrix=score_matrix,
                primary_indexes=candidates,
                fallback_indexes=fallback,
                fallback_label=int(row_labels[row_index]),
                aggregate_mode=aggregate_mode,
            )
        return policy, "sample_view"

    if label_mode.startswith("sample_family_other_"):
        aggregate_mode = label_mode.removeprefix("sample_family_other_")
        grouped: dict[tuple[int, int], list[int]] = defaultdict(list)
        families_by_sample: dict[tuple[int, int], set[str]] = defaultdict(set)
        for row_index, (sample, parent_value, view) in enumerate(
            zip(samples.tolist(), parent.tolist(), views.tolist(), strict=False)
        ):
            sample_key = (int(sample), int(parent_value))
            grouped[sample_key].append(row_index)
            families_by_sample[sample_key].add(view_family(str(view)))

        policy = {}
        for sample_key, indexes in grouped.items():
            for family in sorted(families_by_sample[sample_key]):
                primary = [index for index in indexes if view_family(str(views[index])) != family]
                fallback = list(indexes)
                policy[(*sample_key, family)] = choose_with_fallback(
                    score_matrix=score_matrix,
                    primary_indexes=primary,
                    fallback_indexes=fallback,
                    fallback_label=int(row_labels[indexes[0]]),
                    aggregate_mode=aggregate_mode,
                )
        return policy, "sample_parent_view_family"

    if label_mode.startswith("sample_family_"):
        group_keys = [
            (int(sample), int(parent_value), view_family(str(view)))
            for sample, parent_value, view in zip(samples.tolist(), parent.tolist(), views.tolist(), strict=False)
        ]
        aggregate_mode = label_mode.removeprefix("sample_family_")
        lookup_kind = "sample_parent_view_family"
    elif label_mode.startswith("sample_"):
        group_keys = [
            (int(sample), int(parent_value))
            for sample, parent_value in zip(samples.tolist(), parent.tolist(), strict=False)
        ]
        aggregate_mode = label_mode.removeprefix("sample_")
        lookup_kind = "sample_parent"
    else:
        raise ValueError(f"unknown label mode: {label_mode}")

    grouped: dict[tuple[Any, ...], list[int]] = defaultdict(list)
    for row_index, key in enumerate(group_keys):
        grouped[key].append(row_index)

    policy: dict[tuple[Any, ...], int] = {}
    for key, indexes in grouped.items():
        policy[key] = aggregate_choice(score_matrix[np.asarray(indexes, dtype=np.int64)], aggregate_mode)
    return policy, lookup_kind


def select_for_row(
    *,
    row: dict[str, str],
    policy: dict[tuple[Any, ...], int],
    lookup_kind: str,
    fallback_index: int,
) -> tuple[int, bool]:
    sample = int(row["sample_index"])
    parent = int(row["parent"])
    view = str(row["view_label"])
    if lookup_kind == "sample_view":
        key = (sample, view)
    elif lookup_kind == "sample_parent":
        key = (sample, parent)
    elif lookup_kind == "sample_parent_view_family":
        key = (sample, parent, view_family(view))
    else:
        raise ValueError(f"unknown lookup kind: {lookup_kind}")
    if key not in policy:
        return fallback_index, True
    return int(policy[key]), False


def summarize_policy(
    *,
    policy: dict[tuple[Any, ...], int],
    lookup_kind: str,
    fallback_index: int,
    base_rows: list[dict[str, str]],
    common_keys: list[EventKey],
    source_events: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    fallback_count = 0
    for row_index, row in enumerate(base_rows):
        selected, used_fallback = select_for_row(
            row=row,
            policy=policy,
            lookup_kind=lookup_kind,
            fallback_index=fallback_index,
        )
        fallback_count += int(used_fallback)
        source = source_order[selected]
        chosen[source] += 1
        pred = int(source_events[source][common_keys[row_index]]["stress_pred"])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
    wrong_events = int(sum(value[0] for value in grouped.values()))
    total_events = int(sum(value[1] for value in grouped.values()))
    return {
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": float(wrong_events / max(total_events, 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
        "fallback_count": int(fallback_count),
        "chosen_counts": dict(chosen),
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Apply normal-only source policies by sample/view keys to high-pressure events."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--score-modes", default="raw,log,per_sqrt_dim,per_dim")
    parser.add_argument(
        "--label-modes",
        default=(
            "row_winner,sample_min,sample_correct_count,sample_family_min,sample_family_correct_count,"
            "row_peer_min,row_peer_correct_count,row_other_family_min,row_other_family_correct_count,"
            "sample_family_other_min,sample_family_other_correct_count"
        ),
    )
    parser.add_argument("--fallback-source", default="")
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    normal_order = [name for name, _path in normal_items]
    if source_order != normal_order:
        raise ValueError(f"source order mismatch: {source_order} != {normal_order}")

    fallback_source = args.fallback_source or source_order[0]
    if fallback_source not in source_order:
        raise ValueError(f"fallback source {fallback_source} not in source order")
    fallback_index = source_order.index(fallback_source)

    source_events = {name: read_events(path) for name, path in source_items}
    common_event_keys = sorted(set.intersection(*(set(rows) for rows in source_events.values())))
    if not common_event_keys:
        raise ValueError("source event files have no common high-pressure events")
    base_rows = [source_events[source_order[0]][key] for key in common_event_keys]

    normal_payloads = {name: load_npz(path) for name, path in normal_items}
    normal_maps = {name: normal_key_map(payload) for name, payload in normal_payloads.items()}
    normal_keys = sorted(set.intersection(*(set(mapping) for mapping in normal_maps.values())))
    if not normal_keys:
        raise ValueError("normal params have no common sample/view rows")

    rows: list[dict[str, Any]] = []
    for score_mode in parse_list(args.score_modes):
        score_matrix, parent = build_score_matrix(
            normal_payloads=normal_payloads,
            source_order=source_order,
            normal_keys=normal_keys,
            score_mode=score_mode,
        )
        for label_mode in parse_list(args.label_modes):
            policy, lookup_kind = build_policy_maps(
                score_matrix=score_matrix,
                normal_keys=normal_keys,
                parent=parent,
                label_mode=label_mode,
            )
            summary = summarize_policy(
                policy=policy,
                lookup_kind=lookup_kind,
                fallback_index=fallback_index,
                base_rows=base_rows,
                common_keys=common_event_keys,
                source_events=source_events,
                source_order=source_order,
            )
            rows.append(
                {
                    "policy": f"normal_policy_transfer:{label_mode}:{score_mode}",
                    "label_mode": label_mode,
                    "score_mode": score_mode,
                    "lookup_kind": lookup_kind,
                    "policy_key_count": int(len(policy)),
                    "selection_label_usage": "none",
                    "normal_training_usage": "normal params only",
                    "high_pressure_usage": "evaluation_only",
                    "runtime_feature_usage": "sample/view key diagnostic_not_deployable",
                    **{key: value for key, value in summary.items() if key != "chosen_counts"},
                    "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
                }
            )

    rows = sorted(
        rows,
        key=lambda row: (
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "policy_summary.csv", rows)
    summary_json = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "source_order": source_order,
        "fallback_source": fallback_source,
        "common_normal_rows": int(len(normal_keys)),
        "common_high_pressure_events": int(len(common_event_keys)),
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "normal params only",
        "runtime_feature_usage": "sample/view key diagnostic_not_deployable",
        "label_modes": parse_list(args.label_modes),
        "score_modes": parse_list(args.score_modes),
        "top_policies": rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary_json)
    print(json.dumps(summary_json, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
