import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from train_tiny32_v5_visual_subclass_scan import (
    C4_SUBCLASS_INDEX,
    PARENT_NAMES,
    VISUAL_CLASS_NAMES,
    VISUAL_TO_PARENT,
    config_from_dict,
    hard_indices,
    load_dataset_v5,
    predictions_from_outputs,
    stratified_folds,
    stress_batch_any,
    tflite_outputs,
)


DEFAULT_STRESS = [
    "rot90",
    "rot180",
    "rot270",
    "mirror_lr",
    "mirror_lr_rot90",
    "mirror_lr_rot180",
    "mirror_lr_rot270",
    "noise_0p06",
    "hblur5_noise_0p06",
    "diagblur5_noise_0p08",
    "noise_0p10",
    "vblur5",
    "diagblur5",
    "cam_blur2a0",
    "cam_blur3a90",
    "cam_blur5a45",
    "cam_blur5a135",
    "cam_noise0p02",
    "cam_noise0p04",
    "cam_blur3a0_noise0p02",
    "cam_blur5a45_noise0p04",
]


def get(data: dict[str, Any], *keys: str, default: Any = None) -> Any:
    cur: Any = data
    for key in keys:
        if not isinstance(cur, dict):
            return default
        cur = cur.get(key)
    return default if cur is None else cur


def parent_acc(run: dict[str, Any], group: str) -> float:
    return float(get(run, group, "parent", "accuracy", default=0.0))


def stress_worst(run: dict[str, Any], camera_only: bool = False) -> float:
    values: list[float] = []
    for name, item in (run.get("int8_stress") or {}).items():
        if camera_only and not str(name).startswith("cam_"):
            continue
        if isinstance(item, dict):
            values.append(float(item.get("worst_recall", 0.0)))
    return min(values) if values else float(get(run, "int8_test", "parent", "worst_recall", default=0.0))


def c4_all(run: dict[str, Any]) -> float:
    return float(
        get(
            run,
            "c4_eval",
            "all",
            "closed_set_c4_parent_recall",
            default=get(run, "c4_eval", "all", "c4_parent_recall", default=0.0),
        )
    )


def c4_test(run: dict[str, Any]) -> float:
    return float(get(run, "c4_eval", "test", "c4_parent_recall", default=0.0))


def c4_camera(run: dict[str, Any]) -> float:
    return float(get(run, "c4_camera_eval", "c4_camera_stress_recall", default=0.0))


def c4_camera_fp(run: dict[str, Any]) -> float:
    return float(get(run, "c4_camera_eval", "c4_camera_false_positive_rate", default=1.0))


