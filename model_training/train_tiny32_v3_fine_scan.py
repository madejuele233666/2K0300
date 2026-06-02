import argparse
import copy
import json
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
    ModelConfig,
    config_to_dict,
    estimate_board_latency_us,
    final_train_and_export,
    load_dataset,
    load_existing_results,
    stratified_folds,
    train_eval_config,
)


DEFAULT_STRESS = (
    "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10"
)


ANCHORS = [
    {
        "tag": "global_search_best",
        "filters": (10, 20, 40),
        "dense": 0,
        "dropout": 0.30,
        "l2": 3.0e-4,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": True,
        "augment": "v3_lowres_noise",
    },
    {
        "tag": "speed9_stress_best",
        "filters": (10, 20, 40),
        "dense": 0,
        "dropout": 0.0,
        "l2": 1.0e-4,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": False,
        "augment": "v3_lowres_noise",
    },
    {
        "tag": "final_int8_best",
        "filters": (16, 24, 48),
        "dense": 16,
        "dropout": 0.20,
        "l2": 0.0,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "avg",
        "kernel": 3,
        "extra": True,
        "augment": "v3_speed_noise",
    },
    {
        "tag": "fast_extra_aggressive",
        "filters": (8, 16, 32),
        "dense": 0,
        "dropout": 0.0,
        "l2": 0.0,
        "lr": 1.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": True,
        "augment": "v3_aggressive_noise",
    },
    {
        "tag": "balanced_8_16_32",
        "filters": (8, 16, 32),
        "dense": 16,
        "dropout": 0.0,
        "l2": 3.0e-4,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": True,
        "augment": "v3_lowres_noise",
    },
    {
        "tag": "sub8_anchor",
        "filters": (6, 12, 24),
        "dense": 16,
        "dropout": 0.0,
        "l2": 1.0e-4,
        "lr": 2.0e-3,
        "batch": 64,
        "pool": "avg",
        "kernel": 3,
        "extra": True,
        "augment": "v3_lowres_noise",
    },
]


def safe_name(name: str, limit: int = 96) -> str:
    allowed = []
    for char in name:
        if char.isalnum() or char in {"-", "_", "."}:
            allowed.append(char)
        else:
            allowed.append("_")
    return "".join(allowed)[:limit].strip("._-") or "candidate"


def make_config(
    prefix: str,
    tag: str,
    filters: tuple[int, int, int],
    dense: int,
    dropout: float,
    l2: float,
    lr: float,
    batch: int,
    pool: str,
    kernel: int,
    extra: bool,
    augment: str,
) -> ModelConfig:
    name = (
        f"v3fine_{prefix}_{tag}_f{'-'.join(map(str, filters))}_d{dense}_do{dropout:g}"
        f"_l2{l2:g}_lr{lr:g}_b{batch}_{pool}_k{kernel}_x{int(extra)}_{augment}"
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
        first_kernel=kernel,
        extra_conv=extra,
        augment=AUGMENTS[augment],
        architecture="spacetodepth_conv",
        activation="relu",
        train_transforms="rot_mirror",
    )


def anchor_configs(prefix: str) -> list[ModelConfig]:
    return [
        make_config(
            prefix,
            str(anchor["tag"]),
            anchor["filters"],
            int(anchor["dense"]),
            float(anchor["dropout"]),
            float(anchor["l2"]),
            float(anchor["lr"]),
            int(anchor["batch"]),
            str(anchor["pool"]),
            int(anchor["kernel"]),
            bool(anchor["extra"]),
            str(anchor["augment"]),
        )
        for anchor in ANCHORS
    ]


def config_distance_score(config: ModelConfig, lane: str) -> tuple[float, int, str]:
    latency = estimate_board_latency_us(config)
    lowres_bonus = 0.0 if config.augment.name == "v3_lowres_noise" else 0.012
    speed_bonus = 0.005 if config.augment.name == "v3_speed_noise" else 0.0
    aggressive_penalty = 0.010 if config.augment.name == "v3_aggressive_noise" else 0.0
    dense_penalty = 0.004 * abs(config.dense_units - (16 if lane == "accuracy" else 0)) / 8.0
    dropout_target = {"fast": 0.0, "balanced": 0.10, "accuracy": 0.20, "stability": 0.10}.get(lane, 0.10)
    dropout_penalty = abs(config.dropout - dropout_target)
    lr_penalty = abs(np.log(config.learning_rate / 0.002))
    extra_penalty = 0.0 if config.extra_conv else (0.04 if lane == "accuracy" else 0.01)
    pool_penalty = 0.0 if config.pool == "max" else (0.02 if lane in {"fast", "balanced"} else 0.0)
    if lane == "fast":
        latency_penalty = max(0.0, latency - 9000) / 9000.0 + max(0.0, 7000 - latency) / 14000.0
    elif lane == "balanced":
        latency_penalty = abs(latency - 10500) / 14000.0
    elif lane == "accuracy":
        latency_penalty = abs(latency - 15000) / 20000.0
    else:
        latency_penalty = latency / 50000.0
    score = (
        latency_penalty
        + dropout_penalty
        + 0.22 * lr_penalty
        + dense_penalty
        + lowres_bonus
        + speed_bonus
        + aggressive_penalty
        + extra_penalty
        + pool_penalty
    )
    return score, latency, config.name


