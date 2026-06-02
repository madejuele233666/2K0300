import argparse
import csv
import json
import math
import random
from dataclasses import replace
from pathlib import Path

import numpy as np

from train_tiny32_v5_visual_subclass_scan import (
    ALL_AUGMENTS,
    V5Config,
    anchor_configs,
    config_from_dict,
    config_to_dict,
    make_config,
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


def parentfirst_priority(config: V5Config, rng: random.Random) -> tuple[float, int, str]:
    if config.lane == "fast":
        target_filters = (6, 12, 24)
        target_us = 5600
        lr_targets = [0.00286, 0.00294, 0.00318]
    elif config.lane == "accuracy":
        target_filters = (10, 20, 40)
        target_us = 10500
        lr_targets = [0.0016, 0.0020, 0.0023, 0.0026]
    else:
        target_filters = (8, 16, 32)
        target_us = 7163
        lr_targets = [0.00286, 0.00302, 0.00318]

    filter_penalty = sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, target_filters)) / 3.0
    lr_penalty = min(abs(math.log(config.learning_rate / target)) for target in lr_targets) / 0.45
    l2_penalty = abs(math.log(config.l2 / 1.0e-4)) / 2.6 if config.l2 > 0 else 0.45
    latency = float(config_to_dict(config)["estimated_board_us"])
    latency_penalty = max(0.0, latency - target_us) / 18000.0
    arch_penalty = {
        "spacetodepth_conv": 0.0,
        "depthwise_pool": 0.06,
        "stride_conv": 0.14,
        "hardswish_depthwise": 0.16,
        "conv_pool": 0.24,
    }.get(config.architecture, 0.4)
    if config.lane == "accuracy":
        arch_penalty *= 0.55
    head_penalty = {"parent": 0.0, "dual_parent": 0.018, "subclass": 0.095}.get(config.head, 0.16)
    calibration_penalty = {
        "mild_stress": 0.0,
        "balanced_rotmirror": 0.012,
        "balanced_clean": 0.02,
        "hard_stress": 0.035,
        "hard_clean": 0.055,
    }.get(config.calibration, 0.04)
    dual_weight_penalty = 0.0
    if config.head == "dual_parent":
        dual_weight_penalty = abs(config.parent_loss_weight - 1.5) * 0.012 + abs(config.subclass_loss_weight - 0.18) * 0.05
    return (
        0.46 * filter_penalty
        + 0.34 * lr_penalty
        + 0.20 * l2_penalty
        + 2.6 * config.dropout
        + arch_penalty
        + head_penalty
        + calibration_penalty
        + dual_weight_penalty
        + latency_penalty
        + (0.012 if config.logits else 0.0)
        + (0.03 if config.pool != "max" else 0.0)
        + (0.02 if config.activation != "relu" and config.architecture != "hardswish_depthwise" else 0.0)
        + rng.random() * 1.0e-4,
        int(latency),
        config.name,
    )


