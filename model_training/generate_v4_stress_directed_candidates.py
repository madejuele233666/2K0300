import argparse
import json
import math
import random
from dataclasses import replace
from pathlib import Path

from train_tiny32_sixclass_scan import AugmentConfig, config_to_dict, estimate_board_latency_us
from train_tiny32_v4_mild_calib_scan import CUSTOM_AUGMENTS, augment_by_name, make_config, semantic_key


BASE_AUG = augment_by_name("v3_lowres_noise")


def aug_variant(
    name: str,
    *,
    pad: int | None = None,
    brightness: float | None = None,
    contrast_delta: float | None = None,
    noise_std: float | None = None,
    salt_pepper: float | None = None,
    blur_prob: float | None = None,
    blur_kernel: int | None = None,
    downscale_prob: float | None = None,
    downscale_min: int | None = None,
) -> AugmentConfig:
    return replace(
        BASE_AUG,
        name=name,
        pad=BASE_AUG.pad if pad is None else pad,
        brightness=BASE_AUG.brightness if brightness is None else brightness,
        contrast_delta=BASE_AUG.contrast_delta if contrast_delta is None else contrast_delta,
        noise_std=BASE_AUG.noise_std if noise_std is None else noise_std,
        salt_pepper=BASE_AUG.salt_pepper if salt_pepper is None else salt_pepper,
        blur_prob=BASE_AUG.blur_prob if blur_prob is None else blur_prob,
        blur_kernel=BASE_AUG.blur_kernel if blur_kernel is None else blur_kernel,
        downscale_prob=BASE_AUG.downscale_prob if downscale_prob is None else downscale_prob,
        downscale_min=BASE_AUG.downscale_min if downscale_min is None else downscale_min,
    )


def augment_options() -> list[AugmentConfig]:
    return [
        aug_variant("sdiag_base"),
        aug_variant("sdiag_soft", blur_prob=0.34, blur_kernel=5, noise_std=0.042, downscale_prob=0.46, downscale_min=16),
        aug_variant("sdiag_mid", blur_prob=0.42, blur_kernel=5, noise_std=0.050, salt_pepper=0.008, downscale_prob=0.46, downscale_min=16),
        aug_variant("sdiag_hard", blur_prob=0.52, blur_kernel=5, noise_std=0.060, salt_pepper=0.010, brightness=0.14, contrast_delta=0.22, downscale_prob=0.40, downscale_min=17),
        aug_variant("sdiag_lowres", blur_prob=0.38, blur_kernel=5, noise_std=0.044, downscale_prob=0.60, downscale_min=14),
        aug_variant("sdiag_roi", pad=1, brightness=0.10, contrast_delta=0.16, noise_std=0.038, blur_prob=0.36, blur_kernel=5, downscale_prob=0.48, downscale_min=16),
        replace(CUSTOM_AUGMENTS["v4_speed_mild"], name="sdiag_speed"),
        replace(CUSTOM_AUGMENTS["v4_lowres_mix"], name="sdiag_lowmix"),
    ]


def make_stress_config(
    tag: str,
    filters: tuple[int, int, int],
    lr: float,
    l2: float,
    dropout: float,
    batch: int,
    pool: str,
    activation: str,
    augment: AugmentConfig,
):
    config = make_config(
        "sdiag",
        tag,
        "spacetodepth_conv",
        filters,
        0,
        dropout,
        l2,
        lr,
        batch,
        pool,
        3,
        False,
        "v3_lowres_noise",
        activation,
    )
    return replace(config, augment=augment)


