import argparse
import csv
import json
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf

from evaluate_v8_embedding_prototypes import metric_summary, predict_closed, predict_int8, quantize, write_csv
from train_v8_end_to_end_embedding import build_embedding_model, build_view_dataset, parse_filters


DEFAULT_MEDIUM_STRESS = (
    "cam_blur3a45,"
    "cam_blur3a135,"
    "cam_noise0p03,"
    "cam_blur3a45_noise0p02,"
    "cam_blur3a135_noise0p02,"
    "cam_bright0p06,"
    "cam_contrast0p12,"
    "cam_bright0p04_contrast0p10"
)


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def load_best_int8_scale(summary_path: Path | None, default: float) -> float:
    if summary_path is None or not summary_path.exists():
        return default
    data = json.loads(summary_path.read_text(encoding="utf-8"))
    best = data.get("best") or {}
    return float(best.get("int8_scale", default))


def load_embedding_model(model_path: Path) -> tf.keras.Model:
    config_path = model_path.parent / "train_config.json"
    if config_path.exists():
        config = json.loads(config_path.read_text(encoding="utf-8"))
        model = build_embedding_model(
            filters=parse_filters(",".join(str(x) for x in config["filters"])),
            embedding_dim=int(config["embedding_dim"]),
            learning_rate=float(config.get("learning_rate", 0.0015)),
            l2=float(config.get("l2", 1.0e-4)),
            dropout=float(config.get("dropout", 0.0)),
            first_kernel=int(config.get("first_kernel", 3)),
            embedding_output_mode=str(config.get("embedding_output_mode", "raw")),
            backbone_architecture=str(config.get("backbone_architecture", "spacetodepth_conv")),
            activation=str(config.get("activation", "relu6")),
            pool=str(config.get("pool", "max")),
            extra_conv=bool(config.get("extra_conv", False)),
        )
        model.load_weights(model_path)
        return model
    return tf.keras.models.load_model(model_path, safe_mode=False)


def repair_medium_prototypes(
    *,
    payload: dict[str, np.ndarray],
    embeddings: np.ndarray,
    flat: dict[str, Any],
    int8_scale: float,
    max_extra: int,
    max_iterations: int = 8,
) -> tuple[dict[str, np.ndarray], int, int]:
    prototypes = np.asarray(payload["prototypes"], dtype=np.float32).copy()
    prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64).copy()
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    added: set[int] = set()
    added_order: list[int] = []

    for _ in range(max_iterations):
        pred, margin = predict_closed(embeddings, prototypes, prototype_parent)
        int8_pred, _int8_margin = predict_int8(quantize(embeddings, int8_scale), quantize(prototypes, int8_scale), prototype_parent)
        wrong = np.where((pred != y_parent) | (int8_pred != y_parent))[0]
        if len(wrong) == 0 or len(added) >= max_extra:
            break
        new_wrong = [int(index) for index in wrong if int(index) not in added]
        if not new_wrong:
            break
        new_wrong.sort(key=lambda index: (float(margin[index]), str(view_labels[index]), int(sample_index[index])))
        for index in new_wrong[: max_extra - len(added)]:
            added.add(index)
            added_order.append(index)
            prototypes = np.concatenate([prototypes, embeddings[index : index + 1].astype(np.float32)], axis=0)
            prototype_parent = np.concatenate([prototype_parent, np.asarray([y_parent[index]], dtype=np.int64)])

    repaired = dict(payload)
    repaired["prototypes"] = prototypes.astype(np.float32)
    repaired["prototype_parent"] = prototype_parent.astype(np.int64)
    if "prototype_subclass" in repaired:
        extra_sub = [int(y_sub[index]) for index in added_order]
        repaired["prototype_subclass"] = np.concatenate(
            [np.asarray(repaired["prototype_subclass"], dtype=np.int64), np.asarray(extra_sub, dtype=np.int64)]
        )
    if "prototype_cluster" in repaired:
        start = len(np.asarray(repaired["prototype_cluster"]))
        repaired["prototype_cluster"] = np.concatenate(
            [np.asarray(repaired["prototype_cluster"], dtype=np.int64), -2000 - np.arange(start, start + len(added), dtype=np.int64)]
        )
    if "prototype_sample_index" in repaired:
        extra_sample = [int(sample_index[index]) for index in added_order]
        repaired["prototype_sample_index"] = np.concatenate(
            [np.asarray(repaired["prototype_sample_index"], dtype=np.int64), np.asarray(extra_sample, dtype=np.int64)]
        )
    if "prototype_view_label" in repaired:
        extra_view = [str(view_labels[index]) for index in added_order]
        repaired["prototype_view_label"] = np.concatenate(
            [np.asarray(repaired["prototype_view_label"]).astype(str), np.asarray(extra_view)]
        )
    pred, _margin = predict_closed(embeddings, prototypes, prototype_parent)
    int8_pred, _int8_margin = predict_int8(quantize(embeddings, int8_scale), quantize(prototypes, int8_scale), prototype_parent)
    unresolved = int(np.sum((pred != y_parent) | (int8_pred != y_parent)))
    return repaired, len(added), unresolved


