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
        aug_variant("stab_v3_base"),
        aug_variant("stab_v3_soft_lowres", downscale_prob=0.42, downscale_min=17),
        aug_variant("stab_v3_hard_lowres", downscale_prob=0.58, downscale_min=14),
        aug_variant("stab_v3_noise_plus", noise_std=0.048, salt_pepper=0.008),
        aug_variant("stab_v3_blur5", blur_prob=0.32, blur_kernel=5),
        aug_variant("stab_v3_roi_soft", pad=1, brightness=0.10, contrast_delta=0.16, noise_std=0.034, downscale_prob=0.44, downscale_min=16),
        replace(CUSTOM_AUGMENTS["v4_lowres_mix"], name="stab_v4_lowres_mix"),
        replace(CUSTOM_AUGMENTS["v4_speed_mild"], name="stab_v4_speed_mild"),
    ]


def make_stability_config(
    tag: str,
    filters: tuple[int, int, int],
    lr: float,
    l2: float,
    dropout: float,
    batch: int,
    augment: AugmentConfig,
):
    config = make_config(
        "stability",
        tag,
        "spacetodepth_conv",
        filters,
        0,
        dropout,
        l2,
        lr,
        batch,
        "max",
        3,
        False,
        "v3_lowres_noise",
        "relu",
    )
    return replace(config, augment=augment)


def priority(config, rng: random.Random) -> tuple[float, str]:
    lr_target = 0.00286
    l2_target = 1.0e-4
    filter_penalty = sum(abs(a - b) / b for a, b in zip(config.filters, (8, 16, 32))) / 3.0
    lr_penalty = abs(math.log(config.learning_rate / lr_target)) / 0.35
    l2_penalty = abs(math.log(config.l2 / l2_target)) / 1.6
    dropout_penalty = config.dropout * 9.0
    batch_penalty = 0.0 if config.batch_size == 16 else 0.035
    augment_penalty = {
        "stab_v3_base": 0.00,
        "stab_v3_soft_lowres": 0.015,
        "stab_v3_hard_lowres": 0.040,
        "stab_v3_noise_plus": 0.030,
        "stab_v3_blur5": 0.040,
        "stab_v3_roi_soft": 0.025,
        "stab_v4_lowres_mix": 0.030,
        "stab_v4_speed_mild": 0.045,
    }.get(config.augment.name, 0.06)
    latency = estimate_board_latency_us(config)
    latency_penalty = max(0, latency - 7800) / 9000
    score = (
        0.55 * filter_penalty
        + 0.42 * lr_penalty
        + 0.32 * l2_penalty
        + dropout_penalty
        + batch_penalty
        + augment_penalty
        + latency_penalty
        + rng.random() * 1.0e-4
    )
    return score, config.name


def generate_candidates(limit: int, seed: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    filters_options = [(6, 12, 24), (8, 16, 32), (10, 18, 36), (10, 20, 40)]
    lr_options = [0.00250, 0.00260, 0.00270, 0.00278, 0.00286, 0.00294, 0.00305, 0.00318]
    l2_options = [5.0e-5, 7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4, 3.0e-4]
    dropout_options = [0.0, 0.005, 0.01]
    batch_options = [16, 24]
    augments = augment_options()

    selected = []
    seen: set[tuple[object, ...]] = set()

    def add(config, reason: str) -> None:
        if estimate_board_latency_us(config) > 9800:
            return
        key = semantic_key(config)
        if key in seen:
            return
        seen.add(key)
        selected.append({"config": config, "reason": reason})

    base = make_stability_config("base", (8, 16, 32), 0.00286, 1.0e-4, 0.0, 16, augments[0])
    add(base, "base")
    for lr in lr_options:
        add(make_stability_config("lr", (8, 16, 32), lr, 1.0e-4, 0.0, 16, augments[0]), "lr_axis")
    for l2 in l2_options:
        add(make_stability_config("l2", (8, 16, 32), 0.00286, l2, 0.0, 16, augments[0]), "l2_axis")
    for dropout in dropout_options:
        add(make_stability_config("drop", (8, 16, 32), 0.00286, 1.0e-4, dropout, 16, augments[0]), "dropout_axis")
    for batch in batch_options:
        add(make_stability_config("batch", (8, 16, 32), 0.00286, 1.0e-4, 0.0, batch, augments[0]), "batch_axis")
    for augment in augments:
        add(make_stability_config("aug", (8, 16, 32), 0.00286, 1.0e-4, 0.0, 16, augment), "augment_axis")
    for filters in filters_options:
        add(make_stability_config("filters", filters, 0.00286, 1.0e-4, 0.0, 16, augments[0]), "filters_axis")

    pool = []
    for filters in filters_options:
        for lr in lr_options:
            for l2 in l2_options:
                for dropout in dropout_options:
                    for batch in batch_options:
                        for augment in augments:
                            config = make_stability_config("grid", filters, lr, l2, dropout, batch, augment)
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
        label = f"stab{index:03d}"
        config = replace(config, name=label)
        candidates.append(
            {
                "label": label,
                "source": {
                    "reason": item["reason"],
                    "generator": "generate_v4_stability_candidates.py",
                    "filters": list(config.filters),
                    "learning_rate": config.learning_rate,
                    "l2": config.l2,
                    "dropout": config.dropout,
                    "batch_size": config.batch_size,
                    "augment": config.augment.name,
                },
                "config": config_to_dict(config),
                "estimated_board_us": estimate_board_latency_us(config),
            }
        )
    return candidates


def select_stage_candidates(summary_path: Path, candidates_path: Path, output_path: Path, top_k: int, force_base: bool) -> None:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    candidates_data = json.loads(candidates_path.read_text(encoding="utf-8"))
    by_label = {item["label"]: item for item in candidates_data["candidates"]}
    ranked = sorted(
        summary["summary"],
        key=lambda row: (
            float(row["score_mean"]),
            float(row["test_worst_min"]),
            float(row["all_worst_mean"]),
            float(row["stress_min_mean"]),
            -float(row["us"]),
        ),
        reverse=True,
    )
    selected = []
    seen = set()
    if force_base:
        for item in candidates_data["candidates"]:
            if item["source"].get("reason") == "base":
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
            f"do={cfg['dropout']} b={cfg['batch_size']} aug={cfg['augment']['name']} us={item['estimated_board_us']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate/select fast_x0_lr286 stability candidates.")
    sub = parser.add_subparsers(dest="cmd", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("--output", required=True, type=Path)
    gen.add_argument("--limit", type=int, default=120)
    gen.add_argument("--seed", type=int, default=20261000)
    sel = sub.add_parser("select")
    sel.add_argument("--summary", required=True, type=Path)
    sel.add_argument("--candidates", required=True, type=Path)
    sel.add_argument("--output", required=True, type=Path)
    sel.add_argument("--top-k", type=int, default=12)
    sel.add_argument("--force-base", action="store_true")
    args = parser.parse_args()

    if args.cmd == "generate":
        candidates = generate_candidates(args.limit, args.seed)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"candidates": candidates}, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"candidates={len(candidates)} output={args.output}")
        for item in candidates[:20]:
            cfg = item["config"]
            print(
                f"{item['label']} f={cfg['filters']} lr={cfg['learning_rate']} l2={cfg['l2']} "
                f"do={cfg['dropout']} b={cfg['batch_size']} aug={cfg['augment']['name']} us={item['estimated_board_us']}"
            )
    elif args.cmd == "select":
        select_stage_candidates(args.summary, args.candidates, args.output, args.top_k, args.force_base)


if __name__ == "__main__":
    main()
