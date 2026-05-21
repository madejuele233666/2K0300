import argparse
import json
import random
import re
from collections import Counter, defaultdict
from dataclasses import replace
from pathlib import Path

from train_tiny32_v5_visual_subclass_scan import (
    ALL_AUGMENTS,
    V5Config,
    config_from_dict,
    config_to_dict,
    semantic_key,
)


DEFAULT_STAGE1 = Path("experiments/v6_closed_parent_primary_coarse_20260512_230236/stage1_coarse")

FINE_FILTERS = {
    "v6_fast": [(6, 12, 24), (7, 14, 28), (8, 16, 24)],
    "v6_balance": [(7, 14, 28), (8, 16, 24), (8, 16, 32), (10, 18, 36)],
    "v6_accuracy": [(10, 18, 36), (10, 20, 40), (10, 20, 48)],
}

FINE_AUGMENTS = {
    "v6_fast": ["sdiag_base", "sdiag_soft", "sdiag_lowres", "sdiag_speed", "v4_lowres_mix", "v6_camera_mild"],
    "v6_balance": ["sdiag_roi", "sdiag_speed", "sdiag_soft", "sdiag_lowres", "v4_lowres_mix", "v6_camera_mild", "v6_camera_blur_noise"],
    "v6_accuracy": ["sdiag_soft", "sdiag_speed", "sdiag_hard", "sdiag_lowres", "v6_camera_mild", "v6_camera_blur_noise"],
}

FINE_HEADS = [
    "parent_weapon_c4",
    "parent_weapon_c4_box_circuit",
    "parent_c4_box_circuit",
]


def slug(text: str, limit: int = 42) -> str:
    clean = re.sub(r"[^A-Za-z0-9_]+", "_", text)
    clean = re.sub(r"_+", "_", clean).strip("_")
    return clean[:limit] or "candidate"


def load_stage1_rows(stage1_dir: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted(stage1_dir.glob("shard_*/trial_results.jsonl")):
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            row["shard"] = path.parent.name
            rows.append(row)
    if not rows:
        raise RuntimeError(f"no stage1 rows found under {stage1_dir}")
    return rows


def score(row: dict[str, object]) -> float:
    return float(row.get("score_mean", 0.0))


def low_fp(row: dict[str, object], limit: float = 0.05) -> bool:
    return float(row.get("c4_camera_false_positive_mean", 1.0)) <= limit


def select_anchors(rows: list[dict[str, object]], per_role: int) -> list[dict[str, object]]:
    selectors = [
        (
            "top_score",
            lambda r: True,
            lambda r: (
                score(r),
                float(r.get("clean_parent_worst_min", 0.0)),
                float(r.get("camera_stress_parent_worst_min", 0.0)),
            ),
        ),
        (
            "deploy_balance",
            lambda r: low_fp(r, 0.05)
            and float(r.get("estimated_board_us", 999999)) <= 7600
            and float(r.get("clean_parent_worst_min", 0.0)) >= 0.65,
            lambda r: (
                score(r),
                float(r.get("c4_camera_stress_recall_mean", 0.0)),
                -float(r.get("estimated_board_us", 999999)),
            ),
        ),
        (
            "fast",
            lambda r: low_fp(r, 0.06) and float(r.get("estimated_board_us", 999999)) <= 6000,
            lambda r: (
                score(r),
                float(r.get("hard_parent_accuracy_mean", 0.0)),
                float(r.get("camera_stress_parent_worst_min", 0.0)),
            ),
        ),
        (
            "camera",
            lambda r: low_fp(r, 0.06)
            and float(r.get("camera_stress_parent_worst_min", 0.0)) >= 0.55
            and float(r.get("clean_parent_accuracy_mean", 0.0)) >= 0.78,
            lambda r: (
                float(r.get("camera_stress_parent_worst_min", 0.0)),
                score(r),
                float(r.get("clean_parent_worst_min", 0.0)),
            ),
        ),
        (
            "c4_rescue",
            lambda r: low_fp(r, 0.05)
            and float(r.get("c4_camera_stress_recall_mean", 0.0)) >= 0.50
            and float(r.get("clean_parent_worst_min", 0.0)) >= 0.65,
            lambda r: (
                float(r.get("c4_camera_stress_recall_mean", 0.0)),
                score(r),
                float(r.get("hard_parent_accuracy_mean", 0.0)),
            ),
        ),
        (
            "low_fp",
            lambda r: float(r.get("c4_camera_false_positive_mean", 1.0)) <= 0.01
            and float(r.get("clean_parent_worst_min", 0.0)) >= 0.65,
            lambda r: (
                score(r),
                float(r.get("camera_stress_parent_worst_min", 0.0)),
                float(r.get("c4_camera_stress_recall_mean", 0.0)),
            ),
        ),
    ]
    selected: list[dict[str, object]] = []
    seen: set[str] = set()
    for role, predicate, key_fn in selectors:
        matches = [r for r in rows if predicate(r)]
        matches.sort(key=key_fn, reverse=True)
        for row in matches[:per_role]:
            label = str(row["trial"])
            if label in seen:
                continue
            row = dict(row)
            row["anchor_role"] = role
            selected.append(row)
            seen.add(label)
    if not selected:
        raise RuntimeError("no anchors selected from stage1 rows")
    return selected


def config_from_row(row: dict[str, object]) -> V5Config:
    config_data = row["config"]
    return config_from_dict(config_data, str(row["trial"]))


def clone(config: V5Config, name: str, **updates: object) -> V5Config:
    if "augment_name" in updates:
        augment_name = str(updates.pop("augment_name"))
        updates["augment"] = ALL_AUGMENTS[augment_name]
    return replace(config, name=name, **updates)


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
    if config.architecture != "spacetodepth_conv":
        return
    if config.head == "parent_weapon_c4_instance" and reason != "anchor_keep":
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
                "generator": "generate_v6_parent_primary_fine_candidates.py",
                "anchor_trial": str(anchor.get("trial", anchor.get("anchor_trial", ""))),
                "anchor_role": str(anchor.get("anchor_role", "")),
                "anchor_score": anchor.get("score_mean", 0.0),
            },
        }
    )


