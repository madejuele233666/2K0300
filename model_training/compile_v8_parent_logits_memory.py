import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf

from evaluate_v8_embedding_prototypes import (
    ROT_MIRROR_VIEWS,
    metric_summary,
    parse_csv,
    predict_closed,
    predict_int8,
    write_csv,
)
from train_v8_parent_classifier import build_view_dataset


DEFAULT_STRESS = (
    "rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,"
    "noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,"
    "cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,"
    "cam_blur3a0_noise0p02,cam_blur5a45_noise0p04"
)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def tflite_outputs(path: Path, images: np.ndarray) -> tuple[np.ndarray, np.ndarray | None, list[str]]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_index = int(input_detail["index"])
    output_index = int(output_detail["index"])
    in_scale, in_zero = input_detail.get("quantization", (0.0, 0))
    out_scale, out_zero = output_detail.get("quantization", (0.0, 0))
    float_rows: list[np.ndarray] = []
    int8_rows: list[np.ndarray] = []
    for image in images:
        value = image[None, ...].astype(np.float32)
        if input_detail["dtype"] == np.int8:
            value = np.clip(np.rint(value / float(in_scale) + int(in_zero)), -128, 127).astype(np.int8)
        interpreter.set_tensor(input_index, value)
        interpreter.invoke()
        output = interpreter.get_tensor(output_index)
        if output_detail["dtype"] == np.int8:
            int8_rows.append(output[0].astype(np.int8))
            dequant = (output.astype(np.float32) - int(out_zero)) * float(out_scale)
            float_rows.append(dequant[0].astype(np.float32))
        else:
            float_rows.append(output[0].astype(np.float32))
    op_names = [str(item["op_name"]) for item in interpreter._get_ops_details()]  # noqa: SLF001
    int8 = np.stack(int8_rows).astype(np.int8) if int8_rows else None
    return np.stack(float_rows).astype(np.float32), int8, sorted(set(op_names))


