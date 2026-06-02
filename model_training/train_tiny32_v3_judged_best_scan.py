import argparse
import copy
import json
import math
import os
import random
import time
from dataclasses import asdict
from pathlib import Path

import numpy as np
import tensorflow as tf

from train_tiny32_sixclass_scan import (
    AUGMENTS,
    BOARD_LATENCY_US,
    PARENT_NAMES,
    SUBCLASS_NAMES,
    SUBCLASS_TO_PARENT,
    AugmentConfig,
    ModelConfig,
    config_to_dict,
    estimate_board_latency_us,
    final_train_and_export,
    load_dataset,
    load_existing_results,
    stratified_folds,
    train_eval_config,
)
from train_tiny32_v3_fine_scan import DEFAULT_STRESS, final_score, safe_name, summarize_stress_min


CUSTOM_AUGMENTS = {
    "v3_speed_lowres_mix": AugmentConfig(
        "v3_speed_lowres_mix",
        pad=2,
        brightness=0.14,
        contrast_delta=0.22,
        noise_std=0.048,
        salt_pepper=0.008,
        blur_prob=0.34,
        blur_kernel=5,
        downscale_prob=0.44,
        downscale_min=16,
    ),
    "v3_speed_roi_noise": AugmentConfig(
        "v3_speed_roi_noise",
        pad=1,
        brightness=0.12,
        contrast_delta=0.20,
        noise_std=0.052,
        salt_pepper=0.008,
        blur_prob=0.30,
        blur_kernel=5,
        downscale_prob=0.34,
        downscale_min=18,
    ),
}


def augment_by_name(name: str) -> AugmentConfig:
    if name in AUGMENTS:
        return AUGMENTS[name]
    return CUSTOM_AUGMENTS[name]


def semantic_key_from_config(config: ModelConfig) -> tuple[object, ...]:
    return (
        tuple(int(v) for v in config.filters),
        int(config.dense_units),
        round(float(config.dropout), 6),
        round(float(config.l2), 10),
        round(float(config.learning_rate), 10),
        int(config.batch_size),
        str(config.pool),
        int(config.first_kernel),
        bool(config.extra_conv),
        str(config.augment.name),
        str(config.architecture),
        str(config.activation),
        str(config.train_transforms),
    )


def semantic_key_from_dict(config: dict[str, object]) -> tuple[object, ...]:
    augment = config["augment"]
    augment_name = augment["name"] if isinstance(augment, dict) else str(augment)
    return (
        tuple(int(v) for v in config["filters"]),
        int(config["dense_units"]),
        round(float(config["dropout"]), 6),
        round(float(config["l2"]), 10),
        round(float(config["learning_rate"]), 10),
        int(config["batch_size"]),
        str(config["pool"]),
        int(config["first_kernel"]),
        bool(config["extra_conv"]),
        str(augment_name),
        str(config.get("architecture", "spacetodepth_conv")),
        str(config.get("activation", "relu")),
        str(config.get("train_transforms", "rot_mirror")),
    )


def collect_existing_keys(output_dir: Path) -> set[tuple[object, ...]]:
    keys: set[tuple[object, ...]] = set()
    experiments = Path("experiments")
    patterns = [
        "v3_fine_*_20260507_081935/run_config.json",
        "v3_fast_near_*_20260507_161450/run_config.json",
        "v3_judged_best_*/run_config.json",
    ]
    for pattern in patterns:
        for path in experiments.glob(pattern):
            if any(token in path.parent.name for token in ("smoke", "probe")):
                continue
            if output_dir in path.parents:
                continue
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            for config in data.get("configs", []):
                try:
                    keys.add(semantic_key_from_dict(config))
                except (KeyError, TypeError, ValueError):
                    continue
    return keys


def make_config(
    tag: str,
    filters: tuple[int, int, int],
    dense: int,
    dropout: float,
    l2: float,
    lr: float,
    batch: int,
    pool: str,
    extra: bool,
    augment: str,
) -> ModelConfig:
    name = (
        f"v3judged_{tag}_f{'-'.join(map(str, filters))}_d{dense}_do{dropout:g}"
        f"_l2{l2:g}_lr{lr:g}_b{batch}_{pool}_k3_x{int(extra)}_{augment}"
    )
    return ModelConfig(
        name=name,
        filters=filters,
        dense_units=dense,
        dropout=dropout,
        l2=l2,
        learning_rate=lr,
        batch_size=batch,
        pool=pool,
        first_kernel=3,
        extra_conv=extra,
        augment=augment_by_name(augment),
        architecture="spacetodepth_conv",
        activation="relu",
        train_transforms="rot_mirror",
    )


