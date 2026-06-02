import argparse
import copy
import csv
import json
import math
from dataclasses import replace
from pathlib import Path

import numpy as np

from train_tiny32_v4_mild_calib_scan import (
    V4_STRESS,
    config_to_dict,
    dict_to_model_config,
    estimate_board_latency_us,
    final_score,
    final_train_and_export_mild,
    load_dataset,
    make_config,
    safe_name,
    semantic_key,
)
from train_tiny32_sixclass_scan import AugmentConfig, ModelConfig


DEFAULT_SUMMARIES = [
    Path("experiments/v4_fine_fast_focus_20260508_070200/search_summary.json"),
    Path("experiments/v4_fine_shard1_fast_20260508_064600/search_summary.json"),
    Path("experiments/v4_fine_shard2_accuracy_20260508_064600/search_summary.json"),
    Path("experiments/v4_fine_shard0_best_20260508_064600/search_summary.json"),
    Path("experiments/v4_fine_acc_focus_20260508_070200/search_summary.json"),
    Path("experiments/v4_coarse_accuracy_20260508_012154/search_summary.json"),
    Path("experiments/v4_coarse_arch_20260508_012154/search_summary.json"),
    Path("experiments/v4_coarse_fast_20260508_012154/search_summary.json"),
    Path("experiments/v4_coarse_core_20260508_012154/search_summary.json"),
]


def metric_summary(final: dict[str, object]) -> dict[str, float]:
    parent = final["int8_test"]["parent"]
    all_parent = final["int8_all"]["parent"]
    sub = final["int8_test"]["subclass"]
    stress_items = []
    for item in final.get("int8_stress", {}).values():
        if isinstance(item, dict):
            stress_items.append(item.get("parent", item))
    stress_worst = [float(item["worst_recall"]) for item in stress_items if "worst_recall" in item]
    stress_macro = [float(item["macro_recall"]) for item in stress_items if "macro_recall" in item]
    return {
        "test_acc": float(parent["accuracy"]),
        "test_worst": float(parent["worst_recall"]),
        "test_macro": float(parent["macro_recall"]),
        "sub_acc": float(sub["accuracy"]),
        "sub_worst": float(sub["worst_recall"]),
        "all_acc": float(all_parent["accuracy"]),
        "all_worst": float(all_parent["worst_recall"]),
        "all_macro": float(all_parent["macro_recall"]),
        "stress_min_worst": min(stress_worst) if stress_worst else float(parent["worst_recall"]),
        "stress_mean_worst": float(np.mean(stress_worst)) if stress_worst else float(parent["worst_recall"]),
        "stress_mean_macro": float(np.mean(stress_macro)) if stress_macro else float(parent["macro_recall"]),
        "agreement_test": float(final.get("keras_vs_int8_test_agreement", 0.0)),
        "agreement_all": float(final.get("keras_vs_int8_all_agreement", 0.0)),
        "int8_bytes": float(final["export"]["int8_bytes"]),
    }


def load_export_rows(summary_paths: list[Path]) -> list[dict[str, object]]:
    rows = []
    for path in summary_paths:
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        for export in data.get("final_exports", []):
            config = export.get("search", {}).get("config")
            if not isinstance(config, dict):
                continue
            final = export["final"]
            metrics = metric_summary(final)
            rows.append(
                {
                    "scan": path.parent.name,
                    "rank": int(export.get("rank", 0)),
                    "trial": str(export.get("trial", "")),
                    "previous_score": float(export.get("final_score", 0.0)),
                    "config": config,
                    "metrics": metrics,
                    "path": final["export"]["int8_path"],
                }
            )
    return rows


def relabel_config(config, name: str):
    return replace(config, name=name)


def add_candidate(candidates: list[dict[str, object]], seen: set[tuple[object, ...]], label: str, row: dict[str, object]) -> None:
    config = relabel_config(dict_to_model_config(row["config"], label), label)
    key = semantic_key(config)
    if key in seen:
        return
    seen.add(key)
    candidates.append({"label": label, "config": config, "source": row})


