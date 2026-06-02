import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any

import numpy as np


NormalKey = tuple[int, str]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def parse_named_path(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=True) as data:
        return {key: data[key] for key in data.files}


def normal_key_map(payload: dict[str, np.ndarray]) -> dict[NormalKey, int]:
    sample = np.asarray(payload["sample_index"], dtype=np.int64)
    view = np.asarray(payload["view_labels"]).astype(str)
    return {
        (int(sample_id), str(view_label)): int(index)
        for index, (sample_id, view_label) in enumerate(zip(sample.tolist(), view.tolist(), strict=False))
    }


def transformed_margin(margin: np.ndarray, dim: int, mode: str) -> np.ndarray:
    values = np.maximum(margin.astype(np.float64), 0.0)
    if mode == "raw":
        return values
    if mode == "per_sqrt_dim":
        return values / math.sqrt(float(max(dim, 1)))
    if mode == "per_dim":
        return values / float(max(dim, 1))
    if mode == "log":
        return np.log1p(values)
    raise ValueError(f"unknown score mode: {mode}")


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


def build_labels(
    *,
    score_matrix: np.ndarray,
    sample_index: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    label_mode: str,
) -> np.ndarray:
    if label_mode == "row_winner":
        return np.argmax(score_matrix, axis=1).astype(np.int64)

    if label_mode.startswith("sample_family_"):
        group_keys = [
            (int(sample), int(parent_value), view_family(str(view)))
            for sample, parent_value, view in zip(
                sample_index.tolist(),
                parent.tolist(),
                view_labels.astype(str).tolist(),
                strict=False,
            )
        ]
        aggregate_mode = label_mode.removeprefix("sample_family_")
    elif label_mode.startswith("sample_"):
        group_keys = [
            (int(sample), int(parent_value))
            for sample, parent_value in zip(sample_index.tolist(), parent.tolist(), strict=False)
        ]
        aggregate_mode = label_mode.removeprefix("sample_")
    else:
        raise ValueError(f"unknown label mode: {label_mode}")

    grouped: dict[tuple[Any, ...], list[int]] = {}
    for row_index, key in enumerate(group_keys):
        grouped.setdefault(key, []).append(row_index)

    group_choice: dict[tuple[Any, ...], int] = {}
    for key, indexes in grouped.items():
        scores = score_matrix[np.asarray(indexes, dtype=np.int64)]
        if aggregate_mode == "min":
            aggregate = np.min(scores, axis=0)
        elif aggregate_mode == "correct_count":
            correct = scores >= 0.0
            aggregate = np.sum(correct, axis=0).astype(np.float64) * 1.0e6
            aggregate += np.mean(np.maximum(scores, 0.0), axis=0)
        else:
            raise ValueError(f"unknown label aggregate mode: {aggregate_mode}")
        group_choice[key] = int(np.argmax(aggregate))

    return np.asarray([group_choice[key] for key in group_keys], dtype=np.int64)


def softmax(values: np.ndarray, temperature: float) -> np.ndarray:
    temp = max(float(temperature), 1.0e-6)
    scaled = values.astype(np.float64) / temp
    scaled -= np.max(scaled, axis=1, keepdims=True)
    exp = np.exp(scaled)
    return (exp / np.maximum(np.sum(exp, axis=1, keepdims=True), 1.0e-12)).astype(np.float32)