def trend_priority(config: ModelConfig, rng: random.Random) -> tuple[float, int, str]:
    latency = estimate_board_latency_us(config)
    target_l2 = 2.5e-5 if config.extra_conv else 0.0
    l2_penalty = 0.0
    if config.l2 <= 0 and target_l2 > 0:
        l2_penalty = 0.45
    elif config.l2 > 0 and target_l2 <= 0:
        l2_penalty = 0.12
    elif config.l2 > 0 and target_l2 > 0:
        l2_penalty = min(0.8, abs(math.log(config.l2 / target_l2)) / 2.6)
    lr_penalty = min(1.0, abs(math.log(config.learning_rate / 0.00215)) / 1.1)
    dropout_penalty = abs(config.dropout - 0.02) * 3.0
    batch_penalty = {16: 0.0, 24: 0.015, 32: 0.055, 48: 0.09}.get(config.batch_size, 0.14)
    augment_penalty = {
        "v3_speed_noise": 0.0,
        "v3_speed_lowres_mix": 0.012,
        "v3_lowres_noise": 0.030,
        "v3_speed_roi_noise": 0.040,
        "v3_aggressive_noise": 0.075,
    }.get(config.augment.name, 0.10)
    filter_penalty = sum(abs(a - b) / b for a, b in zip(config.filters, (8, 16, 32))) / 3.0
    dense_penalty = 0.0 if config.dense_units == 0 else (0.11 if config.dense_units <= 8 else 0.18)
    extra_penalty = 0.0 if config.extra_conv else 0.08
    pool_penalty = 0.0 if config.pool == "max" else 0.10
    latency_penalty = max(0.0, latency - 10800) / 18000.0
    speed_credit = -0.045 if latency <= 7800 else 0.0
    score = (
        1.1 * filter_penalty
        + 0.42 * l2_penalty
        + 0.32 * lr_penalty
        + dropout_penalty
        + batch_penalty
        + augment_penalty
        + dense_penalty
        + extra_penalty
        + pool_penalty
        + latency_penalty
        + speed_credit
        + rng.random() * 1.0e-4
    )
    return score, latency, config.name


def make_candidates(limit: int, seed: int, output_dir: Path) -> list[ModelConfig]:
    rng = random.Random(seed)
    existing = collect_existing_keys(output_dir)
    candidates: list[ModelConfig] = []
    seen: set[tuple[object, ...]] = set(existing)

    def add(config: ModelConfig) -> None:
        key = semantic_key_from_config(config)
        if key in seen:
            return
        latency = estimate_board_latency_us(config)
        if 6800 <= latency <= 11200:
            seen.add(key)
            candidates.append(config)

    main_filters = [(8, 16, 32), (8, 16, 36), (8, 18, 32), (9, 16, 32), (8, 18, 36)]
    main_l2 = [1.5e-5, 2.0e-5, 2.5e-5, 3.5e-5, 4.0e-5, 5.0e-5, 7.0e-5]
    main_lr = [1.8e-3, 2.0e-3, 2.15e-3, 2.3e-3, 2.5e-3]
    for filters in main_filters:
        for dropout in [0.0, 0.01, 0.02, 0.04, 0.06]:
            for l2 in main_l2:
                for lr in main_lr:
                    for batch in [16, 24, 32]:
                        for augment in ["v3_speed_noise", "v3_speed_lowres_mix", "v3_lowres_noise", "v3_speed_roi_noise"]:
                            add(make_config("main", filters, 0, dropout, l2, lr, batch, "max", True, augment))

    # A narrow speed branch tests whether the faster dense-head variant can keep the new low-l2 trend.
    for filters in [(8, 16, 32), (8, 16, 36), (9, 16, 32)]:
        for dense in [12, 16, 20]:
            for dropout in [0.0, 0.01, 0.02, 0.04]:
                for l2 in [0.0, 5.0e-6, 1.5e-5, 3.0e-5]:
                    for lr in [2.15e-3, 2.35e-3, 2.5e-3, 2.8e-3]:
                        for batch in [16, 24, 32]:
                            for augment in ["v3_lowres_noise", "v3_speed_noise", "v3_speed_lowres_mix"]:
                                add(make_config("speed_cross", filters, dense, dropout, l2, lr, batch, "max", False, augment))

    # A small-capacity variant checks if tiny width changes improve recall without board-cost growth.
    for filters in [(7, 16, 32), (8, 14, 32), (8, 16, 28), (7, 14, 32)]:
        for dropout in [0.0, 0.02, 0.04]:
            for l2 in [1.5e-5, 2.5e-5, 4.0e-5]:
                for lr in [2.0e-3, 2.2e-3, 2.5e-3]:
                    for batch in [16, 24]:
                        for augment in ["v3_speed_noise", "v3_speed_lowres_mix"]:
                            add(make_config("tiny_width", filters, 0, dropout, l2, lr, batch, "max", True, augment))

    candidates.sort(key=lambda item: trend_priority(item, rng))
    return candidates[:limit]