def best_row(rows: list[dict[str, object]], predicate, key):
    filtered = [row for row in rows if predicate(row)]
    if not filtered:
        return None
    return max(filtered, key=key)


def select_candidates(rows: list[dict[str, object]], limit: int) -> list[dict[str, object]]:
    candidates: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()

    def add_best(label: str, predicate, key) -> None:
        row = best_row(rows, predicate, key)
        if row is not None:
            add_candidate(candidates, seen, label, row)

    add_best("best_overall_fast16k", lambda row: True, lambda row: row["previous_score"])
    add_best(
        "best_all_stable",
        lambda row: True,
        lambda row: (row["metrics"]["all_worst"], row["metrics"]["all_acc"], row["previous_score"]),
    )
    add_best(
        "best_stress_stable",
        lambda row: True,
        lambda row: (row["metrics"]["stress_min_worst"], row["metrics"]["stress_mean_worst"], row["previous_score"]),
    )
    add_best(
        "fast_x0_lr286",
        lambda row: (not row["config"].get("extra_conv")) and math.isclose(float(row["config"].get("learning_rate", 0.0)), 0.00286, rel_tol=0.0, abs_tol=1.0e-8),
        lambda row: row["previous_score"],
    )
    add_best(
        "fast_x0_lowres_mix",
        lambda row: (not row["config"].get("extra_conv")) and row["config"].get("augment", {}).get("name") == "v4_lowres_mix",
        lambda row: row["previous_score"],
    )
    add_best(
        "fast_x0_dropout_mix",
        lambda row: (not row["config"].get("extra_conv"))
        and row["config"].get("augment", {}).get("name") == "v4_lowres_mix"
        and float(row["config"].get("dropout", 0.0)) > 0.0,
        lambda row: row["previous_score"],
    )
    add_best(
        "extra_conv_best",
        lambda row: bool(row["config"].get("extra_conv")),
        lambda row: row["previous_score"],
    )
    add_best(
        "extra_conv_all_stable",
        lambda row: bool(row["config"].get("extra_conv")),
        lambda row: (row["metrics"]["all_worst"], row["metrics"]["all_acc"], row["previous_score"]),
    )
    add_best(
        "tiny_13k",
        lambda row: row["metrics"]["int8_bytes"] <= 14000,
        lambda row: row["previous_score"],
    )
    add_best(
        "wide_10_20_40_best",
        lambda row: tuple(row["config"].get("filters", [])) == (10, 20, 40),
        lambda row: row["previous_score"],
    )
    add_best(
        "wide_10_20_40_all",
        lambda row: tuple(row["config"].get("filters", [])) == (10, 20, 40),
        lambda row: (row["metrics"]["all_worst"], row["metrics"]["all_acc"], row["previous_score"]),
    )
    add_best(
        "low_l2_speed_noise",
        lambda row: row["config"].get("augment", {}).get("name") == "v3_speed_noise",
        lambda row: row["previous_score"],
    )

    old_anchor = make_config(
        "retest",
        "old_v3_anchor",
        "spacetodepth_conv",
        (8, 16, 32),
        0,
        0.0,
        3.0e-4,
        0.0020,
        32,
        "max",
        3,
        True,
        "v3_lowres_noise",
        "relu",
    )
    key = semantic_key(old_anchor)
    if key not in seen:
        seen.add(key)
        candidates.append({"label": "old_v3_anchor", "config": relabel_config(old_anchor, "old_v3_anchor"), "source": {"trial": "manual_old_v3_anchor"}})

    return candidates[:limit]