def candidate_priority(item: dict[str, object], rng: random.Random) -> tuple[float, int, str]:
    config = item["config"]
    source = item.get("source", {})
    lane = str(config["lane"])
    filters = tuple(int(v) for v in config["filters"])
    head = str(config["head"])
    augment = str(config["augment"]["name"])
    us = int(config["estimated_board_us"])

    role_bonus = {
        "deploy_balance": -0.045,
        "c4_rescue": -0.040,
        "fast": -0.035,
        "camera": -0.030,
        "top_score": -0.025,
        "low_fp": -0.020,
    }.get(str(source.get("anchor_role", "")), 0.0)
    head_penalty = {
        "parent_weapon_c4": 0.000,
        "parent_weapon_c4_box_circuit": 0.010,
        "parent_c4_box_circuit": 0.018,
        "parent_weapon_aux": 0.030,
        "parent_weapon_c4_instance": 0.080,
    }.get(head, 0.10)
    augment_penalty = {
        "sdiag_roi": 0.000,
        "sdiag_speed": 0.004,
        "sdiag_soft": 0.006,
        "v6_camera_blur_noise": 0.008,
        "v6_camera_mild": 0.010,
        "sdiag_lowres": 0.012,
        "v4_lowres_mix": 0.015,
        "sdiag_hard": 0.018,
        "sdiag_base": 0.020,
    }.get(augment, 0.030)
    target_filters = {
        "v6_fast": (6, 12, 24),
        "v6_balance": (8, 16, 24),
        "v6_accuracy": (10, 20, 40),
    }.get(lane, filters)
    filter_penalty = sum(abs(a - b) / max(1, b) for a, b in zip(filters, target_filters)) / 3.0
    lr = float(config["learning_rate"])
    lr_target = 0.00318 if lane != "v6_accuracy" else 0.0023
    lr_penalty = abs(lr - lr_target) * 45.0
    c4_pos = float(config["c4_pos_weight"])
    c4_pos_penalty = 0.0 if c4_pos == 4.0 else 0.018 if c4_pos in {2.0, 8.0} else 0.040
    gamma = float(config["c4_focal_gamma"])
    gamma_penalty = 0.0 if gamma in {0.0, 1.0} else 0.018
    teacher = float(config["teacher_loss_weight"])
    teacher_penalty = 0.0 if teacher == 0.0 else 0.012 if teacher <= 0.3 else 0.035
    dec_penalty = 0.055 if bool(config["decoupled"]) else 0.0
    batch_penalty = 0.0 if int(config["batch_size"]) == 16 else 0.012
    pool_penalty = 0.0 if str(config["pool"]) == "max" else 0.018
    latency_penalty = max(0.0, us - 7600) / 24000.0
    return (
        role_bonus
        + head_penalty
        + augment_penalty
        + 0.35 * filter_penalty
        + lr_penalty
        + c4_pos_penalty
        + gamma_penalty
        + teacher_penalty
        + dec_penalty
        + batch_penalty
        + pool_penalty
        + latency_penalty
        + rng.random() * 1.0e-5,
        us,
        str(item["label"]),
    )