def command_parentfirst(args: argparse.Namespace) -> None:
    rng = random.Random(args.seed)
    pool: list[V5Config] = []

    lane_options: dict[str, dict[str, object]] = {
        "fast": {
            "filters": [(5, 10, 20), (6, 12, 24), (7, 14, 28), (8, 16, 24), (8, 16, 32)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00318, 0.00326],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008],
            "augment": ["sdiag_base", "sdiag_soft", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "batch": [16, 24],
        },
        "balance": {
            "filters": [(7, 14, 28), (8, 16, 24), (8, 16, 32), (8, 18, 36), (10, 18, 36)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00310, 0.00318, 0.00326],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4, 3.0e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008, 0.012],
            "augment": ["sdiag_base", "sdiag_soft", "sdiag_mid", "sdiag_lowres", "sdiag_speed", "sdiag_roi", "v4_lowres_mix"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "batch": [16, 24],
        },
        "accuracy": {
            "filters": [(8, 18, 36), (10, 18, 36), (10, 20, 40), (10, 20, 48), (12, 24, 48), (16, 24, 48)],
            "lr": [0.0012, 0.0016, 0.0020, 0.0023, 0.0026, 0.00286],
            "l2": [1.0e-6, 5.0e-6, 1.0e-5, 3.0e-5, 7.0e-5, 1.0e-4, 3.0e-4, 6.0e-4],
            "dropout": [0.0, 0.003, 0.008, 0.02, 0.05, 0.08],
            "augment": ["sdiag_soft", "sdiag_mid", "sdiag_hard", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix", "v4_highspeed"],
            "arch": ["spacetodepth_conv", "depthwise_pool", "stride_conv", "hardswish_depthwise", "conv_pool"],
            "batch": [16, 24, 32],
        },
    }
    lanes = ["fast", "balance", "accuracy"] if args.lane == "all" else [args.lane]
    calibrations = ["mild_stress", "balanced_rotmirror", "balanced_clean", "hard_stress"]
    heads = ["parent", "dual_parent"]
    if args.include_controls:
        heads.append("subclass")
    dual_weights = [(1.0, 0.10), (1.0, 0.20), (1.5, 0.15), (1.5, 0.25), (2.0, 0.20)]

    def add(config: V5Config) -> None:
        if float(config_to_dict(config)["estimated_board_us"]) > args.max_board_us:
            return
        pool.append(config)

    for lane in lanes:
        opts = lane_options[lane]
        base_filters = (6, 12, 24) if lane == "fast" else ((10, 20, 40) if lane == "accuracy" else (8, 16, 32))
        base_lr = 0.0023 if lane == "accuracy" else 0.00318
        base_aug = "sdiag_mid" if lane == "accuracy" else ("sdiag_base" if lane == "fast" else "sdiag_lowres")
        for head in heads:
            for calibration in calibrations:
                for logits in ([False, True] if head != "subclass" else [False]):
                    base = make_config(
                        f"parentfirst_{lane}_{head}_{base_aug}_{calibration}_{'logits' if logits else 'softmax'}",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        base_lr,
                        1.0e-4,
                        0.003,
                        base_aug,
                        head=head,
                        logits=logits,
                        calibration=calibration,
                    )
                    if head == "dual_parent":
                        for parent_weight, subclass_weight in dual_weights:
                            add(
                                replace(
                                    base,
                                    name=f"{base.name}_pw{parent_weight:g}_sw{subclass_weight:g}",
                                    parent_loss_weight=parent_weight,
                                    subclass_loss_weight=subclass_weight,
                                )
                            )
                    else:
                        add(base)
        for filters in opts["filters"]:  # type: ignore[index]
            for head in ["parent", "dual_parent"]:
                base = make_config(
                    f"parentfirst_{lane}_filters_{'-'.join(map(str, filters))}_{head}",
                    lane,
                    "spacetodepth_conv",
                    filters,  # type: ignore[arg-type]
                    base_lr,
                    1.0e-4,
                    0.003,
                    base_aug,
                    head=head,
                    calibration="mild_stress",
                )
                add(replace(base, parent_loss_weight=1.5, subclass_loss_weight=0.18) if head == "dual_parent" else base)
        for lr in opts["lr"]:  # type: ignore[index]
            for l2 in [1.0e-4, 1.5e-4, 3.0e-4] if lane != "accuracy" else [1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4]:
                add(
                    make_config(
                        f"parentfirst_{lane}_lr{float(lr):g}_l2{float(l2):g}_parent",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        float(lr),
                        float(l2),
                        0.003,
                        base_aug,
                        head="parent",
                        calibration="mild_stress",
                    )
                )
        for augment in opts["augment"]:  # type: ignore[index]
            for dropout in [0.0, 0.003, 0.008, 0.02]:
                add(
                    make_config(
                        f"parentfirst_{lane}_{augment}_do{dropout:g}_parent",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        base_lr,
                        1.0e-4,
                        dropout,
                        str(augment),
                        head="parent",
                        calibration="mild_stress",
                    )
                )
        lane_start = len(pool)
        attempts = 0
        target_pool = max(args.limit * 8 // max(1, len(lanes)), 220)
        while len(pool) - lane_start < target_pool and attempts < target_pool * 18:
            attempts += 1
            arch = str(rng.choice(opts["arch"]))  # type: ignore[arg-type]
            activation = "hard_swish" if arch == "hardswish_depthwise" else rng.choice(["relu", "relu6"])
            head = rng.choices(["parent", "dual_parent", "subclass"], weights=[0.55, 0.35, 0.10 if args.include_controls else 0.0])[0]
            config = make_config(
                f"parentfirst_rand_{lane}_{attempts:04d}_{head}",
                lane,
                arch,
                rng.choice(opts["filters"]),  # type: ignore[arg-type]
                float(rng.choice(opts["lr"])),  # type: ignore[arg-type]
                float(rng.choice(opts["l2"])),  # type: ignore[arg-type]
                float(rng.choice(opts["dropout"])),  # type: ignore[arg-type]
                str(rng.choice(opts["augment"])),  # type: ignore[arg-type]
                dense_units=0 if lane != "accuracy" else rng.choice([0, 8, 16]),
                batch_size=int(rng.choice(opts["batch"])),  # type: ignore[arg-type]
                pool=rng.choice(["max", "avg"]),
                extra_conv=bool(lane == "accuracy" and rng.random() < 0.35),
                activation=activation,
                head=head,
                logits=bool(head != "subclass" and rng.random() < 0.25),
                class_weight="none" if head == "dual_parent" else rng.choice(["none", "sqrt_balanced"]),
                calibration=rng.choice(calibrations),
            )
            if head == "dual_parent":
                parent_weight, subclass_weight = rng.choice(dual_weights)
                config = replace(config, parent_loss_weight=parent_weight, subclass_loss_weight=subclass_weight)
            add(config)

    pool.sort(key=lambda config: parentfirst_priority(config, rng))
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    for config in pool:
        add_candidate(selected, seen, config, {"reason": "parentfirst_aggressive"})
        if len(selected) >= args.limit:
            break
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": selected}, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"parentfirst_candidates={len(selected)} output={args.output}")
    for item in selected[: args.print_top]:
        cfg = item["config"]
        print(
            f"{item['label']} lane={cfg['lane']} f={cfg['filters']} head={cfg['head']} "
            f"lr={cfg['learning_rate']} l2={cfg['l2']} do={cfg['dropout']} "
            f"aug={cfg['augment']['name']} calib={cfg['calibration']} us={cfg['estimated_board_us']}"
        )


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

    parentfirst = sub.add_parser("parentfirst")
    parentfirst.add_argument("--output", required=True, type=Path)
    parentfirst.add_argument("--limit", type=int, default=384)
    parentfirst.add_argument("--seed", type=int, default=20262700)
    parentfirst.add_argument("--lane", choices=["fast", "balance", "accuracy", "all"], default="all")
    parentfirst.add_argument("--max-board-us", type=int, default=18000)
    parentfirst.add_argument("--include-controls", action="store_true")
    parentfirst.add_argument("--print-top", type=int, default=24)

    summarize = sub.add_parser("summarize")
    summarize.add_argument("--input", action="append", required=True, type=Path)
    summarize.add_argument("--output", required=True, type=Path)
    summarize.add_argument("--print-top", type=int, default=20)

    args = parser.parse_args()
    if args.cmd == "select":
        command_select(args)
    elif args.cmd == "fine":
        command_fine(args)
    elif args.cmd == "parentfirst":
        command_parentfirst(args)
    elif args.cmd == "summarize":
        command_summarize(args)


if __name__ == "__main__":
    main()