def lane_grid(lane: str) -> list[ModelConfig]:
    if lane == "fast":
        filter_sets = [(6, 12, 24), (8, 16, 32), (10, 20, 40)]
        dense_values = [0, 8, 16]
        dropout_values = [0.0, 0.05, 0.10, 0.20]
        l2_values = [0.0, 1.0e-5, 1.0e-4, 3.0e-4]
        lr_values = [1.4e-3, 2.0e-3, 2.5e-3, 3.0e-3]
        batches = [32, 64]
        pools = ["max", "avg"]
        extras = [False, True]
        augments = ["v3_lowres_noise", "v3_aggressive_noise", "v3_speed_noise"]
        kernels = [3]
        min_latency, max_latency = 0, 10000
    elif lane == "balanced":
        filter_sets = [(8, 16, 32), (10, 20, 40), (12, 24, 48)]
        dense_values = [0, 8, 16]
        dropout_values = [0.0, 0.10, 0.20, 0.30]
        l2_values = [0.0, 1.0e-5, 1.0e-4, 3.0e-4]
        lr_values = [1.4e-3, 2.0e-3, 2.5e-3, 3.0e-3]
        batches = [32, 64]
        pools = ["max", "avg"]
        extras = [False, True]
        augments = ["v3_lowres_noise", "v3_speed_noise", "v3_aggressive_noise"]
        kernels = [3]
        min_latency, max_latency = 8500, 13000
    elif lane == "accuracy":
        filter_sets = [(10, 20, 40), (12, 24, 48), (16, 24, 48)]
        dense_values = [0, 8, 16, 24]
        dropout_values = [0.0, 0.10, 0.20, 0.30]
        l2_values = [0.0, 1.0e-5, 1.0e-4, 3.0e-4]
        lr_values = [1.0e-3, 1.4e-3, 2.0e-3, 2.5e-3]
        batches = [32, 64]
        pools = ["max", "avg"]
        extras = [False, True]
        augments = ["v3_lowres_noise", "v3_speed_noise", "v3_aggressive_noise"]
        kernels = [3, 5]
        min_latency, max_latency = 11500, 19000
    elif lane == "stability":
        configs: list[ModelConfig] = []
        for repeat in range(4):
            for config in anchor_configs(f"stability_s{repeat + 1}"):
                configs.append(config)
        return configs
    else:
        raise ValueError(f"unknown lane: {lane}")

    configs: list[ModelConfig] = []
    for filters in filter_sets:
        for dense in dense_values:
            for dropout in dropout_values:
                for l2 in l2_values:
                    for lr in lr_values:
                        for batch in batches:
                            for pool in pools:
                                for kernel in kernels:
                                    for extra in extras:
                                        for augment in augments:
                                            config = make_config(
                                                lane,
                                                "grid",
                                                filters,
                                                dense,
                                                dropout,
                                                l2,
                                                lr,
                                                batch,
                                                pool,
                                                kernel,
                                                extra,
                                                augment,
                                            )
                                            latency = estimate_board_latency_us(config)
                                            if min_latency <= latency <= max_latency:
                                                configs.append(config)
    return configs


def make_lane_configs(lane: str, limit: int, seed: int) -> list[ModelConfig]:
    configs = []
    seen: set[str] = set()
    for config in anchor_configs(lane):
        if config.name not in seen:
            configs.append(config)
            seen.add(config.name)

    candidates = lane_grid(lane)
    rng = random.Random(seed)
    rng.shuffle(candidates)
    candidates.sort(key=lambda config: config_distance_score(config, lane))
    for config in candidates:
        if config.name in seen:
            continue
        configs.append(config)
        seen.add(config.name)
        if len(configs) >= limit:
            break
    return configs[:limit]


def summarize_stress_min(result: dict[str, object]) -> float:
    stresses = result.get("mean_stress_parent_worst_recall", {})
    if not isinstance(stresses, dict) or not stresses:
        return 0.0
    return float(min(float(value) for value in stresses.values()))


def final_score(final_summary: dict[str, object], estimated_board_us: int, speed_weight: float) -> float:
    parent = final_summary["int8_test"]["parent"]
    all_parent = final_summary["int8_all"]["parent"]
    speed_score = max(0.0, min(1.0, (25000.0 - estimated_board_us) / 25000.0))
    return float(
        0.36 * float(parent["accuracy"])
        + 0.26 * float(parent["worst_recall"])
        + 0.18 * float(parent["macro_recall"])
        + 0.12 * float(all_parent["accuracy"])
        + 0.08 * float(all_parent["worst_recall"])
        + speed_weight * speed_score
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Targeted v3 fine scan for SpaceToDepth tiny32 models.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--lane", choices=["fast", "balanced", "accuracy", "stability"], required=True)
    parser.add_argument("--max-trials", type=int, default=72)
    parser.add_argument("--folds", type=int, default=3)
    parser.add_argument("--epochs", type=int, default=150)
    parser.add_argument("--patience", type=int, default=18)
    parser.add_argument("--final-epochs", type=int, default=240)
    parser.add_argument("--final-patience", type=int, default=34)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260510)
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--speed-weight", type=float, default=-1.0)
    parser.add_argument("--final-top-k", type=int, default=3)
    args = parser.parse_args()

    lane_speed_weights = {"fast": 0.10, "balanced": 0.07, "accuracy": 0.04, "stability": 0.06}
    if args.speed_weight < 0:
        args.speed_weight = lane_speed_weights[args.lane]

    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime(f"v3_fine_{args.lane}_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    folds = stratified_folds(y_sub, args.folds, args.seed)
    configs = make_lane_configs(args.lane, args.max_trials, args.seed)
    run_config = {
        "args": vars(args),
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
        f"lane={args.lane} trials={len(configs)} pending={pending_count} "
        f"completed={len(completed_trials)} folds={len(folds)} speed_weight={args.speed_weight}"
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
