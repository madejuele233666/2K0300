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

void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const char* child,
                              float& value,
                              bool& malformed) {
    double temporary = static_cast<double>(value);
    ReadOptionalNestedNumber(root, parent, child, temporary, malformed);
    value = static_cast<float>(temporary);
}

void ReadOptionalNestedNumber(const cv::FileNode& root,
                              const char* parent,
                              const std::string& child,
                              float& value,
                              bool& malformed) {
    double temporary = static_cast<double>(value);
    ReadOptionalNestedNumber(root, parent, child, temporary, malformed);
    value = static_cast<float>(temporary);
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
                                 "D",
                                 parsed.pid_turn_camera_d,
                                 optional_malformed);
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
        for (int index = 0; index < static_cast<int>(port::kBevTrackSampleCount); ++index) {
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
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "NOMINAL_LANE_WIDTH_M",
                                 parsed.bev_geometry.nominal_lane_width_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "MIN_LANE_WIDTH_M",
                                 parsed.bev_geometry.min_lane_width_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "MAX_LANE_WIDTH_M",
                                 parsed.bev_geometry.max_lane_width_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "MIN_VISIBLE_RANGE_M",
                                 parsed.bev_geometry.min_visible_range_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "MIN_TRACK_CONFIDENCE",
                                 parsed.bev_geometry.min_track_confidence,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_GEOMETRY",
                                 "CONTINUITY_BREAK_THRESHOLD_M",
                                 parsed.bev_geometry.continuity_break_threshold_m,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_GEOMETRY",
                              "SAMPLE_ROW_STEP_PX",
                              parsed.bev_geometry.sample_row_step_px,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_GEOMETRY",
                              "IMAGE_BORDER_TRUNCATION_MARGIN_PX",
                              parsed.bev_geometry.image_border_truncation_margin_px,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "BEND_SEVERITY_CONFIRM",
                                 parsed.bev_scene_fsm.bend_severity_confirm,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "CROSS_EXPAND_RATIO_MIN",
                                 parsed.bev_scene_fsm.cross_expand_ratio_min,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "CROSS_BILATERAL_OPEN_MIN_M",
                                 parsed.bev_scene_fsm.cross_bilateral_open_min_m,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_SCENE_FSM",
                              "CROSS_CONFIRM_CYCLES",
                              parsed.bev_scene_fsm.cross_confirm_cycles,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_SCENE_FSM",
                              "CROSS_HOLD_CYCLES",
                              parsed.bev_scene_fsm.cross_hold_cycles,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "ZEBRA_TRANSITION_DENSITY_MIN",
                                 parsed.bev_scene_fsm.zebra_transition_density_min,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_SCENE_FSM",
                              "ZEBRA_HOLD_CYCLES",
                              parsed.bev_scene_fsm.zebra_hold_cycles,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "CIRCLE_OPEN_SCORE_MIN",
                                 parsed.bev_scene_fsm.circle_open_score_min,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "CIRCLE_CONTRACT_SCORE_MIN",
                                 parsed.bev_scene_fsm.circle_contract_score_min,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "CIRCLE_OPPOSITE_HEADING_ABS_MAX",
                                 parsed.bev_scene_fsm.circle_opposite_heading_abs_max,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_SCENE_FSM",
                              "CIRCLE_CONFIRM_CYCLES",
                              parsed.bev_scene_fsm.circle_confirm_cycles,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_SCENE_FSM",
                              "CIRCLE_RELEASE_CYCLES",
                              parsed.bev_scene_fsm.circle_release_cycles,
                              optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_SCENE_FSM",
                                 "RELEASE_TRACK_CONFIDENCE_MIN",
                                 parsed.bev_scene_fsm.release_track_confidence_min,
                                 optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_CONTROL_MODEL",
                              "NEAR_SAMPLE_INDEX",
                              parsed.bev_control_model.near_sample_index,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_CONTROL_MODEL",
                              "FAR_SAMPLE_INDEX",
                              parsed.bev_control_model.far_sample_index,
                              optional_malformed);
        ReadOptionalNestedInt(root,
                              "BEV_CONTROL_MODEL",
                              "CURVATURE_SAMPLE_INDEX",
                              parsed.bev_control_model.curvature_sample_index,
                              optional_malformed);
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
                                 "HEADING_CURVATURE_GAIN",
                                 parsed.bev_control_model.heading_curvature_gain,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "CURVATURE_FEEDFORWARD_GAIN",
                                 parsed.bev_control_model.curvature_feedforward_gain,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "CURVATURE_COMMAND_LIMIT",
                                 parsed.bev_control_model.curvature_command_limit,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "CURVATURE_TO_W_TARGET_GAIN",
                                 parsed.bev_control_model.curvature_to_w_target_gain,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "LOW_CONFIDENCE_THRESHOLD",
                                 parsed.bev_control_model.low_confidence_threshold,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "STEERING_SUPPRESSION_CONFIDENCE",
                                 parsed.bev_control_model.steering_suppression_confidence,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "LOW_VISIBLE_RANGE_M",
                                 parsed.bev_control_model.low_visible_range_m,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "MIN_GAIN_SCALE",
                                 parsed.bev_control_model.min_gain_scale,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "MIN_SPEED_LIMIT_SCALE",
                                 parsed.bev_control_model.min_speed_limit_scale,
                                 optional_malformed);
        ReadOptionalNestedNumber(root,
                                 "BEV_CONTROL_MODEL",
                                 "MAX_REFERENCE_BIAS_M",
                                 parsed.bev_control_model.max_reference_bias_m,
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
            if (std::fabs(out.pid_turn_camera_d) > 1e-6) {
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "params.deprecated.pid_turn_camera_d",
                                  "PID_TURN_CAMERA.D is deprecated and ignored by the current camera outer-loop; keep it at 0.0",
                                  port::NowMs()});
            }
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
