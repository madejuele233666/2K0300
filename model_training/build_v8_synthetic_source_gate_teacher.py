import argparse
import csv
import json
import math
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np

from analyze_v8_synthetic_source_event_gate import (
    classify_features,
    load_npz,
    parse_named_path,
)
from evaluate_v8_embedding_prototypes import parse_csv, write_csv
from stress_test_v8_low_margin import tflite_raw_int8
from train_v8_end_to_end_embedding import build_view_dataset


DEFAULT_HIGHPRESS_PERTURBS = [
    "identity",
    "noise_0p02",
    "noise_0p04",
    "noise_0p06",
    "noise_0p08",
    "noise_0p10",
    "blur3a0",
    "blur3a45",
    "blur3a90",
    "blur3a135",
    "blur5a0",
    "blur5a45",
    "blur5a90",
    "blur5a135",
    "blur7a45",
    "blur7a135",
    "blur5a45_noise0p04",
    "blur5a135_noise0p04",
    "blur7a45_noise0p04",
    "bright_p0p04",
    "bright_p0p08",
    "bright_p0p12",
    "bright_m0p04",
    "bright_m0p08",
    "bright_m0p12",
    "contrast_p0p10",
    "contrast_p0p20",
    "contrast_m0p10",
    "contrast_m0p20",
    "shift_u1",
    "shift_d1",
    "shift_l1",
    "shift_r1",
    "shift_ul1",
    "shift_dr1",
    "shift_u2",
    "shift_d2",
    "shift_l2",
    "shift_r2",
    "shift_u1_noise0p04",
    "shift_l1_noise0p04",
]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def perturb_to_train_view(name: str) -> str:
    if name == "identity":
        return "clean"
    if name.startswith("noise_"):
        return "cam_noise" + name.removeprefix("noise_")
    if name.startswith("blur"):
        return "cam_" + name
    if name.startswith("bright_"):
        raw = name.removeprefix("bright_")
        return "cam_bright" + raw.replace("_", "")
    if name.startswith("contrast_"):
        raw = name.removeprefix("contrast_")
        return "cam_contrast" + raw.replace("_", "")
    if name.startswith("shift_"):
        raw = name.removeprefix("shift_")
        return "cam_shift" + raw.replace("_", "_")
    raise ValueError(f"cannot map perturb to train view: {name}")


def stable_unique(values: list[str]) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for value in values:
        if value not in seen:
            out.append(value)
            seen.add(value)
    return out


def read_excluded_base_rows(stress_path: Path | None) -> set[int]:
    if stress_path is None:
        return set()
    rows: set[int] = set()
    with stress_path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            rows.add(int(row["base_query_index"]))
    return rows


def transformed_margin(values: np.ndarray, dim: int, mode: str) -> np.ndarray:
    score = np.maximum(values.astype(np.float64), 0.0)
    if mode == "raw":
        return score
    if mode == "log":
        return np.log1p(score)
    if mode == "per_sqrt_dim":
        return score / math.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return score / float(max(dim, 1))
    raise ValueError(f"unknown score mode: {mode}")


def softmax(values: np.ndarray, temperature: float) -> np.ndarray:
    temp = max(float(temperature), 1.0e-6)
    scaled = values.astype(np.float64) / temp
    scaled -= np.max(scaled, axis=1, keepdims=True)
    exp = np.exp(scaled)
    return (exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-12)).astype(np.float32)


