import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv


EventKey = tuple[str, int, str, int]
NormalKey = tuple[int, str]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def event_key(row: dict[str, str]) -> EventKey:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def read_events(path: Path) -> dict[EventKey, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    if "sample_index" not in payload:
        return {}
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def margin_bucket(value: int) -> str:
    for threshold in [1, 2, 4, 8, 16, 64, 128]:
        if int(value) <= threshold:
            return f"le{threshold}"
    return "gt128"


def safe_rate(wrong_count: int, total: int) -> float:
    return float(wrong_count / max(total, 1))


def transformed_margin(margin: np.ndarray, dim: int, mode: str) -> np.ndarray:
    values = np.maximum(margin.astype(np.float64), 0.0)
    if mode == "raw":
        return values
    if mode == "per_sqrt_dim":
        return values / np.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return values / float(max(dim, 1))
    if mode == "log":
        return np.log1p(values)
    raise ValueError(f"unknown score mode: {mode}")


def source_normal_record(payload: dict[str, np.ndarray], index: int, score_mode: str) -> dict[str, Any]:
    parent = np.asarray(payload["parent"], dtype=np.int64)
    pred = np.asarray(payload["int8_pred"], dtype=np.int64)
    margin = np.asarray(payload["int8_margin"], dtype=np.int64)
    dim = int(np.asarray(payload["embedding_int8"]).shape[1])
    score = transformed_margin(margin[index : index + 1], dim, score_mode)[0]
    return {
        "correct": bool(int(pred[index]) == int(parent[index])),
        "margin": int(margin[index]),
        "score": float(score),
    }


def normal_feature(row: dict[str, Any], feature_name: str) -> str:
    parts: list[str] = []
    for item in feature_name.split("+"):
        item = item.strip()
        if not item or item == "global":
            continue
        parts.append(f"{item}={row[item]}")
    return "|".join(parts) if parts else "global"


def stress_feature(row: dict[str, str], feature_name: str) -> str:
    base_margin = int(float(row.get("base_margin", row.get("selection_margin", "0"))))
    stress_pred = int(float(row.get("stress_pred", row.get("primary_pred", "0"))))
    values = {
        "global": "global",
        "view_label": str(row["view_label"]),
        "pred_parent": str(stress_pred),
        "base_margin_bucket": margin_bucket(base_margin),
    }
    parts: list[str] = []
    for item in feature_name.split("+"):
        item = item.strip()
        if not item or item == "global":
            continue
        parts.append(f"{item}={values[item]}")
    return "|".join(parts) if parts else "global"


def build_normal_rows(
    *,
    source_names: list[str],
    normal_payloads: dict[str, dict[str, np.ndarray]],
    score_mode: str,
) -> list[dict[str, Any]]:
    base = normal_payloads[source_names[0]]
    base_sample = np.asarray(base["sample_index"], dtype=np.int64)
    base_view = np.asarray(base["view_labels"]).astype(str)
    base_pred = np.asarray(base["int8_pred"], dtype=np.int64)
    base_margin = np.asarray(base["int8_margin"], dtype=np.int64)
    source_maps = {name: normal_key_map(payload) for name, payload in normal_payloads.items()}
    rows: list[dict[str, Any]] = []
    for base_index, (sample_id, view_label) in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False)):
        row: dict[str, Any] = {
            "sample_index": int(sample_id),
            "view_label": str(view_label),
            "pred_parent": str(int(base_pred[base_index])),
            "base_margin_bucket": margin_bucket(int(base_margin[base_index])),
        }
        for name in source_names:
            source_index = source_maps[name].get((int(sample_id), str(view_label)))
            if source_index is None:
                continue
            record = source_normal_record(normal_payloads[name], source_index, score_mode)
            row[f"{name}__present"] = True
            row[f"{name}__correct"] = bool(record["correct"])
            row[f"{name}__score"] = float(record["score"])
            row[f"{name}__margin"] = int(record["margin"])
        rows.append(row)
    return rows


