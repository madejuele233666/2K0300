import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Callable

import numpy as np


EventKey = tuple[str, int, str, int]


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


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def load_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def int_field(row: dict[str, str], key: str, default: int = 0) -> int:
    value = row.get(key, "")
    if value == "":
        return default
    return int(float(value))


def stress_margin(row: dict[str, str]) -> int:
    return int_field(row, "stress_margin", int_field(row, "primary_margin", 0))


def feature_dim(row: dict[str, str]) -> int:
    return max(1, int_field(row, "feature_dim", 1))


def wrong(row: dict[str, str], pred: int | None = None) -> bool:
    value = int(row["stress_pred"]) if pred is None else int(pred)
    return value != int(row["parent"])


def load_normal_margin_stats(items: list[tuple[str, Path]]) -> dict[str, dict[str, float]]:
    stats: dict[str, dict[str, float]] = {}
    for name, path in items:
        with np.load(path, allow_pickle=True) as data:
            if "int8_margin" not in data.files:
                raise ValueError(f"{path} is missing int8_margin")
            margins = np.asarray(data["int8_margin"], dtype=np.float64)
        stats[name] = {
            "median": float(np.median(margins)),
            "p90": float(np.percentile(margins, 90)),
            "mean": float(np.mean(margins)),
        }
    return stats


