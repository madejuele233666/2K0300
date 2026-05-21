import argparse
import json
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np

from evaluate_v8_embedding_prototypes import (
    ROT_MIRROR_VIEWS,
    evaluate_prototype_candidate,
    flatten_cache,
    load_feature_cache,
    parse_csv,
    parse_floats,
    parse_ints,
    row_score,
    run_transform_sweep,
    write_csv,
    zfit,
)


def load_tensorflow():
    import tensorflow as tf

    return tf


def tf_l2_normalize(tf: Any, values: Any) -> Any:
    return values / tf.maximum(tf.norm(values, axis=1, keepdims=True), 1.0e-8)


def tf_l2_normalize_orbit(tf: Any, values: Any) -> Any:
    return values / tf.maximum(tf.norm(values, axis=2, keepdims=True), 1.0e-8)


def off_diagonal(tf: Any, matrix: Any) -> Any:
    size = tf.shape(matrix)[0]
    return tf.reshape(matrix - tf.linalg.diag(tf.linalg.diag_part(matrix)), (size * size,))


def orbit_indexes(flat: dict[str, Any]) -> np.ndarray:
    view_labels = np.asarray(flat["view_labels"])
    sample_index = np.asarray(flat["sample_index"], dtype=np.int64)
    orbit_views = ["clean"] + [view for view in ROT_MIRROR_VIEWS if view in set(flat["view_names"])]
    n = int(np.max(sample_index)) + 1
    rows: list[np.ndarray] = []
    for view in orbit_views:
        indexes = np.where(view_labels == view)[0]
        order = np.argsort(sample_index[indexes])
        rows.append(indexes[order])
    stacked = np.stack(rows)
    if stacked.shape[1] != n:
        raise ValueError(f"unexpected orbit index shape: {stacked.shape}, n={n}")
    return stacked