def build_teacher(
    *,
    normal_params: list[tuple[str, Path]],
    output_dir: Path,
    label_mode: str,
    score_mode: str,
    temperature: float,
    label_smoothing: float,
    gap_weight: float,
) -> None:
    source_names = [name for name, _path in normal_params]
    payloads = {name: load_npz(path) for name, path in normal_params}
    maps = {name: normal_key_map(payload) for name, payload in payloads.items()}
    keys = sorted(set.intersection(*(set(mapping) for mapping in maps.values())))
    if not keys:
        raise ValueError("normal params have no common sample/view rows")

    base = payloads[source_names[0]]
    base_map = maps[source_names[0]]
    base_indexes = np.asarray([base_map[key] for key in keys], dtype=np.int64)
    sample_index = np.asarray([key[0] for key in keys], dtype=np.int64)
    view_labels = np.asarray([key[1] for key in keys]).astype(str)
    parent = np.asarray(base["parent"], dtype=np.int64)[base_indexes]

    score_rows: list[np.ndarray] = []
    for name in source_names:
        payload = payloads[name]
        indexes = np.asarray([maps[name][key] for key in keys], dtype=np.int64)
        pred = np.asarray(payload["int8_pred"], dtype=np.int64)[indexes]
        source_parent = np.asarray(payload["parent"], dtype=np.int64)[indexes]
        if not np.array_equal(source_parent, parent):
            raise ValueError(f"parent mismatch for source {name}")
        margin = np.asarray(payload["int8_margin"], dtype=np.int64)[indexes]
        dim = int(np.asarray(payload["embedding_int8"]).shape[1])
        score = transformed_margin(margin, dim, score_mode)
        score = np.where(pred == parent, score, -1.0 - np.abs(score)).astype(np.float64)
        score_rows.append(score)
    score_matrix = np.stack(score_rows, axis=1)
    labels = build_labels(
        score_matrix=score_matrix,
        sample_index=sample_index,
        parent=parent,
        view_labels=view_labels,
        label_mode=label_mode,
    )
    smooth = float(label_smoothing)
    target = np.full(
        (len(labels), len(source_names)),
        smooth / max(len(source_names) - 1, 1),
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
        sample_index=sample_index.astype(np.int64),
        view_labels=view_labels.astype(str),
        parent=parent.astype(np.int64),
        target_probs=target.astype(np.float32),
        source_label=labels.astype(np.int64),
        weight=weight.astype(np.float32),
        top_gap=top_gap.astype(np.float32),
        score_matrix=score_matrix.astype(np.float32),
        source_names=np.asarray(source_names).astype(str),
        label_mode=np.asarray(str(label_mode)),
        score_mode=np.asarray(str(score_mode)),
        high_pressure_usage=np.asarray("none"),
    )

    rows: list[dict[str, Any]] = []
    for row_index in range(len(labels)):
        label = int(labels[row_index])
        rows.append(
            {
                "row_index": int(row_index),
                "sample_index": int(sample_index[row_index]),
                "view_label": str(view_labels[row_index]),
                "parent": int(parent[row_index]),
                "source_label": label,
                "source_name": source_names[label],
                "top_gap": float(top_gap[row_index]),
                "weight": float(weight[row_index]),
                "scores_json": json.dumps([float(value) for value in score_matrix[row_index].tolist()]),
                "target_probs_json": json.dumps([float(value) for value in target[row_index].tolist()]),
            }
        )
    write_csv(output_dir / "source_gate_teacher_rows.csv", rows)
    label_counts = {
        source_names[index]: int(np.sum(labels == index))
        for index in range(len(source_names))
    }
    summary = {
        "output": str(output_dir / "source_gate_teacher.npz"),
        "normal_params": {name: str(path) for name, path in normal_params},
        "high_pressure_usage": "none",
        "label_mode": str(label_mode),
        "score_mode": str(score_mode),
        "temperature": float(temperature),
        "label_smoothing": float(label_smoothing),
        "gap_weight": float(gap_weight),
        "row_count": int(len(labels)),
        "source_count": int(len(source_names)),
        "source_names": source_names,
        "label_counts": label_counts,
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
    parser = argparse.ArgumentParser(description="Build a V8 normal-only multisource source-gate teacher from deployed params.")
    parser.add_argument("--normal-params", action="append", required=True, help="name=params.npz")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--label-mode", default="sample_correct_count")
    parser.add_argument("--score-mode", default="raw")
    parser.add_argument("--temperature", type=float, default=0.1)
    parser.add_argument("--label-smoothing", type=float, default=0.05)
    parser.add_argument("--gap-weight", type=float, default=4.0)
    args = parser.parse_args()
    build_teacher(
        normal_params=[parse_named_path(item) for item in args.normal_params],
        output_dir=args.output_dir,
        label_mode=args.label_mode,
        score_mode=args.score_mode,
        temperature=args.temperature,
        label_smoothing=args.label_smoothing,
        gap_weight=args.gap_weight,
    )


if __name__ == "__main__":
    main()
