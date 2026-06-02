import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any


IMAGE_SIZE = 32
REFERENCE_AVG_US = {
    "spacetodepth_conv": 7163.0,
    "depthwise_pool": 7386.0,
    "conv_pool": 22829.0,
    "stride_conv": 12019.0,
}
REFERENCE_P95_US = {
    "spacetodepth_conv": 10611.0,
    "depthwise_pool": 14517.0,
    "conv_pool": 24196.0,
    "stride_conv": 15787.0,
}
REFERENCE_FILTERS = (8, 16, 32)
SPACE_TO_DEPTH_US = 67.0
POOL_US = 38.0
MEAN_US = 93.0
FC_US = 366.0


def parse_filters(values: Any) -> tuple[int, int, int]:
    if isinstance(values, str):
        parts = [int(item) for item in values.replace("-", ",").split(",") if item]
    else:
        parts = [int(item) for item in values]
    if len(parts) != 3:
        raise ValueError(f"expected 3 filters, got {values}")
    return parts[0], parts[1], parts[2]


def conv_macs(height: int, width: int, in_ch: int, out_ch: int, kernel: int) -> int:
    return height * width * in_ch * out_ch * kernel * kernel


def sep_macs(height: int, width: int, in_ch: int, out_ch: int, kernel: int) -> int:
    return height * width * in_ch * kernel * kernel + height * width * in_ch * out_ch


def backbone_macs(arch: str, filters: tuple[int, int, int], first_kernel: int, extra_conv: bool) -> int:
    h = IMAGE_SIZE
    w = IMAGE_SIZE
    c = 1
    total = 0
    if arch in {"spacetodepth_conv", "spacetodepth_depthwise", "spacetodepth_hybrid", "double_spacetodepth_conv"}:
        h //= 2
        w //= 2
        c *= 4
    for index, out_ch in enumerate(filters):
        kernel = first_kernel if index == 0 else 3
        if arch in {"spacetodepth_conv", "double_spacetodepth_conv"}:
            total += conv_macs(h, w, c, out_ch, kernel)
        elif arch in {"depthwise_pool", "spacetodepth_depthwise"}:
            total += sep_macs(h, w, c, out_ch, kernel)
        elif arch == "spacetodepth_hybrid":
            total += conv_macs(h, w, c, out_ch, kernel) if index == 0 else sep_macs(h, w, c, out_ch, kernel)
        else:
            raise ValueError(f"unknown arch: {arch}")
        c = out_ch
        if extra_conv and index == 2:
            if arch in {"spacetodepth_conv", "double_spacetodepth_conv"}:
                total += conv_macs(h, w, c, c, 3)
            else:
                total += sep_macs(h, w, c, c, 3)
        if arch == "double_spacetodepth_conv" and index == 0:
            h //= 2
            w //= 2
            c *= 4
        elif index < 2:
            h //= 2
            w //= 2
    return total


def reference_macs(arch: str) -> int:
    if arch in {"spacetodepth_conv", "double_spacetodepth_conv", "spacetodepth_hybrid"}:
        return backbone_macs("spacetodepth_conv", REFERENCE_FILTERS, 3, False)
    if arch in {"depthwise_pool", "spacetodepth_depthwise"}:
        return backbone_macs("depthwise_pool", REFERENCE_FILTERS, 3, False)
    return backbone_macs("spacetodepth_conv", REFERENCE_FILTERS, 3, False)


def calibrated_avg_us(config: dict[str, Any]) -> float:
    arch = str(config.get("backbone_architecture", "spacetodepth_conv"))
    filters = parse_filters(config["filters"])
    first_kernel = int(config.get("first_kernel", 3))
    extra_conv = bool(config.get("extra_conv", False))
    macs = backbone_macs(arch, filters, first_kernel, extra_conv)
    ref = reference_macs(arch)
    ratio = max(macs / max(ref, 1), 0.05)
    if arch == "depthwise_pool":
        base = REFERENCE_AVG_US["depthwise_pool"]
        exponent = 0.95
        overhead = 0.0
    elif arch == "spacetodepth_depthwise":
        base = REFERENCE_AVG_US["depthwise_pool"]
        exponent = 0.95
        overhead = SPACE_TO_DEPTH_US
    elif arch == "spacetodepth_hybrid":
        base = REFERENCE_AVG_US["spacetodepth_conv"]
        exponent = 1.05
        overhead = SPACE_TO_DEPTH_US
    elif arch == "double_spacetodepth_conv":
        base = REFERENCE_AVG_US["spacetodepth_conv"]
        exponent = 1.05
        overhead = SPACE_TO_DEPTH_US
    else:
        base = REFERENCE_AVG_US.get(arch, REFERENCE_AVG_US["spacetodepth_conv"])
        exponent = 1.05
        overhead = 0.0
    return overhead + base * (ratio**exponent) + MEAN_US + FC_US


