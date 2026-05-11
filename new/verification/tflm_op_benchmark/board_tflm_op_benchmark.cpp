#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/core/api/flatbuffer_conversions.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef TFLM_OP_BENCH_MODELS_HEADER
#define TFLM_OP_BENCH_MODELS_HEADER "benchmark_models_empty.h"
#endif

#ifndef TFLM_OP_BENCH_ARENA_BYTES
#define TFLM_OP_BENCH_ARENA_BYTES 1048576
#endif

namespace tflm_op_benchmark {

struct BenchmarkModelAsset {
  const char* name;
  const unsigned char* data;
  unsigned int length;
  const char* source_path;
  const char* expected_ops;
  const char* category;
};

}  // namespace tflm_op_benchmark

#include TFLM_OP_BENCH_MODELS_HEADER

namespace {

template <unsigned int kMaxOps>
class BenchmarkOpResolver final : public tflite::MicroOpResolver {
 public:
  const TFLMRegistration* FindOp(tflite::BuiltinOperator op) const override {
    if (op == tflite::BuiltinOperator_CUSTOM) {
      return nullptr;
    }
    for (unsigned int i = 0; i < registrations_len_; ++i) {
      if (registrations_[i].builtin_code == op) {
        return &registrations_[i];
      }
    }
    return nullptr;
  }

  const TFLMRegistration* FindOp(const char* op) const override {
    for (unsigned int i = 0; i < registrations_len_; ++i) {
      const TFLMRegistration& registration = registrations_[i];
      if (registration.builtin_code == tflite::BuiltinOperator_CUSTOM &&
          registration.custom_name != nullptr &&
          std::strcmp(registration.custom_name, op) == 0) {
        return &registration;
      }
    }
    return nullptr;
  }

  tflite::TfLiteBridgeBuiltinParseFunction GetOpDataParser(
      tflite::BuiltinOperator op) const override {
    for (unsigned int i = 0; i < builtin_len_; ++i) {
      if (builtin_codes_[i] == op) {
        return builtin_parsers_[i];
      }
    }
    return nullptr;
  }

  TfLiteStatus AddBuiltin(tflite::BuiltinOperator op,
                          const TFLMRegistration& registration,
                          tflite::TfLiteBridgeBuiltinParseFunction parser) {
    if (op == tflite::BuiltinOperator_CUSTOM || parser == nullptr) {
      return kTfLiteError;
    }
    if (FindOp(op) != nullptr || registrations_len_ >= kMaxOps ||
        builtin_len_ >= kMaxOps) {
      return kTfLiteError;
    }
    registrations_[registrations_len_] = registration;
    registrations_[registrations_len_].builtin_code = op;
    ++registrations_len_;
    builtin_codes_[builtin_len_] = op;
    builtin_parsers_[builtin_len_] = parser;
    ++builtin_len_;
    return kTfLiteOk;
  }

  unsigned int size() const { return registrations_len_; }

