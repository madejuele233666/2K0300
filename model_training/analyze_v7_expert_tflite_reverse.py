import argparse
import csv
import json
import os
from pathlib import Path

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import tensorflow as tf

import train_tiny32_v5_visual_subclass_scan as train


PARENT_NAMES = train.PARENT_NAMES


def dequantize(value: np.ndarray, detail: dict[str, object]) -> np.ndarray:
    params = detail.get("quantization_parameters", {})
    scales = np.asarray(params.get("scales", []), dtype=np.float32)
    zero_points = np.asarray(params.get("zero_points", []), dtype=np.int32)
    qdim = int(params.get("quantized_dimension", 0) or 0)
    if scales.size == 0:
        scale, zero = detail.get("quantization", (0.0, 0))
        if not scale:
            return value.astype(np.float32)
        return (value.astype(np.float32) - float(zero)) * float(scale)
    if scales.size == 1:
        return (value.astype(np.float32) - float(zero_points[0])) * float(scales[0])
    shape = [1] * value.ndim
    shape[qdim] = scales.size
    return (value.astype(np.float32) - zero_points.reshape(shape)) * scales.reshape(shape)


class TfliteReverse:
    def __init__(self, path: Path):
        self.path = path
        self.interpreter = tf.lite.Interpreter(model_path=str(path), experimental_preserve_all_tensors=True)
        self.interpreter.allocate_tensors()
        self.input_detail = self.interpreter.get_input_details()[0]
        self.output_detail = self.interpreter.get_output_details()[0]
        self.details = {int(item["index"]): item for item in self.interpreter.get_tensor_details()}
        self.weight_index = self._find_parent_weight_index()
        self.bias_index = self._find_parent_bias_index()
        self.parent_weight = dequantize(self.interpreter.get_tensor(self.weight_index), self.details[self.weight_index])
        self.parent_bias = dequantize(self.interpreter.get_tensor(self.bias_index), self.details[self.bias_index])
        if self.parent_weight.shape[0] == len(PARENT_NAMES):
            pass
        elif self.parent_weight.shape[-1] == len(PARENT_NAMES):
            self.parent_weight = self.parent_weight.T
        else:
            raise ValueError(f"unexpected parent weight shape: {self.parent_weight.shape}")
        self.gap_index = self._find_gap_index()
        self.logit_index = self._find_logit_index()

    def _find_parent_weight_index(self) -> int:
        candidates = []
        for index, detail in self.details.items():
            shape = tuple(int(v) for v in detail["shape"])
            name = str(detail["name"])
            if "parent_logits" in name and len(shape) == 2 and len(PARENT_NAMES) in shape:
                candidates.append(index)
        if not candidates:
            raise ValueError(f"parent weight tensor not found in {self.path}")
        return candidates[0]

    def _find_parent_bias_index(self) -> int:
        for index, detail in self.details.items():
            shape = tuple(int(v) for v in detail["shape"])
            name = str(detail["name"])
            if "parent_logits" in name and "BiasAdd" in name and shape == (len(PARENT_NAMES),):
                return index
        raise ValueError(f"parent bias tensor not found in {self.path}")

    def _find_gap_index(self) -> int:
        candidates = []
        for index, detail in self.details.items():
            shape = tuple(int(v) for v in detail["shape"])
            name = str(detail["name"])
            if "/gap" in name and len(shape) == 2 and shape[-1] == self.parent_weight.shape[1]:
                candidates.append(index)
        if not candidates:
            raise ValueError(f"gap tensor not found in {self.path}")
        return candidates[-1]

    def _find_logit_index(self) -> int:
        candidates = []
        for index, detail in self.details.items():
            shape = tuple(int(v) for v in detail["shape"])
            name = str(detail["name"])
            if "parent_logits" in name and "MatMul" in name and len(shape) == 2 and shape[-1] == len(PARENT_NAMES):
                candidates.append(index)
        if not candidates:
            return int(self.output_detail["index"])
        return candidates[-1]

    def invoke_one(self, image: np.ndarray) -> dict[str, np.ndarray | int]:
        scale, zero = self.input_detail["quantization"]
        q = (image[None, ...].astype(np.float32) / scale + zero).astype(self.input_detail["dtype"])
        self.interpreter.set_tensor(int(self.input_detail["index"]), q)
        self.interpreter.invoke()
        output = dequantize(self.interpreter.get_tensor(int(self.output_detail["index"])), self.output_detail)[0]
        logits = dequantize(self.interpreter.get_tensor(self.logit_index), self.details[self.logit_index])[0]
        gap = dequantize(self.interpreter.get_tensor(self.gap_index), self.details[self.gap_index])[0]
        return {
            "pred": int(np.argmax(output)),
            "output": output,
            "logits": logits,
            "gap": gap,
        }

    def infer_dataset(self, x: np.ndarray) -> dict[str, np.ndarray]:
        preds, outputs, logits, gaps = [], [], [], []
        for image in x:
            item = self.invoke_one(image)
            preds.append(item["pred"])
            outputs.append(item["output"])
            logits.append(item["logits"])
            gaps.append(item["gap"])
        return {
            "pred": np.asarray(preds, dtype=np.int64),
            "output": np.asarray(outputs, dtype=np.float32),
            "logits": np.asarray(logits, dtype=np.float32),
            "gap": np.asarray(gaps, dtype=np.float32),
        }


