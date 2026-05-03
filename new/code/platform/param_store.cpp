#include "port/platform_adapter.hpp"

// 参数存储实现 —— 从 JSON 配置文件中加载当前运行时参数。
// 支持 JSON 注释剥离、文件读取、OpenCV FileStorage 解析。
// 缺文件或解析失败时回退到 RuntimeParameters 的内建镜像默认值。

#include <array>
#include <cstddef>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include <opencv2/core/persistence.hpp>

namespace ls2k::platform {
namespace {

// 读取文件全部内容到字符串
bool ReadText(const std::string& path, std::string& out) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

// 剥离 JSON 中的注释（C风格 // 和 /* */），输出纯 JSON。
// 追踪字符串字面量上下文避免误删字符串内的 '//'。
std::string StripJsonComments(const std::string& text) {
    std::string output;
    output.reserve(text.size());

    bool in_string = false;
    bool escaped = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

        if (in_line_comment) {
            if (c == '\n') {
                in_line_comment = false;
                output.push_back(c);
            }
            continue;
        }

        if (in_block_comment) {
            if (c == '\n') {
                output.push_back(c);
                continue;
            }
            if (c == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }

        if (in_string) {
            output.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"' ) {
            in_string = true;
            output.push_back(c);
            continue;
        }

        if (c == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }

        if (c == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }

        output.push_back(c);
    }

    return output;
}

// 基于 OpenCV FileStorage 解析 JSON 字符串为结构化节点树
bool ParseJsonObject(const std::string& text, cv::FileStorage& storage) {
    try {
        const std::string sanitized = StripJsonComments(text);
        if (!storage.open(sanitized,
                          cv::FileStorage::READ | cv::FileStorage::MEMORY |
                              cv::FileStorage::FORMAT_JSON)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    const cv::FileNode root = storage.root();
    return !root.empty() && root.isMap();
}

// 从 JSON 节点读取数值（整数或浮点数）
bool ReadNumberNode(const cv::FileNode& node, double& value) {
    if (node.empty() || (!node.isInt() && !node.isReal())) {
        return false;
    }
    value = static_cast<double>(node.real());
    return true;
}

// 读取必填数值参数，缺失则返回 false
bool ReadRequiredNumber(const cv::FileNode& root, const char* key, double& value) {
    return ReadNumberNode(root[key], value);
}

// 读取整数值，验证数值是否为整数（允许浮点数但必须有整数精度）
bool ReadIntegerValue(const cv::FileNode& node, int& value) {
    double numeric = 0.0;
    if (!ReadNumberNode(node, numeric)) {
        return false;
    }
    const double rounded = std::round(numeric);
    if (std::fabs(numeric - rounded) > 1e-6) {
        return false;
    }
    value = static_cast<int>(rounded);
    return true;
}

bool ReadRequiredInt(const cv::FileNode& root, const char* key, int& value) {
    return ReadIntegerValue(root[key], value);
}

// 读取布尔值，支持整数/字符串格式（true/TRUE/1/yes/on 等）
bool ReadBoolValue(const cv::FileNode& node, bool& value) {
    if (node.empty()) {
        return false;
    }
    if (node.isInt()) {
        value = static_cast<int>(node.real()) != 0;
        return true;
    }
    if (node.isString()) {
        const std::string text = static_cast<std::string>(node);
        if (text == "true" || text == "TRUE" || text == "1" || text == "yes" || text == "on") {
            value = true;
            return true;
        }
        if (text == "false" || text == "FALSE" || text == "0" || text == "no" || text == "off") {
            value = false;
            return true;
        }
    }
    return false;
}

// 读取字符串值
bool ReadStringValue(const cv::FileNode& node, std::string& value) {
    if (node.empty() || !node.isString()) {
        return false;
    }
    value = static_cast<std::string>(node);
    return true;
}

// 读取可选数值参数（缺失不报错，格式错误标记 malformed）
void ReadOptionalNumber(const cv::FileNode& root, const char* key, double& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadNumberNode(node, value)) {
        malformed = true;
    }
}

// 读取可选整数参数
void ReadOptionalInt(const cv::FileNode& root, const char* key, int& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadIntegerValue(node, value)) {
        malformed = true;
    }
}

// 读取可选布尔参数
void ReadOptionalBool(const cv::FileNode& root, const char* key, bool& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadBoolValue(node, value)) {
        malformed = true;
    }
}

// 读取嵌套可选数值参数（const char* child, double 版本）
void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              double& value,
                              bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!ReadNumberNode(node, value)) {
        malformed = true;
    }
}

