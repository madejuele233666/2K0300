import argparse
import os

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_XLA_FLAGS", "--tf_xla_auto_jit=0")
os.environ.setdefault("TF_GPU_ALLOCATOR", "cuda_malloc_async")
os.environ.setdefault("CUDA_CACHE_MAXSIZE", "2147483648")

import numpy as np
import tensorflow as tf

from train_tiny32_sixclass_scan import IMAGE_SIZE, build_model
from train_tiny32_v3_fast_topk_neighborhood import ANCHORS, neighborhood_configs


def structural_key(config):
    return (
        tuple(config.filters),
        config.dense_units,
        config.pool,
        config.first_kernel,
        config.extra_conv,
        config.dropout > 0,
        config.batch_size,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Warm CUDA/PTX cache for v3 fast-neighborhood scans.")
    parser.add_argument("--anchors", default="rank02_best,rank03_speed,rank01_low_l2")
    parser.add_argument("--max-trials", type=int, default=64)
    parser.add_argument("--seed", type=int, default=20260520)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    tf.config.optimizer.set_jit(False)
    tf.config.threading.set_inter_op_parallelism_threads(2)
    tf.config.threading.set_intra_op_parallelism_threads(4)

    rng = np.random.default_rng(args.seed)
    configs = []
    seen = set()
    for offset, anchor in enumerate([item.strip() for item in args.anchors.split(",") if item.strip()]):
        if anchor not in ANCHORS:
            raise ValueError(f"unknown anchor: {anchor}")
        for config in neighborhood_configs(anchor, args.max_trials, args.seed + offset):
            key = structural_key(config)
            if key in seen:
                continue
            seen.add(key)
            configs.append(config)
    if args.limit > 0:
        configs = configs[: args.limit]

    print(f"warmup_structures={len(configs)} anchors={args.anchors}", flush=True)
    for index, config in enumerate(configs, start=1):
        tf.keras.backend.clear_session()
        tf.keras.utils.set_random_seed(args.seed + index)
        model = build_model(config)
        batch = min(config.batch_size, 16)
        x = rng.random((batch, IMAGE_SIZE, IMAGE_SIZE, 1), dtype=np.float32)
        y = rng.integers(0, 6, size=(batch,), dtype=np.int64)
        model.train_on_batch(x, y)
        model.predict(x[: min(batch, 4)], batch_size=min(batch, 4), verbose=0)
        print(
            f"[{index:03d}/{len(configs):03d}] filters={config.filters} dense={config.dense_units} "
            f"pool={config.pool} extra={int(config.extra_conv)} dropout={config.dropout:g} "
            f"batch={config.batch_size}",
            flush=True,
        )
    print("warmup_done", flush=True)


if __name__ == "__main__":
    main()
