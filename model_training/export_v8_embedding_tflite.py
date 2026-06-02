import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf

import train_tiny32_v5_visual_subclass_scan as train
from train_v8_end_to_end_embedding import build_embedding_model, parse_filters


RECOMMENDED_OPS = {
    "SPACE_TO_DEPTH",
    "CONV_2D",
    "DEPTHWISE_CONV_2D",
    "MAX_POOL_2D",
    "AVERAGE_POOL_2D",
    "MEAN",
    "FULLY_CONNECTED",
    "RELU",
    "RELU6",
    "RESHAPE",
    "QUANTIZE",
    "DEQUANTIZE",
    "DELEGATE",
}


def load_embedding_model(model_path: Path) -> tf.keras.Model:
    config = json.loads((model_path.parent / "train_config.json").read_text(encoding="utf-8"))
    model = build_embedding_model(
        filters=parse_filters(",".join(str(x) for x in config["filters"])),
        embedding_dim=int(config["embedding_dim"]),
        learning_rate=float(config.get("learning_rate", 0.0015)),
        l2=float(config.get("l2", 1.0e-4)),
        dropout=float(config.get("dropout", 0.0)),
        first_kernel=int(config.get("first_kernel", 3)),
        embedding_output_mode=str(config.get("embedding_output_mode", "raw")),
        backbone_architecture=str(config.get("backbone_architecture", "spacetodepth_conv")),
        activation=str(config.get("activation", "relu6")),
        pool=str(config.get("pool", "max")),
        extra_conv=bool(config.get("extra_conv", False)),
    )
    model.load_weights(model_path)
    return model


def representative_dataset(dataset_dir: Path, limit: int):
    x, *_rest = train.load_dataset_v5(dataset_dir)
    x = x.astype(np.float32)[:limit]

    def gen():
        for item in x:
            yield [item[None, ...]]

    return gen


def op_names(path: Path) -> list[str]:
    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()
    return [str(item["op_name"]) for item in interpreter._get_ops_details()]  # noqa: SLF001


def convert_float(model: tf.keras.Model, output_path: Path) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    data = converter.convert()
    output_path.write_bytes(data)
    return len(data)


def convert_int8(model: tf.keras.Model, output_path: Path, dataset_dir: Path, representative_limit: int) -> int:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset(dataset_dir, representative_limit)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    data = converter.convert()
    output_path.write_bytes(data)
    return len(data)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export a trained V8 embedding model to TFLite and record actual op names.")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--representative-limit", type=int, default=304)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    model = load_embedding_model(args.model)
    float_path = args.output_dir / "embedding_float.tflite"
    int8_path = args.output_dir / "embedding_int8.tflite"
    summary: dict[str, Any] = {
        "model": str(args.model),
        "float_tflite": str(float_path),
        "int8_tflite": str(int8_path),
        "float_bytes": convert_float(model, float_path),
        "int8_bytes": convert_int8(model, int8_path, args.dataset_dir, args.representative_limit),
    }
    summary["float_ops"] = op_names(float_path)
    summary["int8_ops"] = op_names(int8_path)
    summary["float_unique_ops"] = sorted(set(summary["float_ops"]))
    summary["int8_unique_ops"] = sorted(set(summary["int8_ops"]))
    summary["float_non_recommended_ops"] = [op for op in summary["float_unique_ops"] if op not in RECOMMENDED_OPS]
    summary["int8_non_recommended_ops"] = [op for op in summary["int8_unique_ops"] if op not in RECOMMENDED_OPS]
    (args.output_dir / "tflite_export_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
