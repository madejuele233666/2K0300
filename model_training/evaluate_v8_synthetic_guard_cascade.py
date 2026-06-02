import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from estimate_v8_board_time import calibrated_conservative_us
from evaluate_v8_region_guard_cascade import (
    class_distances,
    evaluate_normal,
    evaluate_stress,
    guard_distances,
    read_csv_rows,
)
from evaluate_v8_embedding_prototypes import write_csv
from stress_test_v8_low_margin import tflite_raw_int8
from train_v8_end_to_end_embedding import build_view_dataset


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def parse_modes(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def view_family(view: str) -> str:
    if view == "clean":
        return "clean"
    if view.startswith("rot") or view.startswith("mirror"):
        return "d4"
    if "shift" in view:
        return "shift"
    if "bright" in view:
        return "brightness"
    if "contrast" in view:
        return "contrast"
    if "noise" in view and "blur" in view:
        return "blur_noise"
    if "noise" in view:
        return "noise"
    if "blur" in view:
        return "blur"
    return "other"


def stable_unique(values: np.ndarray) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for value in values.astype(str).tolist():
        if value not in seen:
            out.append(value)
            seen.add(value)
    return out


def select_teacher_images(
    *,
    teacher: dict[str, np.ndarray],
    dataset_dir: Path,
) -> tuple[dict[str, Any], np.ndarray]:
    teacher_sample = np.asarray(teacher["sample_index"], dtype=np.int64)
    teacher_view = np.asarray(teacher["view_labels"]).astype(str)
    stress_names = [view for view in stable_unique(teacher_view) if view != "clean"]
    flat, images = build_view_dataset(dataset_dir, stress_names)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist(), strict=False))
    }
    indexes: list[int] = []
    missing: list[tuple[int, str]] = []
    for sample, view in zip(teacher_sample.tolist(), teacher_view.tolist(), strict=False):
        key = (int(sample), str(view))
        row_index = row_by_key.get(key)
        if row_index is None:
            missing.append(key)
            continue
        indexes.append(int(row_index))
    if missing:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing[:10])
        raise ValueError(f"teacher rows missing from synthetic view dataset: {len(missing)}, first {preview}")
    return flat, images[np.asarray(indexes, dtype=np.int64)]


def compute_tflite_features(tflite: Path, images: np.ndarray, batch_size: int) -> tuple[np.ndarray, list[str]]:
    chunks: list[np.ndarray] = []
    ops: list[str] = []
    for start in range(0, len(images), int(batch_size)):
        features, ops = tflite_raw_int8(tflite, images[start : start + int(batch_size)])
        chunks.append(features)
    return np.concatenate(chunks).astype(np.int8), ops


