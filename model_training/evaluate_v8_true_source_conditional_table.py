import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from estimate_v8_board_time import calibrated_conservative_us


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


def read_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def stress_margin(row: dict[str, str]) -> int:
    value = row.get("stress_margin") or row.get("primary_margin") or "0"
    return int(float(value))


def feature_dim(row: dict[str, str]) -> int:
    value = row.get("feature_dim") or "1"
    return max(1, int(float(value)))


def wrong(row: dict[str, str], pred: int | None = None) -> bool:
    value = int(row["stress_pred"]) if pred is None else int(pred)
    return value != int(row["parent"])


def load_normal_source(path: Path, config_path: Path | None) -> dict[str, Any]:
    with np.load(path, allow_pickle=True) as data:
        parent = np.asarray(data["parent"], dtype=np.int64)
        pred_key = "int8_pred" if "int8_pred" in data.files else "pred"
        pred = np.asarray(data[pred_key], dtype=np.int64)
        margin_key = "int8_margin" if "int8_margin" in data.files else "margin"
        margin = np.asarray(data[margin_key], dtype=np.float64)
        prototypes = np.asarray(data["prototypes_int8"], dtype=np.int8)
        view_labels = np.asarray(data["view_labels"]).astype(str) if "view_labels" in data.files else np.asarray([])
    wrong_count = int(np.sum(pred != parent))
    per_view_wrong: dict[str, int] = {}
    if len(view_labels) == len(parent):
        for view in sorted(set(view_labels.tolist())):
            mask = view_labels == view
            misses = int(np.sum(pred[mask] != parent[mask]))
            if misses:
                per_view_wrong[str(view)] = misses
    table_macs = int(prototypes.shape[0] * prototypes.shape[1])
    table_us = max(20.0, float(table_macs) * 0.02)
    backbone_us = None
    if config_path is not None:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        backbone_us = float(calibrated_conservative_us(config))
    total_us = None if backbone_us is None else float(backbone_us + table_us)
    return {
        "params_npz": str(path),
        "config": "" if config_path is None else str(config_path),
        "normal_rows": int(len(parent)),
        "normal_wrong": wrong_count,
        "normal_all_correct": bool(wrong_count == 0),
        "normal_per_view_wrong": per_view_wrong,
        "feature_dim": int(prototypes.shape[1]),
        "prototype_count": int(prototypes.shape[0]),
        "estimated_distance_macs": table_macs,
        "int8_margin_min": int(np.min(margin)) if len(margin) else None,
        "int8_margin_mean": float(np.mean(margin)) if len(margin) else None,
        "board_backbone_conservative_us": None if backbone_us is None else int(round(backbone_us)),
        "board_table_worst_us": int(round(table_us)),
        "board_total_conservative_us": None if total_us is None else int(round(total_us)),
        "under_2ms_conservative": None if total_us is None else bool(total_us <= 2000.0),
        "under_8ms_conservative": None if total_us is None else bool(total_us <= 8000.0),
    }


