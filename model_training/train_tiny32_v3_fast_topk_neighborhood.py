import argparse
import copy
import json
import math
import random
import time
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
from train_tiny32_v3_fine_scan import DEFAULT_STRESS, final_score, safe_name, summarize_stress_min


ANCHORS = {
    "rank02_best": {
        "tag": "rank02_best",
        "filters": (8, 16, 32),
        "dense": 0,
        "dropout": 0.0,
        "l2": 3.0e-4,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": True,
        "augment": "v3_lowres_noise",
        "target_us": 9200,
        "max_us": 12000,
    },
    "rank03_speed": {
        "tag": "rank03_speed",
        "filters": (8, 16, 32),
        "dense": 16,
        "dropout": 0.0,
        "l2": 0.0,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": False,
        "augment": "v3_lowres_noise",
        "target_us": 7600,
        "max_us": 10500,
    },
    "rank01_low_l2": {
        "tag": "rank01_low_l2",
        "filters": (8, 16, 32),
        "dense": 0,
        "dropout": 0.0,
        "l2": 1.0e-5,
        "lr": 2.0e-3,
        "batch": 32,
        "pool": "max",
        "kernel": 3,
        "extra": True,
        "augment": "v3_lowres_noise",
        "target_us": 9200,
        "max_us": 12000,
    },
}


FILTER_NEIGHBORS = {
    "rank02_best": [(8, 16, 32), (8, 16, 40), (8, 20, 32), (10, 16, 32), (10, 20, 40), (6, 12, 24)],
    "rank03_speed": [(8, 16, 32), (8, 16, 24), (8, 16, 40), (6, 12, 24), (10, 16, 32), (10, 20, 40)],
    "rank01_low_l2": [(8, 16, 32), (8, 16, 40), (8, 20, 32), (10, 16, 32), (10, 20, 40), (6, 12, 24)],
}


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
        f"v3fastnear_{prefix}_{tag}_f{'-'.join(map(str, filters))}_d{dense}_do{dropout:g}"
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


def anchor_config(anchor_name: str, prefix: str) -> ModelConfig:
    anchor = ANCHORS[anchor_name]
    return make_config(
        prefix,
        str(anchor["tag"]),
        tuple(anchor["filters"]),
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


def l2_distance(lhs: float, rhs: float) -> float:
    if lhs <= 0 and rhs <= 0:
        return 0.0
    if lhs <= 0 or rhs <= 0:
        return 0.75
    return min(1.0, abs(math.log10(lhs / rhs)) / 3.0)


def config_distance(config: ModelConfig, anchor_name: str, rng: random.Random) -> tuple[float, int, str]:
    anchor = ANCHORS[anchor_name]
    anchor_filters = tuple(anchor["filters"])
    latency = estimate_board_latency_us(config)
    target_us = int(anchor["target_us"])
    filter_distance = sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, anchor_filters)) / 3.0
    dense_distance = abs(config.dense_units - int(anchor["dense"])) / 32.0
    dropout_distance = abs(config.dropout - float(anchor["dropout"])) * 2.5
    lr_distance = min(1.0, abs(math.log(config.learning_rate / float(anchor["lr"]))) / 1.2)
    batch_penalty = 0.0 if config.batch_size == int(anchor["batch"]) else 0.05
    pool_penalty = 0.0 if config.pool == str(anchor["pool"]) else 0.08
    extra_penalty = 0.0 if config.extra_conv == bool(anchor["extra"]) else 0.06
    augment_penalty = {
        str(anchor["augment"]): 0.0,
        "v3_speed_noise": 0.04,
        "v3_aggressive_noise": 0.08,
    }.get(config.augment.name, 0.10)
    latency_penalty = abs(latency - target_us) / 9000.0
    score = (
        1.30 * filter_distance
        + 0.85 * dense_distance
        + dropout_distance
        + 0.50 * l2_distance(config.l2, float(anchor["l2"]))
        + 0.40 * lr_distance
        + batch_penalty
        + pool_penalty
        + extra_penalty
        + augment_penalty
        + latency_penalty
        + rng.random() * 1.0e-4
    )
    return score, latency, config.name