def build_candidate_order(
    *,
    teacher: dict[str, np.ndarray],
    syn_pred: np.ndarray,
    syn_margin: np.ndarray,
    syn_class_dist: np.ndarray,
    order_mode: str,
    per_family_cap: int,
    source_mode: str,
    max_primary_margin: int,
    min_source_advantage: float,
) -> tuple[list[int], list[dict[str, Any]]]:
    parent = np.asarray(teacher["parent"], dtype=np.int64)
    view_labels = np.asarray(teacher["view_labels"]).astype(str)
    source_label = np.asarray(teacher["source_label"], dtype=np.int64)
    source_names = np.asarray(teacher["source_names"]).astype(str)
    score_matrix = np.asarray(teacher["score_matrix"], dtype=np.float32)
    top_gap = np.asarray(teacher["top_gap"], dtype=np.float32)
    weights = np.asarray(teacher["weight"], dtype=np.float32)
    order = np.argsort(syn_class_dist, axis=1)
    nearest_wrong_parent = np.where(order[:, 0] == parent, order[:, 1], order[:, 0]).astype(np.int64)
    source_score = score_matrix[np.arange(len(source_label)), source_label]
    d4_score = score_matrix[:, 0]
    source_advantage = (source_score - d4_score).astype(np.float32)
    rows: list[dict[str, Any]] = []
    indexed: list[tuple[tuple[Any, ...], int]] = []
    for index in range(len(parent)):
        label = int(source_label[index])
        primary_wrong = bool(int(syn_pred[index]) != int(parent[index]))
        if source_mode == "non_d4" and label == 0:
            continue
        if source_mode == "source_adv_positive" and float(source_advantage[index]) <= 0.0:
            continue
        if source_mode == "non_d4_or_adv" and label == 0 and float(source_advantage[index]) <= 0.0:
            continue
        if source_mode not in {"all", "non_d4", "source_adv_positive", "non_d4_or_adv"}:
            raise ValueError(f"unknown source mode: {source_mode}")
        if max_primary_margin >= 0 and int(syn_margin[index]) > int(max_primary_margin) and not primary_wrong:
            continue
        if float(source_advantage[index]) < float(min_source_advantage):
            continue
        competitor = int(syn_pred[index]) if primary_wrong else int(nearest_wrong_parent[index])
        family = view_family(str(view_labels[index]))
        score = (
            0 if primary_wrong else 1,
            int(syn_margin[index]),
            -float(source_advantage[index]),
            -float(top_gap[index]),
            -float(weights[index]),
            int(parent[index]),
            competitor,
            str(family),
            int(index),
        )
        row = {
            "teacher_row": int(index),
            "sample_index": int(np.asarray(teacher["sample_index"], dtype=np.int64)[index]),
            "view_label": str(view_labels[index]),
            "view_family": family,
            "parent": int(parent[index]),
            "synthetic_primary_pred": int(syn_pred[index]),
            "synthetic_primary_wrong": primary_wrong,
            "synthetic_primary_margin": int(syn_margin[index]),
            "guard_parent": int(parent[index]),
            "guard_competitor": competitor,
            "source_label": label,
            "source_name": str(source_names[label]),
            "source_score": float(source_score[index]),
            "d4_score": float(d4_score[index]),
            "source_advantage": float(source_advantage[index]),
            "top_gap": float(top_gap[index]),
            "weight": float(weights[index]),
        }
        rows.append(row)
        indexed.append((score, len(rows) - 1))

    if order_mode == "score":
        ordered_row_indexes = [row_index for _score, row_index in sorted(indexed, key=lambda item: item[0])]
        return [int(rows[row_index]["teacher_row"]) for row_index in ordered_row_indexes], rows
    if order_mode != "family_roundrobin":
        raise ValueError(f"unknown order mode: {order_mode}")

    by_family: dict[tuple[int, int, str, int], list[tuple[tuple[Any, ...], int]]] = {}
    for score, row_index in indexed:
        row = rows[row_index]
        key = (
            int(row["guard_parent"]),
            int(row["guard_competitor"]),
            str(row["view_family"]),
            int(row["source_label"]),
        )
        by_family.setdefault(key, []).append((score, row_index))
    for values in by_family.values():
        values.sort(key=lambda item: item[0])
    keys = sorted(by_family)
    selected: list[int] = []
    round_index = 0
    while True:
        added = False
        for key in keys:
            if per_family_cap > 0 and round_index >= int(per_family_cap):
                continue
            values = by_family[key]
            if round_index >= len(values):
                continue
            selected.append(int(rows[values[round_index][1]]["teacher_row"]))
            added = True
        if not added:
            break
        round_index += 1
    return selected, rows


