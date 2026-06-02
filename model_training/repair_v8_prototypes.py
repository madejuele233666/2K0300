import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import (
    build_prototypes,
    metric_summary,
    predict_closed,
    predict_int8,
    quantize,
    write_csv,
)


def parse_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def parse_floats(text: str) -> list[float]:
    return [float(item.strip()) for item in text.split(",") if item.strip()]


def load_training_payload(path: Path) -> tuple[np.ndarray, dict[str, Any], dict[str, np.ndarray]]:
    with np.load(path, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}
    embeddings = np.asarray(payload["embedding_float"], dtype=np.float64)
    flat = {
        "y_parent": np.asarray(payload["parent"], dtype=np.int64),
        "y_sub": np.asarray(payload["subclass"], dtype=np.int64),
        "sample_index": np.asarray(payload["sample_index"], dtype=np.int64),
        "view_labels": np.asarray(payload["view_labels"]).astype(str),
        "paths": np.asarray(payload["paths"]).astype(str),
    }
    view_names = list(dict.fromkeys(flat["view_labels"].tolist()))
    flat["view_names"] = view_names
    return embeddings, flat, payload


def make_row(
    *,
    name: str,
    transform_name: str,
    source: str,
    k_per_subclass: int,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    int8_scale: float,
    repair_extra_count: int,
    repair_iterations: int,
    unresolved_wrong_count: int,
    base_row: dict[str, Any],
) -> dict[str, Any]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    view_order = list(flat["view_names"])
    pred, margin = predict_closed(embeddings, prototypes, prototype_parent)
    int8_pred, int8_margin = predict_int8(quantize(embeddings, int8_scale), quantize(prototypes, int8_scale), prototype_parent)
    row: dict[str, Any] = {
        "name": name,
        "transform": transform_name,
        "prototype_source": f"{source}_repair",
        "base_prototype_source": source,
        "k_per_subclass": int(k_per_subclass),
        "feature_dim": int(embeddings.shape[1]),
        "prototype_count": int(len(prototypes)),
        "base_prototype_count": int(base_row.get("prototype_count", len(prototypes) - repair_extra_count)),
        "repair_extra_count": int(repair_extra_count),
        "repair_iterations": int(repair_iterations),
        "unresolved_wrong_count": int(unresolved_wrong_count),
        "estimated_distance_macs": int(len(prototypes) * embeddings.shape[1]),
        "estimated_float_table_bytes": int(len(prototypes) * embeddings.shape[1] * 4),
        "estimated_int8_table_bytes": int(len(prototypes) * embeddings.shape[1]),
        "margin_min": float(np.min(margin)),
        "margin_mean": float(np.mean(margin)),
        "int8_scale": float(int8_scale),
        "int8_flip_count": int(np.sum(int8_pred != pred)),
        "int8_margin_min": int(np.min(int8_margin)),
        "int8_margin_mean": float(np.mean(int8_margin)),
        "stage": "b_embedding_prototype_repair",
    }
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=pred))
    row.update(metric_summary(view_order=view_order, view_labels=view_labels, y_parent=y_parent, pred=int8_pred, prefix="int8_"))
    for key in [
        "clean_accuracy",
        "rotmirror_min_accuracy",
        "stress_min_accuracy",
        "int8_clean_accuracy",
        "int8_rotmirror_min_accuracy",
        "int8_stress_min_accuracy",
        "margin_min",
        "int8_margin_min",
    ]:
        if key in base_row:
            row[f"base_{key}"] = base_row[key]
    return row


