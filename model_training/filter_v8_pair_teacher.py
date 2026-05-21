import argparse
import csv
import json
from collections import defaultdict
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


def family_key(payload: dict[str, np.ndarray], index: int, keys: list[str]) -> tuple[Any, ...]:
    values: list[Any] = []
    for key in keys:
        if key == "parent":
            values.append(int(np.asarray(payload["parent"], dtype=np.int64)[index]))
        elif key == "teacher_wrong_parent":
            values.append(int(np.asarray(payload["teacher_wrong_parent"], dtype=np.int64)[index]))
        elif key == "view_family":
            values.append(view_family(str(np.asarray(payload["view_labels"]).astype(str)[index])))
        elif key == "view_label":
            values.append(str(np.asarray(payload["view_labels"]).astype(str)[index]))
        else:
            raise ValueError(f"unknown family key: {key}")
    return tuple(values)


def order_indexes(payload: dict[str, np.ndarray], indexes: list[int]) -> list[int]:
    student_margin = np.asarray(payload["student_int8_margin"], dtype=np.int64)
    teacher_vote_count = np.asarray(payload["teacher_vote_count"], dtype=np.int64)
    teacher_margin_mean = np.asarray(payload["teacher_margin_mean"], dtype=np.float32)
    weight = np.asarray(payload["weight"], dtype=np.float32)
    return sorted(
        indexes,
        key=lambda index: (
            int(student_margin[index]),
            -int(teacher_vote_count[index]),
            -float(teacher_margin_mean[index]),
            -float(weight[index]),
            int(index),
        ),
    )


def select_indexes(
    payload: dict[str, np.ndarray],
    *,
    family_keys: list[str],
    per_family_cap: int,
    max_events: int,
    allowed_keys: set[tuple[int, str]] | None = None,
) -> tuple[list[int], dict[str, Any]]:
    groups: dict[tuple[Any, ...], list[int]] = defaultdict(list)
    skipped_missing_allowed = 0
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    count = len(np.asarray(payload["query_index"]))
    for index in range(count):
        if allowed_keys is not None and (int(sample_index[index]), str(view_labels[index])) not in allowed_keys:
            skipped_missing_allowed += 1
            continue
        groups[family_key(payload, index, family_keys)].append(index)
    ordered_groups = {key: order_indexes(payload, indexes) for key, indexes in groups.items()}
    selected: list[int] = []
    round_index = 0
    while True:
        added = False
        for key in sorted(ordered_groups, key=lambda item: tuple(str(part) for part in item)):
            if per_family_cap > 0 and round_index >= per_family_cap:
                continue
            indexes = ordered_groups[key]
            if round_index >= len(indexes):
                continue
            selected.append(int(indexes[round_index]))
            added = True
            if max_events > 0 and len(selected) >= max_events:
                break
        if (max_events > 0 and len(selected) >= max_events) or not added:
            break
        round_index += 1
    summary_groups = [
        {
            "family": "|".join(str(part) for part in key),
            "source_events": int(len(indexes)),
            "selected_events": int(sum(1 for index in selected if family_key(payload, index, family_keys) == key)),
        }
        for key, indexes in sorted(ordered_groups.items(), key=lambda item: tuple(str(part) for part in item[0]))
    ]
    return selected, {
        "family_keys": family_keys,
        "source_event_count": int(count),
        "family_count": int(len(groups)),
        "per_family_cap": int(per_family_cap),
        "max_events": int(max_events),
        "allowed_key_filter_active": bool(allowed_keys is not None),
        "skipped_missing_allowed": int(skipped_missing_allowed),
        "selected_event_count": int(len(selected)),
        "families": summary_groups,
    }


def load_allowed_keys(path: Path | None) -> set[tuple[int, str]] | None:
    if path is None:
        return None
    payload = load_npz(path)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample), str(view))
        for sample, view in zip(sample_index.tolist(), view_labels.tolist(), strict=False)
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Filter a V8 pair teacher by round-robin conflict-family balance.")
    parser.add_argument("--input-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--family-keys", default="parent,teacher_wrong_parent,view_family")
    parser.add_argument("--per-family-cap", type=int, default=32)
    parser.add_argument("--max-events", type=int, default=0)
    parser.add_argument("--allowed-params-npz", type=Path, default=None, help="Optional params npz whose sample/view rows are allowed.")
    args = parser.parse_args()

    payload = load_npz(args.input_npz)
    family_keys = [item.strip() for item in args.family_keys.split(",") if item.strip()]
    allowed_keys = load_allowed_keys(args.allowed_params_npz)
    selected, summary = select_indexes(
        payload,
        family_keys=family_keys,
        per_family_cap=args.per_family_cap,
        max_events=args.max_events,
        allowed_keys=allowed_keys,
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    selected_array = np.asarray(selected, dtype=np.int64)
    np.savez_compressed(
        args.output_dir / "pair_margin_teacher.npz",
        **{
            key: (value[selected_array] if getattr(value, "shape", ()) and len(value) == len(payload["query_index"]) else value)
            for key, value in payload.items()
        },
        source_pair_teacher_npz=np.asarray(str(args.input_npz)),
        pair_filter=np.asarray("round_robin_conflict_family"),
        pair_filter_family_keys=np.asarray(family_keys),
        pair_filter_per_family_cap=np.asarray(int(args.per_family_cap), dtype=np.int64),
        pair_filter_max_events=np.asarray(int(args.max_events), dtype=np.int64),
    )
    query_index = np.asarray(payload["query_index"], dtype=np.int64)
    parent = np.asarray(payload["parent"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    wrong_parent = np.asarray(payload["teacher_wrong_parent"], dtype=np.int64)
    margin = np.asarray(payload["student_int8_margin"], dtype=np.int64)
    vote_count = np.asarray(payload["teacher_vote_count"], dtype=np.int64)
    teacher_margin_mean = np.asarray(payload["teacher_margin_mean"], dtype=np.float32)
    weight = np.asarray(payload["weight"], dtype=np.float32)
    rows = []
    for index in selected:
        rows.append(
            {
                "source_index": int(index),
                "query_index": int(query_index[index]),
                "parent": int(parent[index]),
                "view_label": str(view_labels[index]),
                "view_family": view_family(str(view_labels[index])),
                "teacher_wrong_parent": int(wrong_parent[index]),
                "student_int8_margin": int(margin[index]),
                "teacher_vote_count": int(vote_count[index]),
                "teacher_margin_mean": float(teacher_margin_mean[index]),
                "weight": float(weight[index]),
            }
        )
    write_csv(args.output_dir / "selected_pair_events.csv", rows)
    write_json(
        args.output_dir / "summary.json",
        {
            **summary,
            "input_npz": str(args.input_npz),
            "allowed_params_npz": str(args.allowed_params_npz) if args.allowed_params_npz is not None else "",
            "high_pressure_usage": "none",
        },
    )
    print(json.dumps({**summary, "output_dir": str(args.output_dir)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
