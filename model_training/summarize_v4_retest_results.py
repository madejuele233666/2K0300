import argparse
import csv
import json
from pathlib import Path

import numpy as np


def stress_values(final: dict[str, object]) -> tuple[list[float], list[float]]:
    worst = []
    macro = []
    for item in final.get("int8_stress", {}).values():
        if not isinstance(item, dict):
            continue
        parent = item.get("parent", item)
        if not isinstance(parent, dict):
            continue
        if "worst_recall" in parent:
            worst.append(float(parent["worst_recall"]))
        if "macro_recall" in parent:
            macro.append(float(parent["macro_recall"]))
    return worst, macro


def flatten(item: dict[str, object]) -> dict[str, object]:
    final = item["final"]
    parent = final["int8_test"]["parent"]
    all_parent = final["int8_all"]["parent"]
    subclass = final["int8_test"]["subclass"]
    stress_worst, stress_macro = stress_values(final)
    return {
        "run_id": item["run_id"],
        "label": item["label"],
        "seed": int(item["seed"]),
        "score": float(item["score"]),
        "test_acc": float(parent["accuracy"]),
        "test_worst": float(parent["worst_recall"]),
        "test_macro": float(parent["macro_recall"]),
        "sub_acc": float(subclass["accuracy"]),
        "sub_worst": float(subclass["worst_recall"]),
        "all_acc": float(all_parent["accuracy"]),
        "all_worst": float(all_parent["worst_recall"]),
        "all_macro": float(all_parent["macro_recall"]),
        "stress_min": min(stress_worst) if stress_worst else float(parent["worst_recall"]),
        "stress_mean": float(np.mean(stress_worst)) if stress_worst else float(parent["worst_recall"]),
        "stress_macro_mean": float(np.mean(stress_macro)) if stress_macro else float(parent["macro_recall"]),
        "agreement_all": float(final.get("keras_vs_int8_all_agreement", 0.0)),
        "bytes": int(final["export"]["int8_bytes"]),
        "us": int(item["estimated_board_us"]),
        "path": final["export"]["int8_path"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Combine V4 retest shard results.")
    parser.add_argument("--glob", default="experiments/v4_retest_shard*_20260508_1658/retest_results.jsonl")
    parser.add_argument("--output-prefix", default="experiments/v4_retest_combined_20260508_1658")
    args = parser.parse_args()

    result_paths = sorted(Path(".").glob(args.glob))
    results = []
    for path in result_paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                item = json.loads(line)
                item["_source_file"] = str(path)
                results.append(item)
    runs = [flatten(item) for item in results]
    by_label: dict[str, list[dict[str, object]]] = {}
    for run in runs:
        by_label.setdefault(str(run["label"]), []).append(run)

    summary = []
    for label, items in by_label.items():
        row: dict[str, object] = {
            "label": label,
            "runs": len(items),
            "us": int(items[0]["us"]),
            "bytes_mean": float(np.mean([float(item["bytes"]) for item in items])),
        }
        for key in [
            "score",
            "test_acc",
            "test_worst",
            "sub_acc",
            "sub_worst",
            "all_acc",
            "all_worst",
            "stress_min",
            "stress_mean",
            "agreement_all",
        ]:
            values = np.array([float(item[key]) for item in items], dtype=float)
            row[f"{key}_mean"] = float(values.mean())
            row[f"{key}_min"] = float(values.min())
            row[f"{key}_std"] = float(values.std())
        best = max(items, key=lambda item: float(item["score"]))
        row["best_run_id"] = best["run_id"]
        row["best_path"] = best["path"]
        summary.append(row)

    summary.sort(
        key=lambda row: (
            float(row["score_mean"]),
            float(row["test_worst_min"]),
            float(row["all_worst_mean"]),
            float(row["stress_min_mean"]),
            -float(row["us"]),
        ),
        reverse=True,
    )

    out_json = Path(args.output_prefix + "_summary.json")
    out_csv = Path(args.output_prefix + "_summary.csv")
    out_json.write_text(json.dumps({"summary": summary, "runs": runs}, indent=2, ensure_ascii=False), encoding="utf-8")
    if summary:
        with out_csv.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(summary[0].keys()))
            writer.writeheader()
            writer.writerows(summary)

    print(f"results={len(results)} labels={len(summary)}")
    for row in summary:
        print(
            f"{row['label']:24s} n={row['runs']} score={row['score_mean']:.4f} min={row['score_min']:.4f} "
            f"test={row['test_acc_mean']:.4f}/{row['test_worst_mean']:.4f} worstMin={row['test_worst_min']:.4f} "
            f"all={row['all_acc_mean']:.4f}/{row['all_worst_mean']:.4f} "
            f"stress={row['stress_min_mean']:.4f} stressMin={row['stress_min_min']:.4f} "
            f"bytes={row['bytes_mean']:.0f} us={row['us']}"
        )
    print("WROTE", out_json)
    print("WROTE", out_csv)


if __name__ == "__main__":
    main()