def safe_radii(
    *,
    normal_parent: np.ndarray,
    normal_guard_dist: np.ndarray,
    guard_parent: np.ndarray,
    mode: str,
) -> np.ndarray:
    if len(guard_parent) == 0:
        return np.zeros(0, dtype=np.int64)
    if mode == "none":
        return np.full(len(guard_parent), np.iinfo(np.int64).max, dtype=np.int64)
    if mode != "nonparent":
        raise ValueError(f"unknown safe radius mode: {mode}")
    out: list[int] = []
    for guard_index, candidate_parent in enumerate(guard_parent.tolist()):
        mask = normal_parent != int(candidate_parent)
        out.append(int(np.min(normal_guard_dist[mask, guard_index])) - 1 if np.any(mask) else np.iinfo(np.int64).max)
    return np.asarray(out, dtype=np.int64)


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate synthetic source-choice guard cascade under a D4 prototype budget.")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--base-tflite", type=Path, required=True)
    parser.add_argument("--base-train-config", type=Path, required=True)
    parser.add_argument("--synthetic-teacher-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--budgets", default="0,32,64,128,192,256,320,360")
    parser.add_argument("--radii", default="4,8,16,32,64,128,256,512,1024,2048,4096")
    parser.add_argument("--candidate-gaps", default="0,8,16,32,64,128,256,512")
    parser.add_argument("--primary-margin-maxes", default="8,16,32,64,128,256,-1")
    parser.add_argument("--match-modes", default="competitor,any")
    parser.add_argument("--order-mode", choices=["score", "family_roundrobin"], default="family_roundrobin")
    parser.add_argument("--per-family-cap", type=int, default=0)
    parser.add_argument(
        "--source-mode",
        choices=["all", "non_d4", "source_adv_positive", "non_d4_or_adv"],
        default="non_d4_or_adv",
    )
    parser.add_argument("--candidate-primary-margin-max", type=int, default=256)
    parser.add_argument("--min-source-advantage", type=float, default=0.0)
    parser.add_argument("--safe-radius-mode", choices=["none", "nonparent"], default="nonparent")
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base = load_npz(args.base_params_npz)
    teacher = load_npz(args.synthetic_teacher_npz)
    config = json.loads(args.base_train_config.read_text(encoding="utf-8"))
    shutil.copy2(args.base_train_config, args.output_dir / "train_config.json")

    normal_features = np.asarray(base["embedding_int8"], dtype=np.int8)
    normal_parent = np.asarray(base["parent"], dtype=np.int64)
    normal_view = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    normal_pred, normal_margin, normal_class_dist = class_distances(normal_features, prototypes, prototype_parent)
    if not np.all(normal_pred == normal_parent):
        raise ValueError("base normal int8 replay is not 100%; refuse to evaluate synthetic guards")

    _flat, teacher_images = select_teacher_images(teacher=teacher, dataset_dir=args.dataset_dir)
    synthetic_features, ops = compute_tflite_features(args.base_tflite, teacher_images, args.batch_size)
    syn_pred, syn_margin, syn_class_dist = class_distances(synthetic_features, prototypes, prototype_parent)
    guard_order, candidate_rows = build_candidate_order(
        teacher=teacher,
        syn_pred=syn_pred,
        syn_margin=syn_margin,
        syn_class_dist=syn_class_dist,
        order_mode=args.order_mode,
        per_family_cap=args.per_family_cap,
        source_mode=args.source_mode,
        max_primary_margin=args.candidate_primary_margin_max,
        min_source_advantage=args.min_source_advantage,
    )
    candidate_by_teacher = {int(row["teacher_row"]): row for row in candidate_rows}

    stress_rows = read_csv_rows(args.stress_events_csv)
    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_pred, stress_margin, stress_class_dist = class_distances(stress_features, prototypes, prototype_parent)

    source_label = np.asarray(teacher["source_label"], dtype=np.int64)
    source_names = np.asarray(teacher["source_names"]).astype(str)
    parent = np.asarray(teacher["parent"], dtype=np.int64)
    syn_order = np.argsort(syn_class_dist, axis=1)
    nearest_wrong_parent = np.where(syn_order[:, 0] == parent, syn_order[:, 1], syn_order[:, 0]).astype(np.int64)

    budgets = parse_ints(args.budgets)
    radii = parse_ints(args.radii)
    candidate_gaps = parse_ints(args.candidate_gaps)
    primary_margin_maxes = parse_ints(args.primary_margin_maxes)
    match_modes = parse_modes(args.match_modes)
    rows: list[dict[str, Any]] = []
    best_valid: dict[str, Any] | None = None
    best_payload: tuple[int, int, int, int, str] | None = None
    base_proto_count = int(len(prototypes))
    feature_dim = int(normal_features.shape[1])
    backbone_conservative = calibrated_conservative_us(config)

    for budget in budgets:
        selected_teacher_rows = guard_order[: max(0, int(budget))]
        guard_code = synthetic_features[selected_teacher_rows] if selected_teacher_rows else np.zeros((0, feature_dim), dtype=np.int8)
        guard_parent = parent[selected_teacher_rows] if selected_teacher_rows else np.zeros(0, dtype=np.int64)
        primary_wrong = syn_pred[selected_teacher_rows] != parent[selected_teacher_rows] if selected_teacher_rows else np.zeros(0, dtype=bool)
        guard_competitor = (
            np.where(primary_wrong, syn_pred[selected_teacher_rows], nearest_wrong_parent[selected_teacher_rows]).astype(np.int64)
            if selected_teacher_rows
            else np.zeros(0, dtype=np.int64)
        )
        guard_count = int(len(selected_teacher_rows))
        normal_guard_dist = guard_distances(normal_features, guard_code)
        stress_guard_dist = guard_distances(stress_features, guard_code)
        guard_safe_radius = safe_radii(
            normal_parent=normal_parent,
            normal_guard_dist=normal_guard_dist,
            guard_parent=guard_parent,
            mode=args.safe_radius_mode,
        )
        normal_guard_order = (
            np.argsort(normal_guard_dist, axis=1) if guard_count else np.zeros((len(normal_features), 0), dtype=np.int64)
        )
        stress_guard_order = (
            np.argsort(stress_guard_dist, axis=1) if guard_count else np.zeros((len(stress_features), 0), dtype=np.int64)
        )
        estimated_distance_macs = int((base_proto_count + guard_count) * feature_dim)
        board_total_conservative = float(backbone_conservative + max(20.0, estimated_distance_macs * 0.02))
        for radius in radii:
            for candidate_gap in candidate_gaps:
                for primary_margin_max in primary_margin_maxes:
                    for match_mode in match_modes:
                        normal_row, _normal_traces = evaluate_normal(
                            parent=normal_parent,
                            view_labels=normal_view,
                            pred=normal_pred,
                            margin=normal_margin,
                            class_dist=normal_class_dist,
                            guard_dist=normal_guard_dist,
                            guard_order=normal_guard_order,
                            guard_parent=guard_parent,
                            guard_competitor=guard_competitor,
                            guard_safe_radius=guard_safe_radius,
                            radius=radius,
                            candidate_gap=candidate_gap,
                            primary_margin_max=primary_margin_max,
                            match_mode=match_mode,
                        )
                        stress_summary, _stress_out, _stress_traces = evaluate_stress(
                            stress_rows=stress_rows,
                            pred=stress_pred,
                            margin=stress_margin,
                            class_dist=stress_class_dist,
                            guard_dist=stress_guard_dist,
                            guard_order=stress_guard_order,
                            guard_parent=guard_parent,
                            guard_competitor=guard_competitor,
                            guard_safe_radius=guard_safe_radius,
                            radius=radius,
                            candidate_gap=candidate_gap,
                            primary_margin_max=primary_margin_max,
                            match_mode=match_mode,
                        )
                        row: dict[str, Any] = {
                            "stage": "v8_synthetic_guard_cascade",
                            "name": f"synthguard_b{guard_count}_r{radius}_gap{candidate_gap}_pm{primary_margin_max}_{match_mode}",
                            "feature_source": str(np.asarray(base.get("feature_source", np.asarray("int8_tflite"))).item()),
                            "prototype_source": "base_table_plus_synthetic_guards",
                            "feature_dim": feature_dim,
                            "prototype_count": int(base_proto_count + guard_count),
                            "base_prototype_count": base_proto_count,
                            "guard_count": guard_count,
                            "estimated_distance_macs": estimated_distance_macs,
                            "radius": int(radius),
                            "candidate_gap": int(candidate_gap),
                            "primary_margin_max": int(primary_margin_max),
                            "match_mode": match_mode,
                            "order_mode": args.order_mode,
                            "source_mode": args.source_mode,
                            "candidate_primary_margin_max": int(args.candidate_primary_margin_max),
                            "min_source_advantage": float(args.min_source_advantage),
                            "safe_radius_mode": args.safe_radius_mode,
                            "guard_safe_radius_min": int(np.min(guard_safe_radius)) if len(guard_safe_radius) else "",
                            "guard_safe_radius_median": float(np.median(guard_safe_radius)) if len(guard_safe_radius) else "",
                            "board_backbone_conservative_us": int(round(backbone_conservative)),
                            "board_total_conservative_us": int(round(board_total_conservative)),
                            "under_2ms_conservative": bool(board_total_conservative <= 2000.0),
                            "tflite_unique_ops": json.dumps(ops, ensure_ascii=False),
                            "high_pressure_usage": "evaluation_only",
                            "selection_usage": "non_highpressure_synthetic_teacher_only",
                            **normal_row,
                            **{key: value for key, value in stress_summary.items() if key != "per_group"},
                            "high_pressure_per_group_json": json.dumps(stress_summary["per_group"], ensure_ascii=False),
                        }
                        rows.append(row)
                        if bool(row["all_correct"]) and bool(row["under_2ms_conservative"]):
                            key = (
                                float(row["high_pressure_low_wrong_rate"]),
                                float(row["high_pressure_control_wrong_rate"]),
                                int(row["wrong_events"]),
                                int(row["estimated_distance_macs"]),
                            )
                            if best_valid is None or key < (
                                float(best_valid["high_pressure_low_wrong_rate"]),
                                float(best_valid["high_pressure_control_wrong_rate"]),
                                int(best_valid["wrong_events"]),
                                int(best_valid["estimated_distance_macs"]),
                            ):
                                best_valid = row
                                best_payload = (guard_count, radius, candidate_gap, primary_margin_max, match_mode)

    write_csv(args.output_dir / "candidate_results.csv", rows)
    write_csv(args.output_dir / "candidate_guard_rows.csv", candidate_rows)
    if best_payload is not None:
        guard_count, radius, candidate_gap, primary_margin_max, match_mode = best_payload
        selected_teacher_rows = guard_order[:guard_count]
        guard_code = synthetic_features[selected_teacher_rows] if selected_teacher_rows else np.zeros((0, feature_dim), dtype=np.int8)
        guard_parent = parent[selected_teacher_rows] if selected_teacher_rows else np.zeros(0, dtype=np.int64)
        primary_wrong = syn_pred[selected_teacher_rows] != parent[selected_teacher_rows] if selected_teacher_rows else np.zeros(0, dtype=bool)
        guard_competitor = (
            np.where(primary_wrong, syn_pred[selected_teacher_rows], nearest_wrong_parent[selected_teacher_rows]).astype(np.int64)
            if selected_teacher_rows
            else np.zeros(0, dtype=np.int64)
        )
        normal_guard_dist = guard_distances(normal_features, guard_code)
        stress_guard_dist = guard_distances(stress_features, guard_code)
        guard_safe_radius = safe_radii(
            normal_parent=normal_parent,
            normal_guard_dist=normal_guard_dist,
            guard_parent=guard_parent,
            mode=args.safe_radius_mode,
        )
        normal_guard_order = np.argsort(normal_guard_dist, axis=1) if guard_count else np.zeros((len(normal_features), 0), dtype=np.int64)
        stress_guard_order = np.argsort(stress_guard_dist, axis=1) if guard_count else np.zeros((len(stress_features), 0), dtype=np.int64)
        normal_row, normal_traces = evaluate_normal(
            parent=normal_parent,
            view_labels=normal_view,
            pred=normal_pred,
            margin=normal_margin,
            class_dist=normal_class_dist,
            guard_dist=normal_guard_dist,
            guard_order=normal_guard_order,
            guard_parent=guard_parent,
            guard_competitor=guard_competitor,
            guard_safe_radius=guard_safe_radius,
            radius=radius,
            candidate_gap=candidate_gap,
            primary_margin_max=primary_margin_max,
            match_mode=match_mode,
        )
        stress_summary, stress_out, stress_traces = evaluate_stress(
            stress_rows=stress_rows,
            pred=stress_pred,
            margin=stress_margin,
            class_dist=stress_class_dist,
            guard_dist=stress_guard_dist,
            guard_order=stress_guard_order,
            guard_parent=guard_parent,
            guard_competitor=guard_competitor,
            guard_safe_radius=guard_safe_radius,
            radius=radius,
            candidate_gap=candidate_gap,
            primary_margin_max=primary_margin_max,
            match_mode=match_mode,
        )
        selected_candidate_rows = [candidate_by_teacher[int(index)] for index in selected_teacher_rows]
        best_dir = args.output_dir / "best_valid"
        write_json(
            best_dir / "summary.json",
            {
                "normal": normal_row,
                "stress": stress_summary,
                "guard_count": int(guard_count),
                "radius": int(radius),
                "candidate_gap": int(candidate_gap),
                "primary_margin_max": int(primary_margin_max),
                "match_mode": match_mode,
                "safe_radius_mode": args.safe_radius_mode,
                "guard_safe_radius_min": int(np.min(guard_safe_radius)) if len(guard_safe_radius) else "",
                "guard_safe_radius_median": float(np.median(guard_safe_radius)) if len(guard_safe_radius) else "",
                "high_pressure_usage": "evaluation_only",
                "selection_usage": "non_highpressure_synthetic_teacher_only",
                "source_usage": {
                    str(source_names[source]): int(np.sum(source_label[selected_teacher_rows] == source))
                    for source in sorted(set(source_label[selected_teacher_rows].tolist()))
                }
                if selected_teacher_rows
                else {},
            },
        )
        write_csv(best_dir / "selected_guard_rows.csv", selected_candidate_rows)
        write_csv(best_dir / "normal_traces.csv", normal_traces)
        write_csv(best_dir / "stress_events.csv", stress_out)
        write_csv(best_dir / "stress_traces.csv", stress_traces)

    write_json(
        args.output_dir / "summary.json",
        {
            "base_params_npz": str(args.base_params_npz),
            "base_tflite": str(args.base_tflite),
            "synthetic_teacher_npz": str(args.synthetic_teacher_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "non_highpressure_synthetic_teacher_only",
            "candidate_count": int(len(candidate_rows)),
            "ordered_candidate_count": int(len(guard_order)),
            "settings": int(len(rows)),
            "source_mode": args.source_mode,
            "best_valid": best_valid,
        },
    )
    print(
        json.dumps(
            {
                "output_dir": str(args.output_dir),
                "candidate_count": int(len(candidate_rows)),
                "ordered_candidate_count": int(len(guard_order)),
                "settings": int(len(rows)),
                "best_valid": best_valid,
            },
            indent=2,
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
