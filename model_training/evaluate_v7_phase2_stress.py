import argparse
import csv
import json
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from analyze_v6_parent100_error_attribution import DEFAULT_STRESS
from analyze_v7_expert_tflite_reverse import TfliteReverse
from export_v7_phase2_deploy_bundle import (
    calibrate_two_band_thresholds,
    evaluate_outputs,
    load_params,
)
from run_v7_delta_merge_phase1 import PARENT_NAMES, counts_for, group_masks
import train_tiny32_v5_visual_subclass_scan as train


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def add_prefix(prefix: str, data: dict[str, object]) -> dict[str, object]:
    return {f"{prefix}_{key}": value for key, value in data.items() if key != "wrong_files"}


def wrong_files(paths: list[str], pred: np.ndarray, y_parent: np.ndarray) -> list[str]:
    return [Path(paths[index]).name for index in np.where(pred != y_parent)[0].tolist()]


def evaluate_old(old_logits: np.ndarray, y_parent: np.ndarray, groups: dict[str, np.ndarray], paths: list[str]) -> dict[str, object]:
    pred = np.argmax(old_logits, axis=1).astype(np.int64)
    metrics = counts_for(pred, y_parent, groups)
    metrics.update({"wrong_files": wrong_files(paths, pred, y_parent)})
    return metrics


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate V7 phase2 adapter/router under rot/mirror/blur/noise stress.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path, required=True)
    parser.add_argument("--params", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stress", default=",".join(DEFAULT_STRESS))
    parser.add_argument("--quant-frac-bits", type=int, default=12)
    args = parser.parse_args()

    params = load_params(args.params)
    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    old = TfliteReverse(args.old_tflite)
    rescue = TfliteReverse(args.rescue_tflite)

    clean_old = old.infer_dataset(x)
    clean_rescue = rescue.infer_dataset(x)
    groups = group_masks(paths, y_sub, y_parent, clean_old["pred"], clean_rescue["pred"])

    clean_float_metrics, clean_float_arrays, _ = evaluate_outputs(
        old_logits=clean_old["logits"].astype(np.float64),
        old_gap=clean_old["gap"].astype(np.float64),
        y_parent=y_parent,
        groups=groups,
        paths=paths,
        params=params,
        quant_frac_bits=None,
    )
    calibrated_params, threshold_calibration = calibrate_two_band_thresholds(
        params,
        clean_old["pred"].astype(np.int64),
        clean_float_arrays,
    )
    calibrated_clean_float, calibrated_clean_arrays, _ = evaluate_outputs(
        old_logits=clean_old["logits"].astype(np.float64),
        old_gap=clean_old["gap"].astype(np.float64),
        y_parent=y_parent,
        groups=groups,
        paths=paths,
        params=calibrated_params,
        quant_frac_bits=None,
    )
    if not (
        np.array_equal(clean_float_arrays["gate"], calibrated_clean_arrays["gate"])
        and np.array_equal(clean_float_arrays["pred"], calibrated_clean_arrays["pred"])
    ):
        calibrated_params = params
        calibrated_clean_float = clean_float_metrics
        threshold_calibration["fallback_to_original"] = True
    else:
        threshold_calibration["fallback_to_original"] = False

    stress_names = ["clean"] + [name.strip() for name in args.stress.split(",") if name.strip()]
    summary_rows: list[dict[str, object]] = []
    sample_rows: list[dict[str, object]] = []
    for stress_name in stress_names:
        xs = x if stress_name == "clean" else train.stress_batch_any(stress_name, x)
        stress_old = clean_old if stress_name == "clean" else old.infer_dataset(xs)
        old_logits = stress_old["logits"].astype(np.float64)
        old_gap = stress_old["gap"].astype(np.float64)
        old_metrics = evaluate_old(old_logits, y_parent, groups, paths)
        phase2_float, float_arrays, _ = evaluate_outputs(
            old_logits=old_logits,
            old_gap=old_gap,
            y_parent=y_parent,
            groups=groups,
            paths=paths,
            params=calibrated_params,
            quant_frac_bits=None,
        )
        phase2_quant, quant_arrays, _ = evaluate_outputs(
            old_logits=old_logits,
            old_gap=old_gap,
            y_parent=y_parent,
            groups=groups,
            paths=paths,
            params=calibrated_params,
            quant_frac_bits=args.quant_frac_bits,
        )
        row: dict[str, object] = {
            "stress": stress_name,
            "camera": stress_name.startswith("cam_"),
            "old_wrong_count": len(old_metrics["wrong_files"]),
            "phase2_float_wrong_count": len(phase2_float["wrong_files"]),
            "phase2_q_wrong_count": len(phase2_quant["wrong_files"]),
            "float_quant_pred_mismatch": int(np.sum(float_arrays["pred"] != quant_arrays["pred"])),
            "float_quant_gate_mismatch": int(np.sum(float_arrays["gate"] != quant_arrays["gate"])),
        }
        row.update(add_prefix("old", old_metrics))
        row.update(add_prefix("phase2_float", phase2_float))
        row.update(add_prefix("phase2_q", phase2_quant))
        row["old_wrong_files"] = old_metrics["wrong_files"]
        row["phase2_float_wrong_files"] = phase2_float["wrong_files"]
        row["phase2_q_wrong_files"] = phase2_quant["wrong_files"]
        summary_rows.append(row)

        old_pred = np.argmax(old_logits, axis=1).astype(np.int64)
        for index, path in enumerate(paths):
            if (
                old_pred[index] != y_parent[index]
                or phase2_quant["wrong_files"]
                or bool(quant_arrays["gate"][index])
                or bool(float_arrays["gate"][index] != quant_arrays["gate"][index])
                or bool(float_arrays["pred"][index] != quant_arrays["pred"][index])
            ):
                sample_rows.append(
                    {
                        "stress": stress_name,
                        "index": index,
                        "file": Path(path).name,
                        "visual": train.VISUAL_CLASS_NAMES[int(y_sub[index])],
                        "parent": PARENT_NAMES[int(y_parent[index])],
                        "old_pred": PARENT_NAMES[int(old_pred[index])],
                        "phase2_float_pred": PARENT_NAMES[int(float_arrays["pred"][index])],
                        "phase2_q_pred": PARENT_NAMES[int(quant_arrays["pred"][index])],
                        "old_correct": bool(old_pred[index] == y_parent[index]),
                        "phase2_float_correct": bool(float_arrays["pred"][index] == y_parent[index]),
                        "phase2_q_correct": bool(quant_arrays["pred"][index] == y_parent[index]),
                        "float_gate": bool(float_arrays["gate"][index]),
                        "q_gate": bool(quant_arrays["gate"][index]),
                        "float_gate_score": float(float_arrays["gate_score"][index]),
                        "q_gate_score": float(quant_arrays["gate_score"][index]),
                        "float_adapter_margin": float(float_arrays["adapter_margin"][index]),
                        "q_adapter_margin": float(quant_arrays["adapter_margin"][index]),
                        "stable": bool(groups["stable"][index]),
                        "preserve": bool(groups["preserve"][index]),
                        "rescue": bool(groups["rescue"][index]),
                        "hard": bool(groups["hard"][index]),
                        "c4": bool(groups["c4"][index]),
                    }
                )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "stress_summary.csv", summary_rows)
    write_csv(args.output_dir / "stress_sample_events.csv", sample_rows)

    phase2_q_acc = [float(row["phase2_q_all_accuracy"]) for row in summary_rows]
    old_acc = [float(row["old_all_accuracy"]) for row in summary_rows]
    camera_rows = [row for row in summary_rows if bool(row["camera"])]
    report = {
        "old_tflite": str(args.old_tflite),
        "rescue_tflite": str(args.rescue_tflite),
        "params": str(args.params),
        "quant_frac_bits": args.quant_frac_bits,
        "stress_count": len(stress_names),
        "threshold_calibration": threshold_calibration,
        "clean_phase2_float": calibrated_clean_float,
        "old_accuracy_mean": float(np.mean(old_acc)),
        "old_accuracy_min": float(np.min(old_acc)),
        "phase2_q_accuracy_mean": float(np.mean(phase2_q_acc)),
        "phase2_q_accuracy_min": float(np.min(phase2_q_acc)),
        "phase2_q_camera_accuracy_min": float(np.min([float(row["phase2_q_all_accuracy"]) for row in camera_rows])) if camera_rows else None,
        "worst_phase2_q": sorted(summary_rows, key=lambda row: (float(row["phase2_q_all_accuracy"]), -int(row["phase2_q_gate_count"])))[:5],
        "best_delta_over_old": sorted(summary_rows, key=lambda row: int(row["old_wrong_count"]) - int(row["phase2_q_wrong_count"]), reverse=True)[:5],
    }
    (args.output_dir / "stress_report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