def class_distances_int8(embeddings: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> np.ndarray:
    x = embeddings.astype(np.int32)
    p = prototypes.astype(np.int32)
    dist = np.sum((x[:, None, :] - p[None, :, :]) ** 2, axis=2)
    by_class: list[np.ndarray] = []
    for parent in range(3):
        mask = prototype_parent == parent
        if np.any(mask):
            by_class.append(np.min(dist[:, mask], axis=1).astype(np.int64))
        else:
            by_class.append(np.full(len(embeddings), np.iinfo(np.int32).max, dtype=np.int64))
    return np.stack(by_class, axis=1)


def load_source_decision_teacher(
    path: Path | None,
    sample_index: np.ndarray,
    view_labels: np.ndarray,
    y_parent: np.ndarray,
) -> dict[str, np.ndarray]:
    row_count = len(sample_index)
    teacher = {
        "wrong_parent": np.full(row_count, -1, dtype=np.int64),
        "target_margin": np.zeros(row_count, dtype=np.float32),
        "weight": np.zeros(row_count, dtype=np.float32),
    }
    if path is None:
        return teacher
    row_by_key = {
        (int(sample), str(view)): int(index)
        for index, (sample, view) in enumerate(zip(sample_index.tolist(), view_labels.tolist(), strict=False))
    }
    with np.load(path, allow_pickle=True) as data:
        required = ["sample_index", "view_labels", "parent", "wrong_parent", "target_margin", "weight"]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{path} is missing source decision arrays: {missing}")
        teacher_sample = np.asarray(data["sample_index"], dtype=np.int64)
        teacher_view = np.asarray(data["view_labels"]).astype(str)
        teacher_parent = np.asarray(data["parent"], dtype=np.int64)
        wrong_parent = np.asarray(data["wrong_parent"], dtype=np.int64)
        target_margin = np.asarray(data["target_margin"], dtype=np.float32)
        weight = np.asarray(data["weight"], dtype=np.float32)
    missing_keys: list[tuple[int, str]] = []
    for index, (sample, view) in enumerate(zip(teacher_sample.tolist(), teacher_view.tolist(), strict=False)):
        row_index = row_by_key.get((int(sample), str(view)))
        if row_index is None:
            missing_keys.append((int(sample), str(view)))
            continue
        if int(teacher_parent[index]) != int(y_parent[row_index]):
            raise ValueError(f"teacher parent mismatch for {sample}:{view}")
        teacher["wrong_parent"][row_index] = int(wrong_parent[index])
        teacher["target_margin"][row_index] = float(target_margin[index])
        teacher["weight"][row_index] = float(weight[index])
    if missing_keys:
        preview = ", ".join(f"{sample}:{view}" for sample, view in missing_keys[:10])
        raise ValueError(f"source decision teacher rows missing from compile views: {len(missing_keys)}, first {preview}")
    return teacher


def keras_outputs(run_dir: Path, images: np.ndarray) -> np.ndarray:
    model = tf.keras.models.load_model(run_dir / "parent_model.keras", safe_mode=False)
    return model.predict(images, batch_size=512, verbose=0).astype(np.float32)


def subset_mask(view_labels: np.ndarray, subset: str) -> np.ndarray:
    if subset == "clean":
        return view_labels == "clean"
    if subset == "clean_rotmirror":
        return (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    if subset == "all":
        return np.ones(len(view_labels), dtype=bool)
    raise ValueError(f"unknown subset: {subset}")


def evaluate_exact_table(
    *,
    name: str,
    source_kind: str,
    subset: str,
    embeddings_float: np.ndarray,
    embeddings_int8: np.ndarray | None,
    flat: dict[str, Any],
    ops: list[str],
) -> tuple[dict[str, Any], dict[str, np.ndarray]]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    mask = subset_mask(view_labels, subset)
    prototypes = embeddings_float[mask].astype(np.float64)
    prototype_parent = y_parent[mask].astype(np.int64)
    prototype_subclass = y_sub[mask].astype(np.int64)
    prototype_sample_index = sample_index[mask].astype(np.int64)
    prototype_view_label = view_labels[mask].astype(str)
    pred, margin = predict_closed(embeddings_float.astype(np.float64), prototypes, prototype_parent)
    if embeddings_int8 is not None:
        prototypes_int8 = embeddings_int8[mask].astype(np.int8)
        int8_pred, int8_margin = predict_int8(embeddings_int8, prototypes_int8, prototype_parent)
        int8_scale = 1.0
    else:
        best_int8: tuple[tuple[Any, ...], np.ndarray, np.ndarray, float] | None = None
        for scale in [4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128]:
            z_q = np.clip(np.rint(embeddings_float * float(scale)), -128, 127).astype(np.int8)
            p_q = z_q[mask].astype(np.int8)
            cand_pred, cand_margin = predict_int8(z_q, p_q, prototype_parent)
            score = (
                int(np.sum(cand_pred == y_parent)),
                bool(np.all(cand_pred == y_parent)),
                int(np.min(cand_margin)),
                float(scale),
            )
            if best_int8 is None or score > best_int8[0]:
                best_int8 = (score, cand_pred, cand_margin, float(scale))
        assert best_int8 is not None
        int8_pred = best_int8[1]
        int8_margin = best_int8[2]
        int8_scale = best_int8[3]
        prototypes_int8 = np.clip(np.rint(prototypes * int8_scale), -128, 127).astype(np.int8)
    row: dict[str, Any] = {
        "stage": "v8_parent_logits_exact_memory",
        "name": name,
        "feature_source": source_kind,
        "prototype_source": f"exact_{subset}",
        "k_per_subclass": "",
        "feature_dim": int(embeddings_float.shape[1]),
        "prototype_count": int(len(prototypes)),
        "estimated_distance_macs": int(len(prototypes) * embeddings_float.shape[1]),
        "estimated_float_table_bytes": int(len(prototypes) * embeddings_float.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(prototypes) * embeddings_float.shape[1]),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "int8_scale": float(int8_scale),
        "int8_flip_count": int(np.sum(int8_pred != pred)),
        "int8_margin_min": int(np.min(int8_margin)),
        "int8_margin_mean": float(np.mean(int8_margin)),
        "tflite_unique_ops": json.dumps(ops, ensure_ascii=False),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
    payload = {
        "embedding_float": embeddings_float.astype(np.float32),
        "embedding_int8": embeddings_int8.astype(np.int8)
        if embeddings_int8 is not None
        else np.clip(np.rint(embeddings_float * int8_scale), -128, 127).astype(np.int8),
        "parent": y_parent.astype(np.int64),
        "subclass": y_sub.astype(np.int64),
        "sample_index": sample_index.astype(np.int64),
        "view_labels": view_labels.astype(str),
        "paths": np.asarray(flat["paths"]).astype(str),
        "pred": pred.astype(np.int64),
        "int8_pred": int8_pred.astype(np.int64),
        "margin": margin.astype(np.float32),
        "int8_margin": int8_margin.astype(np.int64),
        "prototypes": prototypes.astype(np.float32),
        "prototypes_int8": prototypes_int8.astype(np.int8),
        "prototype_parent": prototype_parent.astype(np.int64),
        "prototype_subclass": prototype_subclass.astype(np.int64),
        "prototype_cluster": np.arange(len(prototypes), dtype=np.int64),
        "prototype_sample_index": prototype_sample_index.astype(np.int64),
        "prototype_view_label": prototype_view_label.astype(str),
        "prototype_source_kind": np.asarray([f"exact_{subset}"] * len(prototypes)),
        "feature_source": np.asarray(source_kind),
        "int8_scale": np.asarray(float(int8_scale), dtype=np.float32),
        "tie_break_policy": np.asarray("argmin_parent_order"),
    }
    return row, payload


def evaluate_residual_table(
    *,
    name: str,
    source_kind: str,
    base_subset: str,
    embeddings_float: np.ndarray,
    embeddings_int8: np.ndarray | None,
    flat: dict[str, Any],
    ops: list[str],
    max_iterations: int,
    target_margin: float,
    target_int8_margin: int,
    source_decision_teacher: dict[str, np.ndarray],
    source_decision_margin: float,
) -> tuple[dict[str, Any], dict[str, np.ndarray]] | None:
    if embeddings_int8 is None:
        return None
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    selected = subset_mask(view_labels, base_subset).copy()
    source_wrong_parent = np.asarray(source_decision_teacher["wrong_parent"], dtype=np.int64)
    source_target_margin = np.asarray(source_decision_teacher["target_margin"], dtype=np.float32)
    source_weight = np.asarray(source_decision_teacher["weight"], dtype=np.float32)
    source_active = (source_wrong_parent >= 0) & (source_weight > 0.0)
    source_margin_threshold = (
        np.full(len(y_parent), float(source_decision_margin), dtype=np.float32)
        if float(source_decision_margin) >= 0.0
        else source_target_margin
    )
    trace: list[dict[str, Any]] = []
    source_decision_margin_final = np.zeros(len(y_parent), dtype=np.int64)
    for iteration in range(1, max_iterations + 1):
        prototypes = embeddings_float[selected].astype(np.float64)
        prototypes_int8 = embeddings_int8[selected].astype(np.int8)
        prototype_parent = y_parent[selected].astype(np.int64)
        pred, margin = predict_closed(embeddings_float.astype(np.float64), prototypes, prototype_parent)
        int8_class_dist = class_distances_int8(embeddings_int8, prototypes_int8, prototype_parent)
        int8_pred = np.argmin(int8_class_dist, axis=1).astype(np.int64)
        sorted_int8_dist = np.sort(int8_class_dist, axis=1)
        int8_margin = sorted_int8_dist[:, 1].astype(np.int64) - sorted_int8_dist[:, 0].astype(np.int64)
        wrong_mask = (pred != y_parent) | (int8_pred != y_parent)
        source_decision_risk = np.zeros(len(y_parent), dtype=bool)
        if np.any(source_active):
            source_margin = (
                int8_class_dist[np.arange(len(y_parent)), source_wrong_parent.clip(0, 2)]
                - int8_class_dist[np.arange(len(y_parent)), y_parent]
            )
            source_decision_margin_final = source_margin.astype(np.int64)
            source_decision_risk = source_active & (source_margin <= source_margin_threshold)
        risk_mask = wrong_mask | (margin <= target_margin) | (int8_margin <= target_int8_margin) | source_decision_risk
        risk = np.where(risk_mask)[0]
        if len(risk) == 0:
            break
        add_indexes = [int(index) for index in risk if not bool(selected[int(index)])]
        if not add_indexes:
            break
        selected[np.asarray(add_indexes, dtype=np.int64)] = True
        trace.append(
            {
                "iteration": int(iteration),
                "added": int(len(add_indexes)),
                "wrong_before": int(np.sum(pred != y_parent)),
                "int8_wrong_before": int(np.sum(int8_pred != y_parent)),
                "risk_before": int(np.sum(risk_mask)),
                "source_decision_risk_before": int(np.sum(source_decision_risk)),
                "prototype_count_after": int(np.sum(selected)),
            }
        )

    prototypes = embeddings_float[selected].astype(np.float64)
    prototypes_int8 = embeddings_int8[selected].astype(np.int8)
    prototype_parent = y_parent[selected].astype(np.int64)
    prototype_subclass = y_sub[selected].astype(np.int64)
    prototype_sample_index = sample_index[selected].astype(np.int64)
    prototype_view_label = view_labels[selected].astype(str)
    pred, margin = predict_closed(embeddings_float.astype(np.float64), prototypes, prototype_parent)
    int8_class_dist = class_distances_int8(embeddings_int8, prototypes_int8, prototype_parent)
    int8_pred = np.argmin(int8_class_dist, axis=1).astype(np.int64)
    sorted_int8_dist = np.sort(int8_class_dist, axis=1)
    int8_margin = sorted_int8_dist[:, 1].astype(np.int64) - sorted_int8_dist[:, 0].astype(np.int64)
    source_decision_margin_final = np.zeros(len(y_parent), dtype=np.int64)
    if np.any(source_active):
        source_decision_margin_final = (
            int8_class_dist[np.arange(len(y_parent)), source_wrong_parent.clip(0, 2)]
            - int8_class_dist[np.arange(len(y_parent)), y_parent]
        ).astype(np.int64)
    source_decision_active_margin = source_decision_margin_final[source_active]
    row: dict[str, Any] = {
        "stage": "v8_parent_logits_residual_memory",
        "name": name,
        "feature_source": source_kind,
        "prototype_source": f"exact_{base_subset}_residual",
        "k_per_subclass": "",
        "feature_dim": int(embeddings_float.shape[1]),
        "prototype_count": int(len(prototypes)),
        "estimated_distance_macs": int(len(prototypes) * embeddings_float.shape[1]),
        "estimated_float_table_bytes": int(len(prototypes) * embeddings_float.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(prototypes) * embeddings_float.shape[1]),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "int8_scale": 1.0,
        "int8_flip_count": int(np.sum(int8_pred != pred)),
        "int8_margin_min": int(np.min(int8_margin)),
        "int8_margin_mean": float(np.mean(int8_margin)),
        "source_decision_active_rows": int(np.sum(source_active)),
        "source_decision_margin_min": int(np.min(source_decision_active_margin)) if len(source_decision_active_margin) else 0,
        "source_decision_margin_mean": float(np.mean(source_decision_active_margin)) if len(source_decision_active_margin) else 0.0,
        "source_decision_margin_le_target": int(
            np.sum(source_active & (source_decision_margin_final <= source_margin_threshold))
        ),
        "tflite_unique_ops": json.dumps(ops, ensure_ascii=False),
        "selected_trace_json": json.dumps(trace, ensure_ascii=False),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
    payload = {
        "embedding_float": embeddings_float.astype(np.float32),
        "embedding_int8": embeddings_int8.astype(np.int8),
        "parent": y_parent.astype(np.int64),
        "subclass": y_sub.astype(np.int64),
        "sample_index": sample_index.astype(np.int64),
        "view_labels": view_labels.astype(str),
        "paths": np.asarray(flat["paths"]).astype(str),
        "pred": pred.astype(np.int64),
        "int8_pred": int8_pred.astype(np.int64),
        "margin": margin.astype(np.float32),
        "int8_margin": int8_margin.astype(np.int64),
        "source_decision_wrong_parent": source_wrong_parent.astype(np.int64),
        "source_decision_target_margin": source_margin_threshold.astype(np.float32),
        "source_decision_weight": source_weight.astype(np.float32),
        "source_decision_margin": source_decision_margin_final.astype(np.int64),
        "prototypes": prototypes.astype(np.float32),
        "prototypes_int8": prototypes_int8.astype(np.int8),
        "prototype_parent": prototype_parent.astype(np.int64),
        "prototype_subclass": prototype_subclass.astype(np.int64),
        "prototype_cluster": np.arange(len(prototypes), dtype=np.int64),
        "prototype_sample_index": prototype_sample_index.astype(np.int64),
        "prototype_view_label": prototype_view_label.astype(str),
        "prototype_source_kind": np.asarray([f"exact_{base_subset}_residual"] * len(prototypes)),
        "feature_source": np.asarray(source_kind),
        "selected_trace_json": np.asarray(json.dumps(trace, ensure_ascii=False)),
        "int8_scale": np.asarray(1.0, dtype=np.float32),
        "tie_break_policy": np.asarray("argmin_parent_order"),
    }
    return row, payload


def row_score(row: dict[str, Any]) -> tuple[Any, ...]:
    all_acc = min(
        float(row["clean_accuracy"]),
        float(row["rotmirror_min_accuracy"]),
        float(row["stress_min_accuracy"]),
        float(row["int8_clean_accuracy"]),
        float(row["int8_rotmirror_min_accuracy"]),
        float(row["int8_stress_min_accuracy"]),
    )
    return (
        all_acc,
        bool(row["clean_all_correct"]),
        bool(row["rotmirror_all_correct"]),
        bool(row["stress_all_correct"]),
        bool(row["int8_clean_all_correct"]),
        bool(row["int8_rotmirror_all_correct"]),
        bool(row["int8_stress_all_correct"]),
        -int(row["prototype_count"]),
        float(row["margin_min"]),
        int(row["int8_margin_min"]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Compile exact-memory prototypes over V8 parent logits.")
    parser.add_argument("--parent-run-dir", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stress", default=DEFAULT_STRESS)
    parser.add_argument("--feature-sources", default="int8_tflite,float_tflite")
    parser.add_argument("--prototype-subsets", default="clean,clean_rotmirror,all")
    parser.add_argument("--residual-bases", default="clean,clean_rotmirror")
    parser.add_argument("--max-residual-iterations", type=int, default=12)
    parser.add_argument("--residual-target-margin", type=float, default=0.0)
    parser.add_argument("--residual-target-int8-margin", type=int, default=0)
    parser.add_argument("--source-decision-teacher-npz", type=Path, default=None)
    parser.add_argument(
        "--source-decision-compiler-margin",
        type=float,
        default=-1.0,
        help="Use this source-decision distance margin threshold; negative means use per-row teacher target.",
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    flat, images = build_view_dataset(args.dataset_dir, parse_csv(args.stress))
    source_decision_teacher = load_source_decision_teacher(
        args.source_decision_teacher_npz,
        np.asarray(flat["sample_index"], dtype=np.int64),
        np.asarray(flat["view_labels"]).astype(str),
        np.asarray(flat["y_parent"], dtype=np.int64),
    )
    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    sources = parse_csv(args.feature_sources)
    subsets = parse_csv(args.prototype_subsets)
    for source in sources:
        if source == "keras":
            embeddings = keras_outputs(args.parent_run_dir, images)
            embeddings_int8 = None
            ops: list[str] = []
        elif source == "float_tflite":
            embeddings, _raw_int8, ops = tflite_outputs(args.parent_run_dir / "parent_float.tflite", images)
            embeddings_int8 = None
        elif source == "int8_tflite":
            embeddings, raw_int8, ops = tflite_outputs(args.parent_run_dir / "parent_int8.tflite", images)
            if raw_int8 is None:
                raise ValueError(f"{args.parent_run_dir / 'parent_int8.tflite'} did not produce int8 outputs")
            embeddings_int8 = raw_int8
            embeddings = raw_int8.astype(np.float32)
        else:
            raise ValueError(f"unknown feature source: {source}")
        for subset in subsets:
            row, payload = evaluate_exact_table(
                name=f"{args.parent_run_dir.name}_{source}_{subset}",
                source_kind=source,
                subset=subset,
                embeddings_float=embeddings,
                embeddings_int8=embeddings_int8,
                flat=flat,
                ops=ops,
            )
            rows.append(row)
            payloads.append(payload)
            print(json.dumps({"candidate": row}, ensure_ascii=False), flush=True)
        for base_subset in parse_csv(args.residual_bases):
            residual = evaluate_residual_table(
                name=f"{args.parent_run_dir.name}_{source}_{base_subset}_residual",
                source_kind=source,
                base_subset=base_subset,
                embeddings_float=embeddings,
                embeddings_int8=embeddings_int8,
                flat=flat,
                ops=ops,
                max_iterations=args.max_residual_iterations,
                target_margin=args.residual_target_margin,
                target_int8_margin=args.residual_target_int8_margin,
                source_decision_teacher=source_decision_teacher,
                source_decision_margin=args.source_decision_compiler_margin,
            )
            if residual is None:
                continue
            row, payload = residual
            rows.append(row)
            payloads.append(payload)
            print(json.dumps({"candidate": row}, ensure_ascii=False), flush=True)
    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: row_score(item[0]), reverse=True)
    rows_sorted = [row for row, _payload in sorted_pairs]
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    if sorted_pairs:
        np.savez_compressed(args.output_dir / "best_parent_logits_memory_params.npz", **sorted_pairs[0][1])
    config_src = args.parent_run_dir / "train_config.json"
    if config_src.exists():
        shutil.copy2(config_src, args.output_dir / "train_config.json")
    summary = {
        "parent_run_dir": str(args.parent_run_dir),
        "candidate_count": len(rows_sorted),
        "best": rows_sorted[0] if rows_sorted else None,
        "top20": rows_sorted[:20],
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps({"best": summary["best"], "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
