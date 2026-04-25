#include "port/platform_adapter.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include <opencv2/core/persistence.hpp>

namespace ls2k::platform {
namespace {

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

bool ReadNumberNode(const cv::FileNode& node, double& value) {
    if (node.empty() || (!node.isInt() && !node.isReal())) {
        return false;
    }
    value = static_cast<double>(node.real());
    return true;
}

bool ReadRequiredNumber(const cv::FileNode& root, const char* key, double& value) {
    return ReadNumberNode(root[key], value);
}

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

bool ReadStringValue(const cv::FileNode& node, std::string& value) {
    if (node.empty() || !node.isString()) {
        return false;
    }
    value = static_cast<std::string>(node);
    return true;
}

void ReadOptionalNumber(const cv::FileNode& root, const char* key, double& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadNumberNode(node, value)) {
        malformed = true;
    }
}

void ReadOptionalInt(const cv::FileNode& root, const char* key, int& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadIntegerValue(node, value)) {
        malformed = true;
    }
}

void ReadOptionalBool(const cv::FileNode& root, const char* key, bool& value, bool& malformed) {
    const cv::FileNode node = root[key];
    if (node.empty()) {
        return;
    }
    if (!ReadBoolValue(node, value)) {
        malformed = true;
    }
}

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

bool ReadRequiredString(const cv::FileNode& root, const char* key, std::string& value) {
    const cv::FileNode node = root[key];
    if (node.empty() || !node.isString()) {
        return false;
    }
    value = static_cast<std::string>(node);
    return true;
}

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

std::string ProfileBlockError(const char* key) {
    return std::string("hardware profile parse failure for subsystem '") + key +
           "' (missing block or malformed mode/hook)";
}

class ParamStore final : public port::IParamStore {
public:
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
        all_ok &= ReadRequiredNumber(root, "Speed_base", parsed.Speed_base);
        all_ok &= ReadRequiredNumber(root, "see_max", parsed.see_max);
        all_ok &= ReadRequiredNestedNumber(root, "PID_TURN_CAMERA", "D", parsed.pid_turn_camera_d);
        all_ok &= ReadRequiredNestedNumber(root, "PID_TURN_GYRO_CAMERA", "D", parsed.pid_turn_gyro_camera_d);
        all_ok &= ReadRequiredInt(root, "P_Mode", parsed.P_Mode);
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

        bool optional_malformed = false;
        ReadOptionalInt(root, "emergency_threshold", parsed.emergency_threshold, optional_malformed);
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
        ReadOptionalInt(root,
                        "assistant_waveform_publish_interval_ms",
                        parsed.assistant_waveform_publish_interval_ms,
                        optional_malformed);
        ReadOptionalInt(root,
                        "assistant_image_publish_interval_ms",
                        parsed.assistant_image_publish_interval_ms,
                        optional_malformed);
        ReadOptionalBool(root, "steering_media_enabled", parsed.steering_media_enabled, optional_malformed);
        ReadOptionalInt(root, "steering_media_port", parsed.steering_media_port, optional_malformed);
        ReadOptionalInt(root,
                        "steering_media_publish_interval_ms",
                        parsed.steering_media_publish_interval_ms,
                        optional_malformed);
        ReadOptionalInt(root, "camera_frame_width", parsed.camera_frame_width, optional_malformed);
        ReadOptionalInt(root, "camera_frame_height", parsed.camera_frame_height, optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "PID_TURN_CAMERA",
                                 "P",
                                 parsed.pid_turn_camera_p,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "PID_TURN_CAMERA",
                                 "P_SCALE",
                                 parsed.pid_turn_camera_p_scale,
                                 optional_malformed);
        ReadOptionalNestedBool(root,
                               "PID_TURN_CAMERA",
                               "USE_FUZZY",
                               parsed.pid_turn_camera_use_fuzzy,
                               optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "PID_TURN_GYRO_CAMERA",
                                 "P",
                                 parsed.pid_turn_gyro_camera_p,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "PID_TURN_GYRO_CAMERA",
                                 "I",
                                 parsed.pid_turn_gyro_camera_i,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "LOWER_ROW_START",
                              parsed.scene_wide_classifier.lower_row_start,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "LOWER_ROW_END",
                              parsed.scene_wide_classifier.lower_row_end,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "MIDDLE_ROW_START",
                              parsed.scene_wide_classifier.middle_row_start,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "MIDDLE_ROW_END",
                              parsed.scene_wide_classifier.middle_row_end,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "UPPER_ROW_START",
                              parsed.scene_wide_classifier.upper_row_start,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "UPPER_ROW_END",
                              parsed.scene_wide_classifier.upper_row_end,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "ROW_STEP",
                              parsed.scene_wide_classifier.row_step,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "EDGE_MARGIN_PX",
                              parsed.scene_wide_classifier.edge_margin_px,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "UPPER_FULL_SPAN_WIDTH_RATIO",
                                 parsed.scene_wide_classifier.upper_full_span_width_ratio,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO",
                                 parsed.scene_wide_classifier.special_wide_lower_width_min_ratio,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "SPECIAL_WIDE_VALID_ROWS_MIN",
                              parsed.scene_wide_classifier.special_wide_valid_rows_min,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "CIRCLE_OPEN_MIN_PX",
                              parsed.scene_wide_classifier.circle_open_min_px,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "CIRCLE_CONTRACT_MIN_PX",
                              parsed.scene_wide_classifier.circle_contract_min_px,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "CROSS_UPPER_FULL_SPAN_MIN_RATIO",
                                 parsed.scene_wide_classifier.cross_upper_full_span_min_ratio,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "TO_CROSS_MARGIN",
                                 parsed.scene_wide_classifier.to_cross_margin,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "TO_CIRCLE_MARGIN",
                                 parsed.scene_wide_classifier.to_circle_margin,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "ENTER_CONFIRM_CYCLES",
                              parsed.scene_wide_classifier.enter_confirm_cycles,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "SCENE_WIDE_CLASSIFIER",
                              "EXIT_CONFIRM_CYCLES",
                              parsed.scene_wide_classifier.exit_confirm_cycles,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "CROSS_WEIGHT_FULL_SPAN",
                                 parsed.scene_wide_classifier.cross_weight_full_span,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "CROSS_WEIGHT_BOTH_OPEN",
                                 parsed.scene_wide_classifier.cross_weight_both_open,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "CIRCLE_WEIGHT_OPEN",
                                 parsed.scene_wide_classifier.circle_weight_open,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "SCENE_WIDE_CLASSIFIER",
                                 "CIRCLE_WEIGHT_CONTRACT",
                                 parsed.scene_wide_classifier.circle_weight_contract,
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

    void ApplyStartupCritical(port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) override {
        const bool p_mode_ok = params.P_Mode >= 0 && params.P_Mode <= 4;
        const bool exposure_ok = params.exp_light >= 0 && params.exp_light <= 2500;
        params.startup_critical_applied = p_mode_ok && exposure_ok;
        diagnostics.Emit({params.startup_critical_applied ? port::DiagnosticLevel::kInfo
                                                          : port::DiagnosticLevel::kFailSafe,
                          "params.critical.apply",
                          params.startup_critical_applied
                              ? "applied startup-critical fields P_Mode and exp_light before adapter bring-up"
                              : "startup-critical fields invalid; refusing actuator arming",
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
