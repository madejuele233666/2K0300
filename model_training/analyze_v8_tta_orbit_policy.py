import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv
from stress_test_v8_low_margin import (
    apply_perturb,
    build_view_cache,
    classify_one,
    metric_weights_from_payload,
    perturb_by_name,
    prototypes_int8_from_payload,
    tflite_raw_int8,
)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def orbit_image(image: np.ndarray, name: str) -> np.ndarray:
    cur = image.astype(np.float32)
    if name == "identity":
        return cur.copy()
    if name.startswith("mirror_lr"):
        cur = np.flip(cur, axis=1)
        suffix = name.removeprefix("mirror_lr")
        if suffix.startswith("_"):
            name = suffix[1:]
        else:
            return cur.copy()
    if name == "rot90":
        return np.rot90(cur, 1, axes=(0, 1)).copy()
    if name == "rot180":
        return np.rot90(cur, 2, axes=(0, 1)).copy()
    if name == "rot270":
        return np.rot90(cur, 3, axes=(0, 1)).copy()
    raise ValueError(f"unknown orbit view: {name}")


def images_from_payload(dataset_dir: Path, payload: dict[str, np.ndarray]) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    parent = np.asarray(payload["parent"], dtype=np.int64)
    view_cache, _clean_x, _y_parent, _paths = build_view_cache(dataset_dir, sorted(set(view_labels.tolist())))
    images = [view_cache[str(view)][int(sample)] for sample, view in zip(sample_index.tolist(), view_labels.tolist(), strict=False)]
    return np.stack(images).astype(np.float32), parent, sample_index, view_labels


