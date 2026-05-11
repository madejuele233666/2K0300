#!/usr/bin/env python3
"""Generate removable C headers for board TFLM benchmark model assets."""

from __future__ import annotations

import argparse
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path


DEFAULT_TFLITE_CANDIDATES = [
    "model_training/experiments/sixclass_coarse_20260504_103505/sixclass_full_int8.tflite",
    "model_training/experiments/sixclass_coarse_20260504_103505/sixclass_best_int8.tflite",
    "model_training/experiments/observed_best/tiny32_gray_deep_20260503_192439/tiny32_gray_deep_full_int8.tflite",
]

SYNTHETIC_DIRNAME = "synthetic_models"


@dataclass(frozen=True)
class ModelAsset:
    path: Path
    name: str
    category: str
    expected_ops: str


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def c_identifier(name: str, index: int) -> str:
    stem = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if not stem or stem[0].isdigit():
        stem = f"model_{stem}"
    return f"kBenchmarkModel_{index}_{stem}"


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def format_bytes(data: bytes) -> str:
    chunks = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        chunks.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(chunks)


def discover_sources(repo_root: Path, explicit: list[str], max_default_models: int) -> list[ModelAsset]:
    sources: list[ModelAsset] = []
    for raw in explicit:
        path = Path(raw)
        if not path.is_absolute():
            path = repo_root / path
        if path.is_file():
            path = path.resolve()
            sources.append(ModelAsset(path, path.stem, "modelbench", "from_flatbuffer"))
    if sources:
        return sources

    for raw in DEFAULT_TFLITE_CANDIDATES:
        path = repo_root / raw
        if path.is_file():
            path = path.resolve()
            sources.append(ModelAsset(path, path.stem, "modelbench", "from_flatbuffer"))
        if len(sources) >= max_default_models:
            break
    return sources


def conv_block(tf, layers, x, channels: int, *, depthwise: bool, activation: str, pool: str | None, stride: int = 1):
    if depthwise:
        x = layers.DepthwiseConv2D(3, strides=stride, padding="same", use_bias=False)(x)
        x = layers.Conv2D(channels, 1, padding="same", use_bias=False)(x)
    else:
        x = layers.Conv2D(channels, 3, strides=stride, padding="same", use_bias=False)(x)
    if activation == "relu6":
        x = layers.ReLU(max_value=6.0)(x)
    elif activation == "hard_swish":
        x = layers.Activation(tf.keras.activations.hard_silu)(x)
    else:
        x = layers.ReLU()(x)
    if pool == "max":
        x = layers.MaxPooling2D(pool_size=2)(x)
    elif pool == "avg":
        x = layers.AveragePooling2D(pool_size=2)(x)
    return x


def make_classifier_model(tf, layers, *, name: str, channels: tuple[int, ...], depthwise: bool = False, pool: str = "max", activation: str = "relu", space_to_depth: bool = False, stride_first: bool = False):
    inputs = layers.Input(shape=(32, 32, 1), name="image")
    x = inputs
    if space_to_depth:
        x = layers.Lambda(lambda t: tf.nn.space_to_depth(t, 2), output_shape=(16, 16, 4), name="space_to_depth")(x)
    for index, channels_i in enumerate(channels):
        block_pool = pool if index < 2 and not stride_first else None
        stride = 2 if stride_first and index < 2 else 1
        x = conv_block(tf, layers, x, channels_i, depthwise=depthwise, activation=activation, pool=block_pool, stride=stride)
    x = layers.GlobalAveragePooling2D(name="gap")(x)
    outputs = layers.Dense(6, activation="softmax", name="class_logits")(x)
    return tf.keras.Model(inputs, outputs, name=name)


