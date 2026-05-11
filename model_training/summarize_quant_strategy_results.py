import argparse
import csv
import json
from pathlib import Path

import numpy as np


def load_jsonl(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    if not path.exists():
        return rows
    for raw in path.read_bytes().splitlines():
        line = raw.replace(b"\x00", b"").strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line.decode("utf-8")))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
    return rows


def parent_metric(item: dict[str, object], split: str, metric: str) -> float:
    result = item.get(split)
    if not isinstance(result, dict):
        return 0.0
    parent = result.get("parent")
    if not isinstance(parent, dict):
        return 0.0
    return float(parent.get(metric, 0.0))


def summarize(items: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for item in items:
        strategy = item.get("strategy", {})
        if not isinstance(strategy, dict):
            continue
        name = str(strategy.get("name", "unknown"))
        grouped.setdefault(name, []).append(item)

    rows: list[dict[str, object]] = []
    for name, group in grouped.items():
        ok = [item for item in group if item.get("status") == "ok" and item.get("quant_test")]
        failures = [item for item in group if item.get("status") != "ok"]
        strategy_source = ok[0] if ok else group[0]
        strategy = strategy_source.get("strategy", {}) if group else {}
        if not isinstance(strategy, dict):
            strategy = {}
        if not ok:
            rows.append(
                {
                    "strategy": name,
                    "phase": strategy.get("phase", ""),
                    "runs": len(group),
                    "ok_runs": 0,
                    "failures": len(failures),
                    "mean_score": 0.0,
                    "min_score": 0.0,
                    "mean_int8_test_acc": 0.0,
                    "mean_int8_test_worst": 0.0,
                    "mean_int8_all_acc": 0.0,
                    "mean_quant_loss_acc": 0.0,
                    "mean_agreement": 0.0,
                    "mean_bytes": 0.0,
                    "best_case": "",
                    "best_case_dir": "",
                    "failure_reason": failures[0].get("error", failures[0].get("quant_error", "")) if failures else "",
                }
            )
            continue
        best = max(
            ok,
            key=lambda item: (
                float(item.get("final_score", 0.0)),
                parent_metric(item, "quant_test", "worst_recall"),
                parent_metric(item, "quant_test", "accuracy"),
            ),
        )
        rows.append(
            {
                "strategy": name,
                "phase": strategy.get("phase", ""),
                "runs": len(group),
                "ok_runs": len(ok),
                "failures": len(failures),
                "mean_score": float(np.mean([float(item.get("final_score", 0.0)) for item in ok])),
                "min_score": float(np.min([float(item.get("final_score", 0.0)) for item in ok])),
                "mean_int8_test_acc": float(np.mean([parent_metric(item, "quant_test", "accuracy") for item in ok])),
                "mean_int8_test_worst": float(
                    np.mean([parent_metric(item, "quant_test", "worst_recall") for item in ok])
                ),
                "mean_int8_all_acc": float(np.mean([parent_metric(item, "quant_all", "accuracy") for item in ok])),
                "mean_quant_loss_acc": float(
                    np.mean([float(item.get("quant_loss", {}).get("test_parent_accuracy", 0.0)) for item in ok])
                ),
                "mean_agreement": float(
                    np.mean([float(item.get("agreement", {}).get("keras_vs_quant_test", 0.0)) for item in ok])
                ),
                "mean_bytes": float(np.mean([float(item.get("export", {}).get("quant_bytes", 0.0)) for item in ok])),
                "best_case": best.get("case_id", ""),
                "best_case_dir": best.get("case_dir", ""),
                "failure_reason": failures[0].get("error", failures[0].get("quant_error", "")) if failures else "",
            }
        )
    rows.sort(
        key=lambda row: (
            float(row["mean_score"]),
            float(row["min_score"]),
            float(row["mean_int8_test_worst"]),
            float(row["mean_int8_test_acc"]),
        ),
        reverse=True,
    )
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = [
        "strategy",
        "phase",
        "runs",
        "ok_runs",
        "failures",
        "mean_score",
        "min_score",
        "mean_int8_test_acc",
        "mean_int8_test_worst",
        "mean_int8_all_acc",
        "mean_quant_loss_acc",
        "mean_agreement",
        "mean_bytes",
        "best_case",
        "best_case_dir",
        "failure_reason",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def main() -> None:
    parser = argparse.ArgumentParser(description="Merge quant strategy scan outputs.")
    parser.add_argument("dirs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    items: list[dict[str, object]] = []
    source_dirs: list[str] = []
    for directory in args.dirs:
        trial_path = directory / "trial_results.jsonl"
        loaded = load_jsonl(trial_path)
        if loaded:
            items.extend(loaded)
            source_dirs.append(str(directory))

    rows = summarize(items)
    summary = {
        "source_dirs": source_dirs,
        "case_count": len(items),
        "strategy_count": len(rows),
        "best": rows[0] if rows else None,
        "strategies": rows,
    }
    output = args.output or Path("experiments") / "v3_quant_strategy_merged_summary.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    write_csv(output.with_suffix(".csv"), rows)
    print("summary_path=" + str(output))
    if rows:
        print("best=" + json.dumps(rows[0], ensure_ascii=False))


if __name__ == "__main__":
    main()