def build_teacher(
    *,
    source_tflite: list[tuple[str, Path]],
    source_params: list[tuple[str, Path]],
    base_params: Path,
    highpressure_stress: Path | None,
    dataset_dir: Path,
    output_dir: Path,
    perturbs: list[str],
    extra_stress: list[str],
    normal_margin_max: int,
    max_base_rows: int,
    score_mode: str,
    temperature: float,
    label_smoothing: float,
    gap_weight: float,
    batch_size: int,
) -> None:
    source_order = [name for name, _path in source_tflite]
    if [name for name, _path in source_params] != source_order:
        raise ValueError("source names/order must match across tflite and params")

    perturb_views = [perturb_to_train_view(item) for item in perturbs]
    teacher_views = stable_unique(["clean"] + [view for view in perturb_views if view != "clean"])
    stress_names = stable_unique([view for view in extra_stress + teacher_views if view != "clean"])

    flat, images = build_view_dataset(dataset_dir, stress_names)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_set = set(teacher_views)

    base_payload = load_npz(base_params)
    base_sample = np.asarray(base_payload["sample_index"], dtype=np.int64)
    base_view = np.asarray(base_payload["view_labels"]).astype(str)
    base_margin = np.asarray(base_payload["int8_margin"], dtype=np.int64)
    base_margin_by_key = {
        (int(sample), str(view)): int(base_margin[index])
        for index, (sample, view) in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False))
    }
    excluded_rows = read_excluded_base_rows(highpressure_stress)
    excluded_keys = {
        (int(base_sample[index]), str(base_view[index]))
        for index in excluded_rows
        if 0 <= int(index) < len(base_sample)
    }
    clean_margin_by_sample = {
        int(sample): int(base_margin[index])
        for index, (sample, view) in enumerate(zip(base_sample.tolist(), base_view.tolist(), strict=False))
        if str(view) == "clean"
    }
    excluded_clean_samples = {
        int(sample)
        for sample, view in excluded_keys
        if str(view) == "clean"
    }

    active: list[int] = []
    for row_index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist(), strict=False)):
        if str(view) not in view_set:
            continue
        sample_int = int(sample)
        if sample_int in excluded_clean_samples:
            continue
        clean_margin = clean_margin_by_sample.get(sample_int)
        if clean_margin is None or clean_margin > int(normal_margin_max):
            continue
        active.append(int(row_index))
    if max_base_rows > 0:
        by_sample: dict[int, list[int]] = {}
        for row_index in active:
            by_sample.setdefault(int(sample_index[row_index]), []).append(row_index)
        selected_samples = sorted(
            by_sample,
            key=lambda sample: (clean_margin_by_sample.get(int(sample), 10**18), int(sample)),
        )[: int(max_base_rows)]
        keep_samples = set(selected_samples)
        active = [row_index for row_index in active if int(sample_index[row_index]) in keep_samples]
    active_idx = np.asarray(active, dtype=np.int64)
    if len(active_idx) == 0:
        raise ValueError("no active synthetic source-gate teacher rows")

    active_images = images[active_idx]
    active_parent = parent[active_idx]
    payloads = {name: load_npz(path) for name, path in source_params}
    source_arrays: dict[str, dict[str, np.ndarray]] = {}
    for name, tflite_path in source_tflite:
        chunks: list[np.ndarray] = []
        for start in range(0, len(active_images), int(batch_size)):
            features, _ops = tflite_raw_int8(tflite_path, active_images[start : start + int(batch_size)])
            chunks.append(features)
        features = np.concatenate(chunks, axis=0).astype(np.int8)
        source_arrays[name] = classify_features(features=features, parents=active_parent, payload=payloads[name])

    score_columns: list[np.ndarray] = []
    for name in source_order:
        arrays = source_arrays[name]
        dim = int(arrays["feature"].shape[1])
        score = transformed_margin(arrays["margin"], dim, score_mode)
        good = arrays["pred"].astype(np.int64) == active_parent.astype(np.int64)
        score_columns.append(np.where(good, score, -1.0 - np.abs(score)).astype(np.float64))
    score_matrix = np.stack(score_columns, axis=1)
    labels = np.argmax(score_matrix, axis=1).astype(np.int64)

    smooth = float(label_smoothing)
    target = np.full(
        (len(labels), len(source_order)),
        smooth / max(len(source_order) - 1, 1),
        dtype=np.float32,
    )
    target[np.arange(len(labels)), labels] = 1.0 - smooth
    probs = softmax(score_matrix, temperature)
    order = np.argsort(probs, axis=1)
    top1 = probs[np.arange(len(probs)), order[:, -1]]
    top2 = probs[np.arange(len(probs)), order[:, -2]] if probs.shape[1] > 1 else np.zeros(len(probs), dtype=np.float32)
    top_gap = (top1 - top2).astype(np.float32)
    weight = (1.0 + float(gap_weight) * top_gap).astype(np.float32)

    output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_dir / "source_gate_teacher.npz",
        sample_index=sample_index[active_idx].astype(np.int64),
        view_labels=view_labels[active_idx].astype(str),
        parent=active_parent.astype(np.int64),
        target_probs=target.astype(np.float32),
        source_label=labels.astype(np.int64),
        weight=weight.astype(np.float32),
        top_gap=top_gap.astype(np.float32),
        score_matrix=score_matrix.astype(np.float32),
        source_names=np.asarray(source_order).astype(str),
        train_stress_names=np.asarray(stress_names).astype(str),
        teacher_views=np.asarray(teacher_views).astype(str),
        perturb_names=np.asarray(perturbs).astype(str),
        high_pressure_usage=np.asarray("excluded_base_clean_rows_evaluation_only"),
        selection_label_usage=np.asarray("non_highpressure_clean_synthetic_source_winner"),
    )

    rows: list[dict[str, Any]] = []
    for local_index, row_index in enumerate(active_idx.tolist()):
        label = int(labels[local_index])
        sample = int(sample_index[row_index])
        view = str(view_labels[row_index])
        rows.append(
            {
                "row_index": int(row_index),
                "sample_index": sample,
                "view_label": view,
                "parent": int(active_parent[local_index]),
                "clean_margin": int(clean_margin_by_sample.get(sample, -1)),
                "base_margin_same_view": int(base_margin_by_key.get((sample, view), -1)),
                "source_label": label,
                "source_name": source_order[label],
                "top_gap": float(top_gap[local_index]),
                "weight": float(weight[local_index]),
                "scores_json": json.dumps([float(v) for v in score_matrix[local_index].tolist()]),
            }
        )
    write_csv(output_dir / "source_gate_teacher_rows.csv", rows)

    label_counts = {source_order[index]: int(np.sum(labels == index)) for index in range(len(source_order))}
    summary = {
        "output": str(output_dir / "source_gate_teacher.npz"),
        "source_tflite": {name: str(path) for name, path in source_tflite},
        "source_params": {name: str(path) for name, path in source_params},
        "base_params": str(base_params),
        "highpressure_stress": str(highpressure_stress) if highpressure_stress is not None else "",
        "high_pressure_usage": "excluded_base_clean_rows_evaluation_only",
        "selection_label_usage": "non_highpressure_clean_synthetic_source_winner",
        "dataset_dir": str(dataset_dir),
        "stress_names": stress_names,
        "teacher_views": teacher_views,
        "perturbs": perturbs,
        "normal_margin_max": int(normal_margin_max),
        "max_base_rows": int(max_base_rows),
        "active_rows": int(len(active_idx)),
        "active_samples": int(len(set(sample_index[active_idx].astype(int).tolist()))),
        "excluded_highpressure_base_rows": int(len(excluded_rows)),
        "excluded_clean_samples": int(len(excluded_clean_samples)),
        "source_names": source_order,
        "label_counts": label_counts,
        "score_mode": str(score_mode),
        "temperature": float(temperature),
        "label_smoothing": float(label_smoothing),
        "gap_weight": float(gap_weight),
        "top_gap_min": float(np.min(top_gap)),
        "top_gap_mean": float(np.mean(top_gap)),
        "top_gap_p50": float(np.percentile(top_gap, 50)),
        "top_gap_p90": float(np.percentile(top_gap, 90)),
        "weight_min": float(np.min(weight)),
        "weight_mean": float(np.mean(weight)),
        "weight_max": float(np.max(weight)),
    }
    write_json(output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a trainable source-gate teacher from non-high-pressure clean-row synthetic stress views."
    )
    parser.add_argument("--source-tflite", action="append", required=True, help="name=parent_int8.tflite")
    parser.add_argument("--source-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--base-params", type=Path, required=True)
    parser.add_argument("--highpressure-stress", type=Path, default=None)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--perturbs", default=",".join(DEFAULT_HIGHPRESS_PERTURBS))
    parser.add_argument("--extra-stress", default="")
    parser.add_argument("--normal-margin-max", type=int, default=10**9)
    parser.add_argument("--max-base-rows", type=int, default=0)
    parser.add_argument("--score-mode", choices=["raw", "log", "per_sqrt_dim", "per_dim"], default="raw")
    parser.add_argument("--temperature", type=float, default=0.1)
    parser.add_argument("--label-smoothing", type=float, default=0.05)
    parser.add_argument("--gap-weight", type=float, default=4.0)
    parser.add_argument("--batch-size", type=int, default=512)
    args = parser.parse_args()
    build_teacher(
        source_tflite=[parse_named_path(item) for item in args.source_tflite],
        source_params=[parse_named_path(item) for item in args.source_params],
        base_params=args.base_params,
        highpressure_stress=args.highpressure_stress,
        dataset_dir=args.dataset_dir,
        output_dir=args.output_dir,
        perturbs=parse_csv(args.perturbs),
        extra_stress=parse_csv(args.extra_stress) if args.extra_stress else [],
        normal_margin_max=int(args.normal_margin_max),
        max_base_rows=int(args.max_base_rows),
        score_mode=str(args.score_mode),
        temperature=float(args.temperature),
        label_smoothing=float(args.label_smoothing),
        gap_weight=float(args.gap_weight),
        batch_size=int(args.batch_size),
    )


if __name__ == "__main__":
    main()
