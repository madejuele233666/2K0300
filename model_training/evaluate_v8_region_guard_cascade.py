import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from augment_v8_conflict_family_guards import summarize_group
from evaluate_v8_embedding_prototypes import metric_summary, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


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
    if "noise" in view and "blur" in view:
        return "blur_noise"
    if "blur" in view:
        return "blur"
    if "noise" in view:
        return "noise"
    if "bright" in view:
        return "brightness"
    if "contrast" in view:
        return "contrast"
    if "shift" in view:
        return "shift"
    return "other"


def class_distances(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    *,
    batch_size: int = 512,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    class_rows: list[np.ndarray] = []
    pred_rows: list[np.ndarray] = []
    margin_rows: list[np.ndarray] = []
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(3)]
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        class_dist = np.full((len(x), 3), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            class_dist[:, parent] = np.min(dist[:, indexes], axis=1)
        order = np.argsort(class_dist, axis=1)
        rows = np.arange(len(x))
        pred_rows.append(order[:, 0].astype(np.int64))
        margin_rows.append((class_dist[rows, order[:, 1]] - class_dist[rows, order[:, 0]]).astype(np.int64))
        class_rows.append(class_dist)
    return (
        np.concatenate(pred_rows).astype(np.int64),
        np.concatenate(margin_rows).astype(np.int64),
        np.concatenate(class_rows).astype(np.int64),
    )


def guard_distances(features: np.ndarray, guards: np.ndarray, *, batch_size: int = 512) -> np.ndarray:
    if len(guards) == 0:
        return np.zeros((len(features), 0), dtype=np.int64)
    x_all = features.astype(np.int32)
    g_all = guards.astype(np.int32)
    rows: list[np.ndarray] = []
    for start in range(0, len(x_all), batch_size):
        x = x_all[start : start + batch_size]
        dist = np.sum((x[:, None, :] - g_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        rows.append(dist)
    return np.concatenate(rows).astype(np.int64)


def build_guard_order(
    *,
    base: dict[str, np.ndarray],
    teacher: dict[str, np.ndarray],
    order_mode: str,
    per_family_cap: int,
) -> list[int]:
    query_index = np.asarray(teacher["query_index"], dtype=np.int64)
    parent = np.asarray(teacher["parent"], dtype=np.int64)
    teacher_wrong_parent = np.asarray(teacher["teacher_wrong_parent"], dtype=np.int64)
    teacher_vote_count = np.asarray(teacher["teacher_vote_count"], dtype=np.int64)
    teacher_margin_mean = np.asarray(teacher["teacher_margin_mean"], dtype=np.float32)
    student_margin = np.asarray(teacher["student_int8_margin"], dtype=np.int64)
    weights = np.asarray(teacher["weight"], dtype=np.float32)
    base_view = np.asarray(base["view_labels"]).astype(str)

    indexed_rows: list[tuple[tuple[Any, ...], int]] = []
    for event_index, query in enumerate(query_index.tolist()):
        score = (
            int(student_margin[event_index]),
            -int(teacher_vote_count[event_index]),
            -float(teacher_margin_mean[event_index]),
            -float(weights[event_index]),
            int(query),
            int(teacher_wrong_parent[event_index]),
        )
        indexed_rows.append((score, int(event_index)))

    if order_mode == "margin":
        return [event_index for _score, event_index in sorted(indexed_rows, key=lambda item: item[0])]

    if order_mode != "family_roundrobin":
        raise ValueError(f"unknown order mode: {order_mode}")

    by_family: dict[tuple[int, int, str], list[tuple[tuple[Any, ...], int]]] = {}
    for score, event_index in indexed_rows:
        query = int(query_index[event_index])
        key = (
            int(parent[event_index]),
            int(teacher_wrong_parent[event_index]),
            view_family(str(base_view[query])),
        )
        by_family.setdefault(key, []).append((score, event_index))
    for rows in by_family.values():
        rows.sort(key=lambda item: item[0])

    ordered: list[int] = []
    family_keys = sorted(by_family, key=lambda key: (key[0], key[1], key[2]))
    round_index = 0
    while True:
        added = False
        for key in family_keys:
            if per_family_cap > 0 and round_index >= per_family_cap:
                continue
            rows = by_family[key]
            if round_index >= len(rows):
                continue
            ordered.append(int(rows[round_index][1]))
            added = True
        if not added:
            break
        round_index += 1
    return ordered


def apply_gate(
    *,
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    guard_dist: np.ndarray,
    guard_order: np.ndarray,
    guard_parent: np.ndarray,
    guard_competitor: np.ndarray,
    guard_safe_radius: np.ndarray,
    radius: int,
    candidate_gap: int,
    primary_margin_max: int,
    match_mode: str,
) -> tuple[np.ndarray, list[dict[str, Any]]]:
    if guard_dist.shape[1] == 0:
        return pred.copy(), []
    out = pred.copy()
    traces: list[dict[str, Any]] = []
    for row_index in range(len(pred)):
        primary = int(pred[row_index])
        if primary_margin_max >= 0 and int(margin[row_index]) > int(primary_margin_max):
            continue
        for guard_index in guard_order[row_index].tolist():
            dist = int(guard_dist[row_index, guard_index])
            if dist > int(radius):
                break
            effective_radius = min(int(radius), int(guard_safe_radius[guard_index]))
            if effective_radius < 0 or dist > effective_radius:
                continue
            candidate = int(guard_parent[guard_index])
            if candidate == primary:
                continue
            competitor = int(guard_competitor[guard_index])
            if match_mode == "competitor" and primary != competitor:
                continue
            if match_mode not in {"competitor", "any"}:
                raise ValueError(f"unknown match mode: {match_mode}")
            gap = int(class_dist[row_index, candidate]) - int(class_dist[row_index, primary])
            if gap > int(candidate_gap):
                continue
            out[row_index] = candidate
            traces.append(
                {
                    "row_index": int(row_index),
                    "primary_pred": primary,
                    "cascade_pred": candidate,
                    "guard_index": int(guard_index),
                    "guard_dist": dist,
                    "candidate_gap": gap,
                    "primary_margin": int(margin[row_index]),
                    "match_mode": match_mode,
                }
            )
            break
    return out, traces


def evaluate_normal(
    *,
    parent: np.ndarray,
    view_labels: np.ndarray,
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    guard_dist: np.ndarray,
    guard_order: np.ndarray,
    guard_parent: np.ndarray,
    guard_competitor: np.ndarray,
    guard_safe_radius: np.ndarray,
    radius: int,
    candidate_gap: int,
    primary_margin_max: int,
    match_mode: str,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    cascade_pred, traces = apply_gate(
        pred=pred,
        margin=margin,
        class_dist=class_dist,
        guard_dist=guard_dist,
        guard_order=guard_order,
        guard_parent=guard_parent,
        guard_competitor=guard_competitor,
        guard_safe_radius=guard_safe_radius,
        radius=radius,
        candidate_gap=candidate_gap,
        primary_margin_max=primary_margin_max,
        match_mode=match_mode,
    )
    broken = int(np.sum((pred == parent) & (cascade_pred != parent)))
    fixed = int(np.sum((pred != parent) & (cascade_pred == parent)))
    row: dict[str, Any] = {
        "normal_cascade_used": int(len(traces)),
        "normal_fixed": fixed,
        "normal_broken": broken,
        "all_correct": bool(np.all(cascade_pred == parent)),
    }
    view_order = list(dict.fromkeys(view_labels.astype(str).tolist()))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=cascade_pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=cascade_pred, prefix="int8_"))
    for trace in traces:
        trace["true_parent"] = int(parent[int(trace["row_index"])])
        trace["broken"] = bool(
            int(pred[int(trace["row_index"])]) == int(parent[int(trace["row_index"])])
            and int(trace["cascade_pred"]) != int(parent[int(trace["row_index"])])
        )
    return row, traces


def evaluate_stress(
    *,
    stress_rows: list[dict[str, str]],
    pred: np.ndarray,
    margin: np.ndarray,
    class_dist: np.ndarray,
    guard_dist: np.ndarray,
    guard_order: np.ndarray,
    guard_parent: np.ndarray,
    guard_competitor: np.ndarray,
    guard_safe_radius: np.ndarray,
    radius: int,
    candidate_gap: int,
    primary_margin_max: int,
    match_mode: str,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    parent = np.asarray([int(row["parent"]) for row in stress_rows], dtype=np.int64)
    cascade_pred, traces = apply_gate(
        pred=pred,
        margin=margin,
        class_dist=class_dist,
        guard_dist=guard_dist,
        guard_order=guard_order,
        guard_parent=guard_parent,
        guard_competitor=guard_competitor,
        guard_safe_radius=guard_safe_radius,
        radius=radius,
        candidate_gap=candidate_gap,
        primary_margin_max=primary_margin_max,
        match_mode=match_mode,
    )
    trace_by_row = {int(row["row_index"]): row for row in traces}
    out_rows: list[dict[str, Any]] = []
    for index, (source, primary, cascade, margin_value) in enumerate(
        zip(stress_rows, pred.tolist(), cascade_pred.tolist(), margin.tolist(), strict=False)
    ):
        parent_value = int(source["parent"])
        trace = trace_by_row.get(index, {})
        out_rows.append(
            {
                "group": source["group"],
                "base_query_index": int(source["base_query_index"]),
                "sample_index": int(source["sample_index"]),
                "view_label": source["view_label"],
                "parent": parent_value,
                "perturb": source["perturb"],
                "perturb_family": source["perturb_family"],
                "primary_pred": int(primary),
                "cascade_pred": int(cascade),
                "wrong": bool(int(cascade) != parent_value),
                "primary_wrong": bool(int(primary) != parent_value),
                "stress_margin": int(margin_value),
                "cascade_used": bool(index in trace_by_row),
                "guard_dist": trace.get("guard_dist", ""),
                "candidate_gap": trace.get("candidate_gap", ""),
                "guard_index": trace.get("guard_index", ""),
            }
        )
    per_group = summarize_group(out_rows, ["group"])
    wrong_events = sum(1 for row in out_rows if bool(row["wrong"]))
    fixed = sum(1 for row in out_rows if bool(row["primary_wrong"]) and not bool(row["wrong"]))
    broken = sum(1 for row in out_rows if not bool(row["primary_wrong"]) and bool(row["wrong"]))
    for trace in traces:
        row_index = int(trace["row_index"])
        trace["group"] = str(stress_rows[row_index]["group"])
        trace["true_parent"] = int(parent[row_index])
        trace["fixed"] = bool(int(pred[row_index]) != int(parent[row_index]) and int(trace["cascade_pred"]) == int(parent[row_index]))
        trace["broken"] = bool(int(pred[row_index]) == int(parent[row_index]) and int(trace["cascade_pred"]) != int(parent[row_index]))
    summary = {
        "stress_cascade_used": int(len(traces)),
        "stress_cascade_fixed": int(fixed),
        "stress_cascade_broken": int(broken),
        "wrong_events": int(wrong_events),
        "total_events": int(len(stress_rows)),
        "wrong_base_count": int(len({(row["group"], int(row["base_query_index"])) for row in out_rows if bool(row["wrong"])})),
        "high_pressure_low_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "low"), None),
        "high_pressure_control_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "control"), None),
        "per_group": per_group,
    }
    return summary, out_rows, traces


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate a normal-only region guard cascade for V8 high-pressure robustness."
    )
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--base-train-config", type=Path, required=True)
    parser.add_argument("--teacher-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--budgets", default="0,32,64,128,192,256,320")
    parser.add_argument("--radii", default="128,256,512,1024,2048,4096,8192")
    parser.add_argument("--candidate-gaps", default="0,16,64,128,256,512,1024")
    parser.add_argument("--primary-margin-maxes", default="-1,64,128,256,512")
    parser.add_argument("--match-modes", default="competitor,any")
    parser.add_argument("--order-mode", choices=["margin", "family_roundrobin"], default="family_roundrobin")
    parser.add_argument("--per-family-cap", type=int, default=0)
    parser.add_argument("--safe-radius-mode", choices=["none", "nonparent"], default="nonparent")
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base = load_npz(args.base_params_npz)
    teacher = load_npz(args.teacher_npz)
    stress_rows = read_csv_rows(args.stress_events_csv)
    shutil.copy2(args.base_train_config, args.output_dir / "train_config.json")

    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    normal_pred, normal_margin, normal_class_dist = class_distances(
        embeddings,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )
    if not np.all(normal_pred == parent):
        raise ValueError("base normal int8 replay is not 100%; refuse to evaluate cascade")

    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_pred, stress_margin, stress_class_dist = class_distances(
        stress_features,
        prototypes,
        prototype_parent,
        batch_size=args.batch_size,
    )

    guard_order = build_guard_order(
        base=base,
        teacher=teacher,
        order_mode=args.order_mode,
        per_family_cap=args.per_family_cap,
    )
    teacher_query = np.asarray(teacher["query_index"], dtype=np.int64)
    teacher_parent = np.asarray(teacher["parent"], dtype=np.int64)
    teacher_competitor = np.asarray(teacher["teacher_wrong_parent"], dtype=np.int64)
    budgets = parse_ints(args.budgets)
    radii = parse_ints(args.radii)
    candidate_gaps = parse_ints(args.candidate_gaps)
    primary_margin_maxes = parse_ints(args.primary_margin_maxes)
    match_modes = parse_modes(args.match_modes)

    base_proto_count = len(prototypes)
    feature_dim = int(embeddings.shape[1])
    rows: list[dict[str, Any]] = []
    best_valid: dict[str, Any] | None = None
    best_payload: tuple[int, int, int, int, str] | None = None

    for budget in budgets:
        selected_events = guard_order[: max(0, int(budget))]
        selected_query = teacher_query[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        guard_code = embeddings[selected_query] if len(selected_query) else np.zeros((0, feature_dim), dtype=np.int8)
        guard_parent = teacher_parent[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        guard_competitor = teacher_competitor[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        normal_guard_dist = guard_distances(embeddings, guard_code, batch_size=args.batch_size)
        stress_guard_dist = guard_distances(stress_features, guard_code, batch_size=args.batch_size)
        guard_count = len(selected_events)
        if guard_count and args.safe_radius_mode == "nonparent":
            safe_values: list[int] = []
            for guard_index, candidate_parent in enumerate(guard_parent.tolist()):
                mask = parent != int(candidate_parent)
                safe_values.append(int(np.min(normal_guard_dist[mask, guard_index])) - 1 if np.any(mask) else np.iinfo(np.int64).max)
            guard_safe_radius = np.asarray(safe_values, dtype=np.int64)
        elif guard_count:
            guard_safe_radius = np.full(guard_count, np.iinfo(np.int64).max, dtype=np.int64)
        else:
            guard_safe_radius = np.zeros(0, dtype=np.int64)
        normal_guard_order = (
            np.argsort(normal_guard_dist, axis=1)
            if guard_count
            else np.zeros((len(embeddings), 0), dtype=np.int64)
        )
        stress_guard_order = np.argsort(stress_guard_dist, axis=1) if guard_count else np.zeros((len(stress_features), 0), dtype=np.int64)
        for radius in radii:
            for candidate_gap in candidate_gaps:
                for primary_margin_max in primary_margin_maxes:
                    for match_mode in match_modes:
                        normal_row, _normal_traces = evaluate_normal(
                            parent=parent,
                            view_labels=view_labels,
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
                        guard_count = int(len(selected_events))
                        row: dict[str, Any] = {
                            "stage": "v8_region_guard_cascade",
                            "name": (
                                f"region_guard_b{guard_count}_r{radius}_gap{candidate_gap}_"
                                f"pm{primary_margin_max}_{match_mode}"
                            ),
                            "feature_source": str(np.asarray(base.get("feature_source", np.asarray("int8_tflite"))).item()),
                            "prototype_source": "normal_region_guard_cascade",
                            "feature_dim": feature_dim,
                            "prototype_count": int(base_proto_count + guard_count),
                            "base_prototype_count": int(base_proto_count),
                            "guard_count": guard_count,
                            "estimated_distance_macs": int((base_proto_count + guard_count) * feature_dim),
                            "radius": int(radius),
                            "candidate_gap": int(candidate_gap),
                            "primary_margin_max": int(primary_margin_max),
                            "match_mode": match_mode,
                            "order_mode": args.order_mode,
                            "per_family_cap": int(args.per_family_cap),
                            "safe_radius_mode": args.safe_radius_mode,
                            "guard_safe_radius_min": int(np.min(guard_safe_radius)) if len(guard_safe_radius) else "",
                            "guard_safe_radius_median": float(np.median(guard_safe_radius)) if len(guard_safe_radius) else "",
                            "high_pressure_usage": "evaluation_only",
                            "selection_usage": "normal_teacher_cache_only",
                            **normal_row,
                            **{key: value for key, value in stress_summary.items() if key != "per_group"},
                            "high_pressure_per_group_json": json.dumps(stress_summary["per_group"], ensure_ascii=False),
                        }
                        rows.append(row)
                        if bool(row["all_correct"]):
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
    if best_payload is not None:
        guard_count, radius, candidate_gap, primary_margin_max, match_mode = best_payload
        selected_events = guard_order[:guard_count]
        selected_query = teacher_query[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        guard_code = embeddings[selected_query] if len(selected_query) else np.zeros((0, feature_dim), dtype=np.int8)
        guard_parent = teacher_parent[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        guard_competitor = teacher_competitor[selected_events] if selected_events else np.zeros(0, dtype=np.int64)
        normal_guard_dist = guard_distances(embeddings, guard_code, batch_size=args.batch_size)
        stress_guard_dist = guard_distances(stress_features, guard_code, batch_size=args.batch_size)
        if guard_count and args.safe_radius_mode == "nonparent":
            safe_values = []
            for guard_index, candidate_parent in enumerate(guard_parent.tolist()):
                mask = parent != int(candidate_parent)
                safe_values.append(int(np.min(normal_guard_dist[mask, guard_index])) - 1 if np.any(mask) else np.iinfo(np.int64).max)
            guard_safe_radius = np.asarray(safe_values, dtype=np.int64)
        elif guard_count:
            guard_safe_radius = np.full(guard_count, np.iinfo(np.int64).max, dtype=np.int64)
        else:
            guard_safe_radius = np.zeros(0, dtype=np.int64)
        normal_guard_order = np.argsort(normal_guard_dist, axis=1) if guard_count else np.zeros((len(embeddings), 0), dtype=np.int64)
        stress_guard_order = np.argsort(stress_guard_dist, axis=1) if guard_count else np.zeros((len(stress_features), 0), dtype=np.int64)
        normal_row, normal_traces = evaluate_normal(
            parent=parent,
            view_labels=view_labels,
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
                "selection_usage": "normal_teacher_cache_only",
            },
        )
        write_csv(best_dir / "normal_traces.csv", normal_traces)
        write_csv(best_dir / "stress_events.csv", stress_out)
        write_csv(best_dir / "stress_traces.csv", stress_traces)

    write_json(
        args.output_dir / "summary.json",
        {
            "base_params_npz": str(args.base_params_npz),
            "teacher_npz": str(args.teacher_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_teacher_cache_only",
            "guard_candidate_count": int(len(guard_order)),
            "safe_radius_mode": args.safe_radius_mode,
            "settings": int(len(rows)),
            "best_valid": best_valid,
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "settings": len(rows), "best_valid": best_valid}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
