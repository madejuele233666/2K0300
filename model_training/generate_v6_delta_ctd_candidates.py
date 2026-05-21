import argparse
import json
from dataclasses import replace
from pathlib import Path

import train_tiny32_v5_visual_subclass_scan as train


def load_named_config(path: Path, name: str) -> train.V5Config:
    data = json.loads(path.read_text(encoding="utf-8"))
    for index, item in enumerate(data.get("candidates", data if isinstance(data, list) else [])):
        config_data = item.get("config", item)
        label = str(item.get("label") or config_data.get("name") or f"candidate_{index:03d}")
        if label == name or config_data.get("name") == name:
            return train.config_from_dict(config_data, label)
    raise ValueError(f"candidate not found: {name}")


def add_unique(items: list[train.V5Config], config: train.V5Config, seen: set[tuple[object, ...]]) -> None:
    key = train.semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    items.append(config)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--parent-candidates", type=Path, required=True)
    parser.add_argument("--ctd-candidates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=64)
    args = parser.parse_args()

    old = load_named_config(args.parent_candidates, "p100_head_parent_s4_integrated_084_a5024")
    ctd = load_named_config(args.ctd_candidates, "ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int")
    blur_aug = train.ALL_AUGMENTS["v6_camera_blur_noise"]

    candidates: list[train.V5Config] = []
    seen: set[tuple[object, ...]] = set()

    for teacher_weight in (0.0, 0.05, 0.10, 0.18, 0.28):
        for parent_weight in (1.8, 2.2, 2.8):
            for l2 in (1.0e-4, 1.5e-4):
                config = replace(
                    old,
                    name=f"delta_parent_tw{teacher_weight:g}_pw{parent_weight:g}_l2{l2:g}_mild",
                    parent_loss_weight=parent_weight,
                    teacher_loss_weight=teacher_weight,
                    l2=l2,
                    augment=train.ALL_AUGMENTS["v6_camera_mild"],
                )
                add_unique(candidates, config, seen)

    for teacher_weight in (0.05, 0.10, 0.18):
        for parent_weight in (2.2, 2.8):
            config = replace(
                old,
                name=f"delta_parent_blur_tw{teacher_weight:g}_pw{parent_weight:g}",
                parent_loss_weight=parent_weight,
                teacher_loss_weight=teacher_weight,
                l2=1.5e-4,
                augment=blur_aug,
            )
            add_unique(candidates, config, seen)

    for teacher_weight in (0.05, 0.10, 0.18, 0.28):
        for parent_weight in (1.8, 2.2):
            for aux_scale in (0.5, 0.8):
                config = replace(
                    ctd,
                    name=f"delta_aux_tw{teacher_weight:g}_pw{parent_weight:g}_aux{aux_scale:g}",
                    parent_loss_weight=parent_weight,
                    teacher_loss_weight=teacher_weight,
                    weapon_loss_weight=0.10 * aux_scale,
                    c4_loss_weight=0.20 * aux_scale,
                    c4_box_loss_weight=0.10 * aux_scale,
                    c4_circuit_loss_weight=0.10 * aux_scale,
                    c4_instance_loss_weight=0.0,
                    l2=1.0e-4,
                    augment=train.ALL_AUGMENTS["v6_camera_mild"],
                )
                add_unique(candidates, config, seen)

    selected = candidates[: args.limit]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "candidates": [
            {
                "label": config.name,
                "config": train.config_to_dict(config),
                "source": {
                    "generator": "generate_v6_delta_ctd_candidates.py",
                    "note": "delta teacher from old-best preserve and CTD rescue TFLite outputs",
                },
            }
            for config in selected
        ]
    }
    args.output.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {len(selected)} candidates to {args.output}")


if __name__ == "__main__":
    main()