// 读取嵌套可选数值参数（std::string child, double 版本）
void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const std::string& child,
                              double& value,
                              bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!ReadNumberNode(node, value)) {
        malformed = true;
    }
}

// float 特化的嵌套数值读取（const char* child, float 版本，通过 double 中转）
void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              float& value,
                              bool& malformed) {
    double temporary = static_cast<double>(value);
    ReadOptionalNestedNumber(root, parent, child, temporary, malformed);
    value = static_cast<float>(temporary);
}

// float 特化嵌套数值读取（std::string child, float 版本）
void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const std::string& child,
                              float& value,
                              bool& malformed) {
    double temporary = static_cast<double>(value);
    ReadOptionalNestedNumber(root, parent, child, temporary, malformed);
    value = static_cast<float>(temporary);
}

// 读取嵌套可选浮点数组（固定长度 N）
template <std::size_t N>
void ReadOptionalNestedFloatArray(const cv::FileNode& root,
                                  const char* parent,
                                  const char* child,
                                  std::array<float, N>& values,
                                  bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!node.isSeq() || node.size() != N) {
        malformed = true;
        return;
    }
    for (std::size_t index = 0; index < N; ++index) {
        double value = 0.0;
        if (!ReadNumberNode(node[static_cast<int>(index)], value)) {
            malformed = true;
            return;
        }
        values[index] = static_cast<float>(value);
    }
}

// 读取嵌套可选布尔值
void ReadOptionalNestedBool(const cv::FileNode& root,
                            const char* parent,
                            const char* child,
                            bool& value,
                            bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!ReadBoolValue(node, value)) {
        malformed = true;
    }
}

// 读取嵌套可选整数
void ReadOptionalNestedInt(const cv::FileNode& root,
                           const char* parent,
                           const char* child,
                           int& value,
                           bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!ReadIntegerValue(node, value)) {
        malformed = true;
    }
}

// 读取嵌套可选字符串
void ReadOptionalNestedString(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              std::string& value,
                              bool& malformed) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty()) {
        return;
    }
    if (!parent_node.isMap()) {
        malformed = true;
        return;
    }
    const cv::FileNode node = parent_node[child];
    if (node.empty()) {
        return;
    }
    if (!ReadStringValue(node, value)) {
        malformed = true;
    }
}

// 读取必填嵌套数值参数（缺失返回 false）
bool ReadRequiredNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              double& value) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty() || !parent_node.isMap()) {
        return false;
    }
    return ReadNumberNode(parent_node[child], value);
}

// 读取必填嵌套字符串值
bool ReadRequiredNestedString(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              std::string& value) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty() || !parent_node.isMap()) {
        return false;
    }
    return ReadStringValue(parent_node[child], value);
}

// 读取必填嵌套整数值
bool ReadRequiredNestedInt(const cv::FileNode& root,
                           const char* parent,
                           const char* child,
                           int& value) {
    const cv::FileNode parent_node = root[parent];
    if (parent_node.empty() || !parent_node.isMap()) {
        return false;
    }
    return ReadIntegerValue(parent_node[child], value);
}

// 读取必填字符串值
bool ReadRequiredString(const cv::FileNode& root, const char* key, std::string& value) {
    const cv::FileNode node = root[key];
    if (node.empty() || !node.isString()) {
        return false;
    }
    value = static_cast<std::string>(node);
    return true;
}