def calibrated_conservative_us(config: dict[str, Any]) -> float:
    # Keep a margin for modelbench p95 jitter and for new op layouts not yet board-measured directly.
    return calibrated_avg_us(config) * 1.25


def prototype_us(row: dict[str, Any]) -> float:
    macs = int(float(row.get("estimated_distance_macs", 0) or 0))
    if macs <= 0:
        return 0.0
    return max(20.0, macs * 0.02)


def read_csv_rows(path: Path) -> list[dict[str, Any]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def summarize_dir(path: Path, candidates_path: Path | None = None) -> list[dict[str, Any]]:
    config_path = path / "train_config.json"
    candidates_path = candidates_path or path / "candidate_results.csv"
    if not config_path.exists() or not candidates_path.exists():
        return []
    config = json.loads(config_path.read_text(encoding="utf-8"))
    rows = read_csv_rows(candidates_path)
    out = []
    backbone_avg = calibrated_avg_us(config)
    backbone_cons = calibrated_conservative_us(config)
    for row in rows:
        total_avg = backbone_avg + prototype_us(row)
        total_cons = backbone_cons + prototype_us(row)
        out.append(
            {
                "run_dir": str(path),
                "config_name": path.name,
                "arch": config.get("backbone_architecture", "spacetodepth_conv"),
                "filters": "-".join(str(x) for x in parse_filters(config["filters"])),
                "embedding_dim": config.get("embedding_dim"),
                "prototype_source": row.get("prototype_source") or row.get("base_source"),
                "k_per_subclass": row.get("k_per_subclass") or row.get("base_k_per_subclass"),
                "prototype_count": row.get("prototype_count"),
                "estimated_distance_macs": row.get("estimated_distance_macs"),
                "board_backbone_avg_us": int(round(backbone_avg)),
                "board_backbone_conservative_us": int(round(backbone_cons)),
                "board_total_avg_us": int(round(total_avg)),
                "board_total_conservative_us": int(round(total_cons)),
                "under_4ms_conservative": total_cons <= 4000.0,
                "under_2ms_avg": total_avg <= 2000.0,
                "under_2ms_conservative": total_cons <= 2000.0,
                "clean_accuracy": row.get("clean_accuracy"),
                "rotmirror_min_accuracy": row.get("rotmirror_min_accuracy"),
                "stress_min_accuracy": row.get("stress_min_accuracy"),
                "int8_clean_accuracy": row.get("int8_clean_accuracy"),
                "int8_rotmirror_min_accuracy": row.get("int8_rotmirror_min_accuracy"),
                "int8_stress_min_accuracy": row.get("int8_stress_min_accuracy"),
                "margin_min": row.get("margin_min"),
                "int8_margin_min": row.get("int8_margin_min"),
            }
        )
    return out


def rank_key(row: dict[str, Any]) -> tuple[float, float, float, float, float]:
    def f(key: str) -> float:
        value = row.get(key, 0.0)
        if value in ("", None):
            return 0.0
        return float(value)

    all_pass = min(
        f("clean_accuracy"),
        f("rotmirror_min_accuracy"),
        f("stress_min_accuracy"),
        f("int8_clean_accuracy"),
        f("int8_rotmirror_min_accuracy"),
        f("int8_stress_min_accuracy"),
    )
    return (
        1.0 if row["under_2ms_conservative"] else 0.0,
        1.0 if row["under_4ms_conservative"] else 0.0,
        all_pass,
        f("margin_min"),
        -float(row["board_total_conservative_us"]),
        -float(row.get("prototype_count") or 0),
    )


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Estimate V8 board time from local 2K0300 TFLM benchmark calibration.")
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=Path("experiments/v8_board_time_summary.csv"))
    args = parser.parse_args()

    rows: list[dict[str, Any]] = []
    for root in args.inputs:
        for candidate in sorted(root.rglob("candidate_results.csv")):
            rows.extend(summarize_dir(candidate.parent, candidate))
        for candidate in sorted(root.rglob("compiled_candidate_results.csv")):
            rows.extend(summarize_dir(candidate.parent, candidate))
    rows.sort(key=rank_key, reverse=True)
    write_csv(args.output, rows)
    print(json.dumps({"output": str(args.output), "rows": len(rows), "top5": rows[:5]}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