def margin_rows(name: str, paths: list[str], y_parent: np.ndarray, y_sub: np.ndarray, pred: np.ndarray, logits: np.ndarray, groups: dict[str, np.ndarray]) -> list[dict[str, object]]:
    rows = []
    sorted_logits = np.sort(logits, axis=1)
    pred_margin = sorted_logits[:, -1] - sorted_logits[:, -2]
    true_margin = np.zeros(len(paths), dtype=np.float32)
    for i, true in enumerate(y_parent.tolist()):
        others = [j for j in range(len(PARENT_NAMES)) if j != true]
        true_margin[i] = float(logits[i, true] - np.max(logits[i, others]))
    for i, path in enumerate(paths):
        row = {
            "model": name,
            "index": i,
            "path": path,
            "file": Path(path).name,
            "visual": train.VISUAL_CLASS_NAMES[int(y_sub[i])],
            "parent": PARENT_NAMES[int(y_parent[i])],
            "pred": PARENT_NAMES[int(pred[i])],
            "correct": bool(pred[i] == y_parent[i]),
            "pred_margin": float(pred_margin[i]),
            "true_margin": float(true_margin[i]),
        }
        for group_name, mask in groups.items():
            row[group_name] = bool(mask[i])
        rows.append(row)
    return rows


def boundary_channel_rows(model_name: str, analyzer: TfliteReverse, gaps: np.ndarray) -> list[dict[str, object]]:
    rows = []
    mean_gap = np.mean(gaps, axis=0)
    for i, left in enumerate(PARENT_NAMES):
        for j, right in enumerate(PARENT_NAMES):
            if i >= j:
                continue
            boundary = analyzer.parent_weight[i] - analyzer.parent_weight[j]
            contribution = mean_gap * boundary
            order = np.argsort(np.abs(contribution))[::-1]
            for rank, channel in enumerate(order[:12], start=1):
                rows.append(
                    {
                        "model": model_name,
                        "boundary": f"{left}_vs_{right}",
                        "rank": rank,
                        "channel": int(channel),
                        "weight_delta": float(boundary[channel]),
                        "mean_gap": float(mean_gap[channel]),
                        "mean_contribution": float(contribution[channel]),
                    }
                )
    return rows


def group_summary(groups: dict[str, np.ndarray], pred: np.ndarray, y_parent: np.ndarray, true_margin: np.ndarray) -> dict[str, object]:
    out = {}
    for name, mask in groups.items():
        idx = np.where(mask)[0]
        if idx.size == 0:
            continue
        out[name] = {
            "count": int(idx.size),
            "correct": int(np.sum(pred[idx] == y_parent[idx])),
            "accuracy": float(np.mean(pred[idx] == y_parent[idx])),
            "true_margin_mean": float(np.mean(true_margin[idx])),
            "true_margin_min": float(np.min(true_margin[idx])),
        }
    return out


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    keys = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Reverse-analyze tiny32 expert TFLite weights, logits, GAP activations, and group margins.")
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--old-tflite", type=Path, required=True)
    parser.add_argument("--rescue-tflite", type=Path)
    parser.add_argument("--extra-tflite", type=Path, action="append", default=[])
    parser.add_argument("--extra-name", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    x, y_sub, y_parent, paths, _rows = train.load_dataset_v5(args.dataset_dir)
    models: list[tuple[str, Path]] = [("old", args.old_tflite)]
    if args.rescue_tflite is not None:
        models.append(("rescue", args.rescue_tflite))
    for index, path in enumerate(args.extra_tflite):
        name = args.extra_name[index] if index < len(args.extra_name) else f"extra_{index}"
        models.append((name, path))

    inferred = {}
    analyzers = {}
    for name, path in models:
        analyzers[name] = TfliteReverse(path)
        inferred[name] = analyzers[name].infer_dataset(x)

    old_pred = inferred["old"]["pred"]
    rescue_pred = inferred["rescue"]["pred"] if "rescue" in inferred else old_pred
    groups = {
        "stable_both_correct": (old_pred == y_parent) & (rescue_pred == y_parent),
        "preserve_old_correct_rescue_wrong": (old_pred == y_parent) & (rescue_pred != y_parent),
        "rescue_old_wrong_rescue_correct": (old_pred != y_parent) & (rescue_pred == y_parent),
        "both_wrong": (old_pred != y_parent) & (rescue_pred != y_parent),
        "hard": np.asarray([Path(path).name in train.HARD_CLEAN_BASENAMES for path in paths], dtype=bool),
        "c4": y_sub == train.C4_SUBCLASS_INDEX,
    }

    sample_rows = []
    channel_rows = []
    summary = {"models": {}, "group_counts": {name: int(np.sum(mask)) for name, mask in groups.items()}}
    for name, _path in models:
        logits = inferred[name]["logits"]
        pred = inferred[name]["pred"]
        true_margin = np.asarray(
            [logits[i, int(y_parent[i])] - np.max(np.delete(logits[i], int(y_parent[i]))) for i in range(len(y_parent))],
            dtype=np.float32,
        )
        sample_rows.extend(margin_rows(name, paths, y_parent, y_sub, pred, logits, groups))
        channel_rows.extend(boundary_channel_rows(name, analyzers[name], inferred[name]["gap"]))
        summary["models"][name] = {
            "tflite": str(_path),
            "all_correct": int(np.sum(pred == y_parent)),
            "all_total": int(len(y_parent)),
            "all_accuracy": float(np.mean(pred == y_parent)),
            "parent_bias": [float(v) for v in analyzers[name].parent_bias.tolist()],
            "parent_weight_shape": list(analyzers[name].parent_weight.shape),
            "dead_gap_channels": int(np.sum(np.std(inferred[name]["gap"], axis=0) < 1.0e-6)),
            "group_summary": group_summary(groups, pred, y_parent, true_margin),
            "wrong_files": [Path(paths[i]).name for i in np.where(pred != y_parent)[0].tolist()],
        }

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "sample_margins.csv", sample_rows)
    write_csv(args.output_dir / "boundary_top_channels.csv", channel_rows)
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
