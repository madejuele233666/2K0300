import argparse
import json
from dataclasses import replace
from pathlib import Path

import generate_v7_expert_teacher_candidates as round1
import generate_v7_expert_teacher_round3_candidates as round3
import train_tiny32_v5_visual_subclass_scan as train


def geometric_variant(
    config: train.V5Config,
    *,
    name: str,
    consistency_weight: float,
    calibration: str,
    parent_scale: float,
    teacher_scale: float,
    negative_scale: float,
) -> train.V5Config:
    batch_size = config.batch_size if config.batch_size % 8 == 0 else 16
    return replace(
        config,
        name=name,
        batch_size=batch_size,
        train_transforms="rot_mirror",
        validation_transforms="rot_mirror",
        calibration=calibration,
        parent_loss_weight=config.parent_loss_weight * parent_scale,
        teacher_loss_weight=config.teacher_loss_weight * teacher_scale,
        negative_margin_weight=config.negative_margin_weight * negative_scale,
        geometric_consistency_weight=consistency_weight,
        geometric_consistency_group=8,
    )


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


def geometric_pool(
    base_items: list[dict[str, object]],
    role: str,
    max_board_us: int,
) -> list[dict[str, object]]:
    pool: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    if role == "stable":
        consistency_weights = (0.15, 0.35, 0.70, 1.10)
        parent_scales = (1.0, 1.25)
        teacher_scales = (0.85, 1.0)
        negative_scales = (1.0, 1.25)
    else:
        consistency_weights = (0.20, 0.45, 0.85, 1.30)
        parent_scales = (1.0, 1.20, 1.45)
        teacher_scales = (0.75, 1.0)
        negative_scales = (1.0, 1.20)

    for item in base_items:
        config = item["config"]
        assert isinstance(config, train.V5Config)
        base_source = str(item.get("source", "round3"))
        for consistency_weight in consistency_weights:
            for parent_scale in parent_scales:
                for teacher_scale in teacher_scales:
                    for negative_scale in negative_scales:
                        if consistency_weight >= 0.85 and parent_scale > 1.25 and role == "rescue":
                            # Keep the strongest consistency variants from overwhelming scarce rescue positives.
                            continue
                        suffix = (
                            f"geo_w{consistency_weight:g}_p{parent_scale:g}_"
                            f"t{teacher_scale:g}_n{negative_scale:g}_brm"
                        )
                        add_unique(
                            pool,
                            seen,
                            geometric_variant(
                                config,
                                name=f"{config.name}_{suffix}",
                                consistency_weight=consistency_weight,
                                calibration="balanced_rotmirror",
                                parent_scale=parent_scale,
                                teacher_scale=teacher_scale,
                                negative_scale=negative_scale,
                            ),
                            role,
                            f"{base_source}+8view_consistency+rotmirror_val+rotmirror_cal",
                            max_board_us,
                        )
    return pool


def write_candidates(path: Path, items: list[dict[str, object]], limit: int, role: str, sources: dict[str, str]) -> None:
    selected = items[:limit]
    payload = {
        "meta": {
            "generator": "generate_v7_expert_geometric_candidates.py",
            "role": role,
            "source_files": sources,
            "candidate_pool": len(items),
            "selected": len(selected),
            "selection": (
                "Round3 Expert-CTD plus strict 8-view parent consistency, "
                "rot/mirror validation, and balanced_rotmirror int8 calibration"
            ),
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
    print(f"wrote {len(selected)} {role} geometric candidates to {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate V7 strict geometric-consistency expert candidate sets.")
    parser.add_argument("--parent-candidates", type=Path, required=True)
    parser.add_argument("--ctd-candidates", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stable-limit", type=int, default=96)
    parser.add_argument("--rescue-limit", type=int, default=96)
    parser.add_argument("--base-stable-limit", type=int, default=24)
    parser.add_argument("--base-rescue-limit", type=int, default=24)
    parser.add_argument("--max-board-us", type=int, default=12000)
    args = parser.parse_args()

    old = round1.load_named_config(args.parent_candidates, round1.OLD_PARENT_NAME)
    ctd = round1.load_named_config(args.ctd_candidates, round1.RESCUE_CTD_NAME)
    stable_base = round3.stable_pool(old, args.max_board_us)[: args.base_stable_limit]
    rescue_base = round3.rescue_pool(ctd, old, args.max_board_us)[: args.base_rescue_limit]
    sources = {
        "parent_candidates": str(args.parent_candidates),
        "ctd_candidates": str(args.ctd_candidates),
        "old_anchor": round1.OLD_PARENT_NAME,
        "rescue_anchor": round1.RESCUE_CTD_NAME,
        "base_generator": "generate_v7_expert_teacher_round3_candidates.py",
    }
    write_candidates(
        args.output_dir / "stable_geometric_candidates.json",
        geometric_pool(stable_base, "stable", args.max_board_us),
        args.stable_limit,
        "stable",
        sources,
    )
    write_candidates(
        args.output_dir / "rescue_geometric_candidates.json",
        geometric_pool(rescue_base, "rescue", args.max_board_us),
        args.rescue_limit,
        "rescue",
        sources,
    )


if __name__ == "__main__":
    main()
