import argparse
import json
import random
from dataclasses import replace
from pathlib import Path

from train_tiny32_v5_visual_subclass_scan import V5Config, config_to_dict, make_config, semantic_key


def add_candidate(selected: list[dict[str, object]], seen: set[tuple[object, ...]], config: V5Config, reason: str) -> None:
    key = semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    selected.append({"label": config.name, "config": config_to_dict(config), "source": {"reason": reason, "generator": "generate_v6_parent_primary_candidates.py"}})


def priority(config: V5Config) -> tuple[float, int, str]:
    if "fast" in config.lane:
        target_filters = (6, 12, 24)
        target_us = 5600
    elif "accuracy" in config.lane:
        target_filters = (10, 20, 40)
        target_us = 10500
    else:
        target_filters = (8, 16, 32)
        target_us = 7163
    est_us = int(config_to_dict(config)["estimated_board_us"])
    filter_penalty = sum(abs(a - b) / max(1, b) for a, b in zip(config.filters, target_filters)) / 3.0
    latency_penalty = max(0.0, est_us - target_us) / 18000.0
    family_penalty = {
        "parent_weapon_c4_box_circuit": -0.020,
        "parent_weapon_c4_instance": -0.012,
        "parent_c4_box_circuit": -0.006,
        "parent_c4_instance": 0.000,
        "parent_weapon_c4": 0.00,
        "parent_weapon_aux": 0.025,
        "parent_c4_attr": 0.035,
        "parent": 0.055,
        "dual_parent": 0.075,
    }.get(config.head, 0.10)
    if config.teacher_loss_weight > 0:
        family_penalty -= 0.018
    if config.decoupled:
        family_penalty -= 0.012
    return (
        family_penalty
        + filter_penalty * 0.35
        + latency_penalty
        + abs(config.learning_rate - 0.00318) * 22.0
        + config.dropout * 2.5,
        est_us,
        config.name,
    )


