import argparse
import json
import random
import re
from collections import Counter
from dataclasses import replace
from pathlib import Path

from train_tiny32_v5_visual_subclass_scan import (
    ALL_AUGMENTS,
    V5Config,
    config_from_dict,
    config_to_dict,
    semantic_key,
)


DEFAULT_SOURCE_DIRS = [
    Path("experiments/v6_closed_parent_primary_coarse_20260512_230236/stage4_toplocal_20260514_141523"),
    Path("experiments/v6_closed_parent_primary_coarse_20260512_230236/stage3_retest_20260514_100008_16way"),
]

PREFERRED_TRIALS = [
    "s4_cross_016_cross",
    "s4_integrated_058_a5024",
    "s4_integrated_064_a5024",
    "s4_hard_016_a0676",
    "s4_hard_012_a0676",
    "s4_integrated_074_a5024",
    "s4_fast_037_afast",
    "s4_fast_027_afast",
    "s4_hard_006_a0676",
    "s4_integrated_080_a5024",
    "s4_c4cam_019_a4317",
    "s2_rand_5024_v6_accuracy_C_teacher_cl",
    "s2_rand_4317_v6_accuracy_F_c4_focal_g",
    "s2_deploy_balance_v6_fast_augment_v6_camera",
]


def slug(value: str, limit: int = 52) -> str:
    clean = re.sub(r"[^A-Za-z0-9_]+", "_", value)
    clean = re.sub(r"_+", "_", clean).strip("_")
    return (clean or "candidate")[:limit]


