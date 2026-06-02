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

    for teacher_weight in (0.45, 0.75, 1.10):
        for parent_weight in (1.0, 1.4, 1.8):
            for negative_weight in (0.25, 0.55, 0.90):
                add_unique(
                    pool,
                    seen,
                    replace(
                        old,
                        name=f"v7r3_stable_oldkd_t{teacher_weight:g}_p{parent_weight:g}_neg{negative_weight:g}",
                        teacher_loss_weight=teacher_weight,
                        parent_loss_weight=parent_weight,
                        negative_margin_weight=negative_weight,
                        negative_margin=1.25,
                        head="parent",
                        class_weight="none",
                        calibration="balanced_clean",
                    ),
                    "stable",
                    "old_replay_with_anti_ctd_margin",
                    max_board_us,
                )

    for margin in (0.8, 1.6):
        for negative_weight in (0.55, 1.10):
            add_unique(
                pool,
                seen,
                replace(
                    old,
                    name=f"v7r3_stable_margin_m{margin:g}_neg{negative_weight:g}",
                    teacher_loss_weight=0.75,
                    parent_loss_weight=1.4,
                    negative_margin_weight=negative_weight,
                    negative_margin=margin,
                    head="parent",
                    class_weight="none",
                    calibration="balanced_clean",
                ),
                "stable",
                "stable_margin_sensitivity",
                max_board_us,
            )

    for filters in ((7, 14, 28), (8, 16, 32), old.filters):
        for negative_weight in (0.55, 0.90):
            add_unique(
                pool,
                seen,
                replace(
                    old,
                    name=f"v7r3_stable_f{'x'.join(map(str, filters))}_neg{negative_weight:g}",
                    filters=filters,
                    teacher_loss_weight=0.75,
                    parent_loss_weight=1.4,
                    negative_margin_weight=negative_weight,
                    negative_margin=1.25,
                    head="parent",
                    class_weight="none",
                    calibration="balanced_clean",
                ),
                "stable",
                "width_replay_with_negative_margin",
                max_board_us,
            )
    return pool


def rescue_pool(ctd: train.V5Config, old: train.V5Config, max_board_us: int) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()

    for teacher_weight in (0.25, 0.45, 0.70):
        for parent_weight in (1.4, 2.0, 2.8):
            for negative_weight in (0.55, 0.95, 1.35):
                add_unique(
                    pool,
                    seen,
                    replace(
                        ctd,
                        name=f"v7r3_rescue_ctdkd_t{teacher_weight:g}_p{parent_weight:g}_neg{negative_weight:g}",
                        teacher_loss_weight=teacher_weight,
                        parent_loss_weight=parent_weight,
                        negative_margin_weight=negative_weight,
                        negative_margin=1.25,
                        weapon_loss_weight=0.03,
                        c4_loss_weight=0.12,
                        c4_box_loss_weight=0.06,
                        c4_circuit_loss_weight=0.12,
                        c4_instance_loss_weight=0.0,
                        head="parent_weapon_c4_box_circuit",
                        class_weight="none",
                        calibration="balanced_clean",
                        decoupled=False,
                    ),
                    "rescue",
                    "ctd_rescue_with_old_error_margin",
                    max_board_us,
                )

    for aux_scale in (0.0, 0.25, 0.50):
        for negative_weight in (0.95, 1.35):
            add_unique(
                pool,
                seen,
                replace(
                    ctd,
                    name=f"v7r3_rescue_aux{aux_scale:g}_neg{negative_weight:g}",
                    teacher_loss_weight=0.45,
                    parent_loss_weight=2.0,
                    negative_margin_weight=negative_weight,
                    negative_margin=1.25,
                    weapon_loss_weight=0.08 * aux_scale,
                    c4_loss_weight=0.24 * aux_scale,
                    c4_box_loss_weight=0.12 * aux_scale,
                    c4_circuit_loss_weight=0.24 * aux_scale,
                    c4_instance_loss_weight=0.0,
                    head="parent_weapon_c4_box_circuit",
                    class_weight="none",
                    calibration="balanced_clean",
                    decoupled=False,
                ),
                "rescue",
                "aux_strength_with_negative_margin",
                max_board_us,
            )

    for filters in ((8, 16, 32), old.filters, (10, 20, 40)):
        add_unique(
            pool,
            seen,
            replace(
                ctd,
                name=f"v7r3_rescue_f{'x'.join(map(str, filters))}_neg0.95",
                filters=filters,
                teacher_loss_weight=0.45,
                parent_loss_weight=2.0,
                negative_margin_weight=0.95,
                negative_margin=1.25,
                weapon_loss_weight=0.03,
                c4_loss_weight=0.12,
                c4_box_loss_weight=0.06,
                c4_circuit_loss_weight=0.12,
                c4_instance_loss_weight=0.0,
                head="parent_weapon_c4_box_circuit",
                class_weight="none",
                calibration="balanced_clean",
                decoupled=False,
            ),
            "rescue",
            "width_ctd_with_negative_margin",
            max_board_us,
        )
    return pool


def write_candidates(path: Path, items: list[dict[str, object]], limit: int, role: str, sources: dict[str, str]) -> None:
    selected = items[:limit]
    payload = {
        "meta": {
            "generator": "generate_v7_expert_teacher_round3_candidates.py",
            "role": role,
            "source_files": sources,
            "candidate_pool": len(items),
            "selected": len(selected),
            "selection": "Expert-CTD v3: replay KD plus explicit negative parent margin",
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
    parser = argparse.ArgumentParser(description="Generate V7 round3 negative-margin expert candidate sets.")
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