def make_micro_model(tf, layers, name: str):
    inputs = layers.Input(shape=(32, 32, 1), name="image")
    if name == "micro_conv2d_c8_k3":
        outputs = layers.Conv2D(8, 3, padding="same", use_bias=False)(inputs)
        expected_ops = "CONV_2D"
    elif name == "micro_conv2d_c16_k5":
        outputs = layers.Conv2D(16, 5, padding="same", use_bias=False)(inputs)
        expected_ops = "CONV_2D"
    elif name == "micro_depthwise_c1_k3":
        outputs = layers.DepthwiseConv2D(3, padding="same", use_bias=False)(inputs)
        expected_ops = "DEPTHWISE_CONV_2D"
    elif name == "micro_depthwise_pointwise_c16":
        x = layers.DepthwiseConv2D(3, padding="same", use_bias=False)(inputs)
        outputs = layers.Conv2D(16, 1, padding="same", use_bias=False)(x)
        expected_ops = "DEPTHWISE_CONV_2D|CONV_2D"
    elif name == "micro_maxpool":
        outputs = layers.MaxPooling2D(pool_size=2)(inputs)
        expected_ops = "MAX_POOL_2D"
    elif name == "micro_avgpool":
        outputs = layers.AveragePooling2D(pool_size=2)(inputs)
        expected_ops = "AVERAGE_POOL_2D"
    elif name == "micro_mean":
        outputs = layers.GlobalAveragePooling2D()(inputs)
        expected_ops = "MEAN"
    elif name == "micro_softmax":
        flat = layers.Flatten()(inputs)
        logits = layers.Dense(16, use_bias=False)(flat)
        outputs = layers.Softmax()(logits)
        expected_ops = "FULLY_CONNECTED|SOFTMAX"
    elif name == "micro_fully_connected":
        flat = layers.Flatten()(inputs)
        outputs = layers.Dense(64, use_bias=False)(flat)
        expected_ops = "FULLY_CONNECTED"
    elif name == "micro_relu":
        outputs = layers.ReLU()(inputs)
        expected_ops = "RELU"
    elif name == "micro_relu6":
        outputs = layers.ReLU(max_value=6.0)(inputs)
        expected_ops = "RELU6"
    elif name == "micro_hard_swish":
        outputs = layers.Activation(tf.keras.activations.hard_silu)(inputs)
        expected_ops = "HARD_SWISH"
    elif name == "micro_add":
        outputs = layers.Add()([inputs, inputs])
        expected_ops = "ADD"
    elif name == "micro_mul":
        outputs = layers.Multiply()([inputs, inputs])
        expected_ops = "MUL"
    elif name == "micro_concat":
        outputs = layers.Concatenate(axis=-1)([inputs, inputs])
        expected_ops = "CONCATENATION"
    elif name == "micro_space_to_depth":
        outputs = layers.Lambda(lambda t: tf.nn.space_to_depth(t, 2), output_shape=(16, 16, 4))(inputs)
        expected_ops = "SPACE_TO_DEPTH"
    elif name == "micro_reshape":
        outputs = layers.Reshape((1024,))(inputs)
        expected_ops = "RESHAPE"
    else:
        raise ValueError(f"unknown micro model: {name}")
    return tf.keras.Model(inputs, outputs, name=name), expected_ops


def representative_dataset(np, shape: tuple[int, ...]):
    rng = np.random.default_rng(20260506)
    for _ in range(16):
        yield [rng.normal(0.0, 0.5, size=(1, *shape)).astype("float32")]


def export_int8_tflite(tf, np, model, output_path: Path, input_shape: tuple[int, ...]) -> None:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: representative_dataset(np, input_shape)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    output_path.write_bytes(converter.convert())


def export_float_tflite(tf, func, input_specs, output_path: Path) -> None:
    concrete = tf.function(func, input_signature=input_specs, autograph=False).get_concrete_function()
    converter = tf.lite.TFLiteConverter.from_concrete_functions([concrete])
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]
    output_path.write_bytes(converter.convert())


def export_float_io_quantized_conv(tf, np, output_path: Path) -> None:
    inputs = tf.keras.layers.Input(shape=(8, 8, 1), name="image")
    outputs = tf.keras.layers.Conv2D(2, 3, padding="same", use_bias=False)(inputs)
    model = tf.keras.Model(inputs, outputs, name="op_quantize_dequantize_conv")
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    def rep():
        rng = np.random.default_rng(20260506)
        for _ in range(8):
            yield [rng.normal(0.0, 0.5, size=(1, 8, 8, 1)).astype("float32")]

    converter.representative_dataset = rep
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    output_path.write_bytes(converter.convert())


def inspect_tflite_ops(tf, path: Path) -> str:
    try:
        interpreter = tf.lite.Interpreter(model_path=str(path))
        return "|".join(detail["op_name"] for detail in interpreter._get_ops_details())
    except Exception:
        return "from_flatbuffer"


def schema_buffer(schema, np, data=None, dtype="float32"):
    buffer = schema.BufferT()
    if data is not None:
        buffer.data = np.frombuffer(np.asarray(data, dtype=dtype).tobytes(), dtype=np.uint8).tolist()
    return buffer


def schema_tensor(schema, name: str, shape: list[int], tensor_type: int, buffer: int = 0, is_variable: bool = False):
    tensor = schema.TensorT()
    tensor.shape = shape
    tensor.type = tensor_type
    tensor.buffer = buffer
    tensor.name = name
    tensor.isVariable = is_variable
    return tensor


def write_manual_model(schema, flatbuffers, path: Path, model, arena_hint: int = 4096) -> None:
    builder = flatbuffers.Builder(arena_hint)
    offset = model.Pack(builder)
    builder.Finish(offset, file_identifier=b"TFL3")
    path.write_bytes(bytes(builder.Output()))