 private:
  TFLMRegistration registrations_[kMaxOps] = {};
  tflite::BuiltinOperator builtin_codes_[kMaxOps] = {};
  tflite::TfLiteBridgeBuiltinParseFunction builtin_parsers_[kMaxOps] = {};
  unsigned int registrations_len_ = 0;
  unsigned int builtin_len_ = 0;
};

struct OpSpec {
  const char* name;
  tflite::BuiltinOperator op;
  TFLMRegistration (*registration)();
  tflite::TfLiteBridgeBuiltinParseFunction parser;
  bool core_candidate;
};

const OpSpec kOpSpecs[] = {
    {"CONV_2D", tflite::BuiltinOperator_CONV_2D, tflite::Register_CONV_2D,
     tflite::ParseConv2D, true},
    {"DEPTHWISE_CONV_2D", tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
     tflite::Register_DEPTHWISE_CONV_2D, tflite::ParseDepthwiseConv2D, true},
    {"FULLY_CONNECTED", tflite::BuiltinOperator_FULLY_CONNECTED,
     tflite::Register_FULLY_CONNECTED, tflite::ParseFullyConnected, true},
    {"MAX_POOL_2D", tflite::BuiltinOperator_MAX_POOL_2D,
     tflite::Register_MAX_POOL_2D, tflite::ParsePool, true},
    {"AVERAGE_POOL_2D", tflite::BuiltinOperator_AVERAGE_POOL_2D,
     tflite::Register_AVERAGE_POOL_2D, tflite::ParsePool, true},
    {"MEAN", tflite::BuiltinOperator_MEAN, tflite::Register_MEAN,
     tflite::ParseReducer, true},
    {"SOFTMAX", tflite::BuiltinOperator_SOFTMAX, tflite::Register_SOFTMAX,
     tflite::ParseSoftmax, true},
    {"RESHAPE", tflite::BuiltinOperator_RESHAPE, tflite::Register_RESHAPE,
     tflite::ParseReshape, true},
    {"RELU", tflite::BuiltinOperator_RELU, tflite::Register_RELU,
     tflite::ParseRelu, true},
    {"RELU6", tflite::BuiltinOperator_RELU6, tflite::Register_RELU6,
     tflite::ParseRelu6, true},
    {"QUANTIZE", tflite::BuiltinOperator_QUANTIZE, tflite::Register_QUANTIZE,
     tflite::ParseQuantize, true},
    {"DEQUANTIZE", tflite::BuiltinOperator_DEQUANTIZE,
     tflite::Register_DEQUANTIZE, tflite::ParseDequantize, true},
    {"CAST", tflite::BuiltinOperator_CAST, tflite::Register_CAST,
     tflite::ParseCast, false},
    {"ADD", tflite::BuiltinOperator_ADD, tflite::Register_ADD,
     tflite::ParseAdd, true},
    {"MUL", tflite::BuiltinOperator_MUL, tflite::Register_MUL,
     tflite::ParseMul, true},
    {"CONCATENATION", tflite::BuiltinOperator_CONCATENATION,
     tflite::Register_CONCATENATION, tflite::ParseConcatenation, true},
    {"PAD", tflite::BuiltinOperator_PAD, tflite::Register_PAD,
     tflite::ParsePad, false},
    {"PADV2", tflite::BuiltinOperator_PADV2, tflite::Register_PADV2,
     tflite::ParsePadV2, false},
    {"MAXIMUM", tflite::BuiltinOperator_MAXIMUM, tflite::Register_MAXIMUM,
     tflite::ParseMaximum, true},
    {"MINIMUM", tflite::BuiltinOperator_MINIMUM, tflite::Register_MINIMUM,
     tflite::ParseMinimum, true},
    {"SQUEEZE", tflite::BuiltinOperator_SQUEEZE, tflite::Register_SQUEEZE,
     tflite::ParseSqueeze, false},
    {"EXPAND_DIMS", tflite::BuiltinOperator_EXPAND_DIMS,
     tflite::Register_EXPAND_DIMS, tflite::ParseExpandDims, false},
    {"TRANSPOSE", tflite::BuiltinOperator_TRANSPOSE, tflite::Register_TRANSPOSE,
     tflite::ParseTranspose, false},
    {"STRIDED_SLICE", tflite::BuiltinOperator_STRIDED_SLICE,
     tflite::Register_STRIDED_SLICE, tflite::ParseStridedSlice, false},
    {"SLICE", tflite::BuiltinOperator_SLICE, tflite::Register_SLICE,
     tflite::ParseSlice, false},
    {"PACK", tflite::BuiltinOperator_PACK, tflite::Register_PACK,
     tflite::ParsePack, false},
    {"SHAPE", tflite::BuiltinOperator_SHAPE, tflite::Register_SHAPE,
     tflite::ParseShape, false},
    {"SUB", tflite::BuiltinOperator_SUB, tflite::Register_SUB,
     tflite::ParseSub, false},
    {"DIV", tflite::BuiltinOperator_DIV, tflite::Register_DIV,
     tflite::ParseDiv, false},
    {"ADD_N", tflite::BuiltinOperator_ADD_N, tflite::Register_ADD_N,
     tflite::ParseAddN, false},
    {"SUM", tflite::BuiltinOperator_SUM, tflite::Register_SUM,
     tflite::ParseReducer, false},
    {"REDUCE_MAX", tflite::BuiltinOperator_REDUCE_MAX,
     tflite::Register_REDUCE_MAX, tflite::ParseReducer, false},
    {"REDUCE_MIN", tflite::BuiltinOperator_REDUCE_MIN,
     tflite::Register_REDUCE_MIN, tflite::ParseReducer, false},
    {"REDUCE_ALL", tflite::BuiltinOperator_REDUCE_ALL,
     tflite::Register_REDUCE_ALL, tflite::ParseReducer, false},
    {"ARG_MAX", tflite::BuiltinOperator_ARG_MAX, tflite::Register_ARG_MAX,
     tflite::ParseArgMax, false},
    {"ARG_MIN", tflite::BuiltinOperator_ARG_MIN, tflite::Register_ARG_MIN,
     tflite::ParseArgMin, false},
    {"GATHER", tflite::BuiltinOperator_GATHER, tflite::Register_GATHER,
     tflite::ParseGather, false},
    {"GATHER_ND", tflite::BuiltinOperator_GATHER_ND,
     tflite::Register_GATHER_ND, tflite::ParseGatherNd, false},
    {"SPLIT", tflite::BuiltinOperator_SPLIT, tflite::Register_SPLIT,
     tflite::ParseSplit, false},
    {"SPLIT_V", tflite::BuiltinOperator_SPLIT_V, tflite::Register_SPLIT_V,
     tflite::ParseSplitV, false},
    {"UNPACK", tflite::BuiltinOperator_UNPACK, tflite::Register_UNPACK,
     tflite::ParseUnpack, false},
    {"BROADCAST_ARGS", tflite::BuiltinOperator_BROADCAST_ARGS,
     tflite::Register_BROADCAST_ARGS, tflite::ParseBroadcastArgs, false},
    {"BROADCAST_TO", tflite::BuiltinOperator_BROADCAST_TO,
     tflite::Register_BROADCAST_TO, tflite::ParseBroadcastTo, false},
    {"BATCH_TO_SPACE_ND", tflite::BuiltinOperator_BATCH_TO_SPACE_ND,
     tflite::Register_BATCH_TO_SPACE_ND, tflite::ParseBatchToSpaceNd, false},
    {"SPACE_TO_BATCH_ND", tflite::BuiltinOperator_SPACE_TO_BATCH_ND,
     tflite::Register_SPACE_TO_BATCH_ND, tflite::ParseSpaceToBatchNd, false},
    {"SPACE_TO_DEPTH", tflite::BuiltinOperator_SPACE_TO_DEPTH,
     tflite::Register_SPACE_TO_DEPTH, tflite::ParseSpaceToDepth, true},
    {"DEPTH_TO_SPACE", tflite::BuiltinOperator_DEPTH_TO_SPACE,
     tflite::Register_DEPTH_TO_SPACE, tflite::ParseDepthToSpace, false},
    {"MIRROR_PAD", tflite::BuiltinOperator_MIRROR_PAD,
     tflite::Register_MIRROR_PAD, tflite::ParseMirrorPad, false},
    {"FILL", tflite::BuiltinOperator_FILL, tflite::Register_FILL,
     tflite::ParseFill, false},
    {"ZEROS_LIKE", tflite::BuiltinOperator_ZEROS_LIKE,
     tflite::Register_ZEROS_LIKE, tflite::ParseZerosLike, false},
    {"SELECT_V2", tflite::BuiltinOperator_SELECT_V2,
     tflite::Register_SELECT_V2, tflite::ParseSelectV2, false},
    {"BATCH_MATMUL", tflite::BuiltinOperator_BATCH_MATMUL,
     tflite::Register_BATCH_MATMUL, tflite::ParseBatchMatMul, false},
    {"TRANSPOSE_CONV", tflite::BuiltinOperator_TRANSPOSE_CONV,
     tflite::Register_TRANSPOSE_CONV, tflite::ParseTransposeConv, false},
    {"SVDF", tflite::BuiltinOperator_SVDF, tflite::Register_SVDF,
     tflite::ParseSvdf, false},
    {"UNIDIRECTIONAL_SEQUENCE_LSTM",
     tflite::BuiltinOperator_UNIDIRECTIONAL_SEQUENCE_LSTM,
     tflite::Register_UNIDIRECTIONAL_SEQUENCE_LSTM,
     tflite::ParseUnidirectionalSequenceLSTM, false},
    {"L2_NORMALIZATION", tflite::BuiltinOperator_L2_NORMALIZATION,
     tflite::Register_L2_NORMALIZATION, tflite::ParseL2Normalization, false},
    {"L2_POOL_2D", tflite::BuiltinOperator_L2_POOL_2D,
     tflite::Register_L2_POOL_2D, tflite::ParsePool, false},
    {"LOGISTIC", tflite::BuiltinOperator_LOGISTIC, tflite::Register_LOGISTIC,
     tflite::ParseLogistic, false},
    {"TANH", tflite::BuiltinOperator_TANH, tflite::Register_TANH,
     tflite::ParseTanh, false},
    {"LEAKY_RELU", tflite::BuiltinOperator_LEAKY_RELU,
     tflite::Register_LEAKY_RELU, tflite::ParseLeakyRelu, false},
    {"ELU", tflite::BuiltinOperator_ELU, tflite::Register_ELU,
     tflite::ParseElu, false},
    {"HARD_SWISH", tflite::BuiltinOperator_HARD_SWISH,
     tflite::Register_HARD_SWISH, tflite::ParseHardSwish, true},
    {"LOG_SOFTMAX", tflite::BuiltinOperator_LOG_SOFTMAX,
     tflite::Register_LOG_SOFTMAX, tflite::ParseLogSoftmax, false},
    {"PRELU", tflite::BuiltinOperator_PRELU, nullptr, tflite::ParsePrelu,
     false},
};

constexpr int kDefaultWarmup = 3;
constexpr int kDefaultLoops = 30;
constexpr int kMaxLoops = 256;

using Resolver = BenchmarkOpResolver<96>;

int64_t NowMicros() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::microseconds>(
             clock::now().time_since_epoch())
      .count();
}