def learn_gate(
    *,
    normal_rows: list[dict[str, Any]],
    source_names: list[str],
    feature_name: str,
    score_quantile: float,
) -> tuple[dict[str, str], list[dict[str, Any]]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in normal_rows:
        groups[normal_feature(row, feature_name)].append(row)
    mapping: dict[str, str] = {}
    rows_out: list[dict[str, Any]] = []
    for group, rows in sorted(groups.items()):
        best_source = source_names[0]
        best_score: tuple[Any, ...] | None = None
        per_source: dict[str, dict[str, Any]] = {}
        for source in source_names:
            present = [row for row in rows if row.get(f"{source}__present")]
            if not present:
                continue
            correct = np.asarray([bool(row[f"{source}__correct"]) for row in present], dtype=bool)
            scores = np.asarray([float(row[f"{source}__score"]) for row in present], dtype=np.float64)
            accuracy = float(np.mean(correct)) if len(correct) else 0.0
            correct_scores = scores[correct] if np.any(correct) else np.asarray([], dtype=np.float64)
            q_score = float(np.percentile(correct_scores, score_quantile)) if correct_scores.size else -1.0
            med_score = float(np.median(correct_scores)) if correct_scores.size else -1.0
            source_score = (
                accuracy,
                q_score,
                med_score,
                len(present),
                -source_names.index(source),
            )
            per_source[source] = {
                "present": int(len(present)),
                "accuracy": accuracy,
                "score_q": q_score,
                "score_median": med_score,
            }
            if best_score is None or source_score > best_score:
                best_score = source_score
                best_source = source
        mapping[group] = best_source
        rows_out.append(
            {
                "feature": feature_name,
                "group": group,
                "selected_source": best_source,
                "row_count": int(len(rows)),
                "per_source_json": json.dumps(per_source, ensure_ascii=False),
            }
        )
    return mapping, rows_out


def summarize_stress(
    *,
    base_rows: list[dict[str, str]],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_names: list[str],
    feature_name: str,
    mapping: dict[str, str],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    fallback_count = 0
    trace: list[dict[str, Any]] = []
    for row in base_rows:
        key = event_key(row)
        group_key = stress_feature(row, feature_name)
        source = mapping.get(group_key)
        if source is None or key not in sources.get(source, {}):
            source = source_names[0]
            fallback_count += 1
        pred = int(sources[source][key]["stress_pred"])
        parent = int(row["parent"])
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
        if len(trace) < 200:
            trace.append(
                {
                    "feature": feature_name,
                    "group_key": group_key,
                    "chosen_source": source,
                    "event_group": str(row["group"]),
                    "sample_index": int(row["sample_index"]),
                    "view_label": str(row["view_label"]),
                    "perturb": str(row["perturb"]),
                    "parent": parent,
                    "pred": pred,
                    "wrong": bool(wrong),
                }
            )
    wrong_events = int(sum(values[0] for values in grouped.values()))
    total_events = int(sum(values[1] for values in grouped.values()))
    summary = {
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": safe_rate(wrong_events, total_events),
        "low_wrong_rate": safe_rate(grouped.get("low", [0, 0])[0], grouped.get("low", [0, 0])[1]),
        "control_wrong_rate": safe_rate(grouped.get("control", [0, 0])[0], grouped.get("control", [0, 0])[1]),
        "fallback_count": int(fallback_count),
        "chosen_counts": dict(chosen),
    }
    return summary, trace


def oracle_any(
    *,
    base_rows: list[dict[str, str]],
    sources: dict[str, dict[EventKey, dict[str, str]]],
    source_names: list[str],
) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    chosen = Counter()
    for row in base_rows:
        key = event_key(row)
        parent = int(row["parent"])
        source = source_names[0]
        pred = int(sources[source][key]["stress_pred"])
        for candidate in source_names:
            candidate_pred = int(sources[candidate][key]["stress_pred"])
            if candidate_pred == parent:
                source = candidate
                pred = candidate_pred
                break
        wrong = int(pred != parent)
        grouped[str(row["group"])][0] += wrong
        grouped[str(row["group"])][1] += 1
        chosen[source] += 1
    wrong_events = int(sum(values[0] for values in grouped.values()))
    total_events = int(sum(values[1] for values in grouped.values()))
    return {
        "policy": "oracle_any",
        "selection_label_usage": "true_parent",
        "wrong_events": wrong_events,
        "total_events": total_events,
        "wrong_rate": safe_rate(wrong_events, total_events),
        "low_wrong_rate": safe_rate(grouped.get("low", [0, 0])[0], grouped.get("low", [0, 0])[1]),
        "control_wrong_rate": safe_rate(grouped.get("control", [0, 0])[0], grouped.get("control", [0, 0])[1]),
        "chosen_counts_json": json.dumps(dict(chosen), ensure_ascii=False),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Learn normal-only source routing tables and evaluate on V8 high-pressure events.")
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--features",
        default="global,pred_parent,base_margin_bucket,pred_parent+base_margin_bucket,view_label,view_label+pred_parent,view_label+base_margin_bucket,view_label+pred_parent+base_margin_bucket",
    )
    parser.add_argument("--score-modes", default="raw,per_sqrt_dim,per_dim,log")
    parser.add_argument("--score-quantile", type=float, default=5.0)
    args = parser.parse_args()

    source_items = [parse_named_path(item) for item in args.source]
    normal_items = [parse_named_path(item) for item in args.normal_params]
    source_names = [name for name, _path in source_items]
    normal_names = [name for name, _path in normal_items]
    if source_names != normal_names:
        raise ValueError(f"source and normal names/order must match: {source_names} != {normal_names}")

    sources = {name: read_events(path) for name, path in source_items}
    common_keys = sorted(set.intersection(*(set(rows) for rows in sources.values())))
    if not common_keys:
        raise ValueError("sources have no common high-pressure events")
    base_name = source_names[0]
    base_rows = [sources[base_name][key] for key in common_keys]
    normal_payloads = {name: load_npz(path) for name, path in normal_items}

    args.output_dir.mkdir(parents=True, exist_ok=True)
    policy_rows: list[dict[str, Any]] = []
    mapping_rows_all: list[dict[str, Any]] = []
    trace_rows: list[dict[str, Any]] = []
    for score_mode in [item.strip() for item in args.score_modes.split(",") if item.strip()]:
        normal_rows = build_normal_rows(
            source_names=source_names,
            normal_payloads=normal_payloads,
            score_mode=score_mode,
        )
        for feature_name in [item.strip() for item in args.features.split(",") if item.strip()]:
            mapping, mapping_rows = learn_gate(
                normal_rows=normal_rows,
                source_names=source_names,
                feature_name=feature_name,
                score_quantile=float(args.score_quantile),
            )
            summary, trace = summarize_stress(
                base_rows=base_rows,
                sources=sources,
                source_names=source_names,
                feature_name=feature_name,
                mapping=mapping,
            )
            policy_rows.append(
                {
                    "policy": f"normal_gate:{feature_name}:{score_mode}:q{args.score_quantile:g}",
                    "selection_label_usage": "none",
                    "normal_feature": feature_name,
                    "normal_score_mode": score_mode,
                    **{key: value for key, value in summary.items() if key != "chosen_counts"},
                    "chosen_counts_json": json.dumps(summary["chosen_counts"], ensure_ascii=False),
                }
            )
            for row in mapping_rows:
                mapping_rows_all.append({"score_mode": score_mode, **row})
            trace_rows.extend({"score_mode": score_mode, **row} for row in trace[:50])

    policy_rows.append(oracle_any(base_rows=base_rows, sources=sources, source_names=source_names))
    policy_rows = sorted(
        policy_rows,
        key=lambda row: (
            str(row["selection_label_usage"]) != "none",
            float(row["low_wrong_rate"]),
            float(row["control_wrong_rate"]),
            float(row["wrong_rate"]),
        ),
    )
    write_csv(args.output_dir / "policy_summary.csv", policy_rows)
    write_csv(args.output_dir / "normal_gate_mapping.csv", mapping_rows_all)
    write_csv(args.output_dir / "stress_trace_sample.csv", trace_rows)
    summary = {
        "sources": {name: str(path) for name, path in source_items},
        "normal_params": {name: str(path) for name, path in normal_items},
        "source_order": source_names,
        "high_pressure_usage": "evaluation_only",
        "normal_training_usage": "source routing table only",
        "common_events": int(len(common_keys)),
        "dropped_events": {name: int(len(rows) - len(common_keys)) for name, rows in sources.items()},
        "score_quantile": float(args.score_quantile),
        "top_policies": policy_rows[:10],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