def summarize(
    *,
    base_rows: list[dict[str, str]],
    pred_by_key: dict[EventKey, int],
    chosen_by_key: dict[EventKey, str],
    source_order: list[str],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    wrong_events = 0
    for row in base_rows:
        key = event_key(row)
        is_wrong = wrong(row, pred_by_key[key])
        grouped[str(row["group"])][0] += int(is_wrong)
        grouped[str(row["group"])][1] += 1
        wrong_events += int(is_wrong)
    chosen_counts = Counter(chosen_by_key.values())
    rows = [
        {"source": name, "chosen": int(chosen_counts.get(name, 0))}
        for name in source_order
    ]
    summary = {
        "wrong_events": int(wrong_events),
        "total_events": int(len(base_rows)),
        "wrong_rate": float(wrong_events / max(len(base_rows), 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(
            grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)
        ),
    }
    return summary, rows


def select_by_score(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    score: Callable[[str, dict[str, str]], float],
) -> tuple[dict[EventKey, int], dict[EventKey, str]]:
    pred_by_key: dict[EventKey, int] = {}
    chosen_by_key: dict[EventKey, str] = {}
    for key in keys:
        chosen = max(
            source_order,
            key=lambda name: (score(name, sources[name][key]), -source_order.index(name)),
        )
        pred_by_key[key] = int(sources[chosen][key]["stress_pred"])
        chosen_by_key[key] = chosen
    return pred_by_key, chosen_by_key


def select_oracle_any(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
) -> tuple[dict[EventKey, int], dict[EventKey, str]]:
    pred_by_key: dict[EventKey, int] = {}
    chosen_by_key: dict[EventKey, str] = {}
    for key in keys:
        base = sources[source_order[0]][key]
        chosen = source_order[0]
        for name in source_order:
            if int(sources[name][key]["stress_pred"]) == int(base["parent"]):
                chosen = name
                break
        pred_by_key[key] = int(sources[chosen][key]["stress_pred"])
        chosen_by_key[key] = chosen
    return pred_by_key, chosen_by_key


def select_vote_or_score_sum(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    mode: str,
) -> tuple[dict[EventKey, int], dict[EventKey, str]]:
    pred_by_key: dict[EventKey, int] = {}
    chosen_by_key: dict[EventKey, str] = {}
    for key in keys:
        votes = [0, 0, 0]
        score = [0.0, 0.0, 0.0]
        support: list[list[str]] = [[], [], []]
        for source_name in source_order:
            row = sources[source_name][key]
            pred = int(row["stress_pred"])
            votes[pred] += 1
            support[pred].append(source_name)
            margin = float(max(stress_margin(row), 0))
            if mode == "majority_base_tie":
                score[pred] = float(votes[pred])
            elif mode == "majority_margin_tie":
                score[pred] = float(votes[pred])
            elif mode == "margin_sum":
                score[pred] += margin
            elif mode == "margin_sum_per_sqrt_dim":
                score[pred] += margin / math.sqrt(float(feature_dim(row)))
            elif mode == "margin_sum_per_dim":
                score[pred] += margin / float(feature_dim(row))
            elif mode == "log_margin_sum":
                score[pred] += math.log1p(margin)
            else:
                raise ValueError(f"unknown vote/score mode: {mode}")
        base_pred = int(sources[source_order[0]][key]["stress_pred"])
        margin_sum = [0.0, 0.0, 0.0]
        if mode == "majority_margin_tie":
            for source_name in source_order:
                row = sources[source_name][key]
                margin_sum[int(row["stress_pred"])] += float(max(stress_margin(row), 0))
        if mode == "majority_base_tie":
            pred = max(range(3), key=lambda parent: (votes[parent], parent == base_pred, -parent))
        elif mode == "majority_margin_tie":
            pred = max(range(3), key=lambda parent: (votes[parent], margin_sum[parent], -parent))
        else:
            pred = max(range(3), key=lambda parent: (score[parent], -parent))
        pred_by_key[key] = int(pred)
        chosen_by_key[key] = "+".join(support[pred]) if support[pred] else "none"
    return pred_by_key, chosen_by_key


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate label-free source-confidence gates and oracle upper bounds for V8 stress events."
    )
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", default=[], help="Optional name=params.npz for normal margin stats.")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_order = [name for name, _path in source_items]
    sources = {name: load_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in sources.values())))
    if not common_keys:
        raise ValueError("sources have no common events")
    base_name = source_order[0]
    base_rows = [sources[base_name][key] for key in common_keys]
    dropped = {name: int(len(rows) - len(common_keys)) for name, rows in sources.items()}
    normal_stats = load_normal_margin_stats(normal_items) if normal_items else {}

    policy_rows: list[dict[str, Any]] = []
    chosen_rows: list[dict[str, Any]] = []

    def add_policy(
        *,
        name: str,
        label_usage: str,
        pred_by_key: dict[EventKey, int],
        chosen_by_key: dict[EventKey, str],
    ) -> None:
        summary, counts = summarize(
            base_rows=base_rows,
            pred_by_key=pred_by_key,
            chosen_by_key=chosen_by_key,
            source_order=source_order,
        )
        policy_rows.append(
            {
                "policy": name,
                "selection_label_usage": label_usage,
                **summary,
            }
        )
        for row in counts:
            chosen_rows.append({"policy": name, **row})

    for name in source_order:
        pred = {key: int(sources[name][key]["stress_pred"]) for key in common_keys}
        chosen = {key: name for key in common_keys}
        add_policy(name=f"single:{name}", label_usage="none", pred_by_key=pred, chosen_by_key=chosen)

    pred, chosen = select_by_score(
        keys=common_keys,
        sources=sources,
        source_order=source_order,
        score=lambda _name, row: float(stress_margin(row)),
    )
    add_policy(name="max_stress_margin", label_usage="none", pred_by_key=pred, chosen_by_key=chosen)

    pred, chosen = select_by_score(
        keys=common_keys,
        sources=sources,
        source_order=source_order,
        score=lambda _name, row: float(stress_margin(row)) / math.sqrt(float(feature_dim(row))),
    )
    add_policy(name="max_stress_margin_per_sqrt_dim", label_usage="none", pred_by_key=pred, chosen_by_key=chosen)

    pred, chosen = select_by_score(
        keys=common_keys,
        sources=sources,
        source_order=source_order,
        score=lambda _name, row: float(stress_margin(row)) / float(feature_dim(row)),
    )
    add_policy(name="max_stress_margin_per_dim", label_usage="none", pred_by_key=pred, chosen_by_key=chosen)

    if normal_stats:
        for stat_name in ["median", "p90", "mean"]:
            missing = [name for name in source_order if name not in normal_stats]
            if missing:
                raise ValueError(f"missing normal stats for sources: {missing}")
            pred, chosen = select_by_score(
                keys=common_keys,
                sources=sources,
                source_order=source_order,
                score=lambda name, row, stat_name=stat_name: float(stress_margin(row))
                / max(float(normal_stats[name][stat_name]), 1.0),
            )
            add_policy(
                name=f"max_stress_margin_over_normal_{stat_name}",
                label_usage="none",
                pred_by_key=pred,
                chosen_by_key=chosen,
            )

    for policy_name in [
        "majority_base_tie",
        "majority_margin_tie",
        "margin_sum",
        "margin_sum_per_sqrt_dim",
        "margin_sum_per_dim",
        "log_margin_sum",
    ]:
        pred, chosen = select_vote_or_score_sum(
            keys=common_keys,
            sources=sources,
            source_order=source_order,
            mode=policy_name,
        )
        summary, counts = summarize(
            base_rows=base_rows,
            pred_by_key=pred,
            chosen_by_key=chosen,
            source_order=source_order,
        )
        policy_rows.append(
            {
                "policy": policy_name,
                "selection_label_usage": "none",
                **summary,
            }
        )
        for source, count in Counter(chosen.values()).most_common():
            chosen_rows.append({"policy": policy_name, "source": source, "chosen": int(count)})

    pred, chosen = select_oracle_any(keys=common_keys, sources=sources, source_order=source_order)
    add_policy(name="oracle_any", label_usage="true_parent", pred_by_key=pred, chosen_by_key=chosen)

    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "chosen_counts.csv", chosen_rows)
    write_json(
        args.output_dir / "summary.json",
        {
            "sources": {name: str(path) for name, path in source_items},
            "source_order": source_order,
            "normal_params": {name: str(path) for name, path in normal_items},
            "normal_margin_stats": normal_stats,
            "high_pressure_usage": "diagnostic_only",
            "common_events": int(len(common_keys)),
            "dropped_events": dropped,
            "policy_summary": policy_rows,
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "policy_summary": policy_rows}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