void PrintCsvHeader() {
  std::puts(
      "CSV,category,case,ops,status,arena_used_bytes,warmup_us,invoke_min_us,"
      "invoke_avg_us,invoke_p95_us,error");
}

void PrintCsv(const char* category, const char* case_name, const char* ops,
              const char* status, size_t arena_used_bytes, int64_t warmup_us,
              int64_t min_us, int64_t avg_us, int64_t p95_us,
              const char* error) {
  std::printf("CSV,%s,%s,%s,%s,%zu,%lld,%lld,%lld,%lld,%s\n", category,
              case_name, ops, status, arena_used_bytes,
              static_cast<long long>(warmup_us), static_cast<long long>(min_us),
              static_cast<long long>(avg_us), static_cast<long long>(p95_us),
              error == nullptr ? "" : error);
  std::fflush(stdout);
}

TfLiteStatus AddOpToResolver(Resolver* resolver, const OpSpec& spec) {
  if (spec.registration == nullptr || spec.parser == nullptr) {
    return kTfLiteError;
  }
  return resolver->AddBuiltin(spec.op, spec.registration(), spec.parser);
}

void EmitResolverAvailability() {
  Resolver resolver;
  for (const OpSpec& spec : kOpSpecs) {
    const TfLiteStatus status = AddOpToResolver(&resolver, spec);
    PrintCsv("availability", spec.name, spec.name,
             status == kTfLiteOk ? "OK" : "RED", 0, 0, 0, 0, 0,
             status == kTfLiteOk ? "" : "register_failed");
  }
}