def write_trend_summary(output_dir: Path, configs: list[ModelConfig], args: argparse.Namespace) -> None:
    summary = {
        "working_assumptions": [
            "Prior final-int8 best was 8-16-32/dense0/extra_conv/max with board estimate near 9.2ms.",
            "Current neighborhood results favor batch16, low l2 around 1e-5 to 3e-5, and v3_speed_noise for stress robustness.",
            "rank03_speed shows a viable faster branch at 7.6ms, but its final-int8 worst recall was previously weaker, so it is only a narrow cross-check.",
            "Search score is not final proof; final int8 top-k remains the deployment criterion.",
        ],
        "new_scan_bias": {
            "primary": "8-16-32-ish, dense0, extra_conv=true, max pool, batch16/24, l2=1.5e-5..7e-5, lr=0.0018..0.0025",
            "secondary": "dense12/16/20 with extra_conv=false for the 7.6ms speed branch",
            "augment": "standard v3_speed_noise/v3_lowres_noise plus custom v3_speed_lowres_mix and v3_speed_roi_noise",
            "dedupe": "semantic parameter keys are loaded from prior v3_fine, current v3_fast_near, and prior v3_judged_best run_config files",
        },
        "args": vars(args),
        "generated_trials": len(configs),
        "configs": [config_to_dict(config) | {"name": config.name} for config in configs],
    }
    (output_dir / "trend_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Judgment-guided v3 scan around the best observed parameter trends.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--max-trials", type=int, default=48)
    parser.add_argument("--folds", type=int, default=3)
    parser.add_argument("--epochs", type=int, default=180)
    parser.add_argument("--patience", type=int, default=24)
    parser.add_argument("--final-epochs", type=int, default=300)
    parser.add_argument("--final-patience", type=int, default=40)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260530)
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--speed-weight", type=float, default=0.10)
    parser.add_argument("--final-top-k", type=int, default=5)
    args = parser.parse_args()

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime("v3_judged_best_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    folds = stratified_folds(y_sub, args.folds, args.seed)
    configs = make_candidates(args.max_trials, args.seed, output_dir)
    write_trend_summary(output_dir, configs, args)
    run_config = {
        "args": vars(args),
        "custom_augments": {name: asdict(augment) for name, augment in CUSTOM_AUGMENTS.items()},
        "subclass_names": SUBCLASS_NAMES,
        "parent_names": PARENT_NAMES,
        "subclass_to_parent": SUBCLASS_TO_PARENT.tolist(),
        "sample_count": int(len(y_sub)),
        "subclass_counts": {SUBCLASS_NAMES[i]: int(np.sum(y_sub == i)) for i in range(len(SUBCLASS_NAMES))},
        "generated_trials": len(configs),
        "speed_weight": args.speed_weight,
        "board_latency_reference_us": BOARD_LATENCY_US,
        "paths": paths,
        "configs": [config_to_dict(config) | {"name": config.name} for config in configs],
    }
    (output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2, ensure_ascii=False), encoding="utf-8")

    results: list[dict[str, object]] = []
    completed_trials: set[str] = set()
    skipped_corrupt = 0
    trial_results_path = output_dir / "trial_results.jsonl"
    if args.resume and trial_results_path.exists():
        results, skipped_corrupt = load_existing_results(trial_results_path)
        completed_trials = {str(result["trial"]) for result in results}

    pending_count = sum(1 for config in configs if config.name not in completed_trials)
    print("output_dir=" + str(output_dir))
    print("sample_count=" + str(len(y_sub)))
    print("subclass_counts=" + json.dumps(run_config["subclass_counts"], ensure_ascii=False))
    if skipped_corrupt:
        print(f"resume_skipped_corrupt_lines={skipped_corrupt}")
    print(
        f"judged_best trials={len(configs)} pending={pending_count} completed={len(completed_trials)} "
        f"folds={len(folds)} speed_weight={args.speed_weight}",
        flush=True,
    )

    for index, config in enumerate(configs, start=1):
        if config.name in completed_trials:
            print(f"[{index:03d}/{len(configs):03d}] {config.name} skipped_existing", flush=True)
            continue
        result = train_eval_config(config, x, y_sub, y_parent, folds, args, output_dir)
        results.append(result)
        print(
            f"[{index:03d}/{len(configs):03d}] {config.name} "
            f"score={result['mean_score']:.4f} quality={result['mean_quality_score']:.4f} "
            f"speed_bonus={result['speed_bonus']:.4f} est_us={result['estimated_board_us']} "
            f"clean_acc={result['mean_clean_parent_accuracy']:.4f} "
            f"clean_worst={result['mean_clean_parent_worst_recall']:.4f} "
            f"stress_min={summarize_stress_min(result):.4f} "
            f"sub_acc={result['mean_subclass_accuracy']:.4f} epochs={result['mean_epochs']:.1f}",
            flush=True,
        )

    if not results:
        raise RuntimeError("no results available")

    results.sort(
        key=lambda item: (
            item["mean_score"],
            item["mean_clean_parent_worst_recall"],
            summarize_stress_min(item),
            item["mean_clean_parent_accuracy"],
        ),
        reverse=True,
    )
    summary: dict[str, object] = {
        "best": results[0],
        "top_results": results[: min(20, len(results))],
        "final_exports": [],
    }
    (output_dir / "search_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print("best_trial=" + str(results[0]["trial"]))

    config_by_name = {config.name: config for config in configs}
    final_exports: list[dict[str, object]] = []
    for rank, result in enumerate(results[: max(0, args.final_top_k)], start=1):
        config = config_by_name[str(result["trial"])]
        candidate_dir = output_dir / "final_exports" / f"rank{rank:02d}_{safe_name(config.name)}"
        candidate_dir.mkdir(parents=True, exist_ok=True)
        final_args = copy.copy(args)
        final_summary = final_train_and_export(config, x, y_sub, y_parent, final_args, candidate_dir)
        final_rank = {
            "rank": rank,
            "trial": result["trial"],
            "search": result,
            "final": final_summary,
            "final_score": final_score(final_summary, int(result["estimated_board_us"]), args.speed_weight),
            "output_dir": str(candidate_dir),
        }
        final_exports.append(final_rank)
        parent = final_summary["int8_test"]["parent"]
        all_parent = final_summary["int8_all"]["parent"]
        print(
            f"final_rank={rank} trial={result['trial']} final_score={final_rank['final_score']:.4f} "
            f"int8_test_acc={parent['accuracy']:.4f} int8_test_worst={parent['worst_recall']:.4f} "
            f"int8_all_acc={all_parent['accuracy']:.4f} int8_all_worst={all_parent['worst_recall']:.4f} "
            f"dir={candidate_dir}",
            flush=True,
        )

    summary["final_exports"] = final_exports
    if final_exports:
        summary["final_best"] = max(
            final_exports,
            key=lambda item: (
                item["final_score"],
                item["final"]["int8_test"]["parent"]["worst_recall"],
                item["final"]["int8_test"]["parent"]["accuracy"],
            ),
        )
    (output_dir / "search_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print("search_summary_path=" + str(output_dir / "search_summary.json"))


if __name__ == "__main__":
    main()