def load_rows(source_dirs: list[Path]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for source_dir in source_dirs:
        for path in sorted(source_dir.glob("shard_*/trial_results.jsonl")):
            for line in path.read_text(encoding="utf-8").splitlines():
                if not line.strip():
                    continue
                row = json.loads(line)
                row["source_dir"] = str(source_dir)
                row["source_shard"] = path.parent.name
                rows.append(row)
    if not rows:
        raise RuntimeError("no trial rows loaded from source dirs")
    return rows


def get_parent_accuracy(run: dict[str, object], group: str) -> float:
    parent = ((run.get(group) or {}).get("parent") or {}) if isinstance(run.get(group), dict) else {}
    return float(parent.get("accuracy", 0.0))


def get_parent_worst(run: dict[str, object], group: str) -> float:
    parent = ((run.get(group) or {}).get("parent") or {}) if isinstance(run.get(group), dict) else {}
    return float(parent.get("worst_recall", 0.0))


def stress_worst(run: dict[str, object], camera_only: bool = False) -> float:
    stresses = run.get("int8_stress") or {}
    values: list[float] = []
    if isinstance(stresses, dict):
        for name, item in stresses.items():
            if camera_only and not str(name).startswith("cam_"):
                continue
            if isinstance(item, dict):
                values.append(float(item.get("worst_recall", 0.0)))
    return min(values) if values else get_parent_worst(run, "int8_test")


def c4_camera_recall(run: dict[str, object]) -> float:
    data = run.get("c4_camera_eval") or {}
    return float(data.get("c4_camera_stress_recall", 0.0)) if isinstance(data, dict) else 0.0


def c4_camera_fp(run: dict[str, object]) -> float:
    data = run.get("c4_camera_eval") or {}
    return float(data.get("c4_camera_false_positive_rate", 1.0)) if isinstance(data, dict) else 1.0


def row_stats(row: dict[str, object]) -> dict[str, float]:
    runs = [run for run in row.get("runs", []) if isinstance(run, dict) and run.get("status") == "ok"]
    if not runs:
        return {
            "score_mean": float(row.get("score_mean", 0.0)),
            "score_min": float(row.get("score_min", 0.0)),
            "test_min": float(row.get("clean_parent_accuracy_min", 0.0)),
            "test_mean": float(row.get("clean_parent_accuracy_mean", 0.0)),
            "all_min": 0.0,
            "all_mean": 0.0,
            "hard_min": float(row.get("hard_parent_accuracy_min", 0.0)),
            "hard_mean": float(row.get("hard_parent_accuracy_mean", 0.0)),
            "stress_min": float(row.get("stress_parent_worst_min", 0.0)),
            "camera_min": float(row.get("camera_stress_parent_worst_min", 0.0)),
            "c4cam_min": float(row.get("c4_camera_stress_recall_min", 0.0)),
            "c4fp_max": float(row.get("c4_camera_false_positive_max", 1.0)),
        }
    scores = [float(run.get("score", 0.0)) for run in runs]
    test = [get_parent_accuracy(run, "int8_test") for run in runs]
    all_acc = [get_parent_accuracy(run, "int8_all") for run in runs]
    hard = [get_parent_accuracy(run, "int8_hard") for run in runs]
    stress = [stress_worst(run) for run in runs]
    camera = [stress_worst(run, camera_only=True) for run in runs]
    c4cam = [c4_camera_recall(run) for run in runs]
    c4fp = [c4_camera_fp(run) for run in runs]
    return {
        "score_mean": sum(scores) / len(scores),
        "score_min": min(scores),
        "test_min": min(test),
        "test_mean": sum(test) / len(test),
        "all_min": min(all_acc),
        "all_mean": sum(all_acc) / len(all_acc),
        "hard_min": min(hard),
        "hard_mean": sum(hard) / len(hard),
        "stress_min": min(stress),
        "camera_min": min(camera),
        "c4cam_min": min(c4cam),
        "c4fp_max": max(c4fp),
    }


def parent100_key(row: dict[str, object]) -> tuple[float, ...]:
    stats = row["parent100_stats"]  # type: ignore[index]
    return (
        float(stats["all_min"]),
        float(stats["all_mean"]),
        float(stats["test_min"]),
        float(stats["hard_min"]),
        float(stats["stress_min"]),
        float(stats["camera_min"]),
        float(stats["score_min"]),
        -float(stats["c4fp_max"]),
    )


def pick_anchors(rows: list[dict[str, object]], limit: int) -> list[dict[str, object]]:
    selected: list[dict[str, object]] = []
    seen: set[str] = set()

    def add(row: dict[str, object], role: str) -> None:
        trial = str(row.get("trial", ""))
        if not trial or trial in seen:
            return
        item = dict(row)
        item["anchor_role"] = role
        selected.append(item)
        seen.add(trial)

    for preferred in PREFERRED_TRIALS:
        matches = [row for row in rows if preferred in str(row.get("trial", ""))]
        matches.sort(key=parent100_key, reverse=True)
        for row in matches[:1]:
            add(row, "preferred")

    selectors = [
        ("all_parent", lambda row: True, lambda row: (row["parent100_stats"]["all_min"], row["parent100_stats"]["all_mean"], parent100_key(row))),
        ("test_parent", lambda row: True, lambda row: (row["parent100_stats"]["test_min"], row["parent100_stats"]["all_mean"], parent100_key(row))),
        ("hard_parent", lambda row: True, lambda row: (row["parent100_stats"]["hard_min"], row["parent100_stats"]["all_mean"], parent100_key(row))),
        ("stress_parent", lambda row: True, lambda row: (row["parent100_stats"]["stress_min"], row["parent100_stats"]["camera_min"], parent100_key(row))),
        (
            "c4_camera",
            lambda row: row["parent100_stats"]["c4cam_min"] >= 2.0 / 3.0 and row["parent100_stats"]["c4fp_max"] <= 0.08,
            lambda row: (row["parent100_stats"]["c4cam_min"], -row["parent100_stats"]["c4fp_max"], parent100_key(row)),
        ),
        (
            "fast_parent",
            lambda row: int(row.get("estimated_board_us", row.get("config", {}).get("estimated_board_us", 999999))) <= 6093,
            lambda row: (row["parent100_stats"]["all_mean"], row["parent100_stats"]["test_min"], parent100_key(row)),
        ),
        ("score_stable", lambda row: True, lambda row: (row["parent100_stats"]["score_min"], row["parent100_stats"]["score_mean"], parent100_key(row))),
    ]
    per_selector = max(3, limit // max(1, len(selectors)))
    for role, predicate, key_fn in selectors:
        matches = [row for row in rows if predicate(row)]
        matches.sort(key=key_fn, reverse=True)
        for row in matches[:per_selector]:
            add(row, role)
            if len(selected) >= limit:
                return selected
    return selected[:limit]


def config_from_row(row: dict[str, object], name: str) -> V5Config:
    return config_from_dict(row["config"], name)


def add_candidate(
    selected: list[dict[str, object]],
    seen: set[tuple[object, ...]],
    config: V5Config,
    *,
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
    selected.append(
        {
            "label": config.name,
            "config": data,
            "source": {
                "reason": reason,
                "generator": "generate_v6_parent100_candidates.py",
                "anchor_trial": str(anchor.get("trial", "")),
                "anchor_role": str(anchor.get("anchor_role", "")),
                "anchor_stats": anchor.get("parent100_stats", {}),
            },
        }
    )


def with_name(config: V5Config, prefix: str, anchor: dict[str, object], **updates: object) -> V5Config:
    if "augment_name" in updates:
        aug_name = str(updates.pop("augment_name"))
        updates["augment"] = ALL_AUGMENTS[aug_name]
    return replace(config, name=f"{prefix}_{slug(str(anchor.get('trial', config.name)), 34)}", **updates)


def filter_options(config: V5Config) -> list[tuple[int, int, int]]:
    if config.lane == "v6_fast":
        return [(6, 12, 24), (7, 14, 28), (8, 16, 24), (8, 16, 32)]
    if config.lane == "v6_accuracy":
        return [(10, 18, 36), (10, 20, 40), (10, 20, 48), (12, 24, 48), (14, 28, 56), (16, 24, 48)]
    return [(8, 16, 24), (8, 16, 32), (10, 18, 36), (10, 20, 40), (12, 24, 48)]


def build_retest(anchors: list[dict[str, object]], limit: int, max_board_us: int) -> list[dict[str, object]]:
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    for anchor in anchors:
        base = config_from_row(anchor, f"p100_retest_{slug(str(anchor.get('trial', 'anchor')), 42)}")
        add_candidate(selected, seen, base, reason="exact_retest", anchor=anchor, max_board_us=max_board_us)
        if len(selected) >= limit:
            break
    return selected


def build_neighborhood(anchors: list[dict[str, object]], limit: int, seed: int, max_board_us: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    pool: list[tuple[float, V5Config, str, dict[str, object]]] = []

    def queue(config: V5Config, reason: str, anchor: dict[str, object], penalty: float) -> None:
        stats = anchor.get("parent100_stats", {})
        anchor_bonus = -0.30 * float(stats.get("all_mean", 0.0)) - 0.14 * float(stats.get("test_min", 0.0))
        head_penalty = {
            "parent": -0.050,
            "parent_weapon_aux": -0.035,
            "dual_parent": -0.025,
            "parent_weapon_c4": -0.015,
            "parent_weapon_c4_box_circuit": 0.000,
            "parent_c4_box_circuit": 0.020,
            "parent_weapon_c4_instance": 0.040,
        }.get(config.head, 0.060)
        aux_penalty = 0.015 * config.weapon_loss_weight + 0.020 * config.c4_loss_weight + 0.015 * config.c4_box_loss_weight
        dropout_penalty = abs(config.dropout - 0.008) * 0.55
        lr_penalty = abs(config.learning_rate - (0.0023 if config.lane == "v6_accuracy" else 0.00302)) * 34.0
        us = int(config_to_dict(config)["estimated_board_us"])
        latency_penalty = max(0.0, us - 12000) / 70000.0
        pool.append((anchor_bonus + head_penalty + aux_penalty + dropout_penalty + lr_penalty + latency_penalty + penalty + rng.random() * 1e-5, config, reason, anchor))

    for anchor in anchors:
        base = config_from_row(anchor, f"p100_near_anchor_{slug(str(anchor.get('trial', 'anchor')), 34)}")
        queue(base, "anchor_keep", anchor, -0.080)

        for head in ["parent", "parent_weapon_aux", "dual_parent", "parent_weapon_c4", "parent_weapon_c4_box_circuit"]:
            queue(with_name(base, f"p100_head_{head}", anchor, head=head), "axis_head", anchor, -0.020)
        for filters in filter_options(base):
            queue(with_name(base, f"p100_filters_{'-'.join(map(str, filters))}", anchor, filters=filters), "axis_filters", anchor, 0.000)
        for lr_mul in [0.82, 0.90, 1.0, 1.10, 1.20]:
            lr = round(max(0.0010, min(0.0036, base.learning_rate * lr_mul)), 8)
            queue(with_name(base, f"p100_lr_{lr:g}", anchor, learning_rate=lr), "axis_lr", anchor, 0.004)
        for l2 in [1.0e-5, 3.0e-5, 7.0e-5, 1.0e-4, 1.5e-4, 3.0e-4, 6.0e-4]:
            queue(with_name(base, f"p100_l2_{l2:g}", anchor, l2=l2), "axis_l2", anchor, 0.006)
        for dropout in [0.0, 0.003, 0.008, 0.012, 0.02, 0.05]:
            queue(with_name(base, f"p100_dropout_{dropout:g}", anchor, dropout=dropout), "axis_dropout", anchor, 0.006)
        for aug in ["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_mid", "sdiag_lowres", "v4_highspeed"]:
            queue(with_name(base, f"p100_aug_{aug}", anchor, augment_name=aug), "axis_augment", anchor, 0.010)
        for calibration in ["balanced_clean", "balanced_rotmirror", "mild_stress", "hard_stress", "hard_clean"]:
            queue(with_name(base, f"p100_cal_{calibration}", anchor, calibration=calibration), "axis_calibration", anchor, 0.008)
        for parent_weight in [1.0, 1.3, 1.6, 2.0, 2.4]:
            queue(with_name(base, f"p100_parentw_{parent_weight:g}", anchor, parent_loss_weight=parent_weight), "axis_parent_loss", anchor, 0.004)
        for subclass_weight in [0.0, 0.03, 0.05, 0.10, 0.20]:
            queue(with_name(base, f"p100_subw_{subclass_weight:g}", anchor, subclass_loss_weight=subclass_weight), "axis_subclass_loss", anchor, 0.004)
        for weapon_weight in [0.0, 0.03, 0.05, 0.10]:
            queue(with_name(base, f"p100_weaponw_{weapon_weight:g}", anchor, weapon_loss_weight=weapon_weight), "axis_weapon_loss", anchor, 0.004)
        for c4_weight in [0.0, 0.03, 0.05, 0.10, 0.20, 0.30]:
            queue(with_name(base, f"p100_c4w_{c4_weight:g}", anchor, c4_loss_weight=c4_weight), "axis_c4_loss", anchor, 0.004)
        for teacher in [0.0, 0.05, 0.10, 0.20]:
            queue(with_name(base, f"p100_teacher_{teacher:g}", anchor, teacher_loss_weight=teacher, teacher_temperature=2.0), "axis_teacher", anchor, 0.014)

        for attempt in range(max(40, limit // max(1, len(anchors)) * 3)):
            head = rng.choices(
                ["parent", "parent_weapon_aux", "dual_parent", "parent_weapon_c4", "parent_weapon_c4_box_circuit"],
                weights=[0.22, 0.18, 0.18, 0.22, 0.20],
            )[0]
            config = replace(
                base,
                name=f"p100_rand_{attempt:04d}_{slug(str(anchor.get('trial', 'anchor')), 28)}",
                filters=rng.choice(filter_options(base)),
                learning_rate=float(rng.choice([0.0016, 0.0020, 0.0023, 0.0026, 0.00286, 0.00302, 0.00318])),
                l2=float(rng.choice([1.0e-5, 3.0e-5, 7.0e-5, 1.0e-4, 1.5e-4, 3.0e-4, 6.0e-4])),
                dropout=float(rng.choice([0.0, 0.003, 0.008, 0.012, 0.02, 0.05])),
                batch_size=int(rng.choices([16, 24], weights=[0.82, 0.18])[0]),
                pool=str(rng.choices(["max", "avg"], weights=[0.85, 0.15])[0]),
                activation=str(rng.choices(["relu", "relu6"], weights=[0.88, 0.12])[0]),
                augment=ALL_AUGMENTS[
                    rng.choices(
                        ["v6_camera_mild", "v6_camera_blur_noise", "sdiag_speed", "sdiag_soft", "sdiag_mid", "sdiag_lowres", "v4_highspeed"],
                        weights=[0.25, 0.20, 0.18, 0.14, 0.10, 0.08, 0.05],
                    )[0]
                ],
                head=head,
                calibration=str(rng.choice(["balanced_clean", "balanced_rotmirror", "mild_stress", "hard_stress", "hard_clean"])),
                parent_loss_weight=float(rng.choice([1.3, 1.6, 2.0, 2.4])),
                subclass_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10, 0.20])),
                weapon_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10])),
                c4_loss_weight=float(rng.choice([0.0, 0.03, 0.05, 0.10, 0.20])),
                c4_pos_weight=float(rng.choice([2.0, 4.0])),
                c4_focal_gamma=float(rng.choice([0.0, 1.0])),
                c4_box_loss_weight=float(rng.choice([0.0, 0.05, 0.10, 0.20])),
                c4_box_pos_weight=float(rng.choice([2.0, 4.0])),
                c4_circuit_loss_weight=float(rng.choice([0.0, 0.05, 0.10, 0.20, 0.40])),
                c4_circuit_pos_weight=float(rng.choice([4.0, 8.0])),
                c4_instance_loss_weight=0.0,
                teacher_loss_weight=float(rng.choices([0.0, 0.05, 0.10, 0.20], weights=[0.74, 0.10, 0.10, 0.06])[0]),
                teacher_temperature=2.0,
                decoupled=bool(rng.choices([False, True], weights=[0.88, 0.12])[0]),
                decouple_parent_epochs=int(rng.choice([40, 56, 72])),
                decouple_aux_epochs=int(rng.choice([12, 20, 28])),
                decouple_joint_lr_scale=float(rng.choice([0.20, 0.25, 0.35])),
            )
            queue(config, "local_random_parent100", anchor, 0.030)

    pool.sort(key=lambda item: item[0])
    for _, config, reason, anchor in pool:
        add_candidate(selected, seen, config, reason=reason, anchor=anchor, max_board_us=max_board_us)
        if len(selected) >= limit:
            break
    return selected