def neighborhood_configs(anchor_name: str, limit: int, seed: int) -> list[ModelConfig]:
    anchor = ANCHORS[anchor_name]
    rng = random.Random(seed)
    configs: list[ModelConfig] = []
    seen: set[str] = set()

    def add(config: ModelConfig) -> None:
        if config.name in seen:
            return
        if estimate_board_latency_us(config) <= int(anchor["max_us"]):
            configs.append(config)
            seen.add(config.name)

    add(anchor_config(anchor_name, anchor_name))

    if anchor_name == "rank03_speed":
        dense_values = [0, 8, 12, 16, 24]
        dropout_values = [0.0, 0.03, 0.05, 0.08, 0.10]
        l2_values = [0.0, 1.0e-6, 1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4]
        extras = [False, True]
        batches = [16, 32, 64]
    elif anchor_name == "rank01_low_l2":
        dense_values = [0, 4, 8, 16]
        dropout_values = [0.0, 0.03, 0.05, 0.08, 0.10]
        l2_values = [0.0, 1.0e-6, 1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4, 5.0e-4]
        extras = [True, False]
        batches = [16, 32, 64]
    else:
        dense_values = [0, 4, 8, 16]
        dropout_values = [0.0, 0.03, 0.05, 0.08, 0.10]
        l2_values = [0.0, 1.0e-5, 1.0e-4, 2.0e-4, 3.0e-4, 5.0e-4, 8.0e-4]
        extras = [True, False]
        batches = [16, 32, 64]

    candidates: list[ModelConfig] = []
    for filters in FILTER_NEIGHBORS[anchor_name]:
        for dense in dense_values:
            for dropout in dropout_values:
                for l2 in l2_values:
                    for lr in [1.0e-3, 1.5e-3, 2.0e-3, 2.5e-3, 3.0e-3]:
                        for batch in batches:
                            for pool in ["max", "avg"]:
                                for extra in extras:
                                    for augment in ["v3_lowres_noise", "v3_speed_noise", "v3_aggressive_noise"]:
                                        config = make_config(
                                            anchor_name,
                                            "neighbor",
                                            filters,
                                            dense,
                                            dropout,
                                            l2,
                                            lr,
                                            batch,
                                            pool,
                                            3,
                                            extra,
                                            augment,
                                        )
                                        latency = estimate_board_latency_us(config)
                                        if 6000 <= latency <= int(anchor["max_us"]):
                                            candidates.append(config)

    candidates.sort(key=lambda item: config_distance(item, anchor_name, rng))
    for config in candidates:
        add(config)
        if len(configs) >= limit:
            break
    return configs[:limit]


def main() -> None:
    parser = argparse.ArgumentParser(description="Fine neighborhood scan around fast top-k v3 models.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-root", default="experiments")
    parser.add_argument("--output-dir")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--anchor", choices=sorted(ANCHORS), required=True)
    parser.add_argument("--max-trials", type=int, default=64)
    parser.add_argument("--folds", type=int, default=3)
    parser.add_argument("--epochs", type=int, default=170)
    parser.add_argument("--patience", type=int, default=22)
    parser.add_argument("--final-epochs", type=int, default=280)
    parser.add_argument("--final-patience", type=int, default=38)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260520)
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--speed-weight", type=float, default=0.10)
    parser.add_argument("--final-top-k", type=int, default=5)
    args = parser.parse_args()

    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    output_dir = (
        Path(args.output_dir)
        if args.output_dir
        else Path(args.output_root) / time.strftime(f"v3_fast_near_{args.anchor}_%Y%m%d_%H%M%S")
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    folds = stratified_folds(y_sub, args.folds, args.seed)
    configs = neighborhood_configs(args.anchor, args.max_trials, args.seed)
    run_config = {
        "args": vars(args),
        "anchor": ANCHORS[args.anchor],
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
        f"anchor={args.anchor} trials={len(configs)} pending={pending_count} "
        f"completed={len(completed_trials)} folds={len(folds)} speed_weight={args.speed_weight}",
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