def priority(config, rng: random.Random) -> float:
    filter_targets = [(8, 16, 32), (6, 12, 24)]
    filter_penalty = min(
        sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, target)) / 3.0 for target in filter_targets
    )
    lr_penalty = min(
        abs(math.log(config.learning_rate / 0.00318)) / 0.34,
        abs(math.log(config.learning_rate / 0.00286)) / 0.34 + 0.03,
    )
    l2_penalty = abs(math.log(config.l2 / 1.0e-4)) / 1.7
    dropout_penalty = config.dropout * 7.0
    batch_penalty = 0.0 if config.batch_size == 16 else 0.025
    pool_penalty = 0.0 if config.pool == "max" else 0.045
    activation_penalty = 0.0 if config.activation == "relu" else 0.055
    augment_penalty = {
        "sdiag_mid": 0.000,
        "sdiag_soft": 0.008,
        "sdiag_roi": 0.012,
        "sdiag_base": 0.018,
        "sdiag_lowres": 0.020,
        "sdiag_speed": 0.026,
        "sdiag_lowmix": 0.030,
        "sdiag_hard": 0.036,
    }.get(config.augment.name, 0.05)
    latency = estimate_board_latency_us(config)
    latency_penalty = max(0, latency - 8200) / 9000 + max(0, 5100 - latency) / 12000
    return (
        0.44 * filter_penalty
        + 0.34 * lr_penalty
        + 0.24 * l2_penalty
        + dropout_penalty
        + batch_penalty
        + pool_penalty
        + activation_penalty
        + augment_penalty
        + latency_penalty
        + rng.random() * 1.0e-4
    )