def config_from_json(data: dict[str, object], name: str) -> ModelConfig:
    augment_data = data["augment"]
    if isinstance(augment_data, dict):
        augment = AugmentConfig(
            name=str(augment_data["name"]),
            pad=int(augment_data["pad"]),
            brightness=float(augment_data["brightness"]),
            contrast_delta=float(augment_data["contrast_delta"]),
            noise_std=float(augment_data["noise_std"]),
            salt_pepper=float(augment_data["salt_pepper"]),
            blur_prob=float(augment_data["blur_prob"]),
            blur_kernel=int(augment_data["blur_kernel"]),
            downscale_prob=float(augment_data["downscale_prob"]),
            downscale_min=int(augment_data["downscale_min"]),
        )
    else:
        return dict_to_model_config(data, name)
    return ModelConfig(
        name=name,
        filters=tuple(int(value) for value in data["filters"]),
        dense_units=int(data["dense_units"]),
        dropout=float(data["dropout"]),
        l2=float(data["l2"]),
        learning_rate=float(data["learning_rate"]),
        batch_size=int(data["batch_size"]),
        pool=str(data["pool"]),
        first_kernel=int(data["first_kernel"]),
        extra_conv=bool(data["extra_conv"]),
        augment=augment,
        architecture=str(data.get("architecture", "spacetodepth_conv")),
        activation=str(data.get("activation", "relu")),
        train_transforms=str(data.get("train_transforms", "rot_mirror")),
    )