def train_projection(
    *,
    flat: dict[str, Any],
    embedding_dim: int,
    seed: int,
    epochs: int,
    learning_rate: float,
    proxy_scale: float,
    lambda_d4: float,
    lambda_var: float,
    lambda_cov: float,
    variance_floor: float,
    log_path: Path,
) -> dict[str, np.ndarray]:
    tf = load_tensorflow()
    tf.keras.utils.set_random_seed(seed)
    raw = np.asarray(flat["old_gap"], dtype=np.float32)
    z, mean, std = zfit(raw.astype(np.float64))
    features = z.astype(np.float32)
    y_sub = np.asarray(flat["y_sub"], dtype=np.int64)
    view_labels = np.asarray(flat["view_labels"])
    train_mask = (view_labels == "clean") | np.isin(view_labels, ROT_MIRROR_VIEWS)
    x_train = tf.constant(features[train_mask], dtype=tf.float32)
    y_train = tf.constant(y_sub[train_mask], dtype=tf.int32)
    x_all = tf.constant(features, dtype=tf.float32)
    orbit = orbit_indexes(flat)
    x_orbit = tf.constant(features[orbit], dtype=tf.float32)

    input_dim = features.shape[1]
    init = tf.keras.initializers.Orthogonal(seed=seed)
    projection = tf.Variable(init(shape=(input_dim, embedding_dim), dtype=tf.float32), name="v8_projection")
    proxies = tf.Variable(tf.random.normal((8, embedding_dim), seed=seed + 17), name="v8_subclass_proxies")
    optimizer = tf.keras.optimizers.Adam(learning_rate=learning_rate)
    ce = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True)

    log_rows: list[dict[str, Any]] = []
    for epoch in range(1, epochs + 1):
        with tf.GradientTape() as tape:
            z_train = tf_l2_normalize(tf, tf.matmul(x_train, projection))
            proxy_norm = tf_l2_normalize(tf, proxies)
            logits = proxy_scale * tf.matmul(z_train, proxy_norm, transpose_b=True)
            proxy_loss = ce(y_train, logits)

            z_orbit = tf_l2_normalize_orbit(tf, tf.tensordot(x_orbit, projection, axes=1))
            orbit_center = tf.stop_gradient(tf.reduce_mean(z_orbit, axis=0, keepdims=True))
            d4_loss = tf.reduce_mean(tf.reduce_sum(tf.square(z_orbit - orbit_center), axis=2))

            z_all = tf_l2_normalize(tf, tf.matmul(x_all, projection))
            stddev = tf.sqrt(tf.math.reduce_variance(z_all, axis=0) + 1.0e-6)
            var_loss = tf.reduce_mean(tf.square(tf.nn.relu(variance_floor - stddev)))
            centered = z_all - tf.reduce_mean(z_all, axis=0, keepdims=True)
            cov = tf.matmul(centered, centered, transpose_a=True) / tf.cast(tf.shape(centered)[0] - 1, tf.float32)
            cov_loss = tf.reduce_sum(tf.square(off_diagonal(tf, cov))) / tf.cast(embedding_dim, tf.float32)
            loss = proxy_loss + lambda_d4 * d4_loss + lambda_var * var_loss + lambda_cov * cov_loss
        grads = tape.gradient(loss, [projection, proxies])
        optimizer.apply_gradients(zip(grads, [projection, proxies]))
        if epoch == 1 or epoch == epochs or epoch % max(1, epochs // 20) == 0:
            pred = tf.argmax(logits, axis=1, output_type=tf.int32)
            train_acc = tf.reduce_mean(tf.cast(pred == y_train, tf.float32))
            row = {
                "epoch": epoch,
                "loss": float(loss.numpy()),
                "proxy_loss": float(proxy_loss.numpy()),
                "d4_loss": float(d4_loss.numpy()),
                "var_loss": float(var_loss.numpy()),
                "cov_loss": float(cov_loss.numpy()),
                "train_subclass_accuracy": float(train_acc.numpy()),
            }
            log_rows.append(row)
            print(json.dumps({"a1_train": {"dim": embedding_dim, "seed": seed, **row}}, ensure_ascii=False), flush=True)

    log_path.parent.mkdir(parents=True, exist_ok=True)
    write_csv(log_path, log_rows)
    projection_np = projection.numpy().astype(np.float32)
    proxies_np = proxies.numpy().astype(np.float32)
    embeddings = features @ projection_np
    embeddings = embeddings / np.maximum(np.linalg.norm(embeddings, axis=1, keepdims=True), 1.0e-8)
    return {
        "feature_mean": mean.astype(np.float32),
        "feature_std": std.astype(np.float32),
        "projection": projection_np,
        "subclass_proxies": proxies_np,
        "embedding_float": embeddings.astype(np.float32),
    }


def run_embedding_sweep(
    *,
    embeddings: np.ndarray,
    flat: dict[str, Any],
    output_dir: Path,
    transform_name: str,
    prototype_sources: list[str],
    k_values: list[int],
    quant_scales: list[float],
    seed: int,
    extra_payload: dict[str, np.ndarray],
) -> list[dict[str, Any]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    payloads: list[dict[str, np.ndarray]] = []
    for source in prototype_sources:
        for k_value in k_values:
            row, payload = evaluate_prototype_candidate(
                embeddings=embeddings,
                flat=flat,
                transform_name=transform_name,
                source=source,
                k_per_subclass=k_value,
                seed=seed,
                quant_scales=quant_scales,
            )
            row["stage"] = "a1_frozen_gap_projection"
            rows.append(row)
            payload.update(extra_payload)
            payload["transform_kind"] = np.asarray(transform_name)
            payloads.append(payload)
    sorted_pairs = sorted(zip(rows, payloads), key=lambda item: row_score(item[0]), reverse=True)
    rows_sorted = [row for row, _payload in sorted_pairs]
    best_payload = sorted_pairs[0][1] if sorted_pairs else {}
    write_csv(output_dir / "candidate_results.csv", rows_sorted)
    if rows_sorted:
        np.savez_compressed(output_dir / "best_v8_embedding_prototype_params.npz", **best_payload)
        write_csv(output_dir / "best_stress_summary.csv", json.loads(str(rows_sorted[0]["per_view_json"])))
    summary = {
        "stage": "a1_frozen_gap_projection",
        "transform": transform_name,
        "candidate_count": len(rows_sorted),
        "best": rows_sorted[0] if rows_sorted else None,
        "top20": rows_sorted[:20],
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    return rows_sorted


def summarize_cache(cache: dict[str, np.ndarray], output_dir: Path, feature_cache: Path) -> None:
    summary = {
        "feature_cache": str(feature_cache),
        "view_names": [str(item) for item in np.asarray(cache["view_names"]).tolist()],
        "sample_count": int(len(cache["y_parent"])),
        "gap_dim": int(np.asarray(cache["old_gap"]).shape[-1]),
        "parent_counts": {str(parent): int(np.sum(np.asarray(cache["y_parent"], dtype=np.int64) == parent)) for parent in range(3)},
        "subclass_counts": {str(sub): int(np.sum(np.asarray(cache["y_sub"], dtype=np.int64) == sub)) for sub in range(8)},
    }
    (output_dir / "feature_cache_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Train V8 Phase A pure embedding prototypes over frozen fast GAP features.")
    parser.add_argument("--feature-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--phase", choices=["a0", "a1", "phaseA"], default="phaseA")
    parser.add_argument("--embedding-dims", default="16,24,32")
    parser.add_argument("--seeds", default="20260519")
    parser.add_argument("--epochs", type=int, default=600)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--proxy-scale", type=float, default=16.0)
    parser.add_argument("--lambda-d4", type=float, default=1.0)
    parser.add_argument("--lambda-var", type=float, default=0.25)
    parser.add_argument("--lambda-cov", type=float, default=0.02)
    parser.add_argument("--variance-floor", type=float, default=0.08)
    parser.add_argument("--prototype-sources", default="medoid,kmeans")
    parser.add_argument("--k-values", default="1,2,4,8,16")
    parser.add_argument("--quant-scales", default="8,12,16,24,32,48,64,96,128")
    parser.add_argument("--a0-transforms", default="")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    cache = load_feature_cache(args.feature_cache)
    flat = flatten_cache(cache)
    summarize_cache(cache, args.output_dir, args.feature_cache)

    prototype_sources = parse_csv(args.prototype_sources)
    k_values = parse_ints(args.k_values)
    quant_scales = parse_floats(args.quant_scales)
    all_rows: list[dict[str, Any]] = []

    if args.phase in {"a0", "phaseA"}:
        a0_dir = args.output_dir / "a0_no_training_compression"
        feature_dim = int(np.asarray(flat["old_gap"]).shape[1])
        if args.a0_transforms:
            transforms = parse_csv(args.a0_transforms)
        else:
            from evaluate_v8_embedding_prototypes import default_transform_specs

            transforms = default_transform_specs(feature_dim)
        a0_rows, _payload = run_transform_sweep(
            flat=flat,
            output_dir=a0_dir,
            transform_specs=transforms,
            prototype_sources=prototype_sources,
            k_values=k_values,
            quant_scales=quant_scales,
            seed=parse_ints(args.seeds)[0],
            name="a0_no_training_compression",
        )
        all_rows.extend(a0_rows)

    if args.phase in {"a1", "phaseA"}:
        for seed in parse_ints(args.seeds):
            for dim in parse_ints(args.embedding_dims):
                name = f"a1_projection_dim{dim}_seed{seed}"
                stage_dir = args.output_dir / name
                trained = train_projection(
                    flat=flat,
                    embedding_dim=dim,
                    seed=seed,
                    epochs=args.epochs,
                    learning_rate=args.learning_rate,
                    proxy_scale=args.proxy_scale,
                    lambda_d4=args.lambda_d4,
                    lambda_var=args.lambda_var,
                    lambda_cov=args.lambda_cov,
                    variance_floor=args.variance_floor,
                    log_path=stage_dir / "training_log.csv",
                )
                np.savez_compressed(stage_dir / "projection_model.npz", **trained)
                rows = run_embedding_sweep(
                    embeddings=trained["embedding_float"],
                    flat=flat,
                    output_dir=stage_dir,
                    transform_name=name,
                    prototype_sources=prototype_sources,
                    k_values=k_values,
                    quant_scales=quant_scales,
                    seed=seed,
                    extra_payload=trained,
                )
                all_rows.extend(rows)

    all_rows_sorted = sorted(all_rows, key=row_score, reverse=True)
    write_csv(args.output_dir / "phaseA_candidate_results.csv", all_rows_sorted)
    summary = {
        "feature_cache": str(args.feature_cache),
        "phase": args.phase,
        "candidate_count": len(all_rows_sorted),
        "best": all_rows_sorted[0] if all_rows_sorted else None,
        "top40": all_rows_sorted[:40],
        "config": {
            "embedding_dims": parse_ints(args.embedding_dims),
            "seeds": parse_ints(args.seeds),
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "lambda_d4": args.lambda_d4,
            "lambda_var": args.lambda_var,
            "lambda_cov": args.lambda_cov,
            "variance_floor": args.variance_floor,
            "prototype_sources": prototype_sources,
            "k_values": k_values,
            "quant_scales": quant_scales,
        },
    }
    (args.output_dir / "phaseA_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"best": summary["best"], "candidate_count": len(all_rows_sorted)}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