// 解析子系统模式文本枚举
bool ParseMode(const std::string& mode_text, port::SubsystemMode& mode) {
    if (mode_text == "adaptation-hook") {
        mode = port::SubsystemMode::kAdaptationHook;
        return true;
    }
    if (mode_text == "disabled") {
        mode = port::SubsystemMode::kDisabled;
        return true;
    }
    if (mode_text == "direct-match") {
        mode = port::SubsystemMode::kDirectMatch;
        return true;
    }
    return false;
}

// 解析硬件配置文件的子系统块（mode + hook）
bool ParseProfileBlock(const cv::FileNode& root,
                       const char* key,
                       port::SubsystemProfile& out_profile) {
    const cv::FileNode block = root[key];
    if (block.empty() || !block.isMap()) {
        return false;
    }

    std::string mode_text;
    std::string hook_name;
    if (!ReadRequiredString(block, "mode", mode_text) || !ReadRequiredString(block, "hook", hook_name)) {
        return false;
    }
    port::SubsystemMode parsed_mode = port::SubsystemMode::kDirectMatch;
    if (!ParseMode(mode_text, parsed_mode)) {
        return false;
    }
    out_profile.mode = parsed_mode;
    out_profile.hook = hook_name;
    return true;
}

// 生成子系统解析错误详情字符串
std::string ProfileBlockError(const char* key) {
    return std::string("hardware profile parse failure for subsystem '") + key +
           "' (missing block or malformed mode/hook)";
}