def build_candidates(rows: list[dict[str, object]], limit: int, seed: int, per_role: int, max_board_us: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    anchors = select_anchors(rows, per_role)
    selected: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    pool: list[dict[str, object]] = []
    pool_seen: set[tuple[object, ...]] = set()

    def add_to_pool(config: V5Config, reason: str, anchor: dict[str, object]) -> None:
        temp: list[dict[str, object]] = []
        add_candidate(temp, pool_seen, config, reason=reason, anchor=anchor, max_board_us=max_board_us)
        pool.extend(temp)

    for anchor in anchors:
        base = config_from_row(anchor)
        base_slug = slug(base.name, 28)
        role = str(anchor.get("anchor_role", "anchor"))
        add_to_pool(clone(base, f"s2_{role}_{base_slug}_anchor"), "anchor_keep", anchor)

        lane_filters = FINE_FILTERS.get(base.lane, [base.filters])
        augments = FINE_AUGMENTS.get(base.lane, [base.augment.name])
        for filters in lane_filters:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_f{'-'.join(map(str, filters))}", filters=filters), "axis_filters", anchor)
        for lr_mul in [0.86, 0.93, 1.0, 1.08, 1.16]:
            lr = round(max(0.0012, min(0.0035, base.learning_rate * lr_mul)), 8)
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_lr{lr:g}", learning_rate=lr), "axis_lr", anchor)
        for l2 in [7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4, 3.0e-4]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_l2{l2:g}", l2=l2), "axis_l2", anchor)
        for dropout in [0.0, 0.003, 0.005, 0.008, 0.012, 0.02]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_do{dropout:g}", dropout=dropout), "axis_dropout", anchor)
        for augment in augments:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_{augment}", augment_name=augment), "axis_augment", anchor)
        for calibration in ["mild_stress", "hard_stress", "balanced_clean", "balanced_rotmirror"]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_cal_{calibration}", calibration=calibration), "axis_calibration", anchor)
        for head in FINE_HEADS + (["parent_weapon_aux"] if role == "camera" else []):
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_{head}", head=head, c4_instance_loss_weight=0.0), "axis_head", anchor)
        for subclass_weight in [0.25, 0.35]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_sub{subclass_weight:g}", subclass_loss_weight=subclass_weight), "axis_subclass_loss", anchor)
        for c4_loss in [0.15, 0.30, 0.45]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_c4{c4_loss:g}", c4_loss_weight=c4_loss), "axis_c4_loss", anchor)
        for c4_pos in [2.0, 4.0, 8.0]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_pos{c4_pos:g}", c4_pos_weight=c4_pos), "axis_c4_pos", anchor)
        for gamma in [0.0, 1.0, 2.0]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_gamma{gamma:g}", c4_focal_gamma=gamma), "axis_focal", anchor)
        for box_weight, circuit_weight in [(0.10, 0.20), (0.10, 0.40), (0.20, 0.40), (0.20, 0.80)]:
            add_to_pool(
                clone(
                    base,
                    f"s2_{role}_{base_slug}_box{box_weight:g}_ckt{circuit_weight:g}",
                    head="parent_weapon_c4_box_circuit" if base.head != "parent_c4_box_circuit" else base.head,
                    c4_box_loss_weight=box_weight,
                    c4_circuit_loss_weight=circuit_weight,
                    c4_instance_loss_weight=0.0,
                ),
                "axis_box_circuit",
                anchor,
            )
        for teacher in [0.0, 0.10, 0.30]:
            add_to_pool(clone(base, f"s2_{role}_{base_slug}_teacher{teacher:g}", teacher_loss_weight=teacher, teacher_temperature=2.0), "axis_teacher", anchor)

    random_attempts = max(limit * 12, 1200)
    for idx in range(random_attempts):
        anchor = rng.choice(anchors)
        base = config_from_row(anchor)
        role = str(anchor.get("anchor_role", "anchor"))
        lane = base.lane
        filters = rng.choice(FINE_FILTERS.get(lane, [base.filters]))
        augment = rng.choice(FINE_AUGMENTS.get(lane, [base.augment.name]))
        head_weights = [
            ("parent_weapon_c4", 0.42),
            ("parent_weapon_c4_box_circuit", 0.38),
            ("parent_c4_box_circuit", 0.16),
            ("parent_weapon_aux", 0.04),
        ]
        roll = rng.random()
        acc = 0.0
        head = "parent_weapon_c4"
        for name, weight in head_weights:
            acc += weight
            if roll <= acc:
                head = name
                break
        if lane == "v6_fast":
            lr_choices = [0.00286, 0.00302, 0.00318, 0.00326]
        elif lane == "v6_accuracy":
            lr_choices = [0.0020, 0.0023, 0.0026, 0.00286]
        else:
            lr_choices = [0.00286, 0.00302, 0.00310, 0.00318, 0.00326]
        teacher = rng.choices([0.0, 0.10, 0.30, 0.60], weights=[0.78, 0.10, 0.08, 0.04])[0]
        if role not in {"top_score", "c4_rescue"} and teacher == 0.60:
            teacher = 0.0
        config = clone(
            base,
            f"s2_rand_{idx:04d}_{slug(str(anchor['trial']), 24)}",
            architecture="spacetodepth_conv",
            filters=filters,
            learning_rate=float(rng.choice(lr_choices)),
            l2=float(rng.choice([7.0e-5, 1.0e-4, 1.25e-4, 1.5e-4, 2.0e-4])),
            dropout=float(rng.choice([0.0, 0.003, 0.005, 0.008, 0.012])),
            batch_size=int(rng.choices([16, 24], weights=[0.78, 0.22])[0]),
            pool=str(rng.choices(["max", "avg"], weights=[0.82, 0.18])[0]),
            activation=str(rng.choices(["relu", "relu6"], weights=[0.82, 0.18])[0]),
            augment_name=augment,
            head=head,
            calibration=str(rng.choice(["mild_stress", "hard_stress", "balanced_clean", "balanced_rotmirror"])),
            parent_loss_weight=float(rng.choice([1.0, 1.3, 1.6])),
            subclass_loss_weight=float(rng.choices([0.25, 0.35], weights=[0.35, 0.65])[0]),
            weapon_loss_weight=float(rng.choice([0.05, 0.10, 0.20])),
            c4_loss_weight=float(rng.choices([0.15, 0.30, 0.45], weights=[0.28, 0.60, 0.12])[0]),
            c4_pos_weight=float(rng.choices([2.0, 4.0, 8.0], weights=[0.18, 0.68, 0.14])[0]),
            c4_focal_gamma=float(rng.choices([0.0, 1.0, 2.0], weights=[0.48, 0.40, 0.12])[0]),
            c4_box_loss_weight=float(rng.choice([0.10, 0.20])),
            c4_circuit_loss_weight=float(rng.choices([0.20, 0.40, 0.80], weights=[0.24, 0.60, 0.16])[0]),
            c4_instance_loss_weight=0.0,
            teacher_loss_weight=float(teacher),
            teacher_temperature=float(rng.choice([2.0, 4.0])),
            decoupled=False,
        )
        add_to_pool(config, "local_random", anchor)

    pool.sort(key=lambda item: candidate_priority(item, rng))
    lane_quota = {
        "v6_fast": limit // 3,
        "v6_balance": limit // 3,
        "v6_accuracy": limit - 2 * (limit // 3),
    }
    lane_count: Counter[str] = Counter()
    for item in pool:
        lane = str(item["config"]["lane"])
        if lane_count[lane] >= lane_quota.get(lane, 0):
            continue
        config = config_from_dict(item["config"], str(item["label"]))
        before = len(selected)
        add_candidate(selected, seen, config, reason=str(item["source"]["reason"]), anchor=item["source"], max_board_us=max_board_us)
        if len(selected) > before:
            lane_count[lane] += 1
        if len(selected) >= limit:
            break
    for item in pool:
        if len(selected) >= limit:
            break
        config = config_from_dict(item["config"], str(item["label"]))
        add_candidate(selected, seen, config, reason=str(item["source"]["reason"]), anchor=item["source"], max_board_us=max_board_us)
    return selected


def print_summary(candidates: list[dict[str, object]], count: int) -> None:
    print(f"fine_candidates={len(candidates)}")
    print("by_lane", Counter(str(item["config"]["lane"]) for item in candidates))
    print("by_head", Counter(str(item["config"]["head"]) for item in candidates))
    print("by_anchor_role", Counter(str(item["source"].get("anchor_role", "")) for item in candidates))
    print("by_augment", Counter(str(item["config"]["augment"]["name"]) for item in candidates))
    for item in candidates[:count]:
        config = item["config"]
        source = item["source"]
        print(
            f"{item['label']} lane={config['lane']} head={config['head']} "
            f"filters={config['filters']} aug={config['augment']['name']} calib={config['calibration']} "
            f"teacher={config.get('teacher_loss_weight', 0)} us={config['estimated_board_us']} "
            f"anchor={source.get('anchor_trial')} role={source.get('anchor_role')}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate V6 parent-primary multi-objective fine candidates from stage1 coarse results.")
    parser.add_argument("--stage1-dir", type=Path, default=DEFAULT_STAGE1)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=480)
    parser.add_argument("--seed", type=int, default=20262910)
    parser.add_argument("--per-role", type=int, default=10)
    parser.add_argument("--max-board-us", type=int, default=11000)
    parser.add_argument("--print-top", type=int, default=32)
    args = parser.parse_args()

    rows = load_stage1_rows(args.stage1_dir)
    candidates = build_candidates(rows, args.limit, args.seed, args.per_role, args.max_board_us)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"candidates": candidates}, indent=2, ensure_ascii=False), encoding="utf-8")
    print_summary(candidates, args.print_top)
    print(f"output={args.output}")


if __name__ == "__main__":
    main()