def load_rows(root: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for stage in ["stage5_parent100_neighborhood", "stage5_parent100_retest"]:
        for path in sorted((root / stage).glob("shard_*/trial_results.jsonl")):
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                if not line.strip():
                    continue
                row = json.loads(line)
                row["_stage"] = stage
                row["_shard"] = path.parent.name
                rows.append(row)
    return rows


def collect_single_runs(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    singles: list[dict[str, Any]] = []
    for row in rows:
        config = row.get("config", {})
        for run in row.get("runs", []):
            if not isinstance(run, dict) or run.get("status") != "ok":
                continue
            export = run.get("export") or {}
            singles.append(
                {
                    "stage": row["_stage"],
                    "trial": row["trial"],
                    "seed": int(run["seed"]),
                    "score": float(run.get("score", 0.0)),
                    "test_acc": parent_acc(run, "int8_test"),
                    "all_acc": parent_acc(run, "int8_all"),
                    "hard_acc": parent_acc(run, "int8_hard"),
                    "stress_worst": stress_worst(run),
                    "camera_worst": stress_worst(run, camera_only=True),
                    "c4_all": c4_all(run),
                    "c4_test": c4_test(run),
                    "c4_camera": c4_camera(run),
                    "c4_camera_fp": c4_camera_fp(run),
                    "model_path": str(export.get("int8_path", "")),
                    "float_path": str(export.get("float_path", "")),
                    "artifact_dir": str(run.get("artifact_dir", "")),
                    "config": config,
                }
            )
    return singles


def pick_models(singles: list[dict[str, Any]], limit: int) -> list[dict[str, Any]]:
    picked: list[dict[str, Any]] = []
    by_path: dict[str, dict[str, Any]] = {}

    def add(reason: str, candidates: list[dict[str, Any]]) -> None:
        if not candidates:
            return
        item = dict(candidates[0])
        path = item["model_path"]
        if path in by_path:
            by_path[path]["selection_reasons"].append(reason)
            return
        item["selection_reasons"] = [reason]
        picked.append(item)
        by_path[path] = item

    add("best_all_parent", sorted(singles, key=lambda x: (x["all_acc"], x["test_acc"], x["hard_acc"], x["score"]), reverse=True))
    add(
        "best_joint_parent",
        sorted(
            singles,
            key=lambda x: (min(x["all_acc"], x["test_acc"], x["hard_acc"]), x["all_acc"], x["score"]),
            reverse=True,
        ),
    )
    add("best_score", sorted(singles, key=lambda x: (x["score"], x["all_acc"], x["hard_acc"]), reverse=True))
    add("best_hard", sorted(singles, key=lambda x: (x["hard_acc"], x["all_acc"], x["score"]), reverse=True))
    add(
        "best_c4_camera",
        sorted(
            [x for x in singles if x["c4_camera_fp"] <= 0.04],
            key=lambda x: (x["c4_all"], x["c4_camera"], x["all_acc"], x["score"]),
            reverse=True,
        ),
    )
    add(
        "best_c4_and_parent",
        sorted(
            [x for x in singles if x["c4_all"] >= 0.8333 and x["c4_camera"] >= 0.6666 and x["c4_camera_fp"] <= 0.04],
            key=lambda x: (x["all_acc"], x["hard_acc"], x["score"]),
            reverse=True,
        ),
    )
    add(
        "best_camera_stress",
        sorted(singles, key=lambda x: (min(x["stress_worst"], x["camera_worst"]), x["all_acc"], x["score"]), reverse=True),
    )
    add(
        "best_fast",
        sorted([x for x in singles if int(x.get("config", {}).get("estimated_board_us", 999999)) <= 6093], key=lambda x: (x["all_acc"], x["score"]), reverse=True),
    )
    add(
        "best_retest_all",
        sorted([x for x in singles if x["stage"].endswith("retest")], key=lambda x: (x["all_acc"], x["test_acc"], x["score"]), reverse=True),
    )
    add(
        "best_retest_stress",
        sorted([x for x in singles if x["stage"].endswith("retest")], key=lambda x: (min(x["stress_worst"], x["camera_worst"]), x["all_acc"], x["score"]), reverse=True),
    )

    for item in sorted(singles, key=lambda x: (x["all_acc"], x["score"], x["hard_acc"]), reverse=True):
        if len(picked) >= limit:
            break
        add("top_all_fill", [item])
    return picked[:limit]


def parent_predictions(raw: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    preds = predictions_from_outputs(raw)
    if raw.shape[1] == len(PARENT_NAMES):
        parent = preds
        parent_scores = raw
    elif raw.shape[1] == len(VISUAL_CLASS_NAMES):
        parent = VISUAL_TO_PARENT[preds.astype(np.int64)]
        parent_scores = np.zeros((len(raw), len(PARENT_NAMES)), dtype=np.float32)
        for subclass_index, parent_index in enumerate(VISUAL_TO_PARENT):
            parent_scores[:, int(parent_index)] += raw[:, subclass_index]
    else:
        raise ValueError(f"unsupported output shape: {raw.shape}")
    order = np.sort(parent_scores, axis=1)
    margin = order[:, -1] - order[:, -2] if parent_scores.shape[1] >= 2 else np.zeros(len(parent_scores), dtype=np.float32)
    confidence = np.max(parent_scores, axis=1)
    return parent.astype(np.int64), confidence.astype(np.float32), margin.astype(np.float32)


def image_features(image: np.ndarray) -> dict[str, float]:
    arr = image.astype(np.float32)
    gx = np.abs(np.diff(arr, axis=1)).mean()
    gy = np.abs(np.diff(arr, axis=0)).mean()
    return {
        "mean": float(np.mean(arr)),
        "std": float(np.std(arr)),
        "min": float(np.min(arr)),
        "max": float(np.max(arr)),
        "edge": float(gx + gy),
    }


def evaluate_model(
    model: dict[str, Any],
    model_dir: Path,
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
    hard_idx: np.ndarray,
    stress_names: list[str],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    rel_model_path = Path(model["model_path"])
    model_path = rel_model_path if rel_model_path.is_absolute() else model_dir / rel_model_path
    config = config_from_dict(model["config"], str(model["trial"]))
    test_idx = stratified_folds(y_sub, 5, int(model["seed"]) + 4242)[0]
    index_sets = {
        "all": np.arange(len(y_parent), dtype=np.int64),
        "test": test_idx,
        "hard": hard_idx,
    }

    error_rows: list[dict[str, Any]] = []
    stress_rows: list[dict[str, Any]] = []
    summary: dict[str, Any] = {
        "model_id": model["model_id"],
        "trial": model["trial"],
        "seed": model["seed"],
        "selection_reasons": ",".join(model["selection_reasons"]),
        "model_path": model["model_path"],
        "head": config.head,
        "lane": config.lane,
        "estimated_us": int(model.get("config", {}).get("estimated_board_us", 0)),
        "source_score": model["score"],
        "source_all_acc": model["all_acc"],
        "source_test_acc": model["test_acc"],
        "source_hard_acc": model["hard_acc"],
    }

    clean_raw = tflite_outputs(model_path, x)
    clean_parent, clean_conf, clean_margin = parent_predictions(clean_raw)
    for group, indexes in index_sets.items():
        if len(indexes) == 0:
            summary[f"{group}_wrong"] = 0
            summary[f"{group}_acc"] = 0.0
            continue
        wrong = indexes[clean_parent[indexes] != y_parent[indexes]]
        summary[f"{group}_wrong"] = int(len(wrong))
        summary[f"{group}_acc"] = float(1.0 - len(wrong) / len(indexes))
        for index in wrong:
            features = image_features(x[index])
            error_rows.append(
                {
                    "model_id": model["model_id"],
                    "group": group,
                    "stress": "clean",
                    "index": int(index),
                    "path": paths[index],
                    "basename": Path(paths[index]).name,
                    "visual": VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "true_parent": PARENT_NAMES[int(y_parent[index])],
                    "pred_parent": PARENT_NAMES[int(clean_parent[index])],
                    "confidence": float(clean_conf[index]),
                    "margin": float(clean_margin[index]),
                    "is_hard": int(index in set(hard_idx.tolist())),
                    "is_c4": int(y_sub[index] == C4_SUBCLASS_INDEX),
                    **features,
                }
            )

    for stress_name in stress_names:
        stressed = stress_batch_any(stress_name, x)
        raw = tflite_outputs(model_path, stressed)
        pred_parent, conf, margin = parent_predictions(raw)
        wrong_all = pred_parent != y_parent
        wrong_idx = np.nonzero(wrong_all)[0]
        stress_rows.append(
            {
                "model_id": model["model_id"],
                "stress": stress_name,
                "wrong": int(len(wrong_idx)),
                "accuracy": float(1.0 - len(wrong_idx) / len(y_parent)),
                "c4_wrong": int(np.sum(wrong_all & (y_sub == C4_SUBCLASS_INDEX))),
                "camera": int(stress_name.startswith("cam_")),
            }
        )
        for index in wrong_idx:
            features = image_features(stressed[index])
            error_rows.append(
                {
                    "model_id": model["model_id"],
                    "group": "stress_all",
                    "stress": stress_name,
                    "index": int(index),
                    "path": paths[index],
                    "basename": Path(paths[index]).name,
                    "visual": VISUAL_CLASS_NAMES[int(y_sub[index])],
                    "true_parent": PARENT_NAMES[int(y_parent[index])],
                    "pred_parent": PARENT_NAMES[int(pred_parent[index])],
                    "confidence": float(conf[index]),
                    "margin": float(margin[index]),
                    "is_hard": int(index in set(hard_idx.tolist())),
                    "is_c4": int(y_sub[index] == C4_SUBCLASS_INDEX),
                    **features,
                }
            )
    summary["stress_mean_acc"] = float(np.mean([row["accuracy"] for row in stress_rows])) if stress_rows else 0.0
    summary["stress_worst_acc"] = float(np.min([row["accuracy"] for row in stress_rows])) if stress_rows else 0.0
    camera_rows = [row for row in stress_rows if row["camera"]]
    summary["camera_mean_acc"] = float(np.mean([row["accuracy"] for row in camera_rows])) if camera_rows else 0.0
    summary["camera_worst_acc"] = float(np.min([row["accuracy"] for row in camera_rows])) if camera_rows else 0.0
    return summary, error_rows, stress_rows


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def aggregate_sample_errors(error_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_sample: dict[str, dict[str, Any]] = {}
    for row in error_rows:
        key = str(row["path"])
        item = by_sample.setdefault(
            key,
            {
                "path": row["path"],
                "basename": row["basename"],
                "visual": row["visual"],
                "true_parent": row["true_parent"],
                "clean_wrong_models": set(),
                "hard_clean_wrong_models": set(),
                "stress_wrong_events": 0,
                "camera_wrong_events": 0,
                "stress_names": Counter(),
                "pred_parents": Counter(),
                "is_hard": row["is_hard"],
                "is_c4": row["is_c4"],
            },
        )
        if row["stress"] == "clean" and row["group"] == "all":
            item["clean_wrong_models"].add(row["model_id"])
        if row["stress"] == "clean" and row["group"] == "hard":
            item["hard_clean_wrong_models"].add(row["model_id"])
        if row["group"] == "stress_all":
            item["stress_wrong_events"] += 1
            item["stress_names"][row["stress"]] += 1
            if str(row["stress"]).startswith("cam_"):
                item["camera_wrong_events"] += 1
        item["pred_parents"][row["pred_parent"]] += 1
    out: list[dict[str, Any]] = []
    for item in by_sample.values():
        out.append(
            {
                "path": item["path"],
                "basename": item["basename"],
                "visual": item["visual"],
                "true_parent": item["true_parent"],
                "is_hard": item["is_hard"],
                "is_c4": item["is_c4"],
                "clean_wrong_model_count": len(item["clean_wrong_models"]),
                "hard_clean_wrong_model_count": len(item["hard_clean_wrong_models"]),
                "stress_wrong_events": item["stress_wrong_events"],
                "camera_wrong_events": item["camera_wrong_events"],
                "top_stress": ";".join(f"{k}:{v}" for k, v in item["stress_names"].most_common(5)),
                "pred_parents": ";".join(f"{k}:{v}" for k, v in item["pred_parents"].most_common()),
            }
        )
    out.sort(
        key=lambda row: (
            int(row["clean_wrong_model_count"]),
            int(row["hard_clean_wrong_model_count"]),
            int(row["stress_wrong_events"]),
            int(row["camera_wrong_events"]),
        ),
        reverse=True,
    )
    return out


def write_report(
    path: Path,
    model_summaries: list[dict[str, Any]],
    sample_summary: list[dict[str, Any]],
    stress_rows: list[dict[str, Any]],
) -> None:
    stress_counter: Counter[str] = Counter()
    camera_counter: Counter[str] = Counter()
    for row in stress_rows:
        stress_counter[str(row["stress"])] += int(row["wrong"])
        if row["camera"]:
            camera_counter[str(row["stress"])] += int(row["wrong"])

    lines: list[str] = []
    lines.append("# V6 Parent100 Error Attribution")
    lines.append("")
    lines.append("## Selected Models")
    lines.append("")
    lines.append("|model|reason|all|test|hard|stress_worst|camera_worst|path|")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---|")
    for row in model_summaries:
        lines.append(
            f"|{row['model_id']}|{row['selection_reasons']}|{row['all_acc']:.4f}|{row['test_acc']:.4f}|"
            f"{row['hard_acc']:.4f}|{row['stress_worst_acc']:.4f}|{row['camera_worst_acc']:.4f}|{row['model_path']}|"
        )
    lines.append("")
    lines.append("## Recurrent Clean Parent Errors")
    lines.append("")
    lines.append("|basename|visual|true_parent|hard|c4|clean_wrong_models|stress_events|camera_events|pred_parents|top_stress|")
    lines.append("|---|---|---|---:|---:|---:|---:|---:|---|---|")
    for row in sample_summary[:40]:
        if int(row["clean_wrong_model_count"]) == 0 and int(row["hard_clean_wrong_model_count"]) == 0:
            continue
        lines.append(
            f"|{row['basename']}|{row['visual']}|{row['true_parent']}|{row['is_hard']}|{row['is_c4']}|"
            f"{row['clean_wrong_model_count']}|{row['stress_wrong_events']}|{row['camera_wrong_events']}|"
            f"{row['pred_parents']}|{row['top_stress']}|"
        )
    lines.append("")
    lines.append("## Stress Families With Most Wrong Events")
    lines.append("")
    for stress, count in stress_counter.most_common(12):
        lines.append(f"- {stress}: {count}")
    lines.append("")
    lines.append("## Camera Stress With Most Wrong Events")
    lines.append("")
    for stress, count in camera_counter.most_common(10):
        lines.append(f"- {stress}: {count}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Per-sample error attribution for V6 parent100 runs.")
    parser.add_argument("--run-root", type=Path, default=Path("experiments/v6_parent100_20260515_0001"))
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--model-limit", type=int, default=10)
    parser.add_argument("--stress", default=",".join(DEFAULT_STRESS))
    args = parser.parse_args()

    output_dir = args.output_dir or args.run_root / "error_attribution"
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = load_rows(args.run_root)
    singles = collect_single_runs(rows)
    selected = pick_models(singles, args.model_limit)
    for index, item in enumerate(selected, start=1):
        item["model_id"] = f"m{index:02d}_{item['trial']}_seed{item['seed']}"
    stress_names = [name.strip() for name in args.stress.split(",") if name.strip()]

    x, y_sub, y_parent, paths, _ = load_dataset_v5(args.dataset_dir)
    hard_idx, missing_hard = hard_indices(paths)

    model_summaries: list[dict[str, Any]] = []
    all_errors: list[dict[str, Any]] = []
    all_stress: list[dict[str, Any]] = []
    model_dir = Path.cwd()
    for item in selected:
        summary, errors, stress_rows = evaluate_model(item, model_dir, x, y_sub, y_parent, paths, hard_idx, stress_names)
        model_summaries.append(summary)
        all_errors.extend(errors)
        all_stress.extend(stress_rows)
        print(
            f"{summary['model_id']} all={summary['all_acc']:.4f} test={summary['test_acc']:.4f} "
            f"hard={summary['hard_acc']:.4f} clean_wrong={summary['all_wrong']} "
            f"stress_worst={summary['stress_worst_acc']:.4f} camera_worst={summary['camera_worst_acc']:.4f}",
            flush=True,
        )

    sample_summary = aggregate_sample_errors(all_errors)
    write_csv(output_dir / "selected_models.csv", selected)
    write_csv(output_dir / "model_summary.csv", model_summaries)
    write_csv(output_dir / "error_events.csv", all_errors)
    write_csv(output_dir / "sample_error_summary.csv", sample_summary)
    write_csv(output_dir / "stress_summary.csv", all_stress)
    (output_dir / "selected_models.json").write_text(json.dumps(selected, indent=2, ensure_ascii=False), encoding="utf-8")
    (output_dir / "missing_hard.json").write_text(json.dumps({"missing_hard": missing_hard}, indent=2, ensure_ascii=False), encoding="utf-8")
    write_report(output_dir / "error_attribution_report.md", model_summaries, sample_summary, all_stress)
    print(f"output_dir={output_dir}", flush=True)
    print(f"report={output_dir / 'error_attribution_report.md'}", flush=True)


if __name__ == "__main__":
    main()