def load_candidates_json(path: Path, limit: int) -> list[dict[str, object]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    loaded = []
    seen: set[tuple[object, ...]] = set()
    for index, item in enumerate(data.get("candidates", [])):
        label = str(item.get("label") or f"candidate_{index:03d}")
        config = config_from_json(item["config"], label)
        key = semantic_key(config)
        if key in seen:
            continue
        seen.add(key)
        loaded.append({"label": label, "config": config, "source": item.get("source", {})})
        if len(loaded) >= limit:
            break
    return loaded


def load_existing(path: Path) -> dict[str, dict[str, object]]:
    if not path.exists():
        return {}
    existing = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        existing[str(item.get("run_id"))] = item
    return existing


def flatten_result(item: dict[str, object]) -> dict[str, object]:
    final = item["final"]
    metrics = metric_summary(final)
    return {
        "run_id": item["run_id"],
        "label": item["label"],
        "seed": item["seed"],
        "score": item["score"],
        "estimated_board_us": item["estimated_board_us"],
        "int8_path": final["export"]["int8_path"],
        **metrics,
    }


def summarize(results: list[dict[str, object]], output_dir: Path) -> None:
    by_label: dict[str, list[dict[str, object]]] = {}
    for result in results:
        by_label.setdefault(str(result["label"]), []).append(flatten_result(result))

    summary_rows = []
    for label, items in sorted(by_label.items()):
        row: dict[str, object] = {"label": label, "runs": len(items)}
        for key in [
            "score",
            "test_acc",
            "test_worst",
            "all_acc",
            "all_worst",
            "stress_min_worst",
            "stress_mean_worst",
            "agreement_all",
        ]:
            values = [float(item[key]) for item in items]
            row[f"{key}_mean"] = float(np.mean(values))
            row[f"{key}_min"] = float(np.min(values))
            row[f"{key}_std"] = float(np.std(values))
        row["estimated_board_us"] = int(items[0]["estimated_board_us"])
        row["int8_bytes_mean"] = float(np.mean([float(item["int8_bytes"]) for item in items]))
        row["best_run_id"] = max(items, key=lambda item: float(item["score"]))["run_id"]
        summary_rows.append(row)

    summary_rows.sort(
        key=lambda row: (
            float(row["score_mean"]),
            float(row["test_worst_min"]),
            float(row["all_worst_mean"]),
            -float(row["estimated_board_us"]),
        ),
        reverse=True,
    )
    (output_dir / "retest_summary.json").write_text(
        json.dumps({"summary": summary_rows, "results": results}, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    if summary_rows:
        with (output_dir / "retest_summary.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(summary_rows[0].keys()))
            writer.writeheader()
            writer.writerows(summary_rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Retest likely-good V4 tiny32 candidates across seeds.")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--summary", action="append", type=Path, default=[])
    parser.add_argument("--candidates-json", type=Path)
    parser.add_argument("--candidate-limit", type=int, default=12)
    parser.add_argument("--seeds", default="20260801,20260802,20260803,20260804,20260805")
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--final-epochs", type=int, default=300)
    parser.add_argument("--final-patience", type=int, default=38)
    parser.add_argument("--full-epochs", type=int, default=0)
    parser.add_argument("--with-full", action="store_true")
    parser.add_argument("--final-stress", default="noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10")
    parser.add_argument("--calibration-limit", type=int, default=192)
    parser.add_argument("--speed-weight", type=float, default=0.14)
    args = parser.parse_args()

    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        raise ValueError("invalid shard arguments")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    source_paths = args.summary or DEFAULT_SUMMARIES
    if args.candidates_json:
        rows = []
        candidates = load_candidates_json(args.candidates_json, args.candidate_limit)
    else:
        rows = load_export_rows(source_paths)
        candidates = select_candidates(rows, args.candidate_limit)
    seeds = [int(seed.strip()) for seed in args.seeds.split(",") if seed.strip()]
    jobs = [(candidate, seed) for candidate in candidates for seed in seeds]
    jobs = [job for index, job in enumerate(jobs) if index % args.shard_count == args.shard_index]

    x, y_sub, y_parent, paths = load_dataset(Path(args.dataset_dir))
    run_config = {
        "source_summaries": [str(path) for path in source_paths],
        "candidate_count": len(candidates),
        "seeds": seeds,
        "shard_index": args.shard_index,
        "shard_count": args.shard_count,
        "jobs": len(jobs),
        "sample_count": len(y_sub),
        "paths": paths,
        "candidates": [
            {
                "label": item["label"],
                "config": config_to_dict(item["config"]),
                "source": item.get("source", {}),
                "estimated_board_us": estimate_board_latency_us(item["config"]),
            }
            for item in candidates
        ],
    }
    (output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2, ensure_ascii=False), encoding="utf-8")

    results_path = output_dir / "retest_results.jsonl"
    existing = load_existing(results_path) if args.resume else {}
    print(
        f"output_dir={output_dir} candidates={len(candidates)} jobs={len(jobs)} "
        f"completed={len(existing)} shard={args.shard_index}/{args.shard_count}",
        flush=True,
    )

    all_results = list(existing.values())
    for job_index, (candidate, seed) in enumerate(jobs, start=1):
        label = str(candidate["label"])
        config = candidate["config"]
        run_id = f"{label}_seed{seed}"
        if run_id in existing:
            print(f"[{job_index:03d}/{len(jobs):03d}] {run_id} skipped_existing", flush=True)
            continue
        run_dir = output_dir / "runs" / safe_name(run_id)
        run_dir.mkdir(parents=True, exist_ok=True)
        run_args = copy.copy(args)
        run_args.seed = seed
        run_args.skip_full = not args.with_full
        if args.with_full and args.full_epochs <= 0:
            run_args.full_epochs = 0
        final = final_train_and_export_mild(config, x, y_sub, y_parent, run_args, run_dir)
        score = final_score(final, estimate_board_latency_us(config), args.speed_weight)
        result = {
            "run_id": run_id,
            "label": label,
            "seed": seed,
            "score": score,
            "estimated_board_us": estimate_board_latency_us(config),
            "config": config_to_dict(config),
            "source": candidate.get("source", {}),
            "final": final,
        }
        with results_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(result, ensure_ascii=False) + "\n")
        all_results.append(result)
        metrics = metric_summary(final)
        print(
            f"[{job_index:03d}/{len(jobs):03d}] {run_id} score={score:.4f} "
            f"test={metrics['test_acc']:.4f}/{metrics['test_worst']:.4f} "
            f"all={metrics['all_acc']:.4f}/{metrics['all_worst']:.4f} "
            f"stress={metrics['stress_min_worst']:.4f}/{metrics['stress_mean_worst']:.4f} "
            f"bytes={int(metrics['int8_bytes'])} us={estimate_board_latency_us(config)}",
            flush=True,
        )
        summarize(all_results, output_dir)

    summarize(all_results, output_dir)
    print("summary_path=" + str(output_dir / "retest_summary.json"), flush=True)


if __name__ == "__main__":
    main()