def images_from_stress_rows(
    *,
    dataset_dir: Path,
    rows: list[dict[str, str]],
    seed: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    view_names = sorted(set(str(row["view_label"]) for row in rows))
    view_cache, _clean_x, _y_parent, _paths = build_view_cache(dataset_dir, view_names)
    perturb_cache = {item.name: item for item in perturb_by_name(sorted(set(str(row["perturb"]) for row in rows)))}
    images: list[np.ndarray] = []
    parent: list[int] = []
    groups: list[str] = []
    for row in rows:
        view = str(row["view_label"])
        sample = int(row["sample_index"])
        query_index = int(row["base_query_index"])
        perturb = perturb_cache[str(row["perturb"])]
        rng_seed = seed + query_index * 1009 + sum((i + 1) * ord(ch) for i, ch in enumerate(perturb.name))
        rng = np.random.default_rng(rng_seed)
        images.append(apply_perturb(view_cache[view][sample], perturb, rng))
        parent.append(int(row["parent"]))
        groups.append(str(row["group"]))
    return np.stack(images).astype(np.float32), np.asarray(parent, dtype=np.int64), np.asarray(groups).astype(str)


def classify_features(
    *,
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    metric_weights: np.ndarray | None,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    pred = np.empty(len(features), dtype=np.int64)
    margin = np.empty(len(features), dtype=np.int64)
    class_dist = np.empty((len(features), 3), dtype=np.int64)
    for index, feature in enumerate(features):
        cls = classify_one(feature, prototypes, prototype_parent, metric_weights=metric_weights)
        pred[index] = int(cls["pred"])
        margin[index] = int(cls["margin"])
        class_dist[index, 0] = int(cls["class0_dist"])
        class_dist[index, 1] = int(cls["class1_dist"])
        class_dist[index, 2] = int(cls["class2_dist"])
    return pred, margin, class_dist


def evaluate_orbits(
    *,
    tflite_path: Path,
    images: np.ndarray,
    orbit_views: list[str],
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    metric_weights: np.ndarray | None,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str]]:
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    class_rows: list[np.ndarray] = []
    ops: list[str] = []
    for orbit in orbit_views:
        transformed = np.stack([orbit_image(image, orbit) for image in images]).astype(np.float32)
        features, ops = tflite_raw_int8(tflite_path, transformed)
        pred, margin, class_dist = classify_features(
            features=features,
            prototypes=prototypes,
            prototype_parent=prototype_parent,
            metric_weights=metric_weights,
        )
        pred_rows.append(pred)
        margin_rows.append(margin)
        class_rows.append(class_dist)
    return (
        np.stack(pred_rows, axis=0),
        np.stack(margin_rows, axis=0),
        np.stack(class_rows, axis=0),
        ops,
    )


def choose_min_score(scores: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(scores, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (scores[np.arange(len(scores)), order[:, 1]] - scores[np.arange(len(scores)), order[:, 0]]).astype(np.float64)
    return pred, margin


def choose_max_score(scores: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(-scores, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (scores[np.arange(len(scores)), order[:, 0]] - scores[np.arange(len(scores)), order[:, 1]]).astype(np.float64)
    return pred, margin


def rank_scores(class_dist: np.ndarray) -> np.ndarray:
    score = np.zeros((class_dist.shape[1], 3), dtype=np.float64)
    for view_index in range(class_dist.shape[0]):
        order = np.argsort(class_dist[view_index], axis=1)
        for row_index in range(order.shape[0]):
            for rank, cls in enumerate(order[row_index].tolist()):
                score[row_index, int(cls)] += float(rank)
    return score


def policy_predictions(
    *,
    pred_by_view: np.ndarray,
    margin_by_view: np.ndarray,
    class_dist_by_view: np.ndarray,
    thresholds: list[int],
) -> dict[str, tuple[np.ndarray, np.ndarray]]:
    policies: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    identity_pred = pred_by_view[0].astype(np.int64)
    identity_margin = margin_by_view[0].astype(np.float64)
    policies["identity"] = (identity_pred, identity_margin)

    best_view = np.argmax(margin_by_view, axis=0)
    best_pred = pred_by_view[best_view, np.arange(pred_by_view.shape[1])].astype(np.int64)
    best_margin = margin_by_view[best_view, np.arange(pred_by_view.shape[1])].astype(np.float64)
    policies["max_orbit_margin"] = (best_pred, best_margin)

    min_dist = np.min(class_dist_by_view.astype(np.float64), axis=0)
    policies["min_orbit_class_distance"] = choose_min_score(min_dist)

    sum_dist = np.sum(class_dist_by_view.astype(np.float64), axis=0)
    policies["sum_orbit_class_distance"] = choose_min_score(sum_dist)

    policies["rank_sum_orbit"] = choose_min_score(rank_scores(class_dist_by_view))

    vote_score = np.zeros((pred_by_view.shape[1], 3), dtype=np.float64)
    vote_margin_score = np.zeros((pred_by_view.shape[1], 3), dtype=np.float64)
    for view_index in range(pred_by_view.shape[0]):
        for cls in range(3):
            mask = pred_by_view[view_index] == cls
            vote_score[mask, cls] += 1.0
            vote_margin_score[mask, cls] += np.log1p(np.maximum(margin_by_view[view_index, mask], 0.0))
    policies["majority_orbit_vote"] = choose_max_score(vote_score)
    policies["majority_orbit_log_margin"] = choose_max_score(vote_margin_score)

    for threshold in thresholds:
        use_orbit = identity_margin <= float(threshold)
        guarded_pred = np.where(use_orbit, best_pred, identity_pred).astype(np.int64)
        guarded_margin = np.where(use_orbit, best_margin, identity_margin).astype(np.float64)
        policies[f"guard_identity_t{threshold}_max_margin"] = (guarded_pred, guarded_margin)

        min_pred, min_margin = policies["min_orbit_class_distance"]
        guarded_pred = np.where(use_orbit, min_pred, identity_pred).astype(np.int64)
        guarded_margin = np.where(use_orbit, min_margin, identity_margin).astype(np.float64)
        policies[f"guard_identity_t{threshold}_min_distance"] = (guarded_pred, guarded_margin)
    return policies


def summarize_predictions(
    *,
    pred: np.ndarray,
    margin: np.ndarray,
    parent: np.ndarray,
    groups: np.ndarray,
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for pred_value, parent_value, group in zip(pred.tolist(), parent.tolist(), groups.tolist(), strict=False):
        wrong = int(int(pred_value) != int(parent_value))
        grouped[str(group)][0] += wrong
        grouped[str(group)][1] += 1
    wrong_events = int(np.sum(pred.astype(np.int64) != parent.astype(np.int64)))
    return {
        "wrong_events": wrong_events,
        "total_events": int(len(parent)),
        "wrong_rate": float(wrong_events / max(len(parent), 1)),
        "low_wrong_rate": float(grouped.get("low", [0, 1])[0] / max(grouped.get("low", [0, 1])[1], 1)),
        "control_wrong_rate": float(grouped.get("control", [0, 1])[0] / max(grouped.get("control", [0, 1])[1], 1)),
        "margin_min": float(np.min(margin)),
        "margin_p05": float(np.percentile(margin, 5)),
        "margin_median": float(np.median(margin)),
    }


def per_group_rows(
    *,
    policy: str,
    pred: np.ndarray,
    margin: np.ndarray,
    parent: np.ndarray,
    rows: list[dict[str, str]],
) -> list[dict[str, Any]]:
    buckets: dict[tuple[str, str, str], list[int]] = defaultdict(list)
    for index, row in enumerate(rows):
        buckets[(str(row["group"]), str(row["perturb_family"]), str(row["perturb"]))].append(index)
    out: list[dict[str, Any]] = []
    for (group, family, perturb), indexes in buckets.items():
        idx = np.asarray(indexes, dtype=np.int64)
        wrong = pred[idx].astype(np.int64) != parent[idx].astype(np.int64)
        out.append(
            {
                "policy": policy,
                "group": group,
                "perturb_family": family,
                "perturb": perturb,
                "total": int(len(idx)),
                "wrong": int(np.sum(wrong)),
                "wrong_rate": float(np.mean(wrong)),
                "margin_median": float(np.median(margin[idx])),
            }
        )
    return sorted(out, key=lambda row: (float(row["wrong_rate"]), -int(row["total"]), str(row["perturb"])))


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate normal-only TTA/orbit policies for V8 prototype inference.")
    parser.add_argument("--tflite", type=Path, required=True)
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--orbit-views",
        default="identity,rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270",
    )
    parser.add_argument("--guard-thresholds", default="1,2,4,8,16,32,64,128")
    parser.add_argument("--highstress-seed", type=int, default=20260520)
    parser.add_argument("--skip-normal", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    orbit_views = parse_csv(args.orbit_views)
    thresholds = [int(item) for item in parse_csv(args.guard_thresholds)]
    payload = load_npz(args.params_npz)
    prototypes, prototype_parent = prototypes_int8_from_payload(payload)
    metric_weights = metric_weights_from_payload(payload, prototypes.shape[1])

    stress_rows = read_csv_rows(args.stress_events_csv)
    high_images, high_parent, high_groups = images_from_stress_rows(
        dataset_dir=args.dataset_dir,
        rows=stress_rows,
        seed=int(args.highstress_seed),
    )
    high_pred_view, high_margin_view, high_class_view, high_ops = evaluate_orbits(
        tflite_path=args.tflite,
        images=high_images,
        orbit_views=orbit_views,
        prototypes=prototypes,
        prototype_parent=prototype_parent,
        metric_weights=metric_weights,
    )
    high_policies = policy_predictions(
        pred_by_view=high_pred_view,
        margin_by_view=high_margin_view,
        class_dist_by_view=high_class_view,
        thresholds=thresholds,
    )

    normal_policies: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    normal_parent = np.zeros(0, dtype=np.int64)
    if not args.skip_normal:
        normal_images, normal_parent, _sample_index, _view_labels = images_from_payload(args.dataset_dir, payload)
        normal_pred_view, normal_margin_view, normal_class_view, _normal_ops = evaluate_orbits(
            tflite_path=args.tflite,
            images=normal_images,
            orbit_views=orbit_views,
            prototypes=prototypes,
            prototype_parent=prototype_parent,
            metric_weights=metric_weights,
        )
        normal_policies = policy_predictions(
            pred_by_view=normal_pred_view,
            margin_by_view=normal_margin_view,
            class_dist_by_view=normal_class_view,
            thresholds=thresholds,
        )

    policy_rows: list[dict[str, Any]] = []
    per_view_rows: list[dict[str, Any]] = []
    high_group_rows: list[dict[str, Any]] = []
    for policy, (high_pred, high_margin) in high_policies.items():
        high_summary = summarize_predictions(
            pred=high_pred,
            margin=high_margin,
            parent=high_parent,
            groups=high_groups,
        )
        if normal_policies:
            normal_pred, normal_margin = normal_policies[policy]
            normal_wrong = int(np.sum(normal_pred.astype(np.int64) != normal_parent.astype(np.int64)))
            normal_margin_min = float(np.min(normal_margin))
        else:
            normal_wrong = -1
            normal_margin_min = float("nan")
        policy_rows.append(
            {
                "policy": policy,
                "orbit_views": ",".join(orbit_views),
                "normal_replay_100": bool(normal_wrong == 0) if normal_wrong >= 0 else None,
                "normal_wrong_events": int(normal_wrong),
                "normal_margin_min": normal_margin_min,
                "high_pressure_usage": "evaluation_only",
                **{f"high_{key}": value for key, value in high_summary.items()},
            }
        )
        high_group_rows.extend(per_group_rows(policy=policy, pred=high_pred, margin=high_margin, parent=high_parent, rows=stress_rows))

    for view_index, view in enumerate(orbit_views):
        summary = summarize_predictions(
            pred=high_pred_view[view_index],
            margin=high_margin_view[view_index],
            parent=high_parent,
            groups=high_groups,
        )
        per_view_rows.append({"orbit_view": view, **{f"high_{key}": value for key, value in summary.items()}})

    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            not bool(row.get("normal_replay_100")),
            float(row["high_low_wrong_rate"]),
            float(row["high_control_wrong_rate"]),
            float(row["high_wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "orbit_view_summary.csv", per_view_rows)
    write_csv(args.output_dir / "policy_perturb_summary.csv", high_group_rows)
    write_json(
        args.output_dir / "summary.json",
        {
            "tflite": str(args.tflite),
            "params_npz": str(args.params_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "dataset_dir": str(args.dataset_dir),
            "orbit_views": orbit_views,
            "guard_thresholds": thresholds,
            "high_pressure_usage": "evaluation_only",
            "normal_policy_selection": "reported_all_fixed_policies_no_highpressure_training",
            "tflite_unique_ops": high_ops,
            "top_policies": policy_rows[:20],
            "orbit_view_summary": per_view_rows,
            "high_wrong_policy_counter": dict(Counter(row["policy"] for row in high_group_rows if int(row["wrong"]) > 0)),
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "top_policies": policy_rows[:10]}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
