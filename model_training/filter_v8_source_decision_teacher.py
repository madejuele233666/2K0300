import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from evaluate_v8_embedding_prototypes import write_csv


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def scalar_string(data: np.lib.npyio.NpzFile, key: str, default: str) -> str:
    if key not in data.files:
        return default
    value = np.asarray(data[key])
    if value.shape == ():
        return str(value.item())
    return str(value)


def main() -> None:
    parser = argparse.ArgumentParser(description="Filter a V8 source-decision margin teacher to focused normal-boundary rows.")
    parser.add_argument("--input-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--base-margin-max", type=int, default=-1)
    parser.add_argument("--normalized-aggregate-margin-min", type=float, default=-1.0)
    parser.add_argument("--normalized-aggregate-margin-max", type=float, default=-1.0)
    parser.add_argument("--support-min", type=int, default=0)
    parser.add_argument("--target-margin", type=float, default=-1.0)
    parser.add_argument("--weight-scale", type=float, default=1.0)
    parser.add_argument("--max-weight", type=float, default=0.0)
    args = parser.parse_args()

    with np.load(args.input_npz, allow_pickle=True) as data:
        required = [
            "sample_index",
            "view_labels",
            "parent",
            "wrong_parent",
            "support",
            "base_margin",
            "normalized_aggregate_margin",
            "target_margin",
            "weight",
        ]
        missing = [key for key in required if key not in data.files]
        if missing:
            raise ValueError(f"{args.input_npz} is missing arrays: {missing}")

        arrays = {key: np.asarray(data[key]) for key in required}
        source_base_npz = scalar_string(data, "source_base_npz", "")
        source_npz = np.asarray(data["source_npz"]).astype(str) if "source_npz" in data.files else np.asarray([], dtype=str)
        high_pressure_usage = scalar_string(data, "high_pressure_usage", "none")

    mask = np.ones(len(arrays["sample_index"]), dtype=bool)
    if args.base_margin_max >= 0:
        mask &= arrays["base_margin"].astype(np.int64) <= int(args.base_margin_max)
    if args.normalized_aggregate_margin_min >= 0:
        mask &= arrays["normalized_aggregate_margin"].astype(np.float32) >= float(args.normalized_aggregate_margin_min)
    if args.normalized_aggregate_margin_max >= 0:
        mask &= arrays["normalized_aggregate_margin"].astype(np.float32) <= float(args.normalized_aggregate_margin_max)
    if args.support_min > 0:
        mask &= arrays["support"].astype(np.int64) >= int(args.support_min)

    indexes = np.where(mask)[0]
    target = arrays["target_margin"][indexes].astype(np.float32)
    if args.target_margin >= 0:
        target = np.full(len(indexes), float(args.target_margin), dtype=np.float32)
    weight = arrays["weight"][indexes].astype(np.float32) * float(args.weight_scale)
    if args.max_weight > 0:
        weight = np.minimum(weight, float(args.max_weight)).astype(np.float32)

    out = {
        "sample_index": arrays["sample_index"][indexes].astype(np.int64),
        "view_labels": arrays["view_labels"][indexes].astype(str),
        "parent": arrays["parent"][indexes].astype(np.int64),
        "wrong_parent": arrays["wrong_parent"][indexes].astype(np.int64),
        "support": arrays["support"][indexes].astype(np.int64),
        "base_margin": arrays["base_margin"][indexes].astype(np.int64),
        "normalized_aggregate_margin": arrays["normalized_aggregate_margin"][indexes].astype(np.float32),
        "target_margin": target,
        "weight": weight,
        "source_base_npz": np.asarray(source_base_npz),
        "source_npz": source_npz,
        "high_pressure_usage": np.asarray(high_pressure_usage),
        "filter_source_npz": np.asarray(str(args.input_npz)),
    }

    args.output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output_dir / "source_decision_margin_teacher.npz", **out)

    rows: list[dict[str, Any]] = []
    for out_index, source_index in enumerate(indexes.tolist()):
        rows.append(
            {
                "source_row": int(source_index),
                "sample_index": int(out["sample_index"][out_index]),
                "view_label": str(out["view_labels"][out_index]),
                "parent": int(out["parent"][out_index]),
                "wrong_parent": int(out["wrong_parent"][out_index]),
                "support": int(out["support"][out_index]),
                "base_margin": int(out["base_margin"][out_index]),
                "normalized_aggregate_margin": float(out["normalized_aggregate_margin"][out_index]),
                "target_margin": float(out["target_margin"][out_index]),
                "weight": float(out["weight"][out_index]),
            }
        )
    write_csv(args.output_dir / "source_decision_margin_rows.csv", rows)

    summary = {
        "input_npz": str(args.input_npz),
        "output": str(args.output_dir / "source_decision_margin_teacher.npz"),
        "high_pressure_usage": high_pressure_usage,
        "input_rows": int(len(arrays["sample_index"])),
        "row_count": int(len(indexes)),
        "base_margin_max": int(args.base_margin_max),
        "normalized_aggregate_margin_min": float(args.normalized_aggregate_margin_min),
        "normalized_aggregate_margin_max": float(args.normalized_aggregate_margin_max),
        "support_min": int(args.support_min),
        "target_margin": float(args.target_margin),
        "weight_scale": float(args.weight_scale),
        "max_weight": float(args.max_weight),
        "weight_min": float(np.min(weight)) if len(weight) else 0.0,
        "weight_max": float(np.max(weight)) if len(weight) else 0.0,
        "weight_mean": float(np.mean(weight)) if len(weight) else 0.0,
    }
    write_json(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