def summarize(
    *,
    model_path: Path,
    params_npz: Path,
    dataset_dir: Path,
    stress_names: list[str],
    output_dir: Path,
    summary_json: Path | None,
    int8_scale: float,
    repair_output_npz: Path | None,
    repair_max_extra: int,
) -> dict[str, Any]:
    flat, images = build_view_dataset(dataset_dir, stress_names)
    model = load_embedding_model(model_path)
    embeddings = model.predict(images, batch_size=256, verbose=0).astype(np.float32)
    with np.load(params_npz, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}
    prototypes = np.asarray(payload["prototypes"], dtype=np.float32)
    prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
    scale = load_best_int8_scale(summary_json, int8_scale)
    base_pred, _base_margin = predict_closed(embeddings, prototypes, prototype_parent)
    base_int8_pred, _base_int8_margin = predict_int8(quantize(embeddings, scale), quantize(prototypes, scale), prototype_parent)
    base_summary = metric_summary(
        view_order=list(flat["view_names"]),
        view_labels=np.asarray(flat["view_labels"]).astype(str),
        y_parent=np.asarray(flat["y_parent"], dtype=np.int64),
        pred=base_pred,
    )
    base_int8_summary = metric_summary(
        view_order=list(flat["view_names"]),
        view_labels=np.asarray(flat["view_labels"]).astype(str),
        y_parent=np.asarray(flat["y_parent"], dtype=np.int64),
        pred=base_int8_pred,
        prefix="int8_",
    )
    repair_extra_count = 0
    repair_unresolved_count = 0
    if repair_output_npz is not None:
        payload, repair_extra_count, repair_unresolved_count = repair_medium_prototypes(
            payload=payload,
            embeddings=embeddings,
            flat=flat,
            int8_scale=scale,
            max_extra=repair_max_extra,
        )
        repair_output_npz.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(repair_output_npz, **payload)
        prototypes = np.asarray(payload["prototypes"], dtype=np.float32)
        prototype_parent = np.asarray(payload["prototype_parent"], dtype=np.int64)
    pred, margin = predict_closed(embeddings, prototypes, prototype_parent)
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    row: dict[str, Any] = {
        "model_path": str(model_path),
        "params_npz": str(params_npz),
        "stress_names": ",".join(stress_names),
        "prototype_source": "medium_repair" if repair_output_npz is not None else "provided",
        "k_per_subclass": -1,
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(len(prototypes)),
        "estimated_distance_macs": int(len(prototypes) * embeddings.shape[1]),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    z_q = quantize(embeddings, scale)
    p_q = quantize(prototypes, scale)
    int8_pred, int8_margin = predict_int8(z_q, p_q, prototype_parent)
    row["int8_scale"] = float(scale)
    row["int8_margin_min"] = int(np.min(int8_margin))
    row["int8_margin_mean"] = float(np.mean(int8_margin))
    row["medium_repair_extra_count"] = int(repair_extra_count)
    row["medium_repair_unresolved_count"] = int(repair_unresolved_count)
    row["base_stress_min_accuracy"] = float(base_summary["stress_min_accuracy"])
    row["base_stress_mean_accuracy"] = float(base_summary["stress_mean_accuracy"])
    row["base_int8_stress_min_accuracy"] = float(base_int8_summary["int8_stress_min_accuracy"])
    row["base_int8_stress_mean_accuracy"] = float(base_int8_summary["int8_stress_mean_accuracy"])
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
    per_view = json.loads(str(row["per_view_json"]))
    int8_per_view = json.loads(str(row["int8_per_view_json"]))
    write_csv(output_dir / "medium_stress_per_view.csv", per_view)
    write_csv(output_dir / "medium_stress_int8_per_view.csv", int8_per_view)
    write_csv(output_dir / "candidate_results.csv", [row])
    train_config = model_path.parent / "train_config.json"
    if train_config.exists():
        import shutil

        shutil.copyfile(train_config, output_dir / "train_config.json")
    (output_dir / "medium_stress_summary.json").write_text(json.dumps(row, indent=2, ensure_ascii=False), encoding="utf-8")
    return row


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate a V8 embedding prototype bundle on optional medium stress views.")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--stress", default=DEFAULT_MEDIUM_STRESS)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--summary-json", type=Path, default=None)
    parser.add_argument("--int8-scale", type=float, default=32.0)
    parser.add_argument("--repair-output-npz", type=Path, default=None)
    parser.add_argument("--repair-max-extra", type=int, default=64)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    row = summarize(
        model_path=args.model,
        params_npz=args.params_npz,
        dataset_dir=args.dataset_dir,
        stress_names=parse_csv(args.stress),
        output_dir=args.output_dir,
        summary_json=args.summary_json,
        int8_scale=args.int8_scale,
        repair_output_npz=args.repair_output_npz,
        repair_max_extra=args.repair_max_extra,
    )
    print(json.dumps(row, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
