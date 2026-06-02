import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import ROT_MIRROR_VIEWS, predict_closed, predict_int8, write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def subset_mask(view_labels: np.ndarray, subset: str) -> np.ndarray:
    if subset == "clean":
        return view_labels == "clean"
    if subset == "clean_rotmirror":
        return (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    if subset == "all":
        return np.ones(len(view_labels), dtype=bool)
    raise ValueError(f"unknown subset: {subset}")


def risk_reason(
    *,
    pred: int,
    int8_pred: int,
    parent: int,
    margin: float,
    int8_margin: int,
    target_margin: float,
    target_int8_margin: int,
) -> str:
    reasons: list[str] = []
    if pred != parent:
        reasons.append("float_wrong")
    if int8_pred != parent:
        reasons.append("int8_wrong")
    if margin <= target_margin:
        reasons.append("float_low_margin")
    if int8_margin <= target_int8_margin:
        reasons.append("int8_low_margin")
    return "+".join(reasons) or "unknown"


def build_residual_risk_teacher(
    *,
    params_npz: Path,
    output_dir: Path,
    base_subset: str,
    max_iterations: int,
    target_margin: float,
    target_int8_margin: int,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with np.load(params_npz, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}

    embeddings_float = np.asarray(payload["embedding_float"], dtype=np.float64)
    embeddings_int8 = np.asarray(payload["embedding_int8"], dtype=np.int8)
    y_parent = np.asarray(payload["parent"], dtype=np.int64)
    y_sub = np.asarray(payload["subclass"], dtype=np.int64)
    sample_index = np.asarray(payload["sample_index"], dtype=np.int64)
    view_labels = np.asarray(payload["view_labels"]).astype(str)
    paths = np.asarray(payload.get("paths", np.asarray([]))).astype(str)

    selected = subset_mask(view_labels, base_subset).copy()
    event_rows: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []

    for iteration in range(1, max_iterations + 1):
        prototypes_float = embeddings_float[selected]
        prototypes_int8 = embeddings_int8[selected]
        prototype_parent = y_parent[selected]
        pred, margin = predict_closed(embeddings_float, prototypes_float, prototype_parent)
        int8_pred, int8_margin = predict_int8(embeddings_int8, prototypes_int8, prototype_parent)
        wrong_mask = (pred != y_parent) | (int8_pred != y_parent)
        risk_mask = wrong_mask | (margin <= target_margin) | (int8_margin <= target_int8_margin)
        risk_indexes = np.where(risk_mask)[0]
        add_indexes = np.asarray([int(index) for index in risk_indexes if not bool(selected[int(index)])], dtype=np.int64)
        trace.append(
            {
                "iteration": int(iteration),
                "risk_before": int(np.sum(risk_mask)),
                "wrong_before": int(np.sum(pred != y_parent)),
                "int8_wrong_before": int(np.sum(int8_pred != y_parent)),
                "added": int(len(add_indexes)),
                "prototype_count_before": int(np.sum(selected)),
                "prototype_count_after": int(np.sum(selected) + len(add_indexes)),
            }
        )
        if len(add_indexes) == 0:
            break
        for index in add_indexes.tolist():
            parent = int(y_parent[index])
            actual_int8_margin = int(int8_margin[index])
            wrong = bool(pred[index] != parent or int8_pred[index] != parent)
            selector_margin = 0 if wrong else actual_int8_margin
            event_rows.append(
                {
                    "iteration": int(iteration),
                    "query_index": int(index),
                    "sample_index": int(sample_index[index]),
                    "path": str(paths[int(sample_index[index])]) if len(paths) > int(sample_index[index]) else "",
                    "view_label": str(view_labels[index]),
                    "parent": parent,
                    "subclass": int(y_sub[index]),
                    "pred_before": int(pred[index]),
                    "int8_pred_before": int(int8_pred[index]),
                    "wrong_before": wrong,
                    "margin_before": float(margin[index]),
                    "int8_margin_before": actual_int8_margin,
                    "teacher_selector_margin": int(selector_margin),
                    "risk_reason": risk_reason(
                        pred=int(pred[index]),
                        int8_pred=int(int8_pred[index]),
                        parent=parent,
                        margin=float(margin[index]),
                        int8_margin=actual_int8_margin,
                        target_margin=target_margin,
                        target_int8_margin=target_int8_margin,
                    ),
                }
            )
        selected[add_indexes] = True

    write_csv(output_dir / "residual_risk_events.csv", event_rows)
    if event_rows:
        teacher_sample = np.asarray([row["sample_index"] for row in event_rows], dtype=np.int64)
        teacher_view = np.asarray([row["view_label"] for row in event_rows])
        teacher_margin = np.asarray([row["teacher_selector_margin"] for row in event_rows], dtype=np.int64)
        teacher_actual_margin = np.asarray([row["int8_margin_before"] for row in event_rows], dtype=np.int64)
        teacher_iteration = np.asarray([row["iteration"] for row in event_rows], dtype=np.int64)
    else:
        teacher_sample = np.zeros((0,), dtype=np.int64)
        teacher_view = np.asarray([], dtype=str)
        teacher_margin = np.zeros((0,), dtype=np.int64)
        teacher_actual_margin = np.zeros((0,), dtype=np.int64)
        teacher_iteration = np.zeros((0,), dtype=np.int64)
    np.savez_compressed(
        output_dir / "residual_risk_teacher.npz",
        sample_index=teacher_sample,
        view_labels=teacher_view,
        int8_margin=teacher_margin,
        actual_int8_margin=teacher_actual_margin,
        iteration=teacher_iteration,
        source_params_npz=np.asarray(str(params_npz)),
        base_subset=np.asarray(base_subset),
        target_margin=np.asarray(float(target_margin), dtype=np.float32),
        target_int8_margin=np.asarray(int(target_int8_margin), dtype=np.int64),
    )
    by_reason: dict[str, int] = {}
    by_view: dict[str, int] = {}
    for row in event_rows:
        by_reason[str(row["risk_reason"])] = by_reason.get(str(row["risk_reason"]), 0) + 1
        by_view[str(row["view_label"])] = by_view.get(str(row["view_label"]), 0) + 1
    write_json(
        output_dir / "summary.json",
        {
            "source_params_npz": str(params_npz),
            "base_subset": base_subset,
            "target_margin": float(target_margin),
            "target_int8_margin": int(target_int8_margin),
            "max_iterations": int(max_iterations),
            "event_count": int(len(event_rows)),
            "unique_sample_view_count": int(len({(row["sample_index"], row["view_label"]) for row in event_rows})),
            "trace": trace,
            "by_reason": dict(sorted(by_reason.items())),
            "by_view_top20": dict(sorted(by_view.items(), key=lambda item: (-item[1], item[0]))[:20]),
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Export normal-replay residual risk rows as a V8 selective-margin teacher.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--base-subset", choices=["clean", "clean_rotmirror", "all"], default="clean_rotmirror")
    parser.add_argument("--max-iterations", type=int, default=12)
    parser.add_argument("--target-margin", type=float, default=0.0)
    parser.add_argument("--target-int8-margin", type=int, default=8)
    args = parser.parse_args()
    build_residual_risk_teacher(
        params_npz=args.params_npz,
        output_dir=args.output_dir,
        base_subset=args.base_subset,
        max_iterations=args.max_iterations,
        target_margin=args.target_margin,
        target_int8_margin=args.target_int8_margin,
    )


if __name__ == "__main__":
    main()