def wrong_indexes(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    prototypes: np.ndarray,
    prototype_parent: np.ndarray,
    int8_scale: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    pred, margin = predict_closed(embeddings, prototypes, prototype_parent)
    int8_pred, int8_margin = predict_int8(quantize(embeddings, int8_scale), quantize(prototypes, int8_scale), prototype_parent)
    wrong = np.where((pred != y_parent) | (int8_pred != y_parent))[0]
    return wrong, pred, margin, int8_pred


def repair_payload(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    base_payload: dict[str, np.ndarray],
    int8_scale: float,
    max_extra_prototypes: int,
    max_iterations: int,
) -> tuple[dict[str, np.ndarray], int, int, list[dict[str, Any]]]:
    prototypes = np.asarray(base_payload["prototypes"], dtype=np.float64).copy()
    prototype_parent = np.asarray(base_payload["prototype_parent"], dtype=np.int64).copy()
    prototype_subclass = np.asarray(base_payload["prototype_subclass"], dtype=np.int64).copy()
    prototype_cluster = np.asarray(base_payload["prototype_cluster"], dtype=np.int64).copy()
    prototype_sample_index = np.asarray(base_payload["prototype_sample_index"], dtype=np.int64).copy()
    prototype_view_label = np.asarray(base_payload["prototype_view_label"]).astype(str).copy()

    y_parent = np.asarray(flat["y_parent"], dtype=np.int64)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"]).astype(str)
    paths = np.asarray(flat["paths"]).astype(str)
    added_indexes: set[int] = set()
    events: list[dict[str, Any]] = []

    iterations = 0
    for iterations in range(1, max_iterations + 1):
        wrong, pred, margin, int8_pred = wrong_indexes(
            embeddings=embeddings,
            flat=flat,
            prototypes=prototypes,
            prototype_parent=prototype_parent,
            int8_scale=int8_scale,
        )
        if len(wrong) == 0 or len(added_indexes) >= max_extra_prototypes:
            break
        new_wrong = [int(index) for index in wrong if int(index) not in added_indexes]
        if not new_wrong:
            break
        new_wrong.sort(key=lambda index: (float(margin[index]), str(view_labels[index]), int(sample_index[index])))
        remaining = max_extra_prototypes - len(added_indexes)
        for index in new_wrong[:remaining]:
            added_indexes.add(index)
            prototypes = np.concatenate([prototypes, embeddings[index : index + 1]], axis=0)
            prototype_parent = np.concatenate([prototype_parent, np.asarray([y_parent[index]], dtype=np.int64)])
            prototype_subclass = np.concatenate([prototype_subclass, np.asarray([y_sub[index]], dtype=np.int64)])
            prototype_cluster = np.concatenate([prototype_cluster, np.asarray([-1000 - len(added_indexes)], dtype=np.int64)])
            prototype_sample_index = np.concatenate([prototype_sample_index, np.asarray([sample_index[index]], dtype=np.int64)])
            prototype_view_label = np.concatenate([prototype_view_label, np.asarray([view_labels[index]])])
            events.append(
                {
                    "event": "added_repair_prototype",
                    "embedding_index": int(index),
                    "sample_index": int(sample_index[index]),
                    "path": str(paths[int(sample_index[index])]) if int(sample_index[index]) < len(paths) else "",
                    "view": str(view_labels[index]),
                    "parent": int(y_parent[index]),
                    "subclass": int(y_sub[index]),
                    "float_pred_before": int(pred[index]),
                    "int8_pred_before": int(int8_pred[index]),
                    "margin_before": float(margin[index]),
                }
            )

    unresolved, pred, margin, int8_pred = wrong_indexes(
        embeddings=embeddings,
        flat=flat,
        prototypes=prototypes,
        prototype_parent=prototype_parent,
        int8_scale=int8_scale,
    )
    for index in unresolved:
        events.append(
            {
                "event": "unresolved_wrong",
                "embedding_index": int(index),
                "sample_index": int(sample_index[index]),
                "path": str(paths[int(sample_index[index])]) if int(sample_index[index]) < len(paths) else "",
                "view": str(view_labels[index]),
                "parent": int(y_parent[index]),
                "subclass": int(y_sub[index]),
                "float_pred_after": int(pred[index]),
                "int8_pred_after": int(int8_pred[index]),
                "margin_after": float(margin[index]),
            }
        )

    repaired_payload = {
        **base_payload,
        "prototypes": prototypes.astype(np.float32),
        "prototype_parent": prototype_parent.astype(np.int64),
        "prototype_subclass": prototype_subclass.astype(np.int64),
        "prototype_cluster": prototype_cluster.astype(np.int64),
        "prototype_sample_index": prototype_sample_index.astype(np.int64),
        "prototype_view_label": prototype_view_label.astype(str),
        "embedding_int8": quantize(embeddings, int8_scale),
        "prototypes_int8": quantize(prototypes, int8_scale),
    }
    return repaired_payload, len(added_indexes), int(len(unresolved)), events


def repair_score(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        bool(row["clean_all_correct"]),
        bool(row["rotmirror_all_correct"]),
        bool(row["stress_all_correct"]),
        bool(row["int8_clean_all_correct"]),
        bool(row["int8_rotmirror_all_correct"]),
        bool(row["int8_stress_all_correct"]),
        -int(row["unresolved_wrong_count"]),
        int(row["clean_correct"]),
        float(row["rotmirror_min_accuracy"]),
        float(row["stress_min_accuracy"]),
        float(row["fixed_stress_min_accuracy"]),
        int(row["int8_clean_correct"]),
        float(row["int8_rotmirror_min_accuracy"]),
        float(row["int8_stress_min_accuracy"]),
        float(row["int8_fixed_stress_min_accuracy"]),
        -int(row["prototype_count"]),
        float(row["margin_min"]),
        int(row["int8_margin_min"]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Repair V8 embedding prototype tables by adding exact failing full-dataset views.")
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--prototype-sources", default="kmeans,medoid")
    parser.add_argument("--k-values", default="32,48,64,96,128")
    parser.add_argument("--quant-scales", default="32,48,64,96,128")
    parser.add_argument("--max-extra-prototypes", type=int, default=240)
    parser.add_argument("--max-iterations", type=int, default=12)
    parser.add_argument("--seed", type=int, default=20260520)
    parser.add_argument("--existing-best-only", action="store_true")
    args = parser.parse_args()

    params_path = args.run_dir / "best_v8_embedding_prototype_params.npz"
    if not params_path.exists():
        raise FileNotFoundError(params_path)
    embeddings, flat, loaded_payload = load_training_payload(params_path)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    train_config = args.run_dir / "train_config.json"
    if train_config.exists():
        shutil.copyfile(train_config, args.output_dir / "train_config.json")

    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    all_events: list[dict[str, Any]] = []

    candidate_specs: list[tuple[str, int, dict[str, np.ndarray]]] = []
    if args.existing_best_only:
        summary_path = args.run_dir / "summary.json"
        best = json.loads(summary_path.read_text(encoding="utf-8")).get("best", {}) if summary_path.exists() else {}
        source = str(best.get("prototype_source", "existing"))
        k_value = int(best.get("k_per_subclass", -1))
        base_payload = {
            "embedding_float": embeddings.astype(np.float32),
            "parent": np.asarray(flat["y_parent"], dtype=np.int64),
            "subclass": np.asarray(flat["y_sub"], dtype=np.int64),
            "sample_index": np.asarray(flat["sample_index"], dtype=np.int64),
            "view_labels": np.asarray(flat["view_labels"]).astype(str),
            "paths": np.asarray(flat["paths"]).astype(str),
            "prototypes": np.asarray(loaded_payload["prototypes"], dtype=np.float64),
            "prototype_parent": np.asarray(loaded_payload["prototype_parent"], dtype=np.int64),
            "prototype_subclass": np.asarray(loaded_payload["prototype_subclass"], dtype=np.int64),
            "prototype_cluster": np.asarray(loaded_payload["prototype_cluster"], dtype=np.int64),
            "prototype_sample_index": np.asarray(loaded_payload["prototype_sample_index"], dtype=np.int64),
            "prototype_view_label": np.asarray(loaded_payload["prototype_view_label"]).astype(str),
        }
        candidate_specs.append((source, k_value, base_payload))
    else:
        for source in parse_csv(args.prototype_sources):
            for k_value in parse_ints(args.k_values):
                base_proto = build_prototypes(
                    embeddings=embeddings,
                    y_parent=np.asarray(flat["y_parent"], dtype=np.int64),
                    y_sub=np.asarray(flat["y_sub"], dtype=np.int64),
                    sample_index=np.asarray(flat["sample_index"], dtype=np.int64),
                    view_labels=np.asarray(flat["view_labels"]).astype(str),
                    source=source,
                    k_per_subclass=k_value,
                    seed=args.seed,
                )
                base_payload = {
                    "embedding_float": embeddings.astype(np.float32),
                    "parent": np.asarray(flat["y_parent"], dtype=np.int64),
                    "subclass": np.asarray(flat["y_sub"], dtype=np.int64),
                    "sample_index": np.asarray(flat["sample_index"], dtype=np.int64),
                    "view_labels": np.asarray(flat["view_labels"]).astype(str),
                    "paths": np.asarray(flat["paths"]).astype(str),
                    **base_proto,
                }
                candidate_specs.append((source, k_value, base_payload))

    for source, k_value, base_payload in candidate_specs:
        for scale in parse_floats(args.quant_scales):
            base_row = make_row(
                name="v8_embedding_prototype_repair_base",
                transform_name="learned_embedding_raw_repair",
                source=source,
                k_per_subclass=k_value,
                embeddings=embeddings,
                flat=flat,
                prototypes=np.asarray(base_payload["prototypes"], dtype=np.float64),
                prototype_parent=np.asarray(base_payload["prototype_parent"], dtype=np.int64),
                int8_scale=scale,
                repair_extra_count=0,
                repair_iterations=0,
                unresolved_wrong_count=0,
                base_row={},
            )
            repaired_payload, extra_count, unresolved_count, events = repair_payload(
                embeddings=embeddings,
                flat=flat,
                base_payload=base_payload,
                int8_scale=scale,
                max_extra_prototypes=args.max_extra_prototypes,
                max_iterations=args.max_iterations,
            )
            row = make_row(
                name="v8_embedding_prototype_repair",
                transform_name="learned_embedding_raw_repair",
                source=source,
                k_per_subclass=k_value,
                embeddings=embeddings,
                flat=flat,
                prototypes=np.asarray(repaired_payload["prototypes"], dtype=np.float64),
                prototype_parent=np.asarray(repaired_payload["prototype_parent"], dtype=np.int64),
                int8_scale=scale,
                repair_extra_count=extra_count,
                repair_iterations=args.max_iterations,
                unresolved_wrong_count=unresolved_count,
                base_row=base_row,
            )
            rows.append(row)
            payloads.append(repaired_payload)
            for event in events:
                event.update(
                    {
                        "prototype_source": source,
                        "k_per_subclass": k_value,
                        "int8_scale": scale,
                        "repair_extra_count": extra_count,
                        "unresolved_wrong_count": unresolved_count,
                    }
                )
                all_events.append(event)

    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: repair_score(item[0]), reverse=True)
    rows_sorted = [row for row, _payload in sorted_pairs]
    write_csv(args.output_dir / "candidate_results.csv", rows_sorted)
    write_csv(args.output_dir / "repair_events.csv", all_events)
    if rows_sorted:
        best_payload = sorted_pairs[0][1]
        np.savez_compressed(args.output_dir / "best_v8_repaired_prototype_params.npz", **best_payload)
        write_csv(args.output_dir / "best_stress_summary.csv", json.loads(str(rows_sorted[0]["per_view_json"])))
    summary = {
        "source_run_dir": str(args.run_dir),
        "candidate_count": len(rows_sorted),
        "best": rows_sorted[0] if rows_sorted else None,
        "top20": rows_sorted[:20],
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"best": rows_sorted[0] if rows_sorted else None, "candidate_count": len(rows_sorted)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