def build_candidates(limit: int, seed: int, max_board_us: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    pool: list[V5Config] = []

    lanes = {
        "v6_fast": {
            "filters": [(5, 10, 20), (6, 12, 24), (7, 14, 28), (8, 16, 24), (8, 16, 32)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00318, 0.00326],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008],
            "augment": ["sdiag_base", "sdiag_soft", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix", "v6_camera_mild"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "batch": [16, 24],
        },
        "v6_balance": {
            "filters": [(7, 14, 28), (8, 16, 24), (8, 16, 32), (8, 18, 36), (10, 18, 36)],
            "lr": [0.00278, 0.00286, 0.00294, 0.00302, 0.00310, 0.00318, 0.00326, 0.00334],
            "l2": [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4, 3.0e-4],
            "dropout": [0.0, 0.003, 0.005, 0.008, 0.012],
            "augment": ["sdiag_base", "sdiag_soft", "sdiag_mid", "sdiag_lowres", "sdiag_speed", "sdiag_roi", "v4_lowres_mix", "v6_camera_mild", "v6_camera_blur_noise"],
            "arch": ["spacetodepth_conv", "depthwise_pool"],
            "batch": [16, 24],
        },
        "v6_accuracy": {
            "filters": [(8, 18, 36), (10, 18, 36), (10, 20, 40), (10, 20, 48), (12, 24, 48)],
            "lr": [0.0016, 0.0020, 0.0023, 0.0026, 0.00286, 0.00318],
            "l2": [1.0e-5, 3.0e-5, 7.0e-5, 1.0e-4, 3.0e-4],
            "dropout": [0.0, 0.003, 0.008, 0.02, 0.05],
            "augment": ["sdiag_soft", "sdiag_mid", "sdiag_hard", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix", "v4_highspeed", "v6_camera_mild", "v6_camera_blur_noise"],
            "arch": ["spacetodepth_conv", "depthwise_pool", "stride_conv", "hardswish_depthwise"],
            "batch": [16, 24],
        },
    }

    def add(config: V5Config) -> None:
        if int(config_to_dict(config)["estimated_board_us"]) <= max_board_us:
            pool.append(config)

    for lane, opts in lanes.items():
        base_filters = (6, 12, 24) if lane == "v6_fast" else ((10, 20, 40) if lane == "v6_accuracy" else (8, 16, 32))
        base_lr = 0.0023 if lane == "v6_accuracy" else 0.00318
        base_aug = "sdiag_mid" if lane == "v6_accuracy" else ("sdiag_base" if lane == "v6_fast" else "sdiag_lowres")

        for head in [
            "parent_weapon_c4_box_circuit",
            "parent_weapon_c4_instance",
            "parent_c4_box_circuit",
            "parent_weapon_c4",
            "parent_weapon_aux",
            "parent_c4_attr",
            "parent",
        ]:
            add(
                make_config(
                    f"{lane}_{head}_anchor",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    base_aug,
                    head=head,
                    calibration="mild_stress",
                )
            )

        for weight in [0.05, 0.10, 0.20]:
            add(
                make_config(
                    f"{lane}_A2_weapon_w{weight:g}",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    base_aug,
                    head="parent_weapon_aux",
                    weapon_loss_weight=weight,
                )
            )
        for c4_weight in [0.15, 0.30, 0.60]:
            for pos_weight in [2.0, 4.0, 8.0]:
                add(
                    make_config(
                        f"{lane}_B_c4_w{c4_weight:g}_pos{pos_weight:g}",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        base_lr,
                        1.0e-4,
                        0.003,
                        base_aug,
                        head="parent_c4_attr",
                        c4_loss_weight=c4_weight,
                        c4_pos_weight=pos_weight,
                    )
                )
        for box_weight in [0.10, 0.20, 0.40]:
            for circuit_weight in [0.20, 0.40, 0.80]:
                add(
                    make_config(
                        f"{lane}_B_closed_box{box_weight:g}_circuit{circuit_weight:g}",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        base_lr,
                        1.0e-4,
                        0.003,
                        "v6_camera_mild" if lane != "v6_fast" else base_aug,
                        head="parent_weapon_c4_box_circuit",
                        c4_loss_weight=0.30,
                        c4_box_loss_weight=box_weight,
                        c4_circuit_loss_weight=circuit_weight,
                        c4_circuit_pos_weight=8.0,
                    )
                )
        for instance_weight in [0.03, 0.08]:
            add(
                make_config(
                    f"{lane}_B_closed_instance_w{instance_weight:g}",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    "v6_camera_mild" if lane != "v6_fast" else base_aug,
                    head="parent_weapon_c4_instance",
                    c4_loss_weight=0.30,
                    c4_box_loss_weight=0.20,
                    c4_circuit_loss_weight=0.40,
                    c4_instance_loss_weight=instance_weight,
                )
            )
        for gamma in [1.0, 2.0]:
            add(
                make_config(
                    f"{lane}_F_c4_focal_g{gamma:g}",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    base_aug,
                    head="parent_weapon_c4",
                    c4_loss_weight=0.30,
                    c4_pos_weight=4.0,
                    c4_focal_gamma=gamma,
                )
            )

        for alpha in [0.10, 0.30, 0.60]:
            for temp in [2.0, 4.0]:
                add(
                    make_config(
                        f"{lane}_C_teacher_closed_a{alpha:g}_t{temp:g}",
                        lane,
                        "spacetodepth_conv",
                        base_filters,
                        base_lr,
                        1.0e-4,
                        0.003,
                        "v6_camera_mild" if lane != "v6_fast" else base_aug,
                        head="parent_weapon_c4_box_circuit",
                        teacher_loss_weight=alpha,
                        teacher_temperature=temp,
                        c4_teacher_scale=1.0,
                        weapon_loss_weight=0.10,
                        c4_loss_weight=0.30,
                        c4_box_loss_weight=0.20,
                        c4_circuit_loss_weight=0.40,
                    )
                )

        for parent_epochs, aux_epochs in [(24, 16), (40, 24), (56, 32)]:
            add(
                make_config(
                    f"{lane}_E_decoupled_p{parent_epochs}_a{aux_epochs}",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    base_aug,
                    head="parent_weapon_c4_instance",
                    decoupled=True,
                    decouple_parent_epochs=parent_epochs,
                    decouple_aux_epochs=aux_epochs,
                    decouple_joint_lr_scale=0.25,
                    weapon_loss_weight=0.10,
                    c4_loss_weight=0.30,
                    c4_box_loss_weight=0.20,
                    c4_circuit_loss_weight=0.40,
                    c4_instance_loss_weight=0.03,
                )
            )

        for filters in opts["filters"]:
            for head in ["parent_weapon_c4_box_circuit", "parent_weapon_c4_instance", "parent_weapon_c4", "parent_weapon_aux", "parent_c4_attr"]:
                add(
                    make_config(
                        f"{lane}_filters_{'-'.join(map(str, filters))}_{head}",
                        lane,
                        "spacetodepth_conv",
                        filters,
                        base_lr,
                        1.0e-4,
                        0.003,
                        base_aug,
                        head=head,
                    )
                )
        for aug in opts["augment"]:
            add(
                make_config(
                    f"{lane}_augment_{aug}_parent_weapon_c4",
                    lane,
                    "spacetodepth_conv",
                    base_filters,
                    base_lr,
                    1.0e-4,
                    0.003,
                    aug,
                    head="parent_weapon_c4",
                )
            )

        target_pool = max(limit * 6 // len(lanes), 280)
        attempts = 0
        while attempts < target_pool:
            attempts += 1
            head = rng.choices(
                [
                    "parent_weapon_c4_box_circuit",
                    "parent_weapon_c4_instance",
                    "parent_c4_box_circuit",
                    "parent_weapon_c4",
                    "parent_weapon_aux",
                    "parent_c4_attr",
                    "parent",
                    "dual_parent",
                ],
                weights=[0.24, 0.16, 0.10, 0.14, 0.12, 0.08, 0.04, 0.12],
            )[0]
            arch = rng.choice(opts["arch"])
            activation = "hard_swish" if arch == "hardswish_depthwise" else rng.choice(["relu", "relu6"])
            config = make_config(
                f"{lane}_rand_{attempts:04d}_{head}",
                lane,
                arch,
                rng.choice(opts["filters"]),
                float(rng.choice(opts["lr"])),
                float(rng.choice(opts["l2"])),
                float(rng.choice(opts["dropout"])),
                str(rng.choice(opts["augment"])),
                batch_size=int(rng.choice(opts["batch"])),
                pool=rng.choice(["max", "avg"]),
                activation=activation,
                head=head,
                calibration=rng.choice(["mild_stress", "balanced_rotmirror", "balanced_clean", "hard_stress"]),
                parent_loss_weight=rng.choice([1.0, 1.3, 1.6]),
                subclass_loss_weight=rng.choice([0.08, 0.15, 0.25]),
                weapon_loss_weight=rng.choice([0.05, 0.10, 0.20]),
                c4_loss_weight=rng.choice([0.15, 0.30, 0.60]),
                c4_pos_weight=rng.choice([2.0, 4.0, 8.0]),
                c4_focal_gamma=rng.choice([0.0, 1.0, 2.0]),
                c4_box_loss_weight=rng.choice([0.10, 0.20, 0.40]),
                c4_box_pos_weight=rng.choice([2.0, 4.0, 8.0]),
                c4_circuit_loss_weight=rng.choice([0.20, 0.40, 0.80]),
                c4_circuit_pos_weight=rng.choice([4.0, 8.0, 12.0]),
                c4_instance_loss_weight=rng.choice([0.00, 0.03, 0.08]),
            )
            roll = rng.random()
            if roll < 0.15 and head in {"parent", "parent_weapon_c4", "parent_weapon_c4_box_circuit", "parent_weapon_c4_instance", "dual_parent"}:
                config = replace(config, teacher_loss_weight=rng.choice([0.10, 0.30, 0.60]), teacher_temperature=rng.choice([2.0, 4.0]), c4_teacher_scale=1.0)
            elif roll < 0.24 and head in {"parent_weapon_c4", "parent_weapon_c4_box_circuit", "parent_weapon_c4_instance", "parent_weapon_aux", "parent_c4_attr", "parent_c4_box_circuit"}:
                config = replace(config, decoupled=True, decouple_parent_epochs=rng.choice([24, 40, 56]), decouple_aux_epochs=rng.choice([16, 24, 32]))
            add(config)

    pool.sort(key=priority)
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    for config in pool:
        add_candidate(selected, seen, config, "v6_parent_primary_coarse")
        if len(selected) >= limit:
            break
    return selected


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate V6 parent-primary C4-rescue coarse candidates.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--limit", type=int, default=768)
    parser.add_argument("--seed", type=int, default=20262800)
    parser.add_argument("--max-board-us", type=int, default=18000)
    parser.add_argument("--print-top", type=int, default=24)
    args = parser.parse_args()
    rows = build_candidates(args.limit, args.seed, args.max_board_us)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": rows}, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"v6_candidates={len(rows)} output={args.output}")
    for item in rows[: args.print_top]:
        config = item["config"]
        print(
            f"{item['label']} lane={config['lane']} head={config['head']} "
            f"teacher={config.get('teacher_loss_weight', 0)} decoupled={config.get('decoupled', False)} "
            f"us={config['estimated_board_us']}"
        )


if __name__ == "__main__":
    main()
