import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np


def parse_csv(text: str) -> list[str]:
    if not text:
        return []
    return [item.strip() for item in text.split(",") if item.strip()]


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields: list[str] = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def class_distances(features: np.ndarray, prototypes: np.ndarray, prototype_parent: np.ndarray) -> np.ndarray:
    out = np.empty((len(features), 3), dtype=np.int64)
    for parent in range(3):
        parent_prototypes = prototypes[prototype_parent == parent].astype(np.int32)
        chunks: list[np.ndarray] = []
        for start in range(0, len(features), 2048):
            chunk = features[start : start + 2048].astype(np.int32)
            dist = np.sum((chunk[:, None, :] - parent_prototypes[None, :, :]) ** 2, axis=2)
            chunks.append(np.min(dist, axis=1).astype(np.int64))
        out[:, parent] = np.concatenate(chunks)
    return out


def pred_margin(class_dist: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(class_dist, axis=1)
    pred = order[:, 0].astype(np.int64)
    margin = (class_dist[np.arange(len(class_dist)), order[:, 1]] - class_dist[np.arange(len(class_dist)), order[:, 0]]).astype(
        np.int64
    )
    return pred, margin


def load_stress_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            feature = tuple(int(item) for item in json.loads(row["feature_json"]))
            rows.append(
                {
                    "group": str(row["group"]),
                    "feature": feature,
                    "parent": int(row["parent"]),
                    "wrong": str(row["wrong"]) == "True",
                    "selection_margin": int(row["selection_margin"]),
                    "perturb_family": str(row["perturb_family"]),
                    "perturb": str(row["perturb"]),
                    "perturb_severity": float(row["perturb_severity"]),
                    "stress_margin": int(row["stress_margin"]),
                    "event_index": int(row["event_index"]),
                    "sample_index": int(row["sample_index"]),
                    "view_label": str(row["view_label"]),
                }
            )
    return rows


def filter_by_perturbs(rows: list[dict[str, Any]], perturbs: set[str] | None) -> np.ndarray:
    if perturbs is None:
        return np.ones(len(rows), dtype=bool)
    return np.asarray([str(row["perturb"]) in perturbs for row in rows], dtype=bool)


def split_summary_rows(
    *,
    rows: list[dict[str, Any]],
    stress_parent: np.ndarray,
    base_pred: np.ndarray,
    final_pred: np.ndarray,
    split_masks: dict[str, np.ndarray],
) -> list[dict[str, Any]]:
    groups = np.asarray([str(row["group"]) for row in rows])
    out: list[dict[str, Any]] = []
    for split_name, split_mask in split_masks.items():
        for group in ["low", "control"]:
            mask = split_mask & (groups == group)
            total = int(np.sum(mask))
            if total == 0:
                continue
            base_wrong = int(np.sum((base_pred != stress_parent) & mask))
            final_wrong = int(np.sum((final_pred != stress_parent) & mask))
            out.append(
                {
                    "split": split_name,
                    "group": group,
                    "total": total,
                    "base_wrong": base_wrong,
                    "base_wrong_rate": float(base_wrong / max(1, total)),
                    "final_wrong": final_wrong,
                    "final_wrong_rate": float(final_wrong / max(1, total)),
                    "wrong_delta": int(final_wrong - base_wrong),
                }
            )
    return out


def build_repair(args: argparse.Namespace) -> dict[str, Any]:
    with np.load(args.base_params_npz, allow_pickle=True) as data:
        base = {key: data[key] for key in data.files}
    normal_features = np.asarray(base["embedding_int8"], dtype=np.int8).astype(np.int32)
    normal_parent = np.asarray(base["parent"], dtype=np.int64)
    base_prototypes = np.asarray(base["prototypes_int8"], dtype=np.int8).astype(np.int32)
    base_prototype_parent = np.asarray(base["prototype_parent"], dtype=np.int64)
    rows = load_stress_rows(args.stress_events_csv)
    stress_features = np.asarray([row["feature"] for row in rows], dtype=np.int32)
    stress_parent = np.asarray([row["parent"] for row in rows], dtype=np.int64)
    groups = np.asarray([row["group"] for row in rows])

    selection_perturbs = set(parse_csv(args.selection_perturbs)) if args.selection_perturbs else None
    eval_perturbs = set(parse_csv(args.eval_perturbs)) if args.eval_perturbs else None
    control_perturbs = set(parse_csv(args.control_safety_perturbs)) if args.control_safety_perturbs else None
    selection_mask = filter_by_perturbs(rows, selection_perturbs)
    eval_mask = filter_by_perturbs(rows, eval_perturbs)
    control_safety_mask = filter_by_perturbs(rows, control_perturbs)

    normal_cd = class_distances(normal_features, base_prototypes, base_prototype_parent)
    normal_pred, normal_margin = pred_margin(normal_cd)
    if int(np.sum(normal_pred != normal_parent)) != 0:
        raise ValueError("base params are not clean on normal replay")

    stress_cd = class_distances(stress_features, base_prototypes, base_prototype_parent)
    stress_pred, _stress_margin = pred_margin(stress_cd)
    low_score_mask = selection_mask & (groups == "low")
    control_score_mask = control_safety_mask & (groups == "control")
    current_low_wrong = int(np.sum((stress_pred != stress_parent) & low_score_mask))
    current_control_wrong = int(np.sum((stress_pred != stress_parent) & control_score_mask))
    current_normal_le8 = int(np.sum(normal_margin <= args.max_normal_margin_bucket))

    candidates: dict[tuple[tuple[int, ...], int], tuple[tuple[Any, ...], dict[str, Any]]] = {}
    for row in rows:
        if row["group"] != "low" or not row["wrong"]:
            continue
        if selection_perturbs is not None and row["perturb"] not in selection_perturbs:
            continue
        key = (tuple(int(item) for item in row["feature"]), int(row["parent"]))
        score = (int(row["selection_margin"]), float(row["perturb_severity"]), int(row["stress_margin"]), int(row["event_index"]))
        if key not in candidates or score < candidates[key][0]:
            candidates[key] = (score, row)

    accepted: list[dict[str, Any]] = []
    current_normal_cd = normal_cd.copy()
    current_stress_cd = stress_cd.copy()
    for _score, row in sorted(candidates.values(), key=lambda item: item[0]):
        feature = np.asarray(row["feature"], dtype=np.int32)
        parent = int(row["parent"])
        trial_normal_cd = current_normal_cd.copy()
        trial_normal_cd[:, parent] = np.minimum(
            trial_normal_cd[:, parent], np.sum((normal_features - feature[None, :]) ** 2, axis=1).astype(np.int64)
        )
        trial_normal_pred, trial_normal_margin = pred_margin(trial_normal_cd)
        if int(np.sum(trial_normal_pred != normal_parent)) != 0:
            continue
        trial_normal_le8 = int(np.sum(trial_normal_margin <= args.max_normal_margin_bucket))
        if args.no_increase_normal_low_margin and trial_normal_le8 > current_normal_le8:
            continue

        trial_stress_cd = current_stress_cd.copy()
        trial_stress_cd[:, parent] = np.minimum(
            trial_stress_cd[:, parent], np.sum((stress_features - feature[None, :]) ** 2, axis=1).astype(np.int64)
        )
        trial_stress_pred, _trial_stress_margin = pred_margin(trial_stress_cd)
        trial_low_wrong = int(np.sum((trial_stress_pred != stress_parent) & low_score_mask))
        trial_control_wrong = int(np.sum((trial_stress_pred != stress_parent) & control_score_mask))
        if args.no_increase_control and trial_control_wrong > current_control_wrong:
            continue
        if trial_low_wrong >= current_low_wrong:
            continue

        accepted.append(
            {
                **row,
                "low_wrong_after": trial_low_wrong,
                "control_wrong_after": trial_control_wrong,
                "normal_low_margin_after": trial_normal_le8,
            }
        )
        current_normal_cd = trial_normal_cd
        current_stress_cd = trial_stress_cd
        current_low_wrong = trial_low_wrong
        current_control_wrong = trial_control_wrong
        current_normal_le8 = trial_normal_le8

    final_normal_pred, final_normal_margin = pred_margin(current_normal_cd)
    final_stress_pred, _final_stress_margin = pred_margin(current_stress_cd)
    add_prototypes = np.asarray([row["feature"] for row in accepted], dtype=np.int8)
    prototypes_int8 = (
        np.concatenate([np.asarray(base["prototypes_int8"], dtype=np.int8), add_prototypes], axis=0)
        if len(accepted)
        else np.asarray(base["prototypes_int8"], dtype=np.int8).copy()
    )
    add_parent = np.asarray([row["parent"] for row in accepted], dtype=np.int64)
    prototype_parent = (
        np.concatenate([np.asarray(base["prototype_parent"], dtype=np.int64), add_parent], axis=0)
        if len(accepted)
        else np.asarray(base["prototype_parent"], dtype=np.int64).copy()
    )
    base_count = len(np.asarray(base["prototypes_int8"]))
    payload = {
        key: value
        for key, value in base.items()
        if key
        not in {
            "prototypes",
            "prototypes_int8",
            "prototype_parent",
            "prototype_subclass",
            "prototype_cluster",
            "prototype_sample_index",
            "prototype_view_label",
            "prototype_source_kind",
            "pred",
            "int8_pred",
            "margin",
            "int8_margin",
        }
    }
    payload.update(
        {
            "prototypes": prototypes_int8.astype(np.float32),
            "prototypes_int8": prototypes_int8.astype(np.int8),
            "prototype_parent": prototype_parent.astype(np.int64),
            "prototype_subclass": np.concatenate(
                [
                    np.asarray(base.get("prototype_subclass", np.full(base_count, -1)), dtype=np.int64),
                    np.full(len(accepted), -1, dtype=np.int64),
                ]
            ),
            "prototype_cluster": np.concatenate(
                [
                    np.asarray(base.get("prototype_cluster", np.arange(base_count)), dtype=np.int64),
                    np.arange(-5000000, -5000000 - len(accepted), -1, dtype=np.int64),
                ]
            ),
            "prototype_sample_index": np.concatenate(
                [
                    np.asarray(base.get("prototype_sample_index", np.full(base_count, -1)), dtype=np.int64),
                    np.asarray([row["sample_index"] for row in accepted], dtype=np.int64),
                ]
            ),
            "prototype_view_label": np.concatenate(
                [
                    np.asarray(base.get("prototype_view_label", np.asarray([""] * base_count))).astype(str),
                    np.asarray([f"{row['view_label']}__{row['perturb']}" for row in accepted]).astype(str),
                ]
            ),
            "prototype_source_kind": np.concatenate(
                [
                    np.asarray(base.get("prototype_source_kind", np.asarray(["base"] * base_count))).astype(str),
                    np.asarray([f"highstress_holdout_{args.name}"] * len(accepted)).astype(str),
                ]
            ),
            "pred": final_normal_pred.astype(np.int64),
            "int8_pred": final_normal_pred.astype(np.int64),
            "margin": final_normal_margin.astype(np.float32),
            "int8_margin": final_normal_margin.astype(np.int64),
            "feature_source": np.asarray(f"parent_logits_highstress_holdout_{args.name}"),
            "tie_break_policy": np.asarray("argmin_parent_order"),
        }
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_npz = args.output_dir / f"{args.name}_params.npz"
    np.savez_compressed(output_npz, **payload)
    write_csv(args.output_dir / f"{args.name}_accepted.csv", accepted)

    split_masks = {
        "all": np.ones(len(rows), dtype=bool),
        "selection": selection_mask,
    }
    if eval_perturbs is not None:
        split_masks["eval"] = eval_mask
    split_rows = split_summary_rows(
        rows=rows,
        stress_parent=stress_parent,
        base_pred=stress_pred,
        final_pred=final_stress_pred,
        split_masks=split_masks,
    )
    write_csv(args.output_dir / f"{args.name}_split_summary.csv", split_rows)
    summary = {
        "name": args.name,
        "output_npz": str(output_npz),
        "base_params_npz": str(args.base_params_npz),
        "stress_events_csv": str(args.stress_events_csv),
        "selection_perturbs": sorted(selection_perturbs) if selection_perturbs is not None else "all",
        "eval_perturbs": sorted(eval_perturbs) if eval_perturbs is not None else "",
        "base_prototypes": int(base_count),
        "accepted_prototypes": int(len(accepted)),
        "prototype_count": int(len(prototypes_int8)),
        "normal_rows": int(len(normal_parent)),
        "normal_wrong": int(np.sum(final_normal_pred != normal_parent)),
        "normal_margin_min": int(np.min(final_normal_margin)),
        "normal_low_margin_le8": int(np.sum(final_normal_margin <= 8)),
        "estimated_distance_macs": int(len(prototypes_int8) * prototypes_int8.shape[1]),
        "split_summary": split_rows,
    }
    (args.output_dir / f"{args.name}_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    if args.train_config and args.train_config.exists():
        shutil.copy2(args.train_config, args.output_dir / "train_config.json")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Build replay-gated V8 high-stress boundary repair prototype tables.")
    parser.add_argument("--base-params-npz", type=Path, required=True)
    parser.add_argument("--stress-events-csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--selection-perturbs", default="")
    parser.add_argument("--eval-perturbs", default="")
    parser.add_argument("--control-safety-perturbs", default="")
    parser.add_argument("--train-config", type=Path, default=None)
    parser.add_argument("--max-normal-margin-bucket", type=int, default=8)
    parser.add_argument("--no-increase-normal-low-margin", action="store_true")
    parser.add_argument("--no-increase-control", action="store_true")
    args = parser.parse_args()
    print(json.dumps(build_repair(args), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