def summarize_policy(
    *,
    base_rows: list[dict[str, str]],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    selected_by_key: dict[EventKey, str],
    pred_by_key: dict[EventKey, int] | None = None,
    source_order: list[str],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    wrong_events = 0
    out_rows: list[dict[str, Any]] = []
    for row in base_rows:
        key = event_key(row)
        source = selected_by_key[key]
        pred = int(pred_by_key[key]) if pred_by_key is not None else int(sources[source][key]["stress_pred"])
        is_wrong = pred != int(row["parent"])
        grouped[str(row["group"])][0] += int(is_wrong)
        grouped[str(row["group"])][1] += 1
        wrong_events += int(is_wrong)
        selected_row = sources[source][key] if source in sources else None
        out_rows.append(
            {
                "group": row["group"],
                "base_query_index": int(row["base_query_index"]),
                "sample_index": int(row["sample_index"]),
                "view_label": row["view_label"],
                "perturb": row["perturb"],
                "perturb_family": row["perturb_family"],
                "event_index": int(row["event_index"]),
                "parent": int(row["parent"]),
                "selected_source": source,
                "pred": pred,
                "wrong": bool(is_wrong),
                "selected_source_margin": "" if selected_row is None else stress_margin(selected_row),
            }
        )
    counts = Counter(row["selected_source"] for row in out_rows)
    summary = {
        "wrong_events": int(wrong_events),
        "total_events": int(len(base_rows)),
        "wrong_rate": float(wrong_events / max(len(base_rows), 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(
            grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)
        ),
        "chosen_counts": {name: int(count) for name, count in sorted(counts.items())},
    }
    return summary, out_rows


def select_by_score(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    score_mode: str,
) -> dict[EventKey, str]:
    selected: dict[EventKey, str] = {}
    for key in keys:
        def score(name: str) -> float:
            row = sources[name][key]
            margin = float(stress_margin(row))
            if score_mode == "max_stress_margin":
                return margin
            if score_mode == "max_stress_margin_per_sqrt_dim":
                return margin / math.sqrt(float(feature_dim(row)))
            if score_mode == "max_stress_margin_per_dim":
                return margin / float(feature_dim(row))
            raise ValueError(f"unknown score mode: {score_mode}")

        selected[key] = max(source_order, key=lambda name: (score(name), -source_order.index(name)))
    return selected


def select_oracle_any(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
) -> dict[EventKey, str]:
    selected: dict[EventKey, str] = {}
    for key in keys:
        parent = int(sources[source_order[0]][key]["parent"])
        correct_sources = [name for name in source_order if int(sources[name][key]["stress_pred"]) == parent]
        if correct_sources:
            selected[key] = max(correct_sources, key=lambda name: (stress_margin(sources[name][key]), -source_order.index(name)))
        else:
            selected[key] = max(source_order, key=lambda name: (stress_margin(sources[name][key]), -source_order.index(name)))
    return selected


def select_margin_sum_pred(
    *,
    keys: list[EventKey],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_order: list[str],
    mode: str,
) -> tuple[dict[EventKey, int], dict[EventKey, str]]:
    pred_by_key: dict[EventKey, int] = {}
    selected_by_key: dict[EventKey, str] = {}
    for key in keys:
        scores = [0.0, 0.0, 0.0]
        support: list[list[str]] = [[], [], []]
        for name in source_order:
            row = sources[name][key]
            pred = int(row["stress_pred"])
            margin = float(max(stress_margin(row), 0))
            if mode == "margin_sum":
                value = margin
            elif mode == "log_margin_sum":
                value = math.log1p(margin)
            else:
                raise ValueError(f"unknown margin-sum mode: {mode}")
            scores[pred] += value
            support[pred].append(name)
        pred = max(range(3), key=lambda cls: (scores[cls], -cls))
        pred_by_key[key] = int(pred)
        selected_by_key[key] = "+".join(support[pred]) if support[pred] else "none"
    return pred_by_key, selected_by_key


def load_selection_npz(
    *,
    text: str,
    keys: list[EventKey],
    source_order: list[str],
) -> tuple[str, dict[EventKey, str], dict[str, Any]]:
    policy, raw_path = parse_named_path(text)
    with np.load(raw_path, allow_pickle=True) as data:
        if "selected_source_index" not in data.files:
            raise ValueError(f"{raw_path} missing selected_source_index")
        selected = np.asarray(data["selected_source_index"], dtype=np.int64)
        artifact_sources = np.asarray(data["source_order"]).astype(str).tolist() if "source_order" in data.files else source_order
        runtime_sources = (
            np.asarray(data["runtime_source_order"]).astype(str).tolist()
            if "runtime_source_order" in data.files
            else artifact_sources
        )
        feature_mode = str(np.asarray(data["feature_mode"]).item()) if "feature_mode" in data.files else ""
        hidden_dim = int(np.asarray(data["hidden_dim"]).item()) if "hidden_dim" in data.files else -1
    if len(selected) != len(keys):
        raise ValueError(f"{raw_path} selection length {len(selected)} does not match common events {len(keys)}")
    if artifact_sources != source_order:
        raise ValueError(f"{raw_path} source_order {artifact_sources} != expected {source_order}")
    selected_by_key = {key: source_order[int(index)] for key, index in zip(keys, selected.tolist(), strict=False)}
    return (
        policy,
        selected_by_key,
        {
            "selection_npz": str(raw_path),
            "feature_mode": feature_mode,
            "hidden_dim": hidden_dim,
            "runtime_source_order": runtime_sources,
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate true source-specific conditional-table boundaries without folding source geometry."
    )
    parser.add_argument("--source-stress", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--source-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--source-config", action="append", default=[], help="Optional name=train_config.json")
    parser.add_argument("--selection-npz", action="append", default=[], help="Optional policy=gate_scaler_and_selection.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    stress_items = [parse_named_path(item) for item in args.source_stress]
    param_items = [parse_named_path(item) for item in args.source_params]
    config_items = dict(parse_named_path(item) for item in args.source_config)
    source_order = [name for name, _path in stress_items]
    if [name for name, _path in param_items] != source_order:
        raise ValueError("--source-stress and --source-params names/order must match")

    sources = {name: read_events(path) for name, path in stress_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in sources.values())))
    if not common_keys:
        raise ValueError("sources have no common stress events")
    base_rows = [sources[source_order[0]][key] for key in common_keys]
    dropped = {name: int(len(rows) - len(common_keys)) for name, rows in sources.items()}

    normal_sources = {
        name: load_normal_source(path, config_items.get(name))
        for name, path in param_items
    }
    source_totals = [
        row["board_total_conservative_us"]
        for row in normal_sources.values()
        if row["board_total_conservative_us"] is not None
    ]
    selected_source_worst_us = int(max(source_totals)) if source_totals else None
    all_source_us = int(sum(source_totals)) if source_totals else None
    normal_replay_guaranteed = all(row["normal_all_correct"] for row in normal_sources.values())

    policy_rows: list[dict[str, Any]] = []
    all_event_rows: list[dict[str, Any]] = []

    def add_policy(
        *,
        policy: str,
        label_usage: str,
        runtime_feature_usage: str,
        selected_by_key: dict[EventKey, str],
        pred_by_key: dict[EventKey, int] | None = None,
        extra: dict[str, Any] | None = None,
    ) -> None:
        summary, event_rows = summarize_policy(
            base_rows=base_rows,
            sources=sources,
            selected_by_key=selected_by_key,
            pred_by_key=pred_by_key,
            source_order=source_order,
        )
        route_us = selected_source_worst_us if runtime_feature_usage == "selected_source_only" else all_source_us
        row = {
            "policy": policy,
            "selection_label_usage": label_usage,
            "runtime_feature_usage": runtime_feature_usage,
            "normal_replay_guaranteed_by_sources": bool(normal_replay_guaranteed),
            "selected_source_worst_us": selected_source_worst_us,
            "all_source_outputs_us": all_source_us,
            "runtime_accounted_us": route_us,
            "under_2ms_if_selector_free": None if selected_source_worst_us is None else bool(selected_source_worst_us <= 2000),
            "under_8ms_if_all_sources_run": None if all_source_us is None else bool(all_source_us <= 8000),
            **summary,
            "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
        }
        if extra:
            row.update(extra)
        policy_rows.append(row)
        for event_row in event_rows:
            all_event_rows.append({"policy": policy, **event_row})

    for name in source_order:
        add_policy(
            policy=f"single:{name}",
            label_usage="none",
            runtime_feature_usage="selected_source_only",
            selected_by_key={key: name for key in common_keys},
        )

    for mode in ["max_stress_margin", "max_stress_margin_per_sqrt_dim", "max_stress_margin_per_dim"]:
        add_policy(
            policy=mode,
            label_usage="none_uses_all_source_outputs",
            runtime_feature_usage="all_source_outputs_diagnostic",
            selected_by_key=select_by_score(keys=common_keys, sources=sources, source_order=source_order, score_mode=mode),
        )

    for mode in ["margin_sum", "log_margin_sum"]:
        pred_by_key, selected_by_key = select_margin_sum_pred(
            keys=common_keys,
            sources=sources,
            source_order=source_order,
            mode=mode,
        )
        add_policy(
            policy=mode,
            label_usage="none_uses_all_source_outputs",
            runtime_feature_usage="all_source_outputs_diagnostic",
            selected_by_key=selected_by_key,
            pred_by_key=pred_by_key,
        )

    add_policy(
        policy="oracle_any_best_margin",
        label_usage="true_parent_evaluation_only",
        runtime_feature_usage="oracle_diagnostic",
        selected_by_key=select_oracle_any(keys=common_keys, sources=sources, source_order=source_order),
    )

    for item in args.selection_npz:
        policy, selected_by_key, info = load_selection_npz(text=item, keys=common_keys, source_order=source_order)
        runtime_sources = info.get("runtime_source_order") or source_order
        runtime_usage = "selected_source_only" if runtime_sources == [source_order[0]] else "all_source_outputs_diagnostic"
        add_policy(
            policy=policy,
            label_usage="external_selection_npz",
            runtime_feature_usage=runtime_usage,
            selected_by_key=selected_by_key,
            extra={
                "selection_npz": info["selection_npz"],
                "selection_feature_mode": info["feature_mode"],
                "selection_hidden_dim": info["hidden_dim"],
                "selection_runtime_source_order_json": json.dumps(runtime_sources, ensure_ascii=False),
            },
        )

    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "selected_events.csv", all_event_rows)
    write_json(
        args.output_dir / "summary.json",
        {
            "high_pressure_usage": "evaluation_only",
            "selection_training_usage": "none_in_this_evaluator",
            "source_order": source_order,
            "source_stress": {name: str(path) for name, path in stress_items},
            "normal_sources": normal_sources,
            "normal_replay_guaranteed_by_sources": bool(normal_replay_guaranteed),
            "common_events": int(len(common_keys)),
            "dropped_events": dropped,
            "selected_source_worst_us": selected_source_worst_us,
            "all_source_outputs_us": all_source_us,
            "policy_summary": policy_rows,
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "top_policies": policy_rows[:8]}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
