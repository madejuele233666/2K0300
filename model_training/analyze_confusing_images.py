import argparse
import csv
import json
from collections import Counter
from pathlib import Path

import numpy as np
import tensorflow as tf
from PIL import Image, ImageDraw

from train_tiny32_sixclass_scan import (
    PARENT_NAMES,
    SUBCLASS_NAMES,
    SUBCLASS_TO_PARENT,
    load_dataset,
)


def predict_one_model(model_path: Path, x: np.ndarray) -> np.ndarray:
    interpreter = tf.lite.Interpreter(model_path=str(model_path), num_threads=1)
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    preds = []
    for image in x:
        data = image[None, ...].astype(np.float32)
        if input_detail["dtype"] in (np.int8, np.uint8, np.int16):
            scale, zero = input_detail["quantization"]
            if scale > 0:
                data = data / scale + zero
        interpreter.set_tensor(input_detail["index"], data.astype(input_detail["dtype"]))
        interpreter.invoke()
        raw = interpreter.get_tensor(output_detail["index"])[0]
        if output_detail["dtype"] in (np.int8, np.uint8, np.int16):
            scale, zero = output_detail["quantization"]
            raw = (raw.astype(np.float32) - zero) * scale if scale > 0 else raw.astype(np.float32)
        preds.append(int(np.argmax(raw)))
    return np.asarray(preds, dtype=np.int64)


def cohort_model_paths(root: Path, cohort: str) -> list[Path]:
    if cohort == "all_scan_best":
        return sorted(root.glob("stage*/shard_*/runs/*/sixclass_best_mild_int8.tflite")) + sorted(
            (root / "final").glob("shard_*/runs/*/sixclass_best_mild_int8.tflite")
        )
    if cohort == "strong_stage2_final_best":
        return sorted((root / "stage2").glob("shard_*/runs/*/sixclass_best_mild_int8.tflite")) + sorted(
            (root / "final").glob("shard_*/runs/*/sixclass_best_mild_int8.tflite")
        )
    if cohort == "final_full":
        return sorted((root / "final").glob("shard_*/runs/*/sixclass_full_mild_int8.tflite"))
    if cohort == "deployment_reference_full":
        return [
            Path("experiments/v4_retest_full_shard1_20260508_1737/runs/fast_x0_lr286_seed20260901/sixclass_full_mild_int8.tflite"),
            Path("experiments/v4_stability_fine_20260508_1832/final/shard_1/runs/stab007_seed20261201/sixclass_full_mild_int8.tflite"),
        ] + sorted((root / "final").glob("shard_*/runs/*/sixclass_full_mild_int8.tflite"))
    raise ValueError(f"unknown cohort: {cohort}")


