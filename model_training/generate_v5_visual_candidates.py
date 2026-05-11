import argparse
import csv
import json
import math
from dataclasses import replace
from pathlib import Path

import numpy as np

from train_tiny32_v5_visual_subclass_scan import (
    ALL_AUGMENTS,
    V5Config,
    anchor_configs,
    config_from_dict,
    config_to_dict,
    safe_name,
    semantic_key,
)


def load_jsonl(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for raw in path.read_bytes().splitlines():
        line = raw.replace(b"\x00", b"").strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line.decode("utf-8")))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
    return rows


def result_files(inputs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in inputs:
        if path.is_dir():
            files.extend(sorted(path.rglob("trial_results.jsonl")))
        elif path.name == "search_summary.json":
            files.append(path)
        elif path.suffix in {".jsonl", ".json"}:
            files.append(path)
    return files


def load_results(inputs: list[Path]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in result_files(inputs):
        if path.name.endswith(".jsonl"):
            rows.extend(load_jsonl(path))
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if "top_results" in data:
            rows.extend(data["top_results"])
        elif "summary" in data:
            rows.extend(data["summary"])
        elif "candidates" in data:
            for item in data["candidates"]:
                rows.append({"trial": item.get("label") or item.get("config", {}).get("name"), "config": item["config"]})
        elif isinstance(data, list):
            rows.extend(data)
    return dedup_rows(rows)


def dedup_rows(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    deduped: dict[tuple[object, ...], dict[str, object]] = {}
    for row in rows:
        config = row.get("config")
        if isinstance(config, dict):
            key = (
                row.get("trial") or config.get("name"),
                config.get("lane"),
                json.dumps(config, sort_keys=True, ensure_ascii=False),
            )
        else:
            key = (row.get("trial"), row.get("lane"), row.get("score_mean"), row.get("score_min"))
        old = deduped.get(key)
        if old is None or rank_key(row) > rank_key(old):
            deduped[key] = row
    return list(deduped.values())


def metric(row: dict[str, object], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    if value in ("", None):
        return default
    return float(value)


def rank_key(row: dict[str, object]) -> tuple[float, float, float, float, float, float]:
    return (
        metric(row, "score_min", metric(row, "score_mean", 0.0)),
        metric(row, "score_mean", 0.0),
        metric(row, "hard_parent_worst_min", 0.0),
        metric(row, "stress_parent_worst_min", 0.0),
        metric(row, "clean_parent_worst_min", 0.0),
        -metric(row, "estimated_board_us", 999999.0),
    )


def axis_keys(row: dict[str, object]) -> dict[str, tuple[float, ...]]:
    score_min = metric(row, "score_min", metric(row, "score_mean", 0.0))
    score_mean = metric(row, "score_mean", 0.0)
    board_us = metric(row, "estimated_board_us", 999999.0)
    int8_bytes = metric(row, "int8_bytes_mean", 999999.0)
    return {
        "hard_acc": (
            metric(row, "hard_parent_accuracy_min", metric(row, "hard_parent_accuracy_mean", 0.0)),
            metric(row, "hard_parent_accuracy_mean", 0.0),
            score_min,
            score_mean,
            -board_us,
        ),
        "hard_worst": (
            metric(row, "hard_parent_worst_min", 0.0),
            metric(row, "hard_parent_accuracy_mean", 0.0),
            score_min,
            -board_us,
        ),
        "stress_worst": (
            metric(row, "stress_parent_worst_min", 0.0),
            metric(row, "stress_parent_macro_mean", 0.0),
            score_min,
            -board_us,
        ),
        "clean_acc": (
            metric(row, "clean_parent_accuracy_min", metric(row, "clean_parent_accuracy_mean", 0.0)),
            metric(row, "clean_parent_accuracy_mean", 0.0),
            metric(row, "clean_parent_worst_min", 0.0),
            score_min,
            -board_us,
        ),
        "clean_worst": (
            metric(row, "clean_parent_worst_min", 0.0),
            metric(row, "clean_parent_accuracy_mean", 0.0),
            score_min,
            -board_us,
        ),
        "speed": (
            score_min / max(board_us, 1.0),
            score_min,
            score_mean,
            -board_us,
        ),
        "size": (
            score_min / max(int8_bytes, 1.0),
            score_min,
            score_mean,
            -int8_bytes,
        ),
    }


def source_for(row: dict[str, object]) -> dict[str, object]:
    return {
        "trial": row.get("trial", ""),
        "lane": row.get("lane", row.get("config", {}).get("lane", "")),
        "score_mean": row.get("score_mean", ""),
        "score_min": row.get("score_min", ""),
        "clean_parent_accuracy_mean": row.get("clean_parent_accuracy_mean", ""),
        "clean_parent_worst_min": row.get("clean_parent_worst_min", ""),
        "hard_parent_accuracy_mean": row.get("hard_parent_accuracy_mean", ""),
        "hard_parent_worst_min": row.get("hard_parent_worst_min", ""),
        "stress_parent_worst_min": row.get("stress_parent_worst_min", ""),
        "estimated_board_us": row.get("estimated_board_us", ""),
    }


def add_candidate(
    out: list[dict[str, object]],
    seen: set[tuple[object, ...]],
    config: V5Config,
    source: dict[str, object],
    label: str | None = None,
) -> None:
    key = semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    if label is not None:
        config = replace(config, name=label)
    out.append({"label": config.name, "source": source, "config": config_to_dict(config)})


def command_select(args: argparse.Namespace) -> None:
    rows = [row for row in load_results(args.input) if isinstance(row.get("config"), dict)]
    rows.sort(key=rank_key, reverse=True)
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    if args.force_anchors:
        for config in anchor_configs():
            add_candidate(selected, seen, config, {"reason": "forced_anchor"})
    per_lane_counts: dict[str, int] = {}
    if args.per_lane > 0:
        for row in rows:
            lane = str(row.get("lane", row["config"].get("lane", "balance")))
            if per_lane_counts.get(lane, 0) >= args.per_lane:
                continue
            config = config_from_dict(row["config"], str(row.get("trial") or row["config"].get("name")))
            add_candidate(selected, seen, config, source_for(row))
            per_lane_counts[lane] = per_lane_counts.get(lane, 0) + 1
    if args.axis_k > 0:
        axis_names = ["hard_acc", "hard_worst", "stress_worst", "clean_acc", "clean_worst", "speed", "size"]
        for axis in axis_names:
            axis_rows = sorted(rows, key=lambda row, axis=axis: axis_keys(row)[axis], reverse=True)
            added = 0
            for row in axis_rows:
                config = config_from_dict(row["config"], str(row.get("trial") or row["config"].get("name")))
                source = source_for(row)
                source["reason"] = f"axis_{axis}"
                before = len(selected)
                add_candidate(selected, seen, config, source)
                if len(selected) > before:
                    added += 1
                if added >= args.axis_k:
                    break
    for row in rows:
        config = config_from_dict(row["config"], str(row.get("trial") or row["config"].get("name")))
        add_candidate(selected, seen, config, source_for(row))
        if len(selected) >= args.top_k:
            break
    selected = selected[: args.top_k]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": selected}, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"selected={len(selected)} output={args.output}")
    for item in selected:
        cfg = item["config"]
        print(
            f"{item['label']} lane={cfg['lane']} f={cfg['filters']} lr={cfg['learning_rate']} "
            f"l2={cfg['l2']} do={cfg['dropout']} b={cfg['batch_size']} head={cfg['head']} "
            f"aug={cfg['augment']['name']} us={cfg['estimated_board_us']}"
        )


def neighbor_values(value: float, multipliers: list[float]) -> list[float]:
    return sorted(set(float(f"{value * mult:.8g}") for mult in multipliers))


def fine_neighbors(base: V5Config, index: int) -> list[V5Config]:
    out: list[V5Config] = []
    lr_values = neighbor_values(base.learning_rate, [0.88, 0.92, 0.96, 1.0, 1.04, 1.08, 1.12])
    l2_base = max(base.l2, 1.0e-6)
    l2_values = neighbor_values(l2_base, [0.5, 0.7, 1.0, 1.25, 1.5, 2.0, 3.0])
    dropout_values = sorted(set([base.dropout, 0.0, 0.003, 0.005, 0.008, 0.01, min(0.25, base.dropout + 0.02)]))
    aug_names = ["sdiag_soft", "sdiag_mid", "sdiag_lowres", "sdiag_speed", "sdiag_roi", "v4_lowres_mix"]
    if base.augment.name not in aug_names:
        aug_names.insert(0, base.augment.name)
    batch_values = sorted(set([base.batch_size, 16, 24]))
    pool_values = sorted(set([base.pool, "max", "avg"]))
    activation_values = sorted(set([base.activation, "relu", "relu6"]))
    head_values = sorted(set([base.head, "subclass", "dual_parent"]))
    logits_values = sorted(set([base.logits, False, True]))
    calibration_values = sorted(set([base.calibration, "mild_stress", "balanced_clean", "balanced_rotmirror", "hard_stress", "hard_clean"]))

    def add(config: V5Config, reason: str) -> None:
        out.append(replace(config, name=f"fine{index:02d}_{len(out):04d}_{reason}_{safe_name(base.name)[:80]}"))

    add(base, "base")
    for lr in lr_values:
        add(replace(base, learning_rate=lr), "lr")
    for l2 in l2_values:
        add(replace(base, l2=l2), "l2")
    for dropout in dropout_values:
        add(replace(base, dropout=dropout), "drop")
    for aug in aug_names:
        add(replace(base, augment=ALL_AUGMENTS[aug]), "aug")
    for batch in batch_values:
        add(replace(base, batch_size=batch), "batch")
    for pool in pool_values:
        add(replace(base, pool=pool), "pool")
    for activation in activation_values:
        if base.architecture == "hardswish_depthwise" and activation != "hard_swish":
            continue
        add(replace(base, activation=activation), "act")
    if base.lane != "fast":
        for head in head_values:
            add(replace(base, head=head), "head")
        for logits in logits_values:
            add(replace(base, logits=logits), "logits")
    for calibration in calibration_values:
        add(replace(base, calibration=calibration), "calib")

    combo_count = 0
    for lr in lr_values:
        for l2 in l2_values:
            for dropout in dropout_values:
                for aug in aug_names:
                    add(replace(base, learning_rate=lr, l2=l2, dropout=dropout, augment=ALL_AUGMENTS[aug]), "combo")
                    combo_count += 1
                    if combo_count >= 160:
                        return out
    return out


def command_fine(args: argparse.Namespace) -> None:
    rows = load_results([args.input])
    if rows and "config" in rows[0] and "score_mean" in rows[0]:
        rows.sort(key=rank_key, reverse=True)
        base_configs = [config_from_dict(row["config"], str(row.get("trial") or row["config"].get("name"))) for row in rows[: args.top_k]]
    else:
        data = json.loads(args.input.read_text(encoding="utf-8"))
        base_configs = [
            config_from_dict(item["config"], str(item.get("label") or item["config"].get("name")))
            for item in data.get("candidates", [])
        ][: args.top_k]
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    for index, base in enumerate(base_configs, start=1):
        for config in fine_neighbors(base, index):
            add_candidate(selected, seen, config, {"reason": "fine_neighbor", "base": base.name})
            if len(selected) >= args.limit:
                break
        if len(selected) >= args.limit:
            break
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": selected}, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"fine_candidates={len(selected)} output={args.output}")


def command_summarize(args: argparse.Namespace) -> None:
    rows = load_results(args.input)
    rows = [row for row in rows if "score_mean" in row]
    rows.sort(key=rank_key, reverse=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {"summary": rows, "best": rows[0] if rows else None}
    args.output.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    csv_path = args.output.with_suffix(".csv")
    fieldnames = [
        "trial",
        "lane",
        "score_mean",
        "score_min",
        "clean_parent_accuracy_mean",
        "clean_parent_worst_min",
        "hard_parent_accuracy_mean",
        "hard_parent_worst_min",
        "stress_parent_worst_min",
        "agreement_mean",
        "estimated_board_us",
        "int8_bytes_mean",
    ]
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})
    print(f"rows={len(rows)} output={args.output} csv={csv_path}")
    for row in rows[: args.print_top]:
        print(
            f"{row['trial']} lane={row['lane']} score_min={float(row['score_min']):.4f} "
            f"score_mean={float(row['score_mean']):.4f} hard_worst={float(row['hard_parent_worst_min']):.4f} "
            f"stress_worst={float(row['stress_parent_worst_min']):.4f} us={row['estimated_board_us']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Select and expand V5 visual-subclass candidates.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sel = sub.add_parser("select")
    sel.add_argument("--input", action="append", required=True, type=Path)
    sel.add_argument("--output", required=True, type=Path)
    sel.add_argument("--top-k", type=int, default=24)
    sel.add_argument("--per-lane", type=int, default=4)
    sel.add_argument("--axis-k", type=int, default=2)
    sel.add_argument("--force-anchors", action="store_true")

    fine = sub.add_parser("fine")
    fine.add_argument("--input", required=True, type=Path)
    fine.add_argument("--output", required=True, type=Path)
    fine.add_argument("--top-k", type=int, default=6)
    fine.add_argument("--limit", type=int, default=240)

    summarize = sub.add_parser("summarize")
    summarize.add_argument("--input", action="append", required=True, type=Path)
    summarize.add_argument("--output", required=True, type=Path)
    summarize.add_argument("--print-top", type=int, default=20)

    args = parser.parse_args()
    if args.cmd == "select":
        command_select(args)
    elif args.cmd == "fine":
        command_fine(args)
    elif args.cmd == "summarize":
        command_summarize(args)


if __name__ == "__main__":
    main()
