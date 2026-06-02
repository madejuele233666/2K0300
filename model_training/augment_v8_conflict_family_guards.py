import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import metric_summary, predict_int8, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


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


def classify_features(
    features: np.ndarray,
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_count = 3
    pred = np.empty(len(x_all), dtype=np.int64)
    margin = np.empty(len(x_all), dtype=np.int64)
    class_dist = np.empty((len(x_all), parent_count), dtype=np.int64)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(parent_count)]
    for start in range(0, len(x_all), 512):
        x = x_all[start : start + 512]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        local_class = np.full((len(x), parent_count), np.iinfo(np.int64).max, dtype=np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            local_class[:, parent] = np.min(dist[:, indexes], axis=1)
        order = np.argsort(local_class, axis=1)
        rows = np.arange(len(x))
        pred[start : start + len(x)] = order[:, 0]
        margin[start : start + len(x)] = local_class[rows, order[:, 1]] - local_class[rows, order[:, 0]]
        class_dist[start : start + len(x)] = local_class
    return pred, margin, class_dist


def summarize_group(rows: list[dict[str, Any]], keys: list[str]) -> list[dict[str, Any]]:
    grouped: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for row in rows:
        grouped.setdefault(tuple(row.get(key) for key in keys), []).append(row)
    out: list[dict[str, Any]] = []
    for values, items in grouped.items():
        margins = np.asarray([int(row["stress_margin"]) for row in items], dtype=np.int64)
        wrong = sum(1 for row in items if bool(row["wrong"]))
        total = len(items)
        out.append(
            {
                **{key: value for key, value in zip(keys, values)},
                "total": int(total),
                "wrong": int(wrong),
                "accuracy": float((total - wrong) / max(total, 1)),
                "wrong_rate": float(wrong / max(total, 1)),
                "stress_margin_min": int(np.min(margins)),
                "stress_margin_p05": float(np.percentile(margins, 5)),
                "stress_margin_median": float(np.median(margins)),
            }
        )
    return sorted(out, key=lambda row: (str(row.get(keys[0], "")), float(row["wrong_rate"])))


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def build_guard_order(
    *,
    base: dict[str, np.ndarray],
    teacher: dict[str, np.ndarray],
    per_family_cap: int,
) -> list[tuple[int, int]]:
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    parent = np.asarray(base["parent"], dtype=np.int64)
    base_proto_sample = np.asarray(base["prototype_sample_index"], dtype=np.int64)
    base_proto_view = np.asarray(base["prototype_view_label"]).astype(str)
    base_proto_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    base_embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    existing_exact = {
        (int(sample), str(view), int(cls))
        for sample, view, cls in zip(base_proto_sample.tolist(), base_proto_view.tolist(), base_proto_parent.tolist())
    }
    existing_code = {
        (tuple(int(v) for v in code.tolist()), int(cls))
        for code, cls in zip(np.asarray(base["prototypes_int8"], dtype=np.int8), base_proto_parent.tolist())
    }

    query = np.asarray(teacher["query_index"], dtype=np.int64)
    teacher_wrong_parent = np.asarray(teacher["teacher_wrong_parent"], dtype=np.int64)
    teacher_vote_count = np.asarray(teacher["teacher_vote_count"], dtype=np.int64)
    teacher_margin_mean = np.asarray(teacher["teacher_margin_mean"], dtype=np.float32)
    student_margin = np.asarray(teacher["student_int8_margin"], dtype=np.int64)
    weights = np.asarray(teacher["weight"], dtype=np.float32)

    family_rows: dict[tuple[Any, ...], list[tuple[tuple[Any, ...], int, int]]] = {}
    seen_query: set[int] = set()
    for event_index, query_index in enumerate(query.tolist()):
        query_index = int(query_index)
        if query_index in seen_query:
            continue
        seen_query.add(query_index)
        cls = int(parent[query_index])
        sample = int(sample_index[query_index])
        view = str(view_labels[query_index])
        if (sample, view, cls) in existing_exact:
            continue
        code_key = (tuple(int(v) for v in base_embeddings[query_index].tolist()), cls)
        if code_key in existing_code:
            continue
        key = (cls, int(teacher_wrong_parent[event_index]), view_family(view))
        score = (
            int(student_margin[event_index]),
            -int(teacher_vote_count[event_index]),
            -float(teacher_margin_mean[event_index]),
            -float(weights[event_index]),
            query_index,
        )
        family_rows.setdefault(key, []).append((score, query_index, int(event_index)))

    for rows in family_rows.values():
        rows.sort(key=lambda item: item[0])

    ordered: list[tuple[int, int]] = []
    family_keys = sorted(family_rows, key=lambda key: (key[0], key[1], str(key[2])))
    round_index = 0
    while True:
        added = False
        for key in family_keys:
            rows = family_rows[key]
            if round_index >= len(rows):
                continue
            if per_family_cap > 0 and round_index >= per_family_cap:
                continue
            ordered.append((int(rows[round_index][1]), int(rows[round_index][2])))
            added = True
        if not added:
            break
        round_index += 1
    return ordered


def evaluate_budget(
    *,
    base: dict[str, np.ndarray],
    teacher: dict[str, np.ndarray],
    guard_order: list[tuple[int, int]],
    budget: int,
    guard_mode: str,
    toward_wrong_alpha: float,
    stress_rows: list[dict[str, str]],
    output_dir: Path,
    base_summary: dict[str, Any],
) -> dict[str, Any]:
    embeddings = np.asarray(base["embedding_int8"], dtype=np.int8)
    parent = np.asarray(base["parent"], dtype=np.int64)
    subclass = np.asarray(base["subclass"], dtype=np.int64)
    sample_index = np.asarray(base["sample_index"], dtype=np.int64)
    view_labels = np.asarray(base["view_labels"]).astype(str)
    paths = np.asarray(base["paths"]).astype(str)
    prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8)
    prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    prototype_subclass = np.asarray(base["prototype_subclass"], dtype=np.int64)
    prototype_cluster = np.asarray(base["prototype_cluster"], dtype=np.int64)
    prototype_sample = np.asarray(base["prototype_sample_index"], dtype=np.int64)
    prototype_view = np.asarray(base["prototype_view_label"]).astype(str)
    prototype_source_kind = np.asarray(base["prototype_source_kind"]).astype(str)

    selected_records = guard_order[: max(0, int(budget))]
    selected_query = [int(query_index) for query_index, _event_index in selected_records]
    selected_event = [int(event_index) for _query_index, event_index in selected_records]
    if selected_query:
        query_code = embeddings[selected_query].astype(np.int32)
        if guard_mode == "query":
            guard_proto = query_code
        elif guard_mode == "toward_wrong":
            if "wrong_proto_sample" in teacher and "wrong_proto_view" in teacher:
                row_by_sample_view = {
                    (int(sample), str(view)): int(index)
                    for index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist()))
                }
                wrong_sample = np.asarray(teacher["wrong_proto_sample"], dtype=np.int64)[selected_event]
                wrong_view = np.asarray(teacher["wrong_proto_view"]).astype(str)[selected_event]
                wrong_rows = [
                    row_by_sample_view[(int(sample), str(view))]
                    for sample, view in zip(wrong_sample.tolist(), wrong_view.tolist())
                ]
                wrong_code = embeddings[wrong_rows].astype(np.int32)
            else:
                teacher_wrong = np.asarray(teacher["teacher_wrong_parent"], dtype=np.int64)[selected_event]
                wrong_code_rows: list[np.ndarray] = []
                proto_i32 = prototypes.astype(np.int32)
                proto_parent_i64 = prototype_parent.astype(np.int64)
                for code, wrong_parent in zip(query_code, teacher_wrong.tolist(), strict=False):
                    indexes = np.where(proto_parent_i64 == int(wrong_parent))[0]
                    if len(indexes) == 0:
                        wrong_code_rows.append(code.copy())
                        continue
                    dist = np.sum((proto_i32[indexes] - code[None, :]) ** 2, axis=1)
                    wrong_code_rows.append(proto_i32[indexes[int(np.argmin(dist))]].copy())
                wrong_code = np.stack(wrong_code_rows).astype(np.int32)
            guard_proto = np.rint(query_code + float(toward_wrong_alpha) * (wrong_code - query_code))
        else:
            raise ValueError(f"unknown guard mode: {guard_mode}")
        guard_proto = np.clip(guard_proto, -128, 127).astype(np.int8)
        guard_parent = parent[selected_query].astype(np.int64)
        guard_subclass = subclass[selected_query].astype(np.int64)
        guard_cluster = np.full(len(selected_query), -1, dtype=np.int64)
        guard_sample = sample_index[selected_query].astype(np.int64)
        guard_view = view_labels[selected_query].astype(str)
        guard_kind = np.asarray([f"normal_conflict_family_guard_{guard_mode}"] * len(selected_query))
        aug_prototypes = np.concatenate([prototypes, guard_proto], axis=0)
        aug_parent = np.concatenate([prototype_parent, guard_parent], axis=0)
        aug_subclass = np.concatenate([prototype_subclass, guard_subclass], axis=0)
        aug_cluster = np.concatenate([prototype_cluster, guard_cluster], axis=0)
        aug_sample = np.concatenate([prototype_sample, guard_sample], axis=0)
        aug_view = np.concatenate([prototype_view, guard_view], axis=0)
        aug_kind = np.concatenate([prototype_source_kind, guard_kind], axis=0)
    else:
        aug_prototypes = prototypes
        aug_parent = prototype_parent
        aug_subclass = prototype_subclass
        aug_cluster = prototype_cluster
        aug_sample = prototype_sample
        aug_view = prototype_view
        aug_kind = prototype_source_kind

    int8_pred, int8_margin = predict_int8(embeddings, aug_prototypes, aug_parent)
    view_order = list(dict.fromkeys(view_labels.tolist()))
    normal_row: dict[str, Any] = {
        "stage": "v8_conflict_family_guard",
        "name": f"normal_conflict_family_guard_{guard_mode}_b{budget}",
        "feature_source": str(np.asarray(base.get("feature_source", np.asarray("int8_tflite"))).item()),
        "prototype_source": "normal_conflict_family_guard",
        "k_per_subclass": "",
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(len(aug_prototypes)),
        "guard_count": int(len(selected_query)),
        "estimated_distance_macs": int(len(aug_prototypes) * embeddings.shape[1]),
        "estimated_float_table_bytes": int(len(aug_prototypes) * embeddings.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(aug_prototypes) * embeddings.shape[1]),
        "margin_min": float(np.min(int8_margin)),
        "margin_mean": float(np.mean(int8_margin)),
        "int8_scale": 1.0,
        "int8_flip_count": 0,
        "int8_margin_min": int(np.min(int8_margin)),
        "int8_margin_mean": float(np.mean(int8_margin)),
        "tflite_unique_ops": str(base_summary.get("tflite_unique_ops", "")),
    }
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=int8_pred))
    normal_row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=parent, pred=int8_pred, prefix="int8_"))

    stress_features = np.asarray([json.loads(row["feature_json"]) for row in stress_rows], dtype=np.int8)
    stress_parent = np.asarray([int(row["parent"]) for row in stress_rows], dtype=np.int64)
    stress_pred, stress_margin, _class_dist = classify_features(stress_features, aug_prototypes, aug_parent)
    stress_out: list[dict[str, Any]] = []
    for row, pred, margin in zip(stress_rows, stress_pred.tolist(), stress_margin.tolist()):
        parent_value = int(row["parent"])
        stress_out.append(
            {
                "group": row["group"],
                "base_query_index": int(row["base_query_index"]),
                "sample_index": int(row["sample_index"]),
                "view_label": row["view_label"],
                "parent": parent_value,
                "perturb": row["perturb"],
                "perturb_family": row["perturb_family"],
                "stress_pred": int(pred),
                "wrong": bool(int(pred) != parent_value),
                "stress_margin": int(margin),
            }
        )
    per_group = summarize_group(stress_out, ["group"])
    wrong_events = sum(1 for row in stress_out if bool(row["wrong"]))
    wrong_base_count = len({(row["group"], int(row["base_query_index"])) for row in stress_out if bool(row["wrong"])})

    budget_dir = output_dir / f"budget_{budget:04d}"
    budget_dir.mkdir(parents=True, exist_ok=True)
    write_csv(budget_dir / "stress_events.csv", stress_out)
    write_csv(budget_dir / "per_group_summary.csv", per_group)
    np.savez_compressed(
        budget_dir / "best_parent_logits_memory_params.npz",
        **{key: value for key, value in base.items() if key not in {
            "prototypes",
            "prototypes_int8",
            "prototype_parent",
            "prototype_subclass",
            "prototype_cluster",
            "prototype_sample_index",
            "prototype_view_label",
            "prototype_source_kind",
            "int8_pred",
            "int8_margin",
        }},
        prototypes_int8=aug_prototypes.astype(np.int8),
        prototypes=aug_prototypes.astype(np.float32),
        prototype_parent=aug_parent.astype(np.int64),
        prototype_subclass=aug_subclass.astype(np.int64),
        prototype_cluster=aug_cluster.astype(np.int64),
        prototype_sample_index=aug_sample.astype(np.int64),
        prototype_view_label=aug_view.astype(str),
        prototype_source_kind=aug_kind.astype(str),
        int8_pred=int8_pred.astype(np.int64),
        int8_margin=int8_margin.astype(np.int64),
        high_pressure_usage=np.asarray("evaluation_only"),
        guard_source=np.asarray("normal_conflict_family_teacher"),
        guard_query_index=np.asarray(selected_query, dtype=np.int64),
        guard_teacher_event_index=np.asarray(selected_event, dtype=np.int64),
        guard_mode=np.asarray(str(guard_mode)),
        guard_toward_wrong_alpha=np.asarray(float(toward_wrong_alpha), dtype=np.float32),
    )
    summary = {
        "budget": int(budget),
        "guard_count": int(len(selected_query)),
        "guard_mode": str(guard_mode),
        "guard_toward_wrong_alpha": float(toward_wrong_alpha),
        "normal_row": normal_row,
        "high_pressure_usage": "evaluation_only",
        "selection_usage": "normal_teacher_cache_only",
        "total_events": int(len(stress_out)),
        "wrong_events": int(wrong_events),
        "wrong_base_count": int(wrong_base_count),
        "per_group": per_group,
    }
    write_json(budget_dir / "summary.json", summary)
    return {
        **normal_row,
        "budget": int(budget),
        "wrong_events": int(wrong_events),
        "wrong_base_count": int(wrong_base_count),
        "high_pressure_low_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "low"), None),
        "high_pressure_control_wrong_rate": next((row["wrong_rate"] for row in per_group if row["group"] == "control"), None),
        "high_pressure_per_group_json": json.dumps(per_group, ensure_ascii=False),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Add normal-only conflict-family guard prototypes and replay stress events offline.")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--base-candidate-results", type=Path, required=True)
    parser.add_argument("--base-train-config", type=Path, required=True)
    parser.add_argument("--teacher-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--budgets", default="0,32,64,96,128,192,256,320")
    parser.add_argument("--per-family-cap", type=int, default=0)
    parser.add_argument("--guard-mode", choices=["query", "toward_wrong"], default="query")
    parser.add_argument("--toward-wrong-alpha", type=float, default=0.35)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base = load_npz(args.base_params_npz)
    teacher = load_npz(args.teacher_npz)
    base_rows = read_csv_rows(args.base_candidate_results)
    base_summary = base_rows[0] if base_rows else {}
    stress_rows = read_csv_rows(args.stress_events_csv)
    budgets = [int(item.strip()) for item in args.budgets.split(",") if item.strip()]
    guard_order = build_guard_order(base=base, teacher=teacher, per_family_cap=args.per_family_cap)
    shutil.copy2(args.base_train_config, args.output_dir / "train_config.json")
    rows = []
    for budget in budgets:
        rows.append(
            evaluate_budget(
                base=base,
                teacher=teacher,
                guard_order=guard_order,
                budget=budget,
                guard_mode=args.guard_mode,
                toward_wrong_alpha=args.toward_wrong_alpha,
                stress_rows=stress_rows,
                output_dir=args.output_dir,
                base_summary=base_summary,
            )
        )
    write_csv(args.output_dir / "candidate_results.csv", rows)
    write_json(
        args.output_dir / "summary.json",
        {
            "base_params_npz": str(args.base_params_npz),
            "teacher_npz": str(args.teacher_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "selection_usage": "normal_teacher_cache_only",
            "guard_candidate_count": int(len(guard_order)),
            "budgets": budgets,
            "per_family_cap": int(args.per_family_cap),
            "guard_mode": str(args.guard_mode),
            "toward_wrong_alpha": float(args.toward_wrong_alpha),
            "best_by_low": min(rows, key=lambda row: float(row["high_pressure_low_wrong_rate"] or 1.0)),
            "best_by_control": min(rows, key=lambda row: float(row["high_pressure_control_wrong_rate"] or 1.0)),
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "guard_candidate_count": len(guard_order), "rows": rows}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