def summarize_cohort(
    cohort: str,
    models: list[Path],
    x: np.ndarray,
    y_sub: np.ndarray,
    y_parent: np.ndarray,
    paths: list[str],
) -> list[dict[str, object]]:
    parent_true_names = [PARENT_NAMES[int(value)] for value in y_parent]
    sub_true_names = [SUBCLASS_NAMES[int(value)] for value in y_sub]
    sub_pred_counts = [Counter() for _ in paths]
    parent_pred_counts = [Counter() for _ in paths]

    for index, model_path in enumerate(models, start=1):
        pred_sub = predict_one_model(model_path, x)
        pred_parent = SUBCLASS_TO_PARENT[pred_sub]
        for sample_index, (subclass_id, parent_id) in enumerate(zip(pred_sub.tolist(), pred_parent.tolist())):
            sub_pred_counts[sample_index][SUBCLASS_NAMES[int(subclass_id)]] += 1
            parent_pred_counts[sample_index][PARENT_NAMES[int(parent_id)]] += 1
        if index % 50 == 0 or index == len(models):
            print(f"{cohort} {index}/{len(models)}", flush=True)

    rows = []
    for sample_index, path in enumerate(paths):
        parent_total = sum(parent_pred_counts[sample_index].values()) or 1
        sub_total = sum(sub_pred_counts[sample_index].values()) or 1
        parent_correct = parent_pred_counts[sample_index][parent_true_names[sample_index]]
        sub_correct = sub_pred_counts[sample_index][sub_true_names[sample_index]]

        wrong_parent_counts = parent_pred_counts[sample_index].copy()
        wrong_parent_counts.pop(parent_true_names[sample_index], None)
        wrong_sub_counts = sub_pred_counts[sample_index].copy()
        wrong_sub_counts.pop(sub_true_names[sample_index], None)
        top_wrong_parent, top_wrong_parent_count = ("", 0) if not wrong_parent_counts else wrong_parent_counts.most_common(1)[0]
        top_wrong_sub, top_wrong_sub_count = ("", 0) if not wrong_sub_counts else wrong_sub_counts.most_common(1)[0]

        rows.append(
            {
                "cohort": cohort,
                "models": len(models),
                "path": path,
                "true_parent": parent_true_names[sample_index],
                "true_subclass": sub_true_names[sample_index],
                "parent_wrong_rate": 1.0 - parent_correct / parent_total,
                "subclass_wrong_rate": 1.0 - sub_correct / sub_total,
                "top_wrong_parent": top_wrong_parent,
                "top_wrong_parent_rate": top_wrong_parent_count / parent_total,
                "top_wrong_subclass": top_wrong_sub,
                "top_wrong_subclass_rate": top_wrong_sub_count / sub_total,
                "parent_votes": dict(parent_pred_counts[sample_index]),
                "subclass_votes": dict(sub_pred_counts[sample_index]),
            }
        )
    rows.sort(
        key=lambda row: (
            float(row["parent_wrong_rate"]),
            float(row["subclass_wrong_rate"]),
            float(row["top_wrong_parent_rate"]),
        ),
        reverse=True,
    )
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = [
        "cohort",
        "models",
        "path",
        "true_parent",
        "true_subclass",
        "parent_wrong_rate",
        "subclass_wrong_rate",
        "top_wrong_parent",
        "top_wrong_parent_rate",
        "top_wrong_subclass",
        "top_wrong_subclass_rate",
        "parent_votes",
        "subclass_votes",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_contact_sheet(rows: list[dict[str, object]], path: Path, limit: int = 24) -> None:
    selected = rows[:limit]
    thumb = 96
    pad = 18
    label_h = 54
    cols = 6
    rows_n = (len(selected) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (thumb + pad) + pad, rows_n * (thumb + label_h + pad) + pad), "white")
    draw = ImageDraw.Draw(sheet)
    for index, row in enumerate(selected):
        col = index % cols
        row_index = index // cols
        x0 = pad + col * (thumb + pad)
        y0 = pad + row_index * (thumb + label_h + pad)
        image = Image.open(str(row["path"])).convert("RGB").resize((thumb, thumb), Image.Resampling.NEAREST)
        sheet.paste(image, (x0, y0))
        label = (
            f"{index + 1}. {Path(str(row['path'])).name}\n"
            f"Perr {float(row['parent_wrong_rate']):.2f} Sub {float(row['subclass_wrong_rate']):.2f}\n"
            f"{row['true_parent']}->{row['top_wrong_parent']}"
        )
        draw.text((x0, y0 + thumb + 2), label, fill=(0, 0, 0))
    sheet.save(path, quality=92)


def main() -> None:
    parser = argparse.ArgumentParser(description="Find dataset images repeatedly confused by trained TFLite models.")
    parser.add_argument("--run-id", default="v4_stress_directed_20260508_215328")
    parser.add_argument("--dataset-dir", default="dataset")
    parser.add_argument(
        "--cohorts",
        default="all_scan_best,strong_stage2_final_best,final_full,deployment_reference_full",
    )
    args = parser.parse_args()

    root = Path("experiments") / args.run_id
    out_dir = root / "confusing_images"
    out_dir.mkdir(parents=True, exist_ok=True)
    x, y_sub, y_parent, paths_raw = load_dataset(Path(args.dataset_dir))
    paths = [str(path) for path in paths_raw]
    all_rows = []

    for cohort in [part.strip() for part in args.cohorts.split(",") if part.strip()]:
        models = [path for path in cohort_model_paths(root, cohort) if path.exists()]
        rows = summarize_cohort(cohort, models, x, y_sub, y_parent, paths)
        all_rows.extend(rows)
        csv_path = out_dir / f"{cohort}_clean_confusing_images.csv"
        write_csv(csv_path, rows)
        print(f"WROTE {csv_path}")
        print(f"== {cohort} models={len(models)}")
        for row in rows[:20]:
            print(
                f"{float(row['parent_wrong_rate']):.3f} sub={float(row['subclass_wrong_rate']):.3f} "
                f"{row['path']} true={row['true_parent']}/{row['true_subclass']} "
                f"wrongP={row['top_wrong_parent']}:{float(row['top_wrong_parent_rate']):.3f} "
                f"wrongS={row['top_wrong_subclass']}:{float(row['top_wrong_subclass_rate']):.3f}"
            )
        if cohort == "strong_stage2_final_best":
            contact_path = out_dir / "strong_stage2_top24_clean_confusing_contact.jpg"
            write_contact_sheet(rows, contact_path)
            print(f"WROTE {contact_path}")

    json_path = out_dir / "clean_confusing_images_summary.json"
    json_path.write_text(json.dumps({"rows": all_rows}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"WROTE {json_path}")


if __name__ == "__main__":
    main()
