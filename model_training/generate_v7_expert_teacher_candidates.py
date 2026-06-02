import argparse
import json
import random
from dataclasses import replace
from pathlib import Path

import train_tiny32_v5_visual_subclass_scan as train


OLD_PARENT_NAME = "p100_head_parent_s4_integrated_084_a5024"
RESCUE_CTD_NAME = "ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int"


def candidate_items(path: Path) -> list[dict[str, object]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(data, list):
        return [dict(item) for item in data]
    raw = data.get("candidates", [])
    if not isinstance(raw, list):
        raise ValueError(f"no candidates list in {path}")
    return [dict(item) for item in raw]


def load_named_config(path: Path, name: str) -> train.V5Config:
    for index, item in enumerate(candidate_items(path)):
        config_data = item.get("config", item)
        if not isinstance(config_data, dict):
            continue
        label = str(item.get("label") or config_data.get("name") or f"candidate_{index:03d}")
        if label == name or config_data.get("name") == name:
            return train.config_from_dict(config_data, label)
    raise ValueError(f"candidate not found: {name} in {path}")


def board_us(config: train.V5Config) -> int:
    return int(train.config_to_dict(config)["estimated_board_us"])


def add_unique(
    pool: list[dict[str, object]],
    seen: set[tuple[object, ...]],
    config: train.V5Config,
    role: str,
    source: str,
    max_board_us: int,
) -> None:
    if board_us(config) > max_board_us:
        return
    key = train.semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    pool.append(
        {
            "config": config,
            "role": role,
            "source": source,
            "estimated_board_us": board_us(config),
        }
    )


def mutate_common(
    base: train.V5Config,
    name: str,
    *,
    filters: tuple[int, int, int] | None = None,
    lr: float | None = None,
    l2: float | None = None,
    dropout: float | None = None,
    augment_name: str | None = None,
    parent_loss_weight: float | None = None,
    teacher_loss_weight: float | None = None,
    head: str | None = None,
    calibration: str | None = None,
    class_weight: str | None = None,
    **extra: object,
) -> train.V5Config:
    kwargs: dict[str, object] = {
        "name": name,
        "filters": filters if filters is not None else base.filters,
        "learning_rate": lr if lr is not None else base.learning_rate,
        "l2": l2 if l2 is not None else base.l2,
        "dropout": dropout if dropout is not None else base.dropout,
        "augment": train.ALL_AUGMENTS[augment_name] if augment_name is not None else base.augment,
        "parent_loss_weight": parent_loss_weight if parent_loss_weight is not None else base.parent_loss_weight,
        "teacher_loss_weight": teacher_loss_weight if teacher_loss_weight is not None else base.teacher_loss_weight,
        "head": head if head is not None else base.head,
        "calibration": calibration if calibration is not None else base.calibration,
        "class_weight": class_weight if class_weight is not None else base.class_weight,
    }
    kwargs.update(extra)
    return replace(base, **kwargs)


def stable_pool(old: train.V5Config, max_board_us: int, seed: int) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    rng = random.Random(seed)
    filter_space = [
        old.filters,
        (8, 16, 32),
        (10, 18, 36),
        (10, 20, 40),
        (12, 24, 48),
    ]

    for teacher_weight in (0.0, 0.05, 0.10, 0.18, 0.28):
        for parent_weight in (2.0, 2.6, 3.2):
            for l2 in (1.0e-4, 1.5e-4):
                config = mutate_common(
                    old,
                    f"v7stable_parent_tw{teacher_weight:g}_pw{parent_weight:g}_l2{l2:g}_mild",
                    head="parent",
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=parent_weight,
                    l2=l2,
                    augment_name="v6_camera_mild",
                    calibration="balanced_clean",
                    class_weight="none",
                )
                add_unique(pool, seen, config, "stable", "old_anchor_parent_teacher_axis", max_board_us)

    for teacher_weight in (0.05, 0.10, 0.18, 0.28):
        for parent_weight in (2.0, 2.6, 3.2):
            config = mutate_common(
                old,
                f"v7stable_blur_tw{teacher_weight:g}_pw{parent_weight:g}",
                head="parent",
                teacher_loss_weight=teacher_weight,
                parent_loss_weight=parent_weight,
                l2=1.5e-4,
                augment_name="v6_camera_blur_noise",
                calibration="balanced_clean",
                class_weight="none",
            )
            add_unique(pool, seen, config, "stable", "camera_blur_noise_preserve", max_board_us)

    for filters in filter_space:
        for teacher_weight in (0.05, 0.10, 0.18):
            for augment_name in ("v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed"):
                config = mutate_common(
                    old,
                    f"v7stable_f{'x'.join(map(str, filters))}_t{teacher_weight:g}_{augment_name}",
                    head="parent",
                    filters=filters,
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=2.6,
                    l2=1.0e-4,
                    augment_name=augment_name,
                    calibration="balanced_clean",
                    class_weight="none",
                )
                add_unique(pool, seen, config, "stable", "width_aug_preserve", max_board_us)

    for calibration in ("balanced_clean", "mild_stress", "balanced_rotmirror", "hard_stress"):
        for teacher_weight in (0.05, 0.10, 0.18):
            config = mutate_common(
                old,
                f"v7stable_cal_{calibration}_t{teacher_weight:g}",
                head="parent",
                teacher_loss_weight=teacher_weight,
                parent_loss_weight=2.6,
                l2=1.5e-4,
                augment_name="v6_camera_mild",
                calibration=calibration,
                class_weight="none",
            )
            add_unique(pool, seen, config, "stable", "quant_calibration_axis", max_board_us)

    for index in range(36):
        filters = rng.choice(filter_space)
        teacher_weight = rng.choice([0.03, 0.05, 0.08, 0.10, 0.14, 0.18, 0.24])
        parent_weight = rng.choice([1.8, 2.2, 2.6, 3.0, 3.4])
        augment_name = rng.choice(["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_lowres"])
        config = mutate_common(
            old,
            f"v7stable_rand{index:02d}_f{'x'.join(map(str, filters))}_t{teacher_weight:g}_p{parent_weight:g}_{augment_name}",
            head="parent",
            filters=filters,
            teacher_loss_weight=teacher_weight,
            parent_loss_weight=parent_weight,
            l2=rng.choice([8.0e-5, 1.0e-4, 1.5e-4, 2.0e-4]),
            dropout=rng.choice([0.0, 0.003, 0.006]),
            augment_name=augment_name,
            calibration=rng.choice(["balanced_clean", "mild_stress", "balanced_rotmirror"]),
            class_weight="none",
        )
        add_unique(pool, seen, config, "stable", "seeded_diversity", max_board_us)

    return pool


def rescue_pool(ctd: train.V5Config, old: train.V5Config, max_board_us: int, seed: int) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    rng = random.Random(seed + 17)
    filter_space = [
        ctd.filters,
        old.filters,
        (8, 16, 32),
        (10, 18, 36),
        (10, 20, 40),
        (12, 24, 48),
    ]

    for teacher_weight in (0.10, 0.18, 0.28, 0.40, 0.55):
        for parent_weight in (2.0, 2.6, 3.2, 4.0):
            for aux_scale in (0.35, 0.65, 1.00):
                config = mutate_common(
                    ctd,
                    f"v7rescue_aux_t{teacher_weight:g}_p{parent_weight:g}_aux{aux_scale:g}",
                    head="parent_weapon_c4_box_circuit",
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=parent_weight,
                    weapon_loss_weight=0.08 * aux_scale,
                    c4_loss_weight=0.28 * aux_scale,
                    c4_box_loss_weight=0.14 * aux_scale,
                    c4_circuit_loss_weight=0.22 * aux_scale,
                    c4_instance_loss_weight=0.0,
                    c4_pos_weight=4.5,
                    c4_box_pos_weight=4.5,
                    c4_circuit_pos_weight=9.0,
                    l2=1.0e-4,
                    augment_name="v6_camera_mild",
                    calibration="balanced_clean",
                    class_weight="none",
                    decoupled=False,
                )
                add_unique(pool, seen, config, "rescue", "ctd_aux_teacher_axis", max_board_us)

    for teacher_weight in (0.18, 0.28, 0.40, 0.55):
        for parent_weight in (2.6, 3.2, 4.0):
            config = mutate_common(
                ctd,
                f"v7rescue_blur_t{teacher_weight:g}_p{parent_weight:g}",
                head="parent_weapon_c4_box_circuit",
                teacher_loss_weight=teacher_weight,
                parent_loss_weight=parent_weight,
                weapon_loss_weight=0.04,
                c4_loss_weight=0.25,
                c4_box_loss_weight=0.12,
                c4_circuit_loss_weight=0.24,
                c4_instance_loss_weight=0.0,
                l2=1.5e-4,
                augment_name="v6_camera_blur_noise",
                calibration="balanced_clean",
                class_weight="none",
                decoupled=False,
            )
            add_unique(pool, seen, config, "rescue", "camera_blur_noise_rescue", max_board_us)

    for teacher_weight in (0.18, 0.28, 0.40):
        for parent_weight in (2.6, 3.2, 4.0):
            config = mutate_common(
                old,
                f"v7rescue_parentonly_t{teacher_weight:g}_p{parent_weight:g}",
                head="parent",
                teacher_loss_weight=teacher_weight,
                parent_loss_weight=parent_weight,
                l2=1.5e-4,
                augment_name="v6_camera_blur_noise",
                calibration="balanced_clean",
                class_weight="none",
            )
            add_unique(pool, seen, config, "rescue", "parent_only_high_weight_rescue", max_board_us)

    for filters in filter_space:
        for teacher_weight in (0.18, 0.28, 0.40):
            config = mutate_common(
                ctd,
                f"v7rescue_f{'x'.join(map(str, filters))}_t{teacher_weight:g}",
                head="parent_weapon_c4_box_circuit",
                filters=filters,
                teacher_loss_weight=teacher_weight,
                parent_loss_weight=3.2,
                weapon_loss_weight=0.06,
                c4_loss_weight=0.24,
                c4_box_loss_weight=0.12,
                c4_circuit_loss_weight=0.26,
                c4_instance_loss_weight=0.0,
                l2=1.0e-4,
                augment_name="v6_camera_mild",
                calibration="balanced_clean",
                class_weight="none",
                decoupled=False,
            )
            add_unique(pool, seen, config, "rescue", "width_rescue_axis", max_board_us)

    for teacher_weight in (0.18, 0.28, 0.40):
        config = mutate_common(
            ctd,
            f"v7rescue_decoupled_t{teacher_weight:g}",
            head="parent_weapon_c4_box_circuit",
            teacher_loss_weight=teacher_weight,
            parent_loss_weight=3.2,
            weapon_loss_weight=0.05,
            c4_loss_weight=0.28,
            c4_box_loss_weight=0.12,
            c4_circuit_loss_weight=0.30,
            c4_instance_loss_weight=0.0,
            l2=1.0e-4,
            augment_name="v6_camera_mild",
            calibration="balanced_clean",
            class_weight="none",
            decoupled=True,
            decouple_parent_epochs=70,
            decouple_aux_epochs=35,
            decouple_joint_lr_scale=0.20,
        )
        add_unique(pool, seen, config, "rescue", "decoupled_aux_probe", max_board_us)

    for index in range(54):
        filters = rng.choice(filter_space)
        teacher_weight = rng.choice([0.12, 0.18, 0.24, 0.28, 0.35, 0.40, 0.50])
        parent_weight = rng.choice([2.2, 2.6, 3.0, 3.4, 4.0, 4.6])
        aux_scale = rng.choice([0.20, 0.35, 0.50, 0.70, 0.90])
        augment_name = rng.choice(["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_hard"])
        head = rng.choice(["parent_weapon_c4_box_circuit", "parent_weapon_c4", "parent_c4_box_circuit", "parent"])
        base = ctd if head != "parent" else old
        config = mutate_common(
            base,
            f"v7rescue_rand{index:02d}_{head}_f{'x'.join(map(str, filters))}_t{teacher_weight:g}_p{parent_weight:g}_{augment_name}",
            head=head,
            filters=filters,
            teacher_loss_weight=teacher_weight,
            parent_loss_weight=parent_weight,
            weapon_loss_weight=0.08 * aux_scale,
            c4_loss_weight=0.32 * aux_scale,
            c4_box_loss_weight=0.16 * aux_scale,
            c4_circuit_loss_weight=0.30 * aux_scale,
            c4_instance_loss_weight=0.0,
            c4_pos_weight=rng.choice([4.0, 5.0, 6.0]),
            c4_box_pos_weight=rng.choice([4.0, 5.0, 6.0]),
            c4_circuit_pos_weight=rng.choice([8.0, 10.0, 12.0]),
            l2=rng.choice([8.0e-5, 1.0e-4, 1.5e-4, 2.0e-4]),
            dropout=rng.choice([0.0, 0.003, 0.006]),
            augment_name=augment_name,
            calibration=rng.choice(["balanced_clean", "mild_stress", "balanced_rotmirror"]),
            class_weight="none",
            decoupled=False,
        )
        add_unique(pool, seen, config, "rescue", "seeded_diversity", max_board_us)

    return pool


def priority(item: dict[str, object]) -> tuple[float, int, str]:
    config = item["config"]
    assert isinstance(config, train.V5Config)
    us = int(item["estimated_board_us"])
    teacher = float(config.teacher_loss_weight)
    parent = float(config.parent_loss_weight)
    aug_bonus = {
        "v6_camera_mild": -0.030,
        "v6_camera_blur_noise": -0.024,
        "sdiag_speed": -0.012,
        "sdiag_soft": -0.004,
    }.get(config.augment.name, 0.008)
    speed_penalty = max(0, us - 8500) / 250000.0
    role = str(item["role"])
    if role == "stable":
        teacher_penalty = abs(teacher - 0.10) * 0.060
        parent_penalty = abs(parent - 2.6) * 0.010
    else:
        teacher_penalty = abs(teacher - 0.28) * 0.050
        parent_penalty = abs(parent - 3.2) * 0.008
    return (teacher_penalty + parent_penalty + aug_bonus + speed_penalty, us, config.name)


def write_candidates(path: Path, items: list[dict[str, object]], limit: int, role: str, source_files: dict[str, str]) -> None:
    selected = sorted(items, key=priority)[:limit]
    payload = {
        "meta": {
            "generator": "generate_v7_expert_teacher_candidates.py",
            "role": role,
            "source_files": source_files,
            "candidate_pool": len(items),
            "selected": len(selected),
            "selection": "priority sorted, <= max board us, diverse axes around V6 parent100 and CTD anchors",
        },
        "candidates": [
            {
                "label": item["config"].name,  # type: ignore[union-attr]
                "role": item["role"],
                "source": item["source"],
                "estimated_board_us": item["estimated_board_us"],
                "config": train.config_to_dict(item["config"]),  # type: ignore[arg-type]
            }
            for item in selected
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {len(selected)} {role} candidates to {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate V7 expert-teacher first-stage candidate sets.")
    parser.add_argument("--parent-candidates", type=Path, required=True)
    parser.add_argument("--ctd-candidates", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stable-limit", type=int, default=64)
    parser.add_argument("--rescue-limit", type=int, default=80)
    parser.add_argument("--max-board-us", type=int, default=12000)
    parser.add_argument("--seed", type=int, default=20263400)
    args = parser.parse_args()

    old = load_named_config(args.parent_candidates, OLD_PARENT_NAME)
    ctd = load_named_config(args.ctd_candidates, RESCUE_CTD_NAME)
    sources = {
        "parent_candidates": str(args.parent_candidates),
        "ctd_candidates": str(args.ctd_candidates),
        "old_anchor": OLD_PARENT_NAME,
        "rescue_anchor": RESCUE_CTD_NAME,
    }
    write_candidates(
        args.output_dir / "stable_teacher_candidates.json",
        stable_pool(old, args.max_board_us, args.seed),
        args.stable_limit,
        "stable",
        sources,
    )
    write_candidates(
        args.output_dir / "rescue_teacher_candidates.json",
        rescue_pool(ctd, old, args.max_board_us, args.seed),
        args.rescue_limit,
        "rescue",
        sources,
    )


if __name__ == "__main__":
    main()