TfLiteStatus BuildFullResolver(Resolver* resolver) {
  for (const OpSpec& spec : kOpSpecs) {
    (void)AddOpToResolver(resolver, spec);
  }
  return kTfLiteOk;
}

void FillTensor(TfLiteTensor* tensor, int seed) {
  if (tensor == nullptr || tensor->data.raw == nullptr) {
    return;
  }
  switch (tensor->type) {
    case kTfLiteFloat32: {
      const size_t count = tensor->bytes / sizeof(float);
      float* data = tflite::GetTensorData<float>(tensor);
      for (size_t i = 0; i < count; ++i) {
        data[i] = static_cast<float>((static_cast<int>(i) + seed) % 17) / 16.0f;
      }
      break;
    }
    case kTfLiteInt8: {
      int8_t* data = tflite::GetTensorData<int8_t>(tensor);
      for (size_t i = 0; i < tensor->bytes; ++i) {
        data[i] = static_cast<int8_t>((static_cast<int>(i) + seed) % 127);
      }
      break;
    }
    case kTfLiteUInt8: {
      uint8_t* data = tflite::GetTensorData<uint8_t>(tensor);
      for (size_t i = 0; i < tensor->bytes; ++i) {
        data[i] = static_cast<uint8_t>((static_cast<int>(i) + seed) & 0xff);
      }
      break;
    }
    case kTfLiteInt16: {
      int16_t* data = tflite::GetTensorData<int16_t>(tensor);
      const size_t count = tensor->bytes / sizeof(int16_t);
      for (size_t i = 0; i < count; ++i) {
        data[i] = static_cast<int16_t>((static_cast<int>(i) + seed) % 1024);
      }
      break;
    }
    case kTfLiteInt32: {
      int32_t* data = tflite::GetTensorData<int32_t>(tensor);
      const size_t count = tensor->bytes / sizeof(int32_t);
      for (size_t i = 0; i < count; ++i) {
        data[i] = static_cast<int32_t>((i % 2) + 1);
      }
      break;
    }
    case kTfLiteInt64: {
      int64_t* data = tflite::GetTensorData<int64_t>(tensor);
      const size_t count = tensor->bytes / sizeof(int64_t);
      for (size_t i = 0; i < count; ++i) {
        data[i] = static_cast<int64_t>((i % 2) + 1);
      }
      break;
    }
    case kTfLiteBool: {
      bool* data = tflite::GetTensorData<bool>(tensor);
      const size_t count = tensor->bytes / sizeof(bool);
      for (size_t i = 0; i < count; ++i) {
        data[i] = ((i + static_cast<size_t>(seed)) % 2) == 0;
      }
      break;
    }
    default:
      std::memset(tensor->data.raw, 0, tensor->bytes);
      break;
  }
}

