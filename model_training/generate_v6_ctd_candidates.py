import argparse
import ast
import csv
import json
import random
import re
from collections import Counter
from dataclasses import replace
from pathlib import Path

from train_tiny32_v5_visual_subclass_scan import ALL_AUGMENTS, V5Config, config_from_dict, config_to_dict, semantic_key


def slug(value: str, limit: int = 44) -> str:
    clean = re.sub(r"[^A-Za-z0-9_]+", "_", value)
    clean = re.sub(r"_+", "_", clean).strip("_")
    return (clean or "candidate")[:limit]


def read_selected(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            config_data = ast.literal_eval(row["config"])
            row["config_obj"] = config_from_dict(config_data, f"ctd_anchor_{slug(row['trial'], 32)}")
            rows.append(row)
    if not rows:
        raise RuntimeError("selected model file is empty")
    return rows


def add_candidate(
    out: list[dict[str, object]],
    seen: set[tuple[object, ...]],
    config: V5Config,
    reason: str,
    anchor: dict[str, object],
    max_board_us: int,
) -> None:
    data = config_to_dict(config)
    if int(data["estimated_board_us"]) > max_board_us:
        return
    key = semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    out.append(
        {
            "label": config.name,
            "config": data,
            "source": {
                "generator": "generate_v6_ctd_candidates.py",
                "reason": reason,
                "anchor_trial": anchor["trial"],
                "anchor_model_id": anchor["model_id"],
                "anchor_all_acc": float(anchor["all_acc"]),
                "anchor_hard_acc": float(anchor["hard_acc"]),
                "anchor_stress_worst": float(anchor["stress_worst"]),
            },
        }
    )


def with_name(base: V5Config, prefix: str, anchor: dict[str, object], **updates: object) -> V5Config:
    if "augment_name" in updates:
        updates["augment"] = ALL_AUGMENTS[str(updates.pop("augment_name"))]
    return replace(base, name=f"{prefix}_{slug(str(anchor['trial']), 30)}", **updates)


def build_candidates(anchors: list[dict[str, object]], limit: int, seed: int, max_board_us: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    out: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    pool: list[tuple[float, V5Config, str, dict[str, object]]] = []

    def queue(config: V5Config, reason: str, anchor: dict[str, object], penalty: float) -> None:
        c = config_to_dict(config)
        if int(c["estimated_board_us"]) > max_board_us:
            return
        all_acc = float(anchor["all_acc"])
        hard = float(anchor["hard_acc"])
        stress = float(anchor["stress_worst"])
        head_penalty = {
            "parent": -0.040,
            "parent_weapon_c4": -0.030,
            "parent_weapon_aux": -0.020,
            "dual_parent": -0.010,
            "parent_weapon_c4_box_circuit": 0.000,
        }.get(config.head, 0.040)
        teacher_bonus = -0.030 if 0.05 <= config.teacher_loss_weight <= 0.20 else 0.010
        parent_penalty = abs(config.parent_loss_weight - 1.6) * 0.008
        aug_penalty = {"v6_camera_mild": -0.010, "v6_camera_blur_noise": -0.004, "sdiag_speed": 0.000}.get(config.augment.name, 0.006)
        score = -(0.45 * all_acc + 0.20 * hard + 0.15 * stress) + head_penalty + teacher_bonus + parent_penalty + aug_penalty + penalty
        pool.append((score + rng.random() * 1.0e-6, config, reason, anchor))

    for anchor in anchors:
        base: V5Config = anchor["config_obj"]  # type: ignore[assignment]
        base = replace(base, teacher_temperature=2.0, logits=False, train_transforms="rot_mirror")

        for teacher in [0.0, 0.05, 0.10, 0.20, 0.30]:
            queue(with_name(base, f"ctd1_teacher_{teacher:g}", anchor, teacher_loss_weight=teacher), "axis_teacher", anchor, -0.030)

        for head in ["parent", "parent_weapon_c4", "parent_weapon_aux", "dual_parent", "parent_weapon_c4_box_circuit"]:
            for teacher in [0.05, 0.10, 0.20]:
                queue(with_name(base, f"ctd1_head_{head}_t{teacher:g}", anchor, head=head, teacher_loss_weight=teacher), "axis_head_teacher", anchor, -0.022)

        for parent_weight in [1.0, 1.3, 1.6, 2.0, 2.4]:
            for teacher in [0.05, 0.10, 0.20]:
                queue(
                    with_name(base, f"ctd1_parentw_{parent_weight:g}_t{teacher:g}", anchor, parent_loss_weight=parent_weight, teacher_loss_weight=teacher),
                    "axis_parent_teacher",
                    anchor,
                    -0.016,
                )

        for aug_name in ["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_mid"]:
            for teacher in [0.05, 0.10, 0.20]:
                queue(with_name(base, f"ctd1_aug_{aug_name}_t{teacher:g}", anchor, augment_name=aug_name, teacher_loss_weight=teacher), "axis_aug_teacher", anchor, -0.010)

        for calibration in ["balanced_clean", "balanced_rotmirror", "mild_stress", "hard_stress", "hard_clean"]:
            for teacher in [0.05, 0.10]:
                queue(with_name(base, f"ctd1_cal_{calibration}_t{teacher:g}", anchor, calibration=calibration, teacher_loss_weight=teacher), "axis_cal_teacher", anchor, -0.006)

        for _ in range(18):
            teacher = float(rng.choices([0.05, 0.10, 0.20, 0.30], weights=[0.30, 0.36, 0.26, 0.08])[0])
            head = str(rng.choices(["parent", "parent_weapon_c4", "parent_weapon_aux", "dual_parent", "parent_weapon_c4_box_circuit"], weights=[0.24, 0.30, 0.16, 0.12, 0.18])[0])
            config = replace(
                base,
                name=f"ctd1_rand_{rng.randrange(100000):05d}_{slug(str(anchor['trial']), 24)}",
                head=head,
                teacher_loss_weight=teacher,
                teacher_temperature=2.0,
                parent_loss_weight=float(rng.choice([1.0, 1.3, 1.6, 2.0, 2.4])),
                subclass_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10])),
                weapon_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10, 0.20])),
                c4_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10, 0.20, 0.30])),
                c4_box_loss_weight=float(rng.choice([0.0, 0.05, 0.10, 0.20])),
                c4_circuit_loss_weight=float(rng.choice([0.0, 0.05, 0.10, 0.20, 0.40])),
                c4_instance_loss_weight=0.0,
                learning_rate=float(rng.choice([0.0016, 0.0020, 0.0023, 0.0026, 0.00286, 0.00302])),
                l2=float(rng.choice([3.0e-5, 7.0e-5, 1.0e-4, 1.5e-4, 3.0e-4])),
                dropout=float(rng.choice([0.0, 0.003, 0.008, 0.012, 0.02])),
                augment=ALL_AUGMENTS[str(rng.choice(["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_mid"]))],
                calibration=str(rng.choice(["balanced_clean", "balanced_rotmirror", "mild_stress", "hard_stress", "hard_clean"])),
                decoupled=bool(rng.choices([False, True], weights=[0.86, 0.14])[0]),
                decouple_parent_epochs=int(rng.choice([40, 56, 72])),
                decouple_aux_epochs=int(rng.choice([12, 20, 28])),
                decouple_joint_lr_scale=float(rng.choice([0.20, 0.25, 0.35])),
            )
            queue(config, "random_ctd_local", anchor, 0.018)

    pool.sort(key=lambda item: item[0])
    for _, config, reason, anchor in pool:
        add_candidate(out, seen, config, reason, anchor, max_board_us)
        if len(out) >= limit:
            break
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate first-round Correct-Teacher Distillation candidates.")
    parser.add_argument("--selected-models", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=160)
    parser.add_argument("--anchor-limit", type=int, default=16)
    parser.add_argument("--seed", type=int, default=20263200)
    parser.add_argument("--max-board-us", type=int, default=18000)
    args = parser.parse_args()

    rows = read_selected(args.selected_models)
    rows.sort(
        key=lambda row: (
            float(row["all_acc"]),
            float(row["hard_acc"]),
            float(row["stress_worst"]),
            -float(row["c4_camera_fp"]),
        ),
        reverse=True,
    )
    anchors = rows[: args.anchor_limit]
    candidates = build_candidates(anchors, args.limit, args.seed, args.max_board_us)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": candidates}, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"anchors={len(anchors)} candidates={len(candidates)} output={args.output}")
    print("by_head", Counter(str(item["config"]["head"]) for item in candidates))
    print("by_teacher", Counter(float(item["config"]["teacher_loss_weight"]) for item in candidates))
    print("by_reason", Counter(str(item["source"]["reason"]) for item in candidates))
    for item in candidates[:20]:
        config = item["config"]
        print(
            f"{item['label']} head={config['head']} teacher={config['teacher_loss_weight']} "
            f"pW={config['parent_loss_weight']} aug={config['augment']['name']} calib={config['calibration']} "
            f"us={config['estimated_board_us']} reason={item['source']['reason']}"
        )


if __name__ == "__main__":
    main()