def generate_candidates(limit: int, seed: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    augments = augment_options()
    by_aug = {aug.name: aug for aug in augments}
    filters_options = [(6, 12, 24), (8, 16, 32), (10, 18, 36)]
    lr_options = [0.00278, 0.00286, 0.00294, 0.00302, 0.00310, 0.00318, 0.00326, 0.00334]
    l2_options = [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4]
    dropout_options = [0.0, 0.003, 0.005, 0.008]
    batch_options = [16, 24]
    pool_options = ["max", "avg"]
    activation_options = ["relu", "relu6"]

    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()

    def add(config, reason: str) -> None:
        if estimate_board_latency_us(config) > 9800:
            return
        key = semantic_key(config)
        if key in seen:
            return
        seen.add(key)
        selected.append({"config": config, "reason": reason})

    add(make_stress_config("base000", (8, 16, 32), 0.00286, 1.0e-4, 0.0, 16, "max", "relu", by_aug["sdiag_base"]), "base_stab000")
    add(make_stress_config("base007", (8, 16, 32), 0.00318, 1.0e-4, 0.0, 16, "max", "relu", by_aug["sdiag_base"]), "base_stab007")
    add(make_stress_config("base024", (6, 12, 24), 0.00286, 1.0e-4, 0.0, 16, "max", "relu", by_aug["sdiag_base"]), "base_stab024")

    for base_lr in [0.00286, 0.00318]:
        for lr in lr_options:
            add(make_stress_config("lr", (8, 16, 32), lr, 1.0e-4, 0.0, 16, "max", "relu", by_aug["sdiag_base"]), "lr_axis")
        for l2 in l2_options:
            add(make_stress_config("l2", (8, 16, 32), base_lr, l2, 0.0, 16, "max", "relu", by_aug["sdiag_base"]), "l2_axis")
        for dropout in dropout_options:
            add(make_stress_config("drop", (8, 16, 32), base_lr, 1.0e-4, dropout, 16, "max", "relu", by_aug["sdiag_base"]), "dropout_axis")
        for augment in augments:
            add(make_stress_config("aug", (8, 16, 32), base_lr, 1.0e-4, 0.0, 16, "max", "relu", augment), "augment_axis")
        for pool in pool_options:
            add(make_stress_config("pool", (8, 16, 32), base_lr, 1.0e-4, 0.0, 16, pool, "relu", by_aug["sdiag_base"]), "pool_axis")
        for activation in activation_options:
            add(make_stress_config("act", (8, 16, 32), base_lr, 1.0e-4, 0.0, 16, "max", activation, by_aug["sdiag_base"]), "activation_axis")

    pool = []
    for filters in filters_options:
        for lr in lr_options:
            for l2 in l2_options:
                for dropout in dropout_options:
                    for batch in batch_options:
                        for pool_name in pool_options:
                            for activation in activation_options:
                                for augment in augments:
                                    config = make_stress_config(
                                        "grid",
                                        filters,
                                        lr,
                                        l2,
                                        dropout,
                                        batch,
                                        pool_name,
                                        activation,
                                        augment,
                                    )
                                    if estimate_board_latency_us(config) <= 9800:
                                        pool.append(config)
    rng.shuffle(pool)
    pool.sort(key=lambda config: priority(config, rng))
    for config in pool:
        add(config, "priority_grid")
        if len(selected) >= limit:
            break

    candidates = []
    for index, item in enumerate(selected[:limit]):
        config = item["config"]
        label = f"sd{index:03d}"
        config = replace(config, name=label)
        candidates.append(
            {
                "label": label,
                "source": {
                    "reason": item["reason"],
                    "generator": "generate_v4_stress_directed_candidates.py",
                    "filters": list(config.filters),
                    "learning_rate": config.learning_rate,
                    "l2": config.l2,
                    "dropout": config.dropout,
                    "batch_size": config.batch_size,
                    "pool": config.pool,
                    "activation": config.activation,
                    "augment": config.augment.name,
                },
                "config": config_to_dict(config),
                "estimated_board_us": estimate_board_latency_us(config),
            }
        )
    return candidates


def rank_value(row: dict[str, object]) -> tuple[float, float, float, float, float, float]:
    score = float(row["score_mean"])
    stress = float(row["stress_min_mean"])
    stress_min = float(row["stress_min_min"])
    test_worst = float(row["test_worst_min"])
    all_worst = float(row["all_worst_mean"])
    speed = max(0.0, min(1.0, (25000.0 - float(row["us"])) / 25000.0))
    composite = 0.32 * score + 0.30 * stress + 0.16 * stress_min + 0.12 * test_worst + 0.07 * all_worst + 0.03 * speed
    return (composite, stress, score, test_worst, all_worst, -float(row["us"]))


def select_stage_candidates(
    summary_path: Path,
    candidates_path: Path,
    output_path: Path,
    top_k: int,
    force_bases: bool,
) -> None:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    candidates_data = json.loads(candidates_path.read_text(encoding="utf-8"))
    by_label = {item["label"]: item for item in candidates_data["candidates"]}
    ranked = sorted(summary["summary"], key=rank_value, reverse=True)
    selected = []
    seen = set()
    if force_bases:
        for reason in ["base_stab000", "base_stab007", "base_stab024"]:
            for item in candidates_data["candidates"]:
                if item["source"].get("reason") == reason and item["label"] not in seen:
                    selected.append(item)
                    seen.add(item["label"])
                    break
    for row in ranked:
        label = row["label"]
        if label in seen or label not in by_label:
            continue
        selected.append(by_label[label])
        seen.add(label)
        if len(selected) >= top_k:
            break
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps({"source_summary": str(summary_path), "candidates": selected}, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(f"selected={len(selected)} output={output_path}")
    for item in selected:
        cfg = item["config"]
        print(
            f"{item['label']} f={cfg['filters']} lr={cfg['learning_rate']} l2={cfg['l2']} "
            f"do={cfg['dropout']} b={cfg['batch_size']} pool={cfg['pool']} act={cfg['activation']} "
            f"aug={cfg['augment']['name']} us={item['estimated_board_us']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate/select V4 stress-directed tiny32 candidates.")
    sub = parser.add_subparsers(dest="cmd", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("--output", required=True, type=Path)
    gen.add_argument("--limit", type=int, default=180)
    gen.add_argument("--seed", type=int, default=20261600)
    sel = sub.add_parser("select")
    sel.add_argument("--summary", required=True, type=Path)
    sel.add_argument("--candidates", required=True, type=Path)
    sel.add_argument("--output", required=True, type=Path)
    sel.add_argument("--top-k", type=int, default=18)
    sel.add_argument("--force-bases", action="store_true")
    args = parser.parse_args()

    if args.cmd == "generate":
        candidates = generate_candidates(args.limit, args.seed)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"candidates": candidates}, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"candidates={len(candidates)} output={args.output}")
        for item in candidates[:24]:
            cfg = item["config"]
            print(
                f"{item['label']} f={cfg['filters']} lr={cfg['learning_rate']} l2={cfg['l2']} "
                f"do={cfg['dropout']} b={cfg['batch_size']} pool={cfg['pool']} act={cfg['activation']} "
                f"aug={cfg['augment']['name']} us={item['estimated_board_us']}"
            )
    elif args.cmd == "select":
        select_stage_candidates(args.summary, args.candidates, args.output, args.top_k, args.force_bases)


if __name__ == "__main__":
    main()