def write_candidates(path: Path, candidates: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"candidates": candidates}, indent=2, ensure_ascii=False), encoding="utf-8")


def print_summary(label: str, candidates: list[dict[str, object]]) -> None:
    print(f"{label}_candidates={len(candidates)}")
    print(f"{label}_by_lane", Counter(str(item["config"]["lane"]) for item in candidates))
    print(f"{label}_by_head", Counter(str(item["config"]["head"]) for item in candidates))
    print(f"{label}_by_reason", Counter(str(item["source"]["reason"]) for item in candidates))
    for item in candidates[:16]:
        config = item["config"]
        source = item["source"]
        print(
            f"{label} {item['label']} lane={config['lane']} head={config['head']} "
            f"filters={config['filters']} aug={config['augment']['name']} calib={config['calibration']} "
            f"pW={config.get('parent_loss_weight')} subW={config.get('subclass_loss_weight')} "
            f"c4W={config.get('c4_loss_weight')} teacher={config.get('teacher_loss_weight')} "
            f"us={config['estimated_board_us']} anchor={source.get('anchor_trial')}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate retest and local-neighborhood candidates aimed at parent 100%.")
    parser.add_argument("--source-dir", action="append", type=Path, default=None)
    parser.add_argument("--output-retest", type=Path, required=True)
    parser.add_argument("--output-neighborhood", type=Path, required=True)
    parser.add_argument("--retest-limit", type=int, default=28)
    parser.add_argument("--neighborhood-limit", type=int, default=384)
    parser.add_argument("--anchor-limit", type=int, default=28)
    parser.add_argument("--seed", type=int, default=20263150)
    parser.add_argument("--max-board-us", type=int, default=18000)
    args = parser.parse_args()

    source_dirs = args.source_dir or DEFAULT_SOURCE_DIRS
    rows = load_rows(source_dirs)
    for row in rows:
        row["parent100_stats"] = row_stats(row)
    anchors = pick_anchors(rows, args.anchor_limit)
    retest = build_retest(anchors, args.retest_limit, args.max_board_us)
    neighborhood = build_neighborhood(anchors, args.neighborhood_limit, args.seed, args.max_board_us)
    write_candidates(args.output_retest, retest)
    write_candidates(args.output_neighborhood, neighborhood)

    print(f"loaded_rows={len(rows)} anchors={len(anchors)} source_dirs={[str(path) for path in source_dirs]}")
    print_summary("retest", retest)
    print_summary("neighborhood", neighborhood)
    print(f"output_retest={args.output_retest}")
    print(f"output_neighborhood={args.output_neighborhood}")


if __name__ == "__main__":
    main()