const char* ModelOps(const tflite::Model* model, char* buffer,
                     size_t buffer_size) {
  if (model == nullptr || buffer == nullptr || buffer_size == 0) {
    return "";
  }
  buffer[0] = '\0';
  const auto* opcodes = model->operator_codes();
  const auto* subgraphs = model->subgraphs();
  if (opcodes == nullptr || subgraphs == nullptr || subgraphs->size() == 0) {
    return "";
  }
  const auto* operators = subgraphs->Get(0)->operators();
  if (operators == nullptr) {
    return "";
  }
  for (unsigned int i = 0; i < operators->size(); ++i) {
    const auto* op = operators->Get(i);
    if (op == nullptr || op->opcode_index() >= opcodes->size()) {
      continue;
    }
    const auto* opcode = opcodes->Get(op->opcode_index());
    const char* name =
        opcode == nullptr ? "UNKNOWN" : tflite::EnumNameBuiltinOperator(
                                        opcode->builtin_code());
    if (buffer[0] != '\0') {
      std::strncat(buffer, "|", buffer_size - std::strlen(buffer) - 1);
    }
    std::strncat(buffer, name, buffer_size - std::strlen(buffer) - 1);
  }
  return buffer;
}

void BenchmarkOneModel(const tflm_op_benchmark::BenchmarkModelAsset& asset,
                       int warmup, int loops) {
  const char* category = asset.category == nullptr ? "modelbench" : asset.category;
  if (asset.data == nullptr || asset.length == 0) {
    PrintCsv(category, asset.name, "NONE", "RED", 0, 0, 0, 0, 0,
             "empty_model");
    return;
  }

  const tflite::Model* model = tflite::GetModel(asset.data);
  char ops[512];
  const char* model_ops = ModelOps(model, ops, sizeof(ops));

  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
    PrintCsv(category, asset.name, model_ops, "RED", 0, 0, 0, 0, 0,
             "schema_version_mismatch");
    return;
  }

  Resolver resolver;
  if (BuildFullResolver(&resolver) != kTfLiteOk) {
    PrintCsv(category, asset.name, model_ops, "RED", 0, 0, 0, 0, 0,
             "resolver_build_failed");
    return;
  }

  alignas(16) static uint8_t tensor_arena[TFLM_OP_BENCH_ARENA_BYTES];
  std::memset(tensor_arena, 0, sizeof(tensor_arena));

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                       sizeof(tensor_arena));
  if (interpreter.initialization_status() != kTfLiteOk) {
    PrintCsv(category, asset.name, model_ops, "RED", 0, 0, 0, 0, 0,
             "interpreter_init_failed");
    return;
  }
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    PrintCsv(category, asset.name, model_ops, "RED", 0, 0, 0, 0, 0,
             "allocate_tensors_failed");
    return;
  }

  for (size_t i = 0; i < interpreter.inputs_size(); ++i) {
    FillTensor(interpreter.input(i), static_cast<int>(i));
  }

  const int64_t warmup_start = NowMicros();
  for (int i = 0; i < warmup; ++i) {
    if (interpreter.Invoke() != kTfLiteOk) {
      PrintCsv(category, asset.name, model_ops, "RED",
               interpreter.arena_used_bytes(), 0, 0, 0, 0, "warmup_failed");
      return;
    }
  }
  const int64_t warmup_us = NowMicros() - warmup_start;

  int64_t samples[kMaxLoops];
  int64_t sum_us = 0;
  for (int i = 0; i < loops; ++i) {
    for (size_t input_index = 0; input_index < interpreter.inputs_size();
         ++input_index) {
      FillTensor(interpreter.input(input_index), i + static_cast<int>(input_index));
    }
    const int64_t start = NowMicros();
    if (interpreter.Invoke() != kTfLiteOk) {
      PrintCsv(category, asset.name, model_ops, "RED",
               interpreter.arena_used_bytes(), warmup_us, 0, 0, 0,
               "invoke_failed");
      return;
    }
    samples[i] = NowMicros() - start;
    sum_us += samples[i];
  }

  std::sort(samples, samples + loops);
  const int p95_index = std::min(loops - 1, (loops * 95) / 100);
  PrintCsv(category, asset.name, model_ops, "OK",
           interpreter.arena_used_bytes(), warmup_us, samples[0],
           sum_us / loops, samples[p95_index], "");
}