def make_single_operator_model(schema, np, flatbuffers, *, op_code: int, op_name: str, tensors, inputs, outputs, operator_inputs, operator_outputs, options_type=None, options=None) -> bytes:
    model = schema.ModelT()
    model.version = 3
    model.description = f"manual {op_name}"
    model.buffers = [schema_buffer(schema, np)]
    op_code_t = schema.OperatorCodeT()
    op_code_t.builtinCode = op_code
    op_code_t.version = 1
    model.operatorCodes = [op_code_t]

    op = schema.OperatorT()
    op.opcodeIndex = 0
    op.inputs = operator_inputs
    op.outputs = operator_outputs
    op.builtinOptionsType = schema.BuiltinOptions.NONE if options_type is None else options_type
    op.builtinOptions = options

    subgraph = schema.SubGraphT()
    subgraph.name = "main"
    subgraph.tensors = tensors
    subgraph.inputs = inputs
    subgraph.outputs = outputs
    subgraph.operators = [op]
    model.subgraphs = [subgraph]

    builder = flatbuffers.Builder(4096)
    offset = model.Pack(builder)
    builder.Finish(offset, file_identifier=b"TFL3")
    return bytes(builder.Output())


def export_manual_exact_op_model(schema, np, flatbuffers, op_name: str, output_path: Path) -> None:
    tt = schema.TensorType
    bo = schema.BuiltinOperator
    opts = schema.BuiltinOptions

    def empty_buffer_model_bytes(op_code, tensors, inputs, outputs, operator_inputs, operator_outputs, options_type=None, options=None):
        return make_single_operator_model(
            schema,
            np,
            flatbuffers,
            op_code=op_code,
            op_name=op_name,
            tensors=tensors,
            inputs=inputs,
            outputs=outputs,
            operator_inputs=operator_inputs,
            operator_outputs=operator_outputs,
            options_type=options_type,
            options=options,
        )

    if op_name == "SHAPE":
        option = schema.ShapeOptionsT()
        option.outType = tt.INT32
        data = empty_buffer_model_bytes(
            bo.SHAPE,
            [
                schema_tensor(schema, "x", [1, 4, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "y", [4], tt.INT32),
            ],
            [0],
            [1],
            [0],
            [1],
            opts.ShapeOptions,
            option,
        )
    elif op_name == "SQUEEZE":
        option = schema.SqueezeOptionsT()
        option.squeezeDims = [1]
        data = empty_buffer_model_bytes(
            bo.SQUEEZE,
            [
                schema_tensor(schema, "x", [1, 1, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "y", [1, 4, 1], tt.FLOAT32),
            ],
            [0],
            [1],
            [0],
            [1],
            opts.SqueezeOptions,
            option,
        )
    elif op_name == "EXPAND_DIMS":
        buffers = [schema_buffer(schema, np), schema_buffer(schema, np, [1], "int32")]
        tensors = [
            schema_tensor(schema, "x", [1, 4, 1], tt.FLOAT32),
            schema_tensor(schema, "axis", [], tt.INT32, 1),
            schema_tensor(schema, "y", [1, 1, 4, 1], tt.FLOAT32),
        ]
        model = schema.ModelT()
        model.version = 3
        model.description = "manual EXPAND_DIMS"
        model.buffers = buffers
        opcode = schema.OperatorCodeT()
        opcode.builtinCode = bo.EXPAND_DIMS
        opcode.version = 1
        model.operatorCodes = [opcode]
        op = schema.OperatorT()
        op.opcodeIndex = 0
        op.inputs = [0, 1]
        op.outputs = [2]
        op.builtinOptionsType = opts.NONE
        subgraph = schema.SubGraphT()
        subgraph.name = "main"
        subgraph.tensors = tensors
        subgraph.inputs = [0]
        subgraph.outputs = [2]
        subgraph.operators = [op]
        model.subgraphs = [subgraph]
        builder = flatbuffers.Builder(4096)
        offset = model.Pack(builder)
        builder.Finish(offset, file_identifier=b"TFL3")
        data = bytes(builder.Output())
    elif op_name == "L2_POOL_2D":
        option = schema.Pool2DOptionsT()
        option.padding = schema.Padding.VALID
        option.strideW = 2
        option.strideH = 2
        option.filterWidth = 2
        option.filterHeight = 2
        option.fusedActivationFunction = schema.ActivationFunctionType.NONE
        data = empty_buffer_model_bytes(
            bo.L2_POOL_2D,
            [
                schema_tensor(schema, "x", [1, 4, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "y", [1, 2, 2, 1], tt.FLOAT32),
            ],
            [0],
            [1],
            [0],
            [1],
            opts.Pool2DOptions,
            option,
        )
    elif op_name == "ZEROS_LIKE":
        data = empty_buffer_model_bytes(
            bo.ZEROS_LIKE,
            [
                schema_tensor(schema, "x", [1, 4, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "y", [1, 4, 4, 1], tt.FLOAT32),
            ],
            [0],
            [1],
            [0],
            [1],
        )
    elif op_name == "SELECT_V2":
        option = schema.SelectV2OptionsT()
        data = empty_buffer_model_bytes(
            bo.SELECT_V2,
            [
                schema_tensor(schema, "condition", [1, 4, 4, 1], tt.BOOL),
                schema_tensor(schema, "x", [1, 4, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "y", [1, 4, 4, 1], tt.FLOAT32),
                schema_tensor(schema, "z", [1, 4, 4, 1], tt.FLOAT32),
            ],
            [0, 1, 2],
            [3],
            [0, 1, 2],
            [3],
            opts.SelectV2Options,
            option,
        )
    elif op_name == "PRELU":
        buffers = [schema_buffer(schema, np), schema_buffer(schema, np, [0.25], "float32")]
        tensors = [
            schema_tensor(schema, "x", [1, 4, 4, 1], tt.FLOAT32),
            schema_tensor(schema, "alpha", [1], tt.FLOAT32, 1),
            schema_tensor(schema, "y", [1, 4, 4, 1], tt.FLOAT32),
        ]
        data = make_single_operator_model(
            schema,
            np,
            flatbuffers,
            op_code=bo.PRELU,
            op_name=op_name,
            tensors=tensors,
            inputs=[0],
            outputs=[2],
            operator_inputs=[0, 1],
            operator_outputs=[2],
        )
        # make_single_operator_model creates a default empty buffer list, so rebuild with alpha.
        model = schema.ModelT()
        model.version = 3
        model.description = "manual PRELU"
        model.buffers = buffers
        opcode = schema.OperatorCodeT()
        opcode.builtinCode = bo.PRELU
        opcode.version = 1
        model.operatorCodes = [opcode]
        op = schema.OperatorT()
        op.opcodeIndex = 0
        op.inputs = [0, 1]
        op.outputs = [2]
        op.builtinOptionsType = opts.NONE
        subgraph = schema.SubGraphT()
        subgraph.name = "main"
        subgraph.tensors = tensors
        subgraph.inputs = [0]
        subgraph.outputs = [2]
        subgraph.operators = [op]
        model.subgraphs = [subgraph]
        builder = flatbuffers.Builder(4096)
        offset = model.Pack(builder)
        builder.Finish(offset, file_identifier=b"TFL3")
        data = bytes(builder.Output())
    elif op_name == "SVDF":
        model = schema.ModelT()
        model.version = 3
        model.description = "manual SVDF"
        model.buffers = [schema_buffer(schema, np)]
        tensors = [schema_tensor(schema, "x", [1, 3], tt.FLOAT32)]

        def add_const(name: str, values):
            model.buffers.append(schema_buffer(schema, np, values, "float32"))
            tensor_index = len(tensors)
            tensors.append(schema_tensor(schema, name, list(np.asarray(values).shape), tt.FLOAT32, len(model.buffers) - 1))
            return tensor_index

        def add_variable(name: str, shape: list[int]):
            tensor_index = len(tensors)
            tensors.append(schema_tensor(schema, name, shape, tt.FLOAT32, 0, True))
            return tensor_index

        rank = 1
        filters = 2
        memory = 4
        units = filters // rank
        weights_feature = add_const("weights_feature", np.full((filters, 3), 0.1, dtype=np.float32))
        weights_time = add_const("weights_time", np.full((filters, memory), 0.1, dtype=np.float32))
        bias = add_const("bias", np.zeros((units,), dtype=np.float32))
        state = add_variable("state", [1, filters * memory])
        output = len(tensors)
        tensors.append(schema_tensor(schema, "y", [1, units], tt.FLOAT32))
        opcode = schema.OperatorCodeT()
        opcode.builtinCode = bo.SVDF
        opcode.version = 1
        model.operatorCodes = [opcode]
        option = schema.SVDFOptionsT()
        option.rank = rank
        option.fusedActivationFunction = schema.ActivationFunctionType.NONE
        op = schema.OperatorT()
        op.opcodeIndex = 0
        op.inputs = [0, weights_feature, weights_time, bias, state]
        op.outputs = [output]
        op.builtinOptionsType = opts.SVDFOptions
        op.builtinOptions = option
        subgraph = schema.SubGraphT()
        subgraph.name = "main"
        subgraph.tensors = tensors
        subgraph.inputs = [0]
        subgraph.outputs = [output]
        subgraph.operators = [op]
        model.subgraphs = [subgraph]
        builder = flatbuffers.Builder(4096)
        offset = model.Pack(builder)
        builder.Finish(offset, file_identifier=b"TFL3")
        data = bytes(builder.Output())
    elif op_name == "UNIDIRECTIONAL_SEQUENCE_LSTM":
        model = schema.ModelT()
        model.version = 3
        model.description = "manual UNIDIRECTIONAL_SEQUENCE_LSTM"
        model.buffers = [schema_buffer(schema, np)]
        tensors = [schema_tensor(schema, "x", [1, 3, 4], tt.FLOAT32)]

        def add_const(name: str, values):
            model.buffers.append(schema_buffer(schema, np, values, "float32"))
            tensor_index = len(tensors)
            tensors.append(schema_tensor(schema, name, list(np.asarray(values).shape), tt.FLOAT32, len(model.buffers) - 1))
            return tensor_index

        def add_variable(name: str, shape: list[int]):
            tensor_index = len(tensors)
            tensors.append(schema_tensor(schema, name, shape, tt.FLOAT32, 0, True))
            return tensor_index

        state = 2
        input_size = 4
        w_i = -1
        w_f = add_const("w_f", np.full((state, input_size), 0.1, dtype=np.float32))
        w_c = add_const("w_c", np.full((state, input_size), 0.1, dtype=np.float32))
        w_o = add_const("w_o", np.full((state, input_size), 0.1, dtype=np.float32))
        r_i = -1
        r_f = add_const("r_f", np.full((state, state), 0.1, dtype=np.float32))
        r_c = add_const("r_c", np.full((state, state), 0.1, dtype=np.float32))
        r_o = add_const("r_o", np.full((state, state), 0.1, dtype=np.float32))
        b_i = -1
        b_f = add_const("b_f", np.zeros((state,), dtype=np.float32))
        b_c = add_const("b_c", np.zeros((state,), dtype=np.float32))
        b_o = add_const("b_o", np.zeros((state,), dtype=np.float32))
        out_state = add_variable("out_state", [1, state])
        cell_state = add_variable("cell_state", [1, state])
        output = len(tensors)
        tensors.append(schema_tensor(schema, "y", [1, 3, state], tt.FLOAT32))
        inputs = [
            0,
            w_i,
            w_f,
            w_c,
            w_o,
            r_i,
            r_f,
            r_c,
            r_o,
            -1,
            -1,
            -1,
            b_i,
            b_f,
            b_c,
            b_o,
            -1,
            -1,
            out_state,
            cell_state,
            -1,
            -1,
            -1,
            -1,
        ]
        opcode = schema.OperatorCodeT()
        opcode.builtinCode = bo.UNIDIRECTIONAL_SEQUENCE_LSTM
        opcode.version = 1
        model.operatorCodes = [opcode]
        option = schema.UnidirectionalSequenceLSTMOptionsT()
        option.fusedActivationFunction = schema.ActivationFunctionType.TANH
        option.cellClip = 0.0
        option.projClip = 0.0
        option.timeMajor = False
        op = schema.OperatorT()
        op.opcodeIndex = 0
        op.inputs = inputs
        op.outputs = [output]
        op.builtinOptionsType = opts.UnidirectionalSequenceLSTMOptions
        op.builtinOptions = option
        subgraph = schema.SubGraphT()
        subgraph.name = "main"
        subgraph.tensors = tensors
        subgraph.inputs = [0]
        subgraph.outputs = [output]
        subgraph.operators = [op]
        model.subgraphs = [subgraph]
        builder = flatbuffers.Builder(8192)
        offset = model.Pack(builder)
        builder.Finish(offset, file_identifier=b"TFL3")
        data = bytes(builder.Output())
    else:
        raise ValueError(f"unknown manual op model: {op_name}")

    output_path.write_bytes(data)


def generate_full_op_suite(tf, np, output_dir: Path) -> tuple[list[ModelAsset], list[str]]:
    from tensorflow.lite.python import schema_py_generated as schema
    import flatbuffers

    f32 = tf.float32
    i32 = tf.int32
    boolean = tf.bool
    notes: list[str] = []
    assets: list[ModelAsset] = []

    tf_specs = [
        ("op_cast", "CAST", lambda x: tf.cast(x, tf.int32), [tf.TensorSpec([1, 4], f32, name="x")]),
        ("op_pad", "PAD", lambda x: tf.pad(x, [[0, 0], [1, 1], [1, 1], [0, 0]]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_padv2", "PADV2", lambda x, v: tf.pad(x, [[0, 0], [1, 1], [1, 1], [0, 0]], constant_values=v[0]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x"), tf.TensorSpec([1], f32, name="v")]),
        ("op_maximum", "MAXIMUM", lambda x: tf.maximum(x, x * 0.5), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_minimum", "MINIMUM", lambda x: tf.minimum(x, x * 0.5), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_transpose", "TRANSPOSE", lambda x: tf.transpose(x, [0, 2, 1, 3]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_strided_slice", "STRIDED_SLICE", lambda x: x[:, 1:4:2, :, :], [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_slice", "SLICE", lambda x: tf.slice(x, [0, 1, 1, 0], [1, 2, 2, 1]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_pack", "PACK", lambda x, y: tf.stack([x, y], axis=0), [tf.TensorSpec([4], f32, name="x"), tf.TensorSpec([4], f32, name="y")]),
        ("op_sub", "SUB", lambda x: x - (x * 0.25), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_div", "DIV", lambda x: x / (x + 2.0), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_add_n", "ADD_N", lambda x: tf.add_n([x, x * 0.5, x * 0.25]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_sum", "SUM", lambda x: tf.reduce_sum(x, axis=[1, 2]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_reduce_max", "REDUCE_MAX", lambda x: tf.reduce_max(x, axis=[1, 2]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_reduce_min", "REDUCE_MIN", lambda x: tf.reduce_min(x, axis=[1, 2]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_reduce_all", "REDUCE_ALL", lambda x: tf.reduce_all(x, axis=[1]), [tf.TensorSpec([1, 4, 4], boolean, name="x")]),
        ("op_arg_max", "ARG_MAX", lambda x: tf.argmax(x, axis=1, output_type=tf.int32), [tf.TensorSpec([1, 4, 4], f32, name="x")]),
        ("op_arg_min", "ARG_MIN", lambda x: tf.argmin(x, axis=1, output_type=tf.int32), [tf.TensorSpec([1, 4, 4], f32, name="x")]),
        ("op_gather", "GATHER", lambda x: tf.gather(x, [0, 2], axis=1), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_gather_nd", "GATHER_ND", lambda x: tf.gather_nd(x, [[0, 0], [0, 2]]), [tf.TensorSpec([1, 4, 4], f32, name="x")]),
        ("op_split", "SPLIT", lambda x: tf.split(x, 2, axis=1), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_split_v", "SPLIT_V", lambda x: tf.split(x, [1, 3], axis=1), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_unpack", "UNPACK", lambda x: tf.unstack(x, axis=1), [tf.TensorSpec([1, 2, 4, 1], f32, name="x")]),
        ("op_broadcast_args", "BROADCAST_ARGS", lambda a, b: tf.raw_ops.BroadcastArgs(s0=a, s1=b), [tf.TensorSpec([2], i32, name="a"), tf.TensorSpec([1], i32, name="b")]),
        ("op_broadcast_to", "BROADCAST_TO", lambda x: tf.broadcast_to(x, [1, 4, 4, 3]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_batch_to_space_nd", "BATCH_TO_SPACE_ND", lambda x: tf.raw_ops.BatchToSpaceND(input=x, block_shape=[2, 2], crops=[[0, 0], [0, 0]]), [tf.TensorSpec([4, 2, 2, 1], f32, name="x")]),
        ("op_space_to_batch_nd", "SPACE_TO_BATCH_ND", lambda x: tf.raw_ops.SpaceToBatchND(input=x, block_shape=[2, 2], paddings=[[0, 0], [0, 0]]), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_depth_to_space", "DEPTH_TO_SPACE", lambda x: tf.nn.depth_to_space(x, 2), [tf.TensorSpec([1, 4, 4, 4], f32, name="x")]),
        ("op_mirror_pad", "MIRROR_PAD", lambda x: tf.raw_ops.MirrorPad(input=x, paddings=[[0, 0], [1, 1], [1, 1], [0, 0]], mode="REFLECT"), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_fill", "FILL", lambda v: tf.fill([4, 4], v[0]), [tf.TensorSpec([1], f32, name="v")]),
        ("op_batch_matmul", "BATCH_MATMUL", lambda a, b: tf.matmul(a, b), [tf.TensorSpec([1, 4, 8], f32, name="a"), tf.TensorSpec([1, 8, 4], f32, name="b")]),
        ("op_transpose_conv", "TRANSPOSE_CONV", lambda x: tf.nn.conv2d_transpose(x, tf.ones([3, 3, 1, 1], f32), [1, 8, 8, 1], strides=[1, 2, 2, 1], padding="SAME"), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_l2_normalization", "L2_NORMALIZATION", lambda x: tf.nn.l2_normalize(x, axis=-1), [tf.TensorSpec([1, 4, 4, 4], f32, name="x")]),
        ("op_logistic", "LOGISTIC", lambda x: tf.math.sigmoid(x), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_tanh", "TANH", lambda x: tf.math.tanh(x), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_leaky_relu", "LEAKY_RELU", lambda x: tf.nn.leaky_relu(x, alpha=0.2), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_elu", "ELU", lambda x: tf.nn.elu(x), [tf.TensorSpec([1, 4, 4, 1], f32, name="x")]),
        ("op_log_softmax", "LOG_SOFTMAX", lambda x: tf.nn.log_softmax(x), [tf.TensorSpec([1, 16], f32, name="x")]),
    ]

    for name, expected_ops, func, input_specs in tf_specs:
        try:
            path = output_dir / f"{name}.tflite"
            export_float_tflite(tf, func, input_specs, path)
            actual_ops = inspect_tflite_ops(tf, path)
            assets.append(ModelAsset(path.resolve(), name, "microbench", expected_ops))
            if expected_ops not in actual_ops.split("|"):
                notes.append(f"{name}: expected_op_not_in_flatbuffer: expected={expected_ops} actual={actual_ops}")
        except Exception as exc:
            notes.append(f"{name}: export_failed: {type(exc).__name__}: {exc}")

    manual_ops = [
        "SHAPE",
        "SQUEEZE",
        "EXPAND_DIMS",
        "L2_POOL_2D",
        "ZEROS_LIKE",
        "SELECT_V2",
        "PRELU",
        "SVDF",
        "UNIDIRECTIONAL_SEQUENCE_LSTM",
    ]
    for op_name in manual_ops:
        name = f"op_{op_name.lower()}"
        try:
            path = output_dir / f"{name}.tflite"
            export_manual_exact_op_model(schema, np, flatbuffers, op_name, path)
            actual_ops = inspect_tflite_ops(tf, path)
            assets.append(ModelAsset(path.resolve(), name, "microbench", op_name))
            if op_name not in actual_ops.split("|"):
                notes.append(f"{name}: expected_op_not_in_flatbuffer: expected={op_name} actual={actual_ops}")
        except Exception as exc:
            notes.append(f"{name}: export_failed: {type(exc).__name__}: {exc}")

    try:
        path = output_dir / "op_quantize_dequantize_conv.tflite"
        export_float_io_quantized_conv(tf, np, path)
        actual_ops = inspect_tflite_ops(tf, path)
        assets.append(ModelAsset(path.resolve(), "op_quantize_dequantize_conv", "microbench", "QUANTIZE|DEQUANTIZE"))
        if "QUANTIZE" not in actual_ops.split("|") or "DEQUANTIZE" not in actual_ops.split("|"):
            notes.append(f"op_quantize_dequantize_conv: expected_op_not_in_flatbuffer: expected=QUANTIZE|DEQUANTIZE actual={actual_ops}")
    except Exception as exc:
        notes.append(f"op_quantize_dequantize_conv: export_failed: {type(exc).__name__}: {exc}")

    return assets, notes


def generate_synthetic_models(output_dir: Path, synthetic_suite: str) -> tuple[list[ModelAsset], list[str]]:
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    import numpy as np
    import tensorflow as tf
    from tensorflow.keras import layers

    output_dir.mkdir(parents=True, exist_ok=True)
    notes: list[str] = []
    assets: list[ModelAsset] = []

    core_micro_names = [
        "micro_conv2d_c8_k3",
        "micro_conv2d_c16_k5",
        "micro_depthwise_c1_k3",
        "micro_depthwise_pointwise_c16",
        "micro_maxpool",
        "micro_avgpool",
        "micro_mean",
        "micro_softmax",
        "micro_fully_connected",
        "micro_relu",
        "micro_relu6",
        "micro_hard_swish",
        "micro_add",
        "micro_mul",
        "micro_concat",
        "micro_space_to_depth",
        "micro_reshape",
    ]
    micro_names = [] if synthetic_suite == "valuable" else core_micro_names
    for name in micro_names:
        try:
            model, expected_ops = make_micro_model(tf, layers, name)
            path = output_dir / f"{name}.tflite"
            export_int8_tflite(tf, np, model, path, (32, 32, 1))
            assets.append(ModelAsset(path.resolve(), name, "microbench", expected_ops))
        except Exception as exc:
            notes.append(f"{name}: export_failed: {type(exc).__name__}: {exc}")

    if synthetic_suite == "full":
        try:
            full_assets, full_notes = generate_full_op_suite(tf, np, output_dir)
            assets.extend(full_assets)
            notes.append(f"full_op_model_count={len(full_assets)}")
            notes.extend(full_notes)
        except Exception as exc:
            notes.append(f"full_op_generation_failed: {type(exc).__name__}: {exc}")

    candidate_specs = [
        ("candidate_conv_8_16_32_max_gap", dict(channels=(8, 16, 32), depthwise=False, pool="max")),
        ("candidate_conv_12_24_48_max_gap", dict(channels=(12, 24, 48), depthwise=False, pool="max")),
        ("candidate_conv_16_32_64_max_gap", dict(channels=(16, 32, 64), depthwise=False, pool="max")),
        ("candidate_conv_8_16_32_avg_gap", dict(channels=(8, 16, 32), depthwise=False, pool="avg")),
        ("candidate_stride_conv_8_16_32_gap", dict(channels=(8, 16, 32), depthwise=False, pool="max", stride_first=True)),
        ("candidate_depthwise_8_16_32_gap", dict(channels=(8, 16, 32), depthwise=True, pool="max")),
        ("candidate_depthwise_16_32_64_gap", dict(channels=(16, 32, 64), depthwise=True, pool="max")),
        ("candidate_spacetodepth_conv_8_16_32", dict(channels=(8, 16, 32), depthwise=False, pool="max", space_to_depth=True)),
        ("candidate_hardswish_depthwise_8_16_32", dict(channels=(8, 16, 32), depthwise=True, pool="max", activation="hard_swish")),
    ]
    valuable_names = {
        "candidate_conv_8_16_32_max_gap",
        "candidate_conv_8_16_32_avg_gap",
        "candidate_stride_conv_8_16_32_gap",
        "candidate_depthwise_8_16_32_gap",
        "candidate_spacetodepth_conv_8_16_32",
        "candidate_hardswish_depthwise_8_16_32",
    }
    if synthetic_suite == "valuable":
        candidate_specs = [(name, kwargs) for name, kwargs in candidate_specs if name in valuable_names]

    for name, kwargs in candidate_specs:
        try:
            model = make_classifier_model(tf, layers, name=name, **kwargs)
            path = output_dir / f"{name}.tflite"
            export_int8_tflite(tf, np, model, path, (32, 32, 1))
            assets.append(ModelAsset(path.resolve(), name, "modelbench", "from_flatbuffer"))
        except Exception as exc:
            notes.append(f"{name}: export_failed: {type(exc).__name__}: {exc}")

    return assets, notes


def write_header(output: Path, sources: list[ModelAsset], repo_root: Path) -> list[dict[str, object]]:
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []
    lines = [
        "#ifndef NEW_VERIFICATION_TFLM_OP_BENCHMARK_GENERATED_MODELS_H_",
        "#define NEW_VERIFICATION_TFLM_OP_BENCHMARK_GENERATED_MODELS_H_",
        "",
        "namespace tflm_op_benchmark {",
        "",
    ]

    symbols: list[tuple[str, ModelAsset, int]] = []
    for index, path in enumerate(sources):
        data = path.path.read_bytes()
        symbol = c_identifier(path.name, index)
        symbols.append((symbol, path, len(data)))
        rel = path.path.relative_to(repo_root) if path.path.is_relative_to(repo_root) else path.path
        lines.extend(
            [
                f"alignas(16) static const unsigned char {symbol}[] = {{",
                format_bytes(data),
                "};",
                "",
            ]
        )
        manifest.append(
            {
                "name": path.name,
                "source_path": str(rel),
                "bytes": len(data),
                "expected_ops": path.expected_ops,
                "category": path.category,
            }
        )

    lines.append("static const BenchmarkModelAsset kBenchmarkModels[] = {")
    if symbols:
        for symbol, path, length in symbols:
            rel = path.path.relative_to(repo_root) if path.path.is_relative_to(repo_root) else path.path
            lines.append(
                "    {"
                f"{c_string(path.name)}, {symbol}, {length}u, "
                f"{c_string(str(rel))}, {c_string(path.expected_ops)}, "
                f"{c_string(path.category)}"
                "},"
            )
    else:
        lines.append("    {nullptr, nullptr, 0u, nullptr, nullptr, nullptr},")
    lines.extend(
        [
            "};",
            f"static const unsigned int kBenchmarkModelCount = {len(symbols)}u;",
            "",
            "}  // namespace tflm_op_benchmark",
            "",
            "#endif  // NEW_VERIFICATION_TFLM_OP_BENCHMARK_GENERATED_MODELS_H_",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8")
    return manifest


def write_manifest(path: Path, manifest: list[dict[str, object]], notes: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "models": manifest,
        "notes": notes,
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=str(repo_root_from_script()))
    parser.add_argument("--output", default="new/verification/tflm_op_benchmark/generated/benchmark_models_generated.h")
    parser.add_argument("--manifest", default="new/verification/tflm_op_benchmark/generated/model_manifest.json")
    parser.add_argument("--source-tflite", action="append", default=[])
    parser.add_argument("--max-default-models", type=int, default=2)
    parser.add_argument("--include-synthetic", action="store_true", help="Generate microbench and candidate TFLite models with TensorFlow")
    parser.add_argument("--synthetic-only", action="store_true", help="Do not include default or explicit baseline .tflite files")
    parser.add_argument("--synthetic-suite", choices=["core", "full", "valuable"], default="full")
    parser.add_argument("--synthetic-output-dir", default="new/verification/tflm_op_benchmark/generated/synthetic_models")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    output = Path(args.output)
    manifest_path = Path(args.manifest)
    if not output.is_absolute():
        output = repo_root / output
    if not manifest_path.is_absolute():
        manifest_path = repo_root / manifest_path

    sources = [] if args.synthetic_only else discover_sources(repo_root, args.source_tflite, args.max_default_models)
    notes = []
    if args.include_synthetic:
        synthetic_output_dir = Path(args.synthetic_output_dir)
        if not synthetic_output_dir.is_absolute():
            synthetic_output_dir = repo_root / synthetic_output_dir
        try:
            synthetic_assets, synthetic_notes = generate_synthetic_models(synthetic_output_dir, args.synthetic_suite)
            sources = synthetic_assets + sources
            notes.append(f"synthetic_model_count={len(synthetic_assets)}")
            notes.append(f"synthetic_suite={args.synthetic_suite}")
            notes.extend(synthetic_notes)
        except Exception as exc:
            notes.append(f"synthetic_generation_failed: {type(exc).__name__}: {exc}")
    if not sources:
        notes.append("no_tflite_sources_found; generated empty benchmark model list")

    manifest = write_header(output, sources, repo_root)
    write_manifest(manifest_path, manifest, notes)
    print(f"generated_header={output}")
    print(f"manifest={manifest_path}")
    print(f"model_count={len(manifest)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
