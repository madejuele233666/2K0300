import argparse
import json
from dataclasses import replace
from pathlib import Path

import generate_v7_expert_teacher_candidates as round1
import train_tiny32_v5_visual_subclass_scan as train


def add_unique(
    pool: list[dict[str, object]],
    seen: set[tuple[object, ...]],
    config: train.V5Config,
    role: str,
    source: str,
    max_board_us: int,
) -> None:
    if int(train.config_to_dict(config)["estimated_board_us"]) > max_board_us:
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
            "estimated_board_us": int(train.config_to_dict(config)["estimated_board_us"]),
        }
    )


def stable_pool(old: train.V5Config, max_board_us: int) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()

    add_unique(
        pool,
        seen,
        replace(old, name="v7r2_stable_old_anchor_exact_no_teacher"),
        "stable",
        "exact_old_anchor_retrain_baseline",
        max_board_us,
    )

    for teacher_weight in (0.35, 0.70, 1.20, 1.80):
        for parent_weight in (1.0, 1.4, 1.8):
            add_unique(
                pool,
                seen,
                replace(
                    old,
                    name=f"v7r2_stable_oldkd_t{teacher_weight:g}_p{parent_weight:g}",
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=parent_weight,
                    head="parent",
                    class_weight="none",
                    calibration="balanced_clean",
                ),
                "stable",
                "strong_old_kd_anchor",
                max_board_us,
            )

    for filters in ((7, 14, 28), (8, 16, 32), old.filters):
        for teacher_weight in (0.70, 1.20):
            for parent_weight in (1.0, 1.4):
                add_unique(
                    pool,
                    seen,
                    replace(
                        old,
                        name=f"v7r2_stable_f{'x'.join(map(str, filters))}_oldkd_t{teacher_weight:g}_p{parent_weight:g}",
                        filters=filters,
                        teacher_loss_weight=teacher_weight,
                        parent_loss_weight=parent_weight,
                        head="parent",
                        class_weight="none",
                        calibration="balanced_clean",
                    ),
                    "stable",
                    "speed_width_strong_old_kd",
                    max_board_us,
                )

    for teacher_weight in (0.70, 1.20):
        for augment_name in ("v6_camera_mild", "v6_camera_blur_noise"):
            add_unique(
                pool,
                seen,
                replace(
                    old,
                    name=f"v7r2_stable_aug_{augment_name}_oldkd_t{teacher_weight:g}",
                    augment=train.ALL_AUGMENTS[augment_name],
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=1.4,
                    head="parent",
                    class_weight="none",
                    calibration="balanced_clean",
                ),
                "stable",
                "augmentation_preserve_with_strong_kd",
                max_board_us,
            )

    return pool


def rescue_pool(ctd: train.V5Config, old: train.V5Config, max_board_us: int) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()

    add_unique(
        pool,
        seen,
        replace(ctd, name="v7r2_rescue_ctd_anchor_exact_no_teacher"),
        "rescue",
        "exact_ctd_anchor_retrain_baseline",
        max_board_us,
    )

    for teacher_weight in (0.55, 0.90, 1.40):
        for parent_weight in (2.0, 2.8, 3.6):
            for aux_scale in (0.35, 0.65):
                add_unique(
                    pool,
                    seen,
                    replace(
                        ctd,
                        name=f"v7r2_rescue_ctdkd_t{teacher_weight:g}_p{parent_weight:g}_aux{aux_scale:g}",
                        teacher_loss_weight=teacher_weight,
                        parent_loss_weight=parent_weight,
                        weapon_loss_weight=0.08 * aux_scale,
                        c4_loss_weight=0.28 * aux_scale,
                        c4_box_loss_weight=0.14 * aux_scale,
                        c4_circuit_loss_weight=0.26 * aux_scale,
                        c4_instance_loss_weight=0.0,
                        head="parent_weapon_c4_box_circuit",
                        class_weight="none",
                        calibration="balanced_clean",
                        decoupled=False,
                    ),
                    "rescue",
                    "strong_ctd_kd_rescue_aux",
                    max_board_us,
                )

    for filters in ((8, 16, 32), old.filters, (10, 20, 40)):
        for teacher_weight in (0.55, 0.90):
            add_unique(
                pool,
                seen,
                replace(
                    ctd,
                    name=f"v7r2_rescue_f{'x'.join(map(str, filters))}_ctdkd_t{teacher_weight:g}",
                    filters=filters,
                    teacher_loss_weight=teacher_weight,
                    parent_loss_weight=2.8,
                    weapon_loss_weight=0.04,
                    c4_loss_weight=0.18,
                    c4_box_loss_weight=0.10,
                    c4_circuit_loss_weight=0.22,
                    c4_instance_loss_weight=0.0,
                    head="parent_weapon_c4_box_circuit",
                    class_weight="none",
                    calibration="balanced_clean",
                    decoupled=False,
                ),
                "rescue",
                "speed_width_strong_ctd_kd",
                max_board_us,
            )

    return pool


def write_candidates(path: Path, items: list[dict[str, object]], limit: int, role: str, sources: dict[str, str]) -> None:
    selected = items[:limit]
    payload = {
        "meta": {
            "generator": "generate_v7_expert_teacher_round2_candidates.py",
            "role": role,
            "source_files": sources,
            "candidate_pool": len(items),
            "selected": len(selected),
            "selection": "expert-preserving round2: exact anchor baselines plus strong KD candidates",
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
    parser = argparse.ArgumentParser(description="Generate V7 round2 expert-preserving candidate sets.")
    parser.add_argument("--parent-candidates", type=Path, required=True)
    parser.add_argument("--ctd-candidates", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stable-limit", type=int, default=32)
    parser.add_argument("--rescue-limit", type=int, default=32)
    parser.add_argument("--max-board-us", type=int, default=12000)
    args = parser.parse_args()

    old = round1.load_named_config(args.parent_candidates, round1.OLD_PARENT_NAME)
    ctd = round1.load_named_config(args.ctd_candidates, round1.RESCUE_CTD_NAME)
    sources = {
        "parent_candidates": str(args.parent_candidates),
        "ctd_candidates": str(args.ctd_candidates),
        "old_anchor": round1.OLD_PARENT_NAME,
        "rescue_anchor": round1.RESCUE_CTD_NAME,
    }
    write_candidates(args.output_dir / "stable_teacher_candidates.json", stable_pool(old, args.max_board_us), args.stable_limit, "stable", sources)
    write_candidates(args.output_dir / "rescue_teacher_candidates.json", rescue_pool(ctd, old, args.max_board_us), args.rescue_limit, "rescue", sources)


if __name__ == "__main__":
    main()