bool ContainsAnyFilter(const char* value, const char* const* filters,
                       int filter_count) {
  if (filter_count == 0) {
    return true;
  }
  if (value == nullptr) {
    return false;
  }
  for (int i = 0; i < filter_count; ++i) {
    if (filters[i] != nullptr && std::strstr(value, filters[i]) != nullptr) {
      return true;
    }
  }
  return false;
}

bool ContainsAnySkip(const char* value, const char* const* skips,
                     int skip_count) {
  if (value == nullptr) {
    return false;
  }
  for (int i = 0; i < skip_count; ++i) {
    if (skips[i] != nullptr && std::strstr(value, skips[i]) != nullptr) {
      return true;
    }
  }
  return false;
}

void EmitModelBenchmarks(int warmup, int loops, const char* const* filters,
                         int filter_count, const char* const* skips,
                         int skip_count) {
  if (tflm_op_benchmark::kBenchmarkModelCount == 0) {
    PrintCsv("modelbench", "no_embedded_model", "NONE", "SKIPPED", 0, 0, 0, 0,
             0, "run_generate_benchmark_assets");
    return;
  }
  for (unsigned int i = 0; i < tflm_op_benchmark::kBenchmarkModelCount; ++i) {
    const auto& asset = tflm_op_benchmark::kBenchmarkModels[i];
    if (!ContainsAnyFilter(asset.name, filters, filter_count) ||
        ContainsAnySkip(asset.name, skips, skip_count)) {
      continue;
    }
    std::printf("INFO,case_start,%u,%s\n", i,
                asset.name == nullptr ? "" : asset.name);
    std::fflush(stdout);
    BenchmarkOneModel(asset, warmup, loops);
  }
}

int ParsePositiveIntArg(const char* value, int fallback, int max_value) {
  if (value == nullptr) {
    return fallback;
  }
  const int parsed = std::atoi(value);
  if (parsed <= 0) {
    return fallback;
  }
  return std::min(parsed, max_value);
}

}  // namespace

int main(int argc, char** argv) {
  int warmup = kDefaultWarmup;
  int loops = kDefaultLoops;
  bool availability = true;
  bool modelbench = true;
  const char* filters[32] = {};
  const char* skips[32] = {};
  int filter_count = 0;
  int skip_count = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      warmup = ParsePositiveIntArg(argv[++i], warmup, kMaxLoops);
    } else if (std::strcmp(argv[i], "--loops") == 0 && i + 1 < argc) {
      loops = ParsePositiveIntArg(argv[++i], loops, kMaxLoops);
    } else if (std::strcmp(argv[i], "--case-filter") == 0 && i + 1 < argc &&
               filter_count < 32) {
      filters[filter_count++] = argv[++i];
    } else if (std::strcmp(argv[i], "--skip-case") == 0 && i + 1 < argc &&
               skip_count < 32) {
      skips[skip_count++] = argv[++i];
    } else if (std::strcmp(argv[i], "--availability-only") == 0) {
      modelbench = false;
    } else if (std::strcmp(argv[i], "--modelbench-only") == 0) {
      availability = false;
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::puts("Usage: tflm_op_benchmark [--warmup N] [--loops N]");
      return 0;
    }
  }

  PrintCsvHeader();
  std::printf("INFO,arena_bytes,%d\n", TFLM_OP_BENCH_ARENA_BYTES);
  std::printf("INFO,model_count,%u\n", tflm_op_benchmark::kBenchmarkModelCount);

  if (availability) {
    EmitResolverAvailability();
  }
  if (modelbench) {
    EmitModelBenchmarks(warmup, loops, filters, filter_count, skips,
                        skip_count);
  }
  return 0;
}