class ParamStore final : public port::IParamStore {
public:
    // 从 JSON 文件加载全部运行时参数。先读文件→剥注释→解析 JSON→
    // 依次提取必填字段和可选字段→校验完整性→回退默认值保护。
    bool LoadRuntimeParameters(const std::string& path,
                               port::RuntimeParameters& out,
                               port::DiagnosticSink& diagnostics) override {
        std::string text;
        if (!ReadText(path, text)) {
            out.loaded_from_defaults = true;
            out.parse_failure = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "params.missing",
                              "parameter file missing, using built-in defaults: " + path,
                              port::NowMs()});
            return true;
        }

        cv::FileStorage json;
        if (!ParseJsonObject(text, json)) {
            out = port::RuntimeParameters{};
            out.loaded_from_defaults = true;
            out.parse_failure = true;
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "params.parse",
                              "parameter file parse failure (invalid JSON object), using defaults",
                              port::NowMs()});
            return true;
        }

        const cv::FileNode root = json.root();
        port::RuntimeParameters parsed{};
        bool all_ok = true;
        // --- 必填参数：缺失直接导致解析失败 ---
        all_ok &= ReadRequiredNumber(root, "RUNNING_SPEED_TARGET", parsed.running_speed_target);
        all_ok &= ReadRequiredNestedNumber(root, "YAW_RATE_PID", "D", parsed.yaw_rate_pid_d);
        all_ok &= ReadRequiredInt(root, "exp_light", parsed.exp_light);
        all_ok &= ReadRequiredNestedNumber(root, "LEFT_WHEEL_PID", "P", parsed.left_wheel_pid.p);
        all_ok &= ReadRequiredNestedNumber(root, "LEFT_WHEEL_PID", "I", parsed.left_wheel_pid.i);
        all_ok &= ReadRequiredNestedNumber(root, "LEFT_WHEEL_PID", "D", parsed.left_wheel_pid.d);
        all_ok &= ReadRequiredNestedNumber(
            root, "LEFT_WHEEL_PID", "INTEGRAL_LIMIT", parsed.left_wheel_pid.integral_limit);
        all_ok &= ReadRequiredNestedNumber(root, "RIGHT_WHEEL_PID", "P", parsed.right_wheel_pid.p);
        all_ok &= ReadRequiredNestedNumber(root, "RIGHT_WHEEL_PID", "I", parsed.right_wheel_pid.i);
        all_ok &= ReadRequiredNestedNumber(root, "RIGHT_WHEEL_PID", "D", parsed.right_wheel_pid.d);
        all_ok &= ReadRequiredNestedNumber(
            root, "RIGHT_WHEEL_PID", "INTEGRAL_LIMIT", parsed.right_wheel_pid.integral_limit);
        all_ok &= ReadRequiredNestedString(root, "assistant_tcp", "host", parsed.assistant_tcp.host);
        all_ok &= ReadRequiredNestedInt(root, "assistant_tcp", "port", parsed.assistant_tcp.port);

        // --- 可选参数：缺失使用结构体默认值，格式错误标记 malformed ---
        bool optional_malformed = false;
        ReadOptionalInt(root, "low_voltage_raw_threshold", parsed.low_voltage_raw_threshold, optional_malformed);
        ReadOptionalInt(root, "control_period_ms", parsed.control_period_ms, optional_malformed);
        ReadOptionalInt(root, "perception_stale_ms", parsed.perception_stale_ms, optional_malformed);
        ReadOptionalInt(root, "pwm_limit", parsed.pwm_limit, optional_malformed);
        ReadOptionalInt(root, "raw_turn_output_limit", parsed.raw_turn_output_limit, optional_malformed);
        ReadOptionalInt(root, "pwm_floor", parsed.pwm_floor, optional_malformed);
        ReadOptionalBool(root, "prohibit_reverse_pwm", parsed.prohibit_reverse_pwm, optional_malformed);
        ReadOptionalInt(root,
                        "prohibit_reverse_pwm_step_limit",
                        parsed.prohibit_reverse_pwm_step_limit,
                        optional_malformed);
        ReadOptionalInt(root,
                        "motion_unveto_confirm_cycles",
                        parsed.motion_unveto_confirm_cycles,
                        optional_malformed);
        ReadOptionalInt(root, "motion_spinup_ms", parsed.motion_spinup_ms, optional_malformed);
        ReadOptionalNumber(root,
                           "motion_turn_limit_spinup",
                           parsed.motion_turn_limit_spinup,
                           optional_malformed);
        ReadOptionalInt(root, "motion_pwm_step_limit", parsed.motion_pwm_step_limit, optional_malformed);
        ReadOptionalInt(root, "motion_stop_ms", parsed.motion_stop_ms, optional_malformed);
        ReadOptionalInt(root,
                        "motion_stop_encoder_threshold",
                        parsed.motion_stop_encoder_threshold,
                        optional_malformed);
        ReadOptionalInt(root,
                        "motion_fault_rearm_hold_ms",
                        parsed.motion_fault_rearm_hold_ms,
                        optional_malformed);
        ReadOptionalNumber(root, "wheel_turn_target_scale", parsed.wheel_turn_target_scale, optional_malformed);
        ReadOptionalInt(root,
                        "control_snapshot_emit_interval_ms",
                        parsed.control_snapshot_emit_interval_ms,
                        optional_malformed);
        ReadOptionalBool(root, "assistant_enabled", parsed.assistant_enabled, optional_malformed);
        ReadOptionalBool(root, "steering_media_enabled", parsed.steering_media_enabled, optional_malformed);
        ReadOptionalInt(root, "steering_media_port", parsed.steering_media_port, optional_malformed);
        ReadOptionalInt(root,
                        "steering_media_publish_interval_ms",
                        parsed.steering_media_publish_interval_ms,
                        optional_malformed);
        ReadOptionalInt(root,
                        "low_voltage_sample_interval_ms",
                        parsed.low_voltage_sample_interval_ms,
                        optional_malformed);
        ReadOptionalInt(root, "camera_frame_width", parsed.camera_frame_width, optional_malformed);
        ReadOptionalInt(root, "camera_frame_height", parsed.camera_frame_height, optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "YAW_RATE_PID",
                                 "P",
                                 parsed.yaw_rate_pid_p,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "YAW_RATE_PID",
                                 "I",
                                 parsed.yaw_rate_pid_i,
                                 optional_malformed);
        ReadOptionalNestedBool(root, "BEV_PROJECTOR", "VALID", parsed.bev_projector.valid, optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_PROJECTOR",
                              "DEBUG_GRID_WIDTH",
                              parsed.bev_projector.debug_grid_width,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_PROJECTOR",
                              "DEBUG_GRID_HEIGHT",
                              parsed.bev_projector.debug_grid_height,
                              optional_malformed);
        ReadOptionalNestedString(root,
                                 "BEV_PROJECTOR",
                                 "PROJECTOR_ID",
                                 parsed.bev_projector.projector_id,
                                 optional_malformed);
        ReadOptionalNestedString(root,
                                 "BEV_PROJECTOR",
                                 "PROJECTOR_HASH",
                                 parsed.bev_projector.projector_hash,
                                 optional_malformed);
        // --- 可选参数：BEV 投影校准 ---
        for (int index = 0; index < static_cast<int>(port::kBevCalibrationPointCount); ++index) {
            ReadOptionalNestedNumber(root,
                                     "BEV_PROJECTOR",
                                     "SOURCE_ROW_" + std::to_string(index),
                                     parsed.bev_projector.source_points[static_cast<std::size_t>(index)].row_px,
                                     optional_malformed);
            ReadOptionalNestedNumber(root,
                                     "BEV_PROJECTOR",
                                     "SOURCE_COL_" + std::to_string(index),
                                     parsed.bev_projector.source_points[static_cast<std::size_t>(index)].col_px,
                                     optional_malformed);
            ReadOptionalNestedNumber(root,
                                     "BEV_PROJECTOR",
                                     "TARGET_FORWARD_" + std::to_string(index),
                                     parsed.bev_projector.target_points[static_cast<std::size_t>(index)].forward_m,
                                     optional_malformed);
            ReadOptionalNestedNumber(root,
                                     "BEV_PROJECTOR",
                                     "TARGET_LATERAL_" + std::to_string(index),
                                     parsed.bev_projector.target_points[static_cast<std::size_t>(index)].lateral_m,
                                     optional_malformed);
        }
        // --- 可选参数：BEV 几何配置 ---
        for (int index = 0; index < static_cast<int>(port::kBevReferenceSampleCount); ++index) {
            ReadOptionalNestedNumber(root,
                                     "BEV_GEOMETRY",
                                     "FORWARD_SAMPLE_" + std::to_string(index),
                                     parsed.bev_geometry.forward_samples_m[static_cast<std::size_t>(index)],
                                     optional_malformed);
        }
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "SEARCH_LATERAL_LIMIT_M",
                                 parsed.bev_geometry.search_lateral_limit_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "LATERAL_STEP_M",
                                 parsed.bev_geometry.lateral_step_m,
                                 optional_malformed);
        // --- BEV 分类与白点 hold 参数 ---
        ReadOptionalNestedNumber(root,
                                 "BEV_CLASSIFICATION",
                                 "WHITE_CONFIDENCE_MIN",
                                 parsed.bev_classification.white_confidence_min,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CLASSIFICATION",
                                 "UNKNOWN_CONFIDENCE_MIN",
                                 parsed.bev_classification.unknown_confidence_min,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_CLASSIFICATION",
                              "HOLD_LAST_MAX_CYCLES",
                              parsed.bev_classification.hold_last_max_cycles,
                              optional_malformed);
        // --- BEV 控制模型参数 ---
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "LOOKAHEAD_VISIBLE_RANGE_RATIO",
                                 parsed.bev_control_model.lookahead_visible_range_ratio,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "LOOKAHEAD_MIN_M",
                                 parsed.bev_control_model.lookahead_min_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "LOOKAHEAD_MAX_M",
                                 parsed.bev_control_model.lookahead_max_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "PURE_PURSUIT_GAIN",
                                 parsed.bev_control_model.pure_pursuit_gain,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "CURVATURE_COMMAND_LIMIT",
                                 parsed.bev_control_model.curvature_command_limit,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "CURVATURE_TO_YAW_RATE_TARGET_GAIN",
                                 parsed.bev_control_model.curvature_to_yaw_rate_target_gain,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_CONTROL_MODEL",
                              "MIN_LEADING_REFERENCE_SAMPLES",
                              parsed.bev_control_model.min_leading_reference_samples,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "LEFT_WHEEL_PID",
                                 "MEASUREMENT_FILTER_ALPHA",
                                 parsed.left_wheel_pid.measurement_filter_alpha,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "RIGHT_WHEEL_PID",
                                 "MEASUREMENT_FILTER_ALPHA",
                                 parsed.right_wheel_pid.measurement_filter_alpha,
                                 optional_malformed);
        // 综合校验：必填字段成功 + 无格式错误
        all_ok = all_ok && !optional_malformed;

        if (!all_ok) {
            out = port::RuntimeParameters{};
            out.loaded_from_defaults = true;
            out.parse_failure = true;
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "params.parse",
                              "parameter parse failure (missing or malformed required fields), using defaults",
                              port::NowMs()});
        } else {
            parsed.loaded_from_defaults = false;
            parsed.parse_failure = false;
            out = parsed;
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "params.loaded",
                              "runtime parameters loaded from " + path,
                              port::NowMs()});
        }
        return true;
    }

    // 从 JSON 文件加载硬件配置（camera/imu/encoder/motor/timer/persistence/display 子系统）
    bool LoadHardwareProfile(const std::string& path,
                             port::HardwareProfile& out,
                             port::DiagnosticSink& diagnostics) override {
        std::string text;
        if (!ReadText(path, text)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.missing",
                              "hardware profile missing; refusing startup in fail-closed mode: " + path,
                              port::NowMs()});
            return false;
        }

        cv::FileStorage json;
        if (!ParseJsonObject(text, json)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse",
                              "hardware profile parse failure (invalid JSON object); refusing startup in fail-closed mode",
                              port::NowMs()});
            return false;
        }

        const cv::FileNode root = json.root();
        port::HardwareProfile parsed{};
        if (!ParseProfileBlock(root, "camera", parsed.camera)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.camera",
                              ProfileBlockError("camera"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "imu", parsed.imu)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.imu",
                              ProfileBlockError("imu"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "encoder", parsed.encoder)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.encoder",
                              ProfileBlockError("encoder"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "motor", parsed.motor)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.motor",
                              ProfileBlockError("motor"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "timer", parsed.timer)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.timer",
                              ProfileBlockError("timer"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "persistence", parsed.persistence)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.persistence",
                              ProfileBlockError("persistence"),
                              port::NowMs()});
            return false;
        }
        if (!ParseProfileBlock(root, "display", parsed.display)) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "profile.parse.display",
                              ProfileBlockError("display"),
                              port::NowMs()});
            return false;
        }

        out = parsed;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "profile.loaded",
                          "hardware profile loaded from " + path,
                          port::NowMs()});
        return true;
    }

    // 应用启动关键参数有效性校验
    void ApplyStartupCritical(port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) override {
        const bool exposure_ok = params.exp_light >= 0 && params.exp_light <= 2500;
        params.startup_critical_applied = exposure_ok;
        diagnostics.Emit({params.startup_critical_applied ? port::DiagnosticLevel::kInfo
                                                          : port::DiagnosticLevel::kFailSafe,
                          "params.critical.apply",
                          params.startup_critical_applied
                              ? "applied startup-critical exp_light before adapter bring-up"
                              : "startup-critical exp_light invalid; refusing actuator arming",
                          port::NowMs()});
        if (params.startup_critical_applied && params.exp_light != 65) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "params.critical.exp_light",
                              "non-default exp_light requests explicit true-baseline camera support; direct-match camera path may fail closed without an adaptation hook",
                              port::NowMs()});
        }
    }
};

}  // namespace

std::unique_ptr<port::IParamStore> MakeParamStore() {
    return std::make_unique<ParamStore>();
}

}  // namespace ls2k::platform
