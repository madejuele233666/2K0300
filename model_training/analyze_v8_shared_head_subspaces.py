import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

import numpy as np


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def classify(features: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    x_all = features.astype(np.int32)
    p_all = prototypes.astype(np.int32)
    parent_count = 3
    class_dist = np.full((len(x_all), parent_count), np.iinfo(np.int64).max, dtype=np.int64)
    parent_indexes = [np.where(prototype_parent == parent)[0] for parent in range(parent_count)]
    for start in range(0, len(x_all), 512):
        x = x_all[start : start + 512]
        dist = np.sum((x[:, None, :] - p_all[None, :, :]) ** 2, axis=2).astype(np.int64)
        for parent, indexes in enumerate(parent_indexes):
            if len(indexes) == 0:
                continue
            class_dist[start : start + len(x), parent] = np.min(dist[:, indexes], axis=1)
    order = np.argsort(class_dist, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (
        class_dist[np.arange(len(x_all)), order[:, 1]]
        - class_dist[np.arange(len(x_all)), order[:, 0]]
    ).astype(np.int64)
    return pred, margin


def subset_mask(view_labels: np.ndarray, subset: str) -> np.ndarray:
    rotmirror = {
        "rot90",
        "rot180",
        "rot270",
        "mirror_lr",
        "mirror_lr_rot90",
        "mirror_lr_rot180",
        "mirror_lr_rot270",
    }
    if subset == "clean":
        return view_labels == "clean"
    if subset == "clean_rotmirror":
        return (view_labels == "clean") | np.isin(view_labels, list(rotmirror))
    if subset == "all":
        return np.ones(len(view_labels), dtype=bool)
    raise ValueError(f"unknown subset: {subset}")


def compile_residual_table(
    *,
    embeddings: np.ndarray,
    parent: np.ndarray,
    view_labels: np.ndarray,
    dims: list[int],
    base_subset: str,
    target_margin: int,
    max_iterations: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, list[dict[str, Any]]]:
    selected = subset_mask(view_labels, base_subset).copy()
    z = embeddings[:, dims]
    trace: list[dict[str, Any]] = []
    for iteration in range(1, max_iterations + 1):
        pred, margin = classify(z, z[selected], parent[selected])
        risk = (pred != parent) | (margin <= int(target_margin))
        add_indexes = [int(index) for index in np.where(risk)[0] if not bool(selected[int(index)])]
        if not add_indexes:
            break
        selected[np.asarray(add_indexes, dtype=np.int64)] = True
        trace.append(
            {
                "iteration": int(iteration),
                "added": int(len(add_indexes)),
                "wrong_before": int(np.sum(pred != parent)),
                "risk_before": int(np.sum(risk)),
                "prototype_count_after": int(np.sum(selected)),
            }
        )
    pred, margin = classify(z, z[selected], parent[selected])
    return z[selected], parent[selected], pred, margin, trace


def load_stress_events(path: Path) -> tuple[list[dict[str, str]], np.ndarray, np.ndarray, list[str]]:
    rows: list[dict[str, str]] = []
    features: list[list[int]] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            rows.append(row)
            features.append(json.loads(row["feature_json"]))
    parent = np.asarray([int(row["parent"]) for row in rows], dtype=np.int64)
    groups = [str(row["group"]) for row in rows]
    return rows, np.asarray(features, dtype=np.int8), parent, groups


def margin_bucket(value: int) -> str:
    for threshold in [1, 2, 4, 8, 16, 64]:
        if int(value) <= threshold:
            return f"le{threshold}"
    return "gt64"


def export_stress_events(
    *,
    path: Path,
    template_rows: list[dict[str, str]],
    features: np.ndarray,
    dims: list[int],
    pred: np.ndarray,
    margin: np.ndarray,
) -> None:
    rows_out: list[dict[str, Any]] = []
    sub_features = features[:, dims].astype(np.int32)
    for index, row in enumerate(template_rows):
        out: dict[str, Any] = dict(row)
        vector = [int(value) for value in sub_features[index].tolist()]
        parent = int(row["parent"])
        prediction = int(pred[index])
        event_margin = int(margin[index])
        out["stress_pred"] = prediction
        out["wrong"] = str(prediction != parent).lower()
        out["primary_pred"] = prediction
        out["primary_margin"] = event_margin
        out["stress_margin"] = event_margin
        out["stress_margin_bucket"] = margin_bucket(event_margin)
        out["feature_dim"] = len(dims)
        out["feature_json"] = json.dumps(vector, separators=(",", ":"))
        for column, value in zip(["feature0", "feature1", "feature2"], vector, strict=False):
            if column in out:
                out[column] = int(value)
        for column in [
            "correct_dist",
            "nearest_wrong_parent",
            "nearest_wrong_dist",
            "nearest_correct_proto",
            "nearest_correct_proto_sample",
            "nearest_correct_proto_view",
            "nearest_wrong_proto",
            "nearest_wrong_proto_sample",
            "nearest_wrong_proto_view",
        ]:
            if column in out:
                out[column] = ""
        rows_out.append(out)
    write_csv(path, rows_out)


def rate_summary(pred: np.ndarray, parent: np.ndarray, groups: list[str]) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for value, target, group in zip(pred.tolist(), parent.tolist(), groups, strict=False):
        grouped[str(group)][0] += int(int(value) != int(target))
        grouped[str(group)][1] += 1
    return {
        group: {
            "wrong": int(values[0]),
            "total": int(values[1]),
            "wrong_rate": float(values[0] / max(values[1], 1)),
        }
        for group, values in sorted(grouped.items())
    }


def parse_subspaces(text: str) -> dict[str, list[int]]:
    out: dict[str, list[int]] = {}
    for item in text.split(";"):
        item = item.strip()
        if not item:
            continue
        name, dims_text = item.split("=", 1)
        out[name.strip()] = [int(value) for value in dims_text.split(",") if value.strip()]
    if not out:
        raise ValueError("no subspaces parsed")
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze subspace prototype tables inside one V8 shared-head payload.")
    parser.add_argument("--params-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--subspaces",
        default="head4=0,1,2,3;side4=4,5,6,7;head4_side01=0,1,2,3,4,5;head4_side23=0,1,2,3,6,7;all8=0,1,2,3,4,5,6,7",
    )
    parser.add_argument("--base-subset", default="clean")
    parser.add_argument("--target-margin", type=int, default=8)
    parser.add_argument("--max-iterations", type=int, default=12)
    parser.add_argument("--board-backbone-conservative-us", type=float, default=1893.0)
    parser.add_argument("--export-subspace-events", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    subspaces = parse_subspaces(args.subspaces)
    with np.load(args.params_npz, allow_pickle=True) as data:
        embeddings = np.asarray(data["embedding_int8"], dtype=np.int8)
        parent = np.asarray(data["parent"], dtype=np.int64)
        view_labels = np.asarray(data["view_labels"]).astype(str)
        sample_index = (
            np.asarray(data["sample_index"], dtype=np.int64)
            if "sample_index" in data.files
            else np.arange(len(parent), dtype=np.int64)
        )
    stress_rows, stress_features, stress_parent, stress_groups = load_stress_events(args.stress_events_csv)

    rows: list[dict[str, Any]] = []
    predictions: dict[str, np.ndarray] = {}
    margins: dict[str, np.ndarray] = {}
    valid_under2_names: list[str] = []
    for name, dims in subspaces.items():
        prototypes, prototype_parent, normal_pred, normal_margin, trace = compile_residual_table(
            embeddings=embeddings,
            parent=parent,
            view_labels=view_labels,
            dims=dims,
            base_subset=args.base_subset,
            target_margin=args.target_margin,
            max_iterations=args.max_iterations,
        )
        stress_pred, stress_margin = classify(stress_features[:, dims], prototypes, prototype_parent)
        predictions[name] = stress_pred
        margins[name] = stress_margin
        export_stress_events_csv = ""
        normal_params_npz = ""
        if args.export_subspace_events:
            events_path = args.output_dir / "subspace_events" / f"{name}_stress_events.csv"
            params_path = args.output_dir / "subspace_events" / f"{name}_normal_params.npz"
            export_stress_events(
                path=events_path,
                template_rows=stress_rows,
                features=stress_features,
                dims=dims,
                pred=stress_pred,
                margin=stress_margin,
            )
            np.savez_compressed(
                params_path,
                embedding_int8=embeddings[:, dims].astype(np.int8),
                parent=parent.astype(np.int64),
                sample_index=sample_index.astype(np.int64),
                view_labels=view_labels.astype(str),
                int8_pred=normal_pred.astype(np.int64),
                int8_margin=normal_margin.astype(np.int64),
                dims=np.asarray(dims, dtype=np.int64),
            )
            export_stress_events_csv = str(events_path)
            normal_params_npz = str(params_path)
        macs = int(len(prototypes) * len(dims))
        board_cons = float(args.board_backbone_conservative_us) + max(20.0, float(macs) * 0.02)
        per_group = rate_summary(stress_pred, stress_parent, stress_groups)
        normal_all = bool(np.all(normal_pred == parent))
        under2 = bool(board_cons <= 2000.0)
        if normal_all and under2:
            valid_under2_names.append(name)
        row = {
            "name": name,
            "dims": ",".join(str(dim) for dim in dims),
            "prototype_count": int(len(prototypes)),
            "estimated_distance_macs": int(macs),
            "board_total_conservative_us": int(round(board_cons)),
            "under_2ms_conservative": under2,
            "normal_replay_all_correct": normal_all,
            "int8_margin_min": int(np.min(normal_margin)),
            "int8_margin_mean": float(np.mean(normal_margin)),
            "highstress_low_wrong_rate": float(per_group.get("low", {}).get("wrong_rate", 0.0)),
            "highstress_control_wrong_rate": float(per_group.get("control", {}).get("wrong_rate", 0.0)),
            "wrong_events": int(sum(values["wrong"] for values in per_group.values())),
            "stress_events_csv": export_stress_events_csv,
            "normal_params_npz": normal_params_npz,
            "trace_json": json.dumps(trace, ensure_ascii=False),
        }
        rows.append(row)

    oracle_base = "all8" if "all8" in predictions else next(iter(predictions))
    oracle = predictions[oracle_base].copy()
    oracle_used = 0
    for index in range(len(stress_parent)):
        if int(oracle[index]) == int(stress_parent[index]):
            continue
        for name in valid_under2_names:
            if name == oracle_base:
                continue
            pred = predictions[name]
            if int(pred[index]) == int(stress_parent[index]):
                oracle[index] = int(pred[index])
                oracle_used += 1
                break
    oracle_rates = rate_summary(oracle, stress_parent, stress_groups)
    gate_rows: list[dict[str, Any]] = [
        {
            "name": f"oracle_any_valid_under2_subspace_from_{oracle_base}",
            "used": int(oracle_used),
            "low_wrong_rate": float(oracle_rates.get("low", {}).get("wrong_rate", 0.0)),
            "control_wrong_rate": float(oracle_rates.get("control", {}).get("wrong_rate", 0.0)),
        }
    ]
    if valid_under2_names:
        valid_base = valid_under2_names[0]
        valid_oracle = predictions[valid_base].copy()
        valid_oracle_used = 0
        for index in range(len(stress_parent)):
            if int(valid_oracle[index]) == int(stress_parent[index]):
                continue
            for name in valid_under2_names:
                if name == valid_base:
                    continue
                pred = predictions[name]
                if int(pred[index]) == int(stress_parent[index]):
                    valid_oracle[index] = int(pred[index])
                    valid_oracle_used += 1
                    break
        valid_oracle_rates = rate_summary(valid_oracle, stress_parent, stress_groups)
        gate_rows.append(
            {
                "name": f"oracle_any_valid_under2_subspace_from_{valid_base}",
                "used": int(valid_oracle_used),
                "low_wrong_rate": float(valid_oracle_rates.get("low", {}).get("wrong_rate", 0.0)),
                "control_wrong_rate": float(valid_oracle_rates.get("control", {}).get("wrong_rate", 0.0)),
            }
        )
    if "head4_side23" in predictions and oracle_base in predictions:
        for threshold in [8, 16, 32, 64, 128, 256, 512, 1024]:
            out = predictions[oracle_base].copy()
            mask = (margins[oracle_base] <= threshold) & (margins["head4_side23"] > margins[oracle_base])
            out[mask] = predictions["head4_side23"][mask]
            rates = rate_summary(out, stress_parent, stress_groups)
            gate_rows.append(
                {
                    "name": f"{oracle_base}_to_head4_side23_margin_le_{threshold}",
                    "used": int(np.sum(mask)),
                    "low_wrong_rate": float(rates.get("low", {}).get("wrong_rate", 0.0)),
                    "control_wrong_rate": float(rates.get("control", {}).get("wrong_rate", 0.0)),
                }
            )

    write_csv(args.output_dir / "subspace_summary.csv", rows)
    write_csv(args.output_dir / "subspace_gate_summary.csv", gate_rows)
    write_json(
        args.output_dir / "summary.json",
        {
            "params_npz": str(args.params_npz),
            "stress_events_csv": str(args.stress_events_csv),
            "high_pressure_usage": "evaluation_only",
            "subspaces": rows,
            "gates": gate_rows,
        },
    )
    print(json.dumps({"output_dir": str(args.output_dir), "subspaces": rows, "gates": gate_rows}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
