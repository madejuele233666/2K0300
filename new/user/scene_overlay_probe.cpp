#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"

namespace {

using ls2k::port::BEVPathSample;
using ls2k::port::BEVPoint;
using ls2k::port::ImagePoint;
using ls2k::port::LegacyCameraFrame;
using ls2k::port::RuntimeParameters;

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels{};

    RgbImage(int image_width, int image_height, Color fill) : width(image_width), height(image_height) {
        pixels.resize(static_cast<std::size_t>(width * height * 3));
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                const std::size_t index =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(col)) *
                    3U;
                pixels[index] = fill.r;
                pixels[index + 1U] = fill.g;
                pixels[index + 2U] = fill.b;
            }
        }
    }

    void SetPixel(int x, int y, const Color& color) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::size_t index =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            3U;
        pixels[index] = color.r;
        pixels[index + 1U] = color.g;
        pixels[index + 2U] = color.b;
    }

    void BlendPixel(int x, int y, const Color& color, float alpha) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const float clamped_alpha = std::clamp(alpha, 0.0F, 1.0F);
        const std::size_t index =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            3U;
        pixels[index] = static_cast<std::uint8_t>(
            std::lround(static_cast<float>(pixels[index]) * (1.0F - clamped_alpha) +
                        static_cast<float>(color.r) * clamped_alpha));
        pixels[index + 1U] = static_cast<std::uint8_t>(
            std::lround(static_cast<float>(pixels[index + 1U]) * (1.0F - clamped_alpha) +
                        static_cast<float>(color.g) * clamped_alpha));
        pixels[index + 2U] = static_cast<std::uint8_t>(
            std::lround(static_cast<float>(pixels[index + 2U]) * (1.0F - clamped_alpha) +
                        static_cast<float>(color.b) * clamped_alpha));
    }
};

struct PanelLayout {
    int raw_x = 0;
    int raw_y = 0;
    int raw_width = 0;
    int raw_height = 0;
    int bev_x = 0;
    int bev_y = 0;
    int bev_width = 0;
    int bev_height = 0;
};

struct RuntimeSnapshotFacts {
    bool available = false;
    std::string source_path{};
    int frame_id = -1;
    bool has_near_lateral_error = false;
    float near_lateral_error = 0.0F;
    bool has_far_heading_error = false;
    float far_heading_error = 0.0F;
    bool has_visible_range_m = false;
    float visible_range_m = 0.0F;
    bool has_track_confidence = false;
    float track_confidence = 0.0F;
    bool has_track_valid = false;
    bool track_valid = false;
    bool has_threshold = false;
    int threshold = 0;
    bool has_raw_turn_output = false;
    float raw_turn_output = 0.0F;
    bool has_applied_turn_output = false;
    float applied_turn_output = 0.0F;
    bool has_lookahead = false;
    float lookahead_distance_m = 0.0F;
    float lookahead_lateral_error = 0.0F;
    bool has_compat_reference = false;
    int compatibility_row = 0;
    int compatibility_col = 0;
    std::string active_module{};
    std::string scene_phase{};
    std::string reference_mode{};
};

LegacyCameraFrame ReadRawFrame(const std::string& path) {
    LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open raw frame: " + path);
    }
    input.read(reinterpret_cast<char*>(frame.gray.data()),
               static_cast<std::streamsize>(frame.width * frame.height));
    if (input.gcount() != frame.width * frame.height) {
        throw std::runtime_error("unexpected raw frame size: " + path);
    }
    return frame;
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open params json: " + path);
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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
        const char next = (i + 1U < text.size()) ? text[i + 1U] : '\0';
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
            } else if (c == '*' && next == '/') {
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
        if (c == '"') {
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

bool IsJsonSpace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

std::size_t SkipJsonSpace(const std::string& text, std::size_t pos) {
    while (pos < text.size() && IsJsonSpace(text[pos])) {
        ++pos;
    }
    return pos;
}

std::size_t FindJsonKey(const std::string& text, const std::string& key) {
    return text.find('"' + key + '"');
}

bool ExtractBalancedBlock(const std::string& text,
                          const std::string& key,
                          char open_char,
                          char close_char,
                          std::string& out) {
    const std::size_t key_pos = FindJsonKey(text, key);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = text.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }
    std::size_t pos = SkipJsonSpace(text, colon_pos + 1U);
    if (pos >= text.size() || text[pos] != open_char) {
        return false;
    }

    const std::size_t begin = pos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == open_char) {
            ++depth;
        } else if (c == close_char) {
            --depth;
            if (depth == 0) {
                out = text.substr(begin + 1U, pos - begin - 1U);
                return true;
            }
        }
    }
    return false;
}

bool ExtractObjectBlock(const std::string& text, const std::string& key, std::string& out) {
    return ExtractBalancedBlock(text, key, '{', '}', out);
}

bool ExtractArrayBlock(const std::string& text, const std::string& key, std::string& out) {
    return ExtractBalancedBlock(text, key, '[', ']', out);
}

bool ExtractScalarValue(const std::string& text, const std::string& key, std::string& out) {
    const std::size_t key_pos = FindJsonKey(text, key);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = text.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }
    std::size_t pos = SkipJsonSpace(text, colon_pos + 1U);
    if (pos >= text.size()) {
        return false;
    }
    if (text[pos] == '"') {
        ++pos;
        std::string value;
        bool escaped = false;
        for (; pos < text.size(); ++pos) {
            const char c = text[pos];
            if (escaped) {
                value.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                out = value;
                return true;
            } else {
                value.push_back(c);
            }
        }
        return false;
    }

    const std::size_t begin = pos;
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']' &&
           text[pos] != '\n' && text[pos] != '\r') {
        ++pos;
    }
    std::size_t end = pos;
    while (end > begin && IsJsonSpace(text[end - 1U])) {
        --end;
    }
    out = text.substr(begin, end - begin);
    return !out.empty();
}

bool ReadFloatField(const std::string& block, const std::string& key, float& out) {
    std::string value;
    if (!ExtractScalarValue(block, key, value)) {
        return false;
    }
    out = std::stof(value);
    return true;
}

bool ReadDoubleField(const std::string& block, const std::string& key, double& out) {
    std::string value;
    if (!ExtractScalarValue(block, key, value)) {
        return false;
    }
    out = std::stod(value);
    return true;
}

bool ReadIntField(const std::string& block, const std::string& key, int& out) {
    std::string value;
    if (!ExtractScalarValue(block, key, value)) {
        return false;
    }
    out = std::stoi(value);
    return true;
}

bool ReadBoolField(const std::string& block, const std::string& key, bool& out) {
    std::string value;
    if (!ExtractScalarValue(block, key, value)) {
        return false;
    }
    out = value == "true" || value == "TRUE" || value == "1";
    return true;
}

bool ReadStringField(const std::string& block, const std::string& key, std::string& out) {
    return ExtractScalarValue(block, key, out);
}

std::vector<float> ParseFloatArray(const std::string& array_text) {
    std::vector<float> values;
    std::size_t pos = 0;
    while (pos < array_text.size()) {
        pos = SkipJsonSpace(array_text, pos);
        if (pos >= array_text.size()) {
            break;
        }
        const std::size_t begin = pos;
        while (pos < array_text.size() && array_text[pos] != ',') {
            ++pos;
        }
        std::size_t end = pos;
        while (end > begin && IsJsonSpace(array_text[end - 1U])) {
            --end;
        }
        if (end > begin) {
            values.push_back(std::stof(array_text.substr(begin, end - begin)));
        }
        if (pos < array_text.size() && array_text[pos] == ',') {
            ++pos;
        }
    }
    return values;
}

template <std::size_t N>
void ReadFloatArrayField(const std::string& block, const std::string& key, std::array<float, N>& out) {
    std::string array_text;
    if (!ExtractArrayBlock(block, key, array_text)) {
        return;
    }
    const std::vector<float> values = ParseFloatArray(array_text);
    for (std::size_t i = 0; i < std::min(N, values.size()); ++i) {
        out[i] = values[i];
    }
}

void LoadProjectorParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_PROJECTOR", block)) {
        return;
    }
    ReadBoolField(block, "VALID", params.bev_projector.valid);
    ReadIntField(block, "DEBUG_GRID_WIDTH", params.bev_projector.debug_grid_width);
    ReadIntField(block, "DEBUG_GRID_HEIGHT", params.bev_projector.debug_grid_height);
    ReadStringField(block, "PROJECTOR_ID", params.bev_projector.projector_id);
    ReadStringField(block, "PROJECTOR_HASH", params.bev_projector.projector_hash);
    for (std::size_t i = 0; i < params.bev_projector.source_points.size(); ++i) {
        ReadFloatField(block, "SOURCE_ROW_" + std::to_string(i), params.bev_projector.source_points[i].row_px);
        ReadFloatField(block, "SOURCE_COL_" + std::to_string(i), params.bev_projector.source_points[i].col_px);
        ReadFloatField(block,
                       "TARGET_FORWARD_" + std::to_string(i),
                       params.bev_projector.target_points[i].forward_m);
        ReadFloatField(block,
                       "TARGET_LATERAL_" + std::to_string(i),
                       params.bev_projector.target_points[i].lateral_m);
    }
}

void LoadGeometryParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_GEOMETRY", block)) {
        return;
    }
    for (std::size_t i = 0; i < params.bev_geometry.forward_samples_m.size(); ++i) {
        ReadFloatField(block, "FORWARD_SAMPLE_" + std::to_string(i), params.bev_geometry.forward_samples_m[i]);
    }
    ReadFloatField(block, "SEARCH_LATERAL_LIMIT_M", params.bev_geometry.search_lateral_limit_m);
    ReadFloatField(block, "LATERAL_STEP_M", params.bev_geometry.lateral_step_m);
    ReadFloatField(block, "NOMINAL_LANE_WIDTH_M", params.bev_geometry.nominal_lane_width_m);
    ReadFloatField(block, "MIN_LANE_WIDTH_M", params.bev_geometry.min_lane_width_m);
    ReadFloatField(block, "MAX_LANE_WIDTH_M", params.bev_geometry.max_lane_width_m);
    ReadFloatField(block, "MIN_VISIBLE_RANGE_M", params.bev_geometry.min_visible_range_m);
    ReadFloatField(block, "MIN_TRACK_CONFIDENCE", params.bev_geometry.min_track_confidence);
    ReadFloatField(block, "CONTINUITY_BREAK_THRESHOLD_M", params.bev_geometry.continuity_break_threshold_m);
    ReadIntField(block, "SAMPLE_ROW_STEP_PX", params.bev_geometry.sample_row_step_px);
    ReadIntField(block, "IMAGE_BORDER_TRUNCATION_MARGIN_PX", params.bev_geometry.image_border_truncation_margin_px);
}

void LoadTopologySamplerParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_TOPOLOGY_SAMPLER", block)) {
        return;
    }
    ReadFloatArrayField(block, "FORWARD_SAMPLES_M", params.bev_topology_sampler.forward_samples_m);
    ReadFloatField(block, "LATERAL_MIN_M", params.bev_topology_sampler.lateral_min_m);
    ReadFloatField(block, "LATERAL_MAX_M", params.bev_topology_sampler.lateral_max_m);
    ReadFloatField(block, "LATERAL_STEP_M", params.bev_topology_sampler.lateral_step_m);
    ReadIntField(block, "SAMPLE_PATCH_RADIUS_PX", params.bev_topology_sampler.sample_patch_radius_px);
    ReadFloatField(block, "DRIVABLE_CONFIDENCE_MIN", params.bev_topology_sampler.drivable_confidence_min);
    ReadFloatField(block, "UNKNOWN_CONFIDENCE_MIN", params.bev_topology_sampler.unknown_confidence_min);
}

void LoadCorridorGraphParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_CORRIDOR_GRAPH", block)) {
        return;
    }
    ReadFloatField(block, "NOMINAL_LANE_WIDTH_M", params.bev_corridor_graph.nominal_lane_width_m);
    ReadFloatField(block, "MIN_INTERVAL_WIDTH_M", params.bev_corridor_graph.min_interval_width_m);
    ReadFloatField(block, "MAX_INTERVAL_WIDTH_M", params.bev_corridor_graph.max_interval_width_m);
    ReadFloatField(block, "MAX_CENTER_JUMP_M", params.bev_corridor_graph.max_center_jump_m);
    ReadFloatField(block, "MAX_WIDTH_CHANGE_M", params.bev_corridor_graph.max_width_change_m);
    ReadFloatField(block, "MAX_CURVATURE_ABS", params.bev_corridor_graph.max_curvature_abs);
    ReadFloatField(block, "PRIOR_CARRY_CONFIDENCE_SCALE", params.bev_corridor_graph.prior_carry_confidence_scale);
}

void LoadTopologyEvidenceParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_TOPOLOGY_EVIDENCE", block)) {
        return;
    }
    ReadFloatField(block, "CROSS_ENTER_SCORE", params.bev_topology_evidence.cross_enter_score);
    ReadFloatField(block, "CROSS_RELEASE_SCORE", params.bev_topology_evidence.cross_release_score);
    ReadFloatField(block, "CIRCLE_ENTER_SCORE", params.bev_topology_evidence.circle_enter_score);
    ReadFloatField(block, "CIRCLE_RELEASE_SCORE", params.bev_topology_evidence.circle_release_score);
    ReadFloatField(block, "ZEBRA_ENTER_SCORE", params.bev_topology_evidence.zebra_enter_score);
    ReadFloatField(block, "ZEBRA_RELEASE_SCORE", params.bev_topology_evidence.zebra_release_score);
    ReadFloatField(block, "ORDINARY_RELEASE_SCORE", params.bev_topology_evidence.ordinary_release_score);
    ReadFloatField(block, "EVIDENCE_DECAY", params.bev_topology_evidence.evidence_decay);
}

void LoadReferencePolicyParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_REFERENCE_POLICY", block)) {
        return;
    }
    ReadIntField(block, "HOLD_LAST_MAX_CYCLES", params.bev_reference_policy.hold_last_max_cycles);
    ReadIntField(block, "BLEND_MIN_CYCLES", params.bev_reference_policy.blend_min_cycles);
    ReadFloatField(block, "ARC_FOLLOW_CONFIDENCE_MIN", params.bev_reference_policy.arc_follow_confidence_min);
    ReadFloatField(block,
                   "STABLE_BOUNDARY_CONFIDENCE_MIN",
                   params.bev_reference_policy.stable_boundary_confidence_min);
}

void LoadSceneFsmParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_SCENE_FSM", block)) {
        return;
    }
    ReadFloatField(block, "BEND_SEVERITY_CONFIRM", params.bev_scene_fsm.bend_severity_confirm);
    ReadFloatField(block, "CROSS_EXPAND_RATIO_MIN", params.bev_scene_fsm.cross_expand_ratio_min);
    ReadFloatField(block, "CROSS_BILATERAL_OPEN_MIN_M", params.bev_scene_fsm.cross_bilateral_open_min_m);
    ReadIntField(block, "CROSS_CONFIRM_CYCLES", params.bev_scene_fsm.cross_confirm_cycles);
    ReadIntField(block, "CROSS_HOLD_CYCLES", params.bev_scene_fsm.cross_hold_cycles);
    ReadFloatField(block, "ZEBRA_TRANSITION_DENSITY_MIN", params.bev_scene_fsm.zebra_transition_density_min);
    ReadIntField(block, "ZEBRA_HOLD_CYCLES", params.bev_scene_fsm.zebra_hold_cycles);
    ReadFloatField(block, "CIRCLE_OPEN_SCORE_MIN", params.bev_scene_fsm.circle_open_score_min);
    ReadFloatField(block, "CIRCLE_CONTRACT_SCORE_MIN", params.bev_scene_fsm.circle_contract_score_min);
    ReadFloatField(block,
                   "CIRCLE_OPPOSITE_HEADING_ABS_MAX",
                   params.bev_scene_fsm.circle_opposite_heading_abs_max);
    ReadIntField(block, "CIRCLE_CONFIRM_CYCLES", params.bev_scene_fsm.circle_confirm_cycles);
    ReadIntField(block, "CIRCLE_RELEASE_CYCLES", params.bev_scene_fsm.circle_release_cycles);
    ReadFloatField(block, "RELEASE_TRACK_CONFIDENCE_MIN", params.bev_scene_fsm.release_track_confidence_min);
}

void LoadControlModelParams(const std::string& json, RuntimeParameters& params) {
    std::string block;
    if (!ExtractObjectBlock(json, "BEV_CONTROL_MODEL", block)) {
        return;
    }
    ReadIntField(block, "NEAR_SAMPLE_INDEX", params.bev_control_model.near_sample_index);
    ReadIntField(block, "FAR_SAMPLE_INDEX", params.bev_control_model.far_sample_index);
    ReadIntField(block, "CURVATURE_SAMPLE_INDEX", params.bev_control_model.curvature_sample_index);
    ReadDoubleField(block, "LOOKAHEAD_VISIBLE_RANGE_RATIO", params.bev_control_model.lookahead_visible_range_ratio);
    ReadDoubleField(block, "LOOKAHEAD_MIN_M", params.bev_control_model.lookahead_min_m);
    ReadDoubleField(block, "LOOKAHEAD_MAX_M", params.bev_control_model.lookahead_max_m);
    ReadDoubleField(block, "PURE_PURSUIT_GAIN", params.bev_control_model.pure_pursuit_gain);
    ReadDoubleField(block, "HEADING_CURVATURE_GAIN", params.bev_control_model.heading_curvature_gain);
    ReadDoubleField(block, "CURVATURE_FEEDFORWARD_GAIN", params.bev_control_model.curvature_feedforward_gain);
    ReadDoubleField(block, "CURVATURE_COMMAND_LIMIT", params.bev_control_model.curvature_command_limit);
    ReadDoubleField(block, "CURVATURE_TO_W_TARGET_GAIN", params.bev_control_model.curvature_to_w_target_gain);
    ReadDoubleField(block, "LOW_CONFIDENCE_THRESHOLD", params.bev_control_model.low_confidence_threshold);
    ReadDoubleField(block,
                    "STEERING_SUPPRESSION_CONFIDENCE",
                    params.bev_control_model.steering_suppression_confidence);
    ReadDoubleField(block, "LOW_VISIBLE_RANGE_M", params.bev_control_model.low_visible_range_m);
    ReadDoubleField(block, "MIN_GAIN_SCALE", params.bev_control_model.min_gain_scale);
    ReadDoubleField(block, "MIN_SPEED_LIMIT_SCALE", params.bev_control_model.min_speed_limit_scale);
    ReadDoubleField(block, "MAX_REFERENCE_BIAS_M", params.bev_control_model.max_reference_bias_m);
}

std::string LoadRuntimeParamsJson(const std::string& path, RuntimeParameters& params) {
    if (path.empty()) {
        return "built-in";
    }
    const std::string json = StripJsonComments(ReadTextFile(path));
    ReadDoubleField(json, "see_max", params.see_max);
    ReadIntField(json, "emergency_threshold", params.emergency_threshold);
    ReadIntField(json, "exp_light", params.exp_light);
    ReadIntField(json, "camera_frame_width", params.camera_frame_width);
    ReadIntField(json, "camera_frame_height", params.camera_frame_height);
    LoadProjectorParams(json, params);
    LoadGeometryParams(json, params);
    LoadTopologySamplerParams(json, params);
    LoadCorridorGraphParams(json, params);
    LoadTopologyEvidenceParams(json, params);
    LoadReferencePolicyParams(json, params);
    LoadSceneFsmParams(json, params);
    LoadControlModelParams(json, params);
    return path;
}

std::string DirName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0U) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

bool FileExists(const std::string& path) {
    std::ifstream input(path);
    return input.good();
}

int ParseFrameIdFromPath(const std::string& path) {
    const std::size_t frame_pos = path.find("frame-");
    if (frame_pos == std::string::npos) {
        return -1;
    }
    std::size_t pos = frame_pos + 6U;
    std::string digits;
    while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') {
        digits.push_back(path[pos]);
        ++pos;
    }
    if (digits.empty()) {
        return -1;
    }
    return std::stoi(digits);
}

void ApplySnapshotBlock(const std::string& block,
                        const std::string& source_path,
                        int frame_id,
                        RuntimeSnapshotFacts& facts) {
    facts.available = true;
    facts.source_path = source_path;
    facts.frame_id = frame_id;
    facts.has_near_lateral_error = ReadFloatField(block, "near_lateral_error", facts.near_lateral_error);
    facts.has_far_heading_error = ReadFloatField(block, "far_heading_error", facts.far_heading_error);
    facts.has_visible_range_m = ReadFloatField(block, "visible_range_m", facts.visible_range_m);
    facts.has_track_confidence = ReadFloatField(block, "track_confidence", facts.track_confidence);
    facts.has_track_valid = ReadBoolField(block, "track_valid", facts.track_valid);
    facts.has_threshold = ReadIntField(block, "threshold", facts.threshold);
    facts.has_raw_turn_output = ReadFloatField(block, "raw_turn_output", facts.raw_turn_output);
    facts.has_applied_turn_output = ReadFloatField(block, "applied_turn_output", facts.applied_turn_output);
    facts.has_lookahead =
        ReadFloatField(block, "lookahead_distance_m", facts.lookahead_distance_m) &&
        ReadFloatField(block, "lookahead_lateral_error", facts.lookahead_lateral_error);
    ReadStringField(block, "active_module", facts.active_module);
    ReadStringField(block, "scene_phase", facts.scene_phase);
    ReadStringField(block, "reference_mode", facts.reference_mode);

    std::string compatibility_block;
    if (ExtractObjectBlock(block, "compatibility", compatibility_block)) {
        int row = 0;
        int col = 0;
        const bool has_row = ReadIntField(compatibility_block, "farthest_line", row) ||
                             ReadIntField(compatibility_block, "highest_line", row);
        const bool has_col = ReadIntField(compatibility_block, "steering_reference_col", col);
        if (has_row && has_col) {
            facts.has_compat_reference = true;
            facts.compatibility_row = row;
            facts.compatibility_col = col;
        }
    }
}

bool LoadRuntimeSnapshotFromJsonl(const std::string& path, int frame_id, RuntimeSnapshotFacts& facts) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string value;
        if (!ExtractScalarValue(line, "frame_id", value)) {
            continue;
        }
        if (std::stoi(value) != frame_id) {
            continue;
        }
        std::string snapshot_block;
        if (ExtractObjectBlock(line, "steering_snapshot", snapshot_block)) {
            ApplySnapshotBlock(snapshot_block, path, frame_id, facts);
        } else {
            ApplySnapshotBlock(line, path, frame_id, facts);
        }
        return facts.available;
    }
    return false;
}

RuntimeSnapshotFacts LoadRuntimeSnapshotFacts(const std::string& raw_path) {
    RuntimeSnapshotFacts facts{};
    const int frame_id = ParseFrameIdFromPath(raw_path);
    if (frame_id < 0) {
        return facts;
    }

    const std::string frames_dir = DirName(raw_path);
    const std::string media_dir = DirName(frames_dir);
    const std::string evidence_dir = DirName(media_dir);

    const std::string metadata_path = media_dir + "/frame_metadata.jsonl";
    if (FileExists(metadata_path) && LoadRuntimeSnapshotFromJsonl(metadata_path, frame_id, facts)) {
        return facts;
    }

    const std::string board_snapshot_path = evidence_dir + "/board_steering_snapshot.jsonl";
    if (FileExists(board_snapshot_path) && LoadRuntimeSnapshotFromJsonl(board_snapshot_path, frame_id, facts)) {
        return facts;
    }

    return facts;
}

void DrawLine(RgbImage& image, int x0, int y0, int x1, int y1, const Color& color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        image.SetPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void DrawSquare(RgbImage& image, int x, int y, int radius, const Color& color, bool filled) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const bool border = std::abs(dx) == radius || std::abs(dy) == radius;
            if (filled || border) {
                image.SetPixel(x + dx, y + dy, color);
            }
        }
    }
}

void DrawSquareAlpha(RgbImage& image, int x, int y, int radius, const Color& color, float alpha, bool filled) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const bool border = std::abs(dx) == radius || std::abs(dy) == radius;
            if (filled || border) {
                image.BlendPixel(x + dx, y + dy, color, alpha);
            }
        }
    }
}

void DrawFilledRect(RgbImage& image, int x, int y, int width, int height, const Color& color) {
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            image.SetPixel(x + col, y + row, color);
        }
    }
}

void DrawLineAlpha(RgbImage& image,
                   int x0,
                   int y0,
                   int x1,
                   int y1,
                   const Color& color,
                   float alpha,
                   int thickness) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    const int radius = std::max(0, thickness / 2);

    while (true) {
        DrawSquareAlpha(image, x0, y0, radius, color, alpha, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void DrawCircle(RgbImage& image, int center_x, int center_y, int radius, const Color& color, bool filled) {
    const int radius_sq = radius * radius;
    const int inner_sq = std::max(0, radius - 1) * std::max(0, radius - 1);
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int dist_sq = dx * dx + dy * dy;
            if (dist_sq > radius_sq) {
                continue;
            }
            if (filled || dist_sq >= inner_sq) {
                image.SetPixel(center_x + dx, center_y + dy, color);
            }
        }
    }
}

void DrawCross(RgbImage& image, int x, int y, int radius, const Color& color) {
    DrawLine(image, x - radius, y, x + radius, y, color);
    DrawLine(image, x, y - radius, x, y + radius, color);
    DrawLine(image, x - radius, y - radius, x + radius, y + radius, color);
    DrawLine(image, x - radius, y + radius, x + radius, y - radius, color);
}

std::array<const char*, 7> GlyphRows(char value) {
    if (value >= 'a' && value <= 'z') {
        value = static_cast<char>(value - 'a' + 'A');
    }
    switch (value) {
        case '0':
            return {"01110", "10001", "10011", "10101", "11001", "10001", "01110"};
        case '1':
            return {"00100", "01100", "00100", "00100", "00100", "00100", "01110"};
        case '2':
            return {"01110", "10001", "00001", "00010", "00100", "01000", "11111"};
        case '3':
            return {"11110", "00001", "00001", "01110", "00001", "00001", "11110"};
        case '4':
            return {"00010", "00110", "01010", "10010", "11111", "00010", "00010"};
        case '5':
            return {"11111", "10000", "10000", "11110", "00001", "00001", "11110"};
        case '6':
            return {"01110", "10000", "10000", "11110", "10001", "10001", "01110"};
        case '7':
            return {"11111", "00001", "00010", "00100", "01000", "01000", "01000"};
        case '8':
            return {"01110", "10001", "10001", "01110", "10001", "10001", "01110"};
        case '9':
            return {"01110", "10001", "10001", "01111", "00001", "00001", "01110"};
        case 'A':
            return {"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
        case 'B':
            return {"11110", "10001", "10001", "11110", "10001", "10001", "11110"};
        case 'C':
            return {"01110", "10001", "10000", "10000", "10000", "10001", "01110"};
        case 'D':
            return {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
        case 'E':
            return {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
        case 'F':
            return {"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
        case 'G':
            return {"01110", "10001", "10000", "10111", "10001", "10001", "01110"};
        case 'H':
            return {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
        case 'I':
            return {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
        case 'J':
            return {"00111", "00010", "00010", "00010", "00010", "10010", "01100"};
        case 'K':
            return {"10001", "10010", "10100", "11000", "10100", "10010", "10001"};
        case 'L':
            return {"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
        case 'M':
            return {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
        case 'N':
            return {"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
        case 'O':
            return {"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
        case 'P':
            return {"11110", "10001", "10001", "11110", "10000", "10000", "10000"};
        case 'Q':
            return {"01110", "10001", "10001", "10001", "10101", "10010", "01101"};
        case 'R':
            return {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
        case 'S':
            return {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
        case 'T':
            return {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
        case 'U':
            return {"10001", "10001", "10001", "10001", "10001", "10001", "01110"};
        case 'V':
            return {"10001", "10001", "10001", "10001", "10001", "01010", "00100"};
        case 'W':
            return {"10001", "10001", "10001", "10101", "10101", "11011", "10001"};
        case 'X':
            return {"10001", "10001", "01010", "00100", "01010", "10001", "10001"};
        case 'Y':
            return {"10001", "10001", "01010", "00100", "00100", "00100", "00100"};
        case 'Z':
            return {"11111", "00001", "00010", "00100", "01000", "10000", "11111"};
        case '-':
            return {"00000", "00000", "00000", "11111", "00000", "00000", "00000"};
        case '.':
            return {"00000", "00000", "00000", "00000", "00000", "01100", "01100"};
        case ':':
            return {"00000", "01100", "01100", "00000", "01100", "01100", "00000"};
        case '/':
            return {"00001", "00010", "00010", "00100", "01000", "01000", "10000"};
        case '=':
            return {"00000", "00000", "11111", "00000", "11111", "00000", "00000"};
        default:
            return {"00000", "00000", "00000", "00000", "00000", "00000", "00000"};
    }
}

void DrawText(RgbImage& image, int x, int y, const std::string& text, const Color& color, int scale) {
    const int safe_scale = std::max(1, scale);
    int cursor_x = x;
    for (char value : text) {
        const std::array<const char*, 7> rows = GlyphRows(value);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (rows[static_cast<std::size_t>(row)][col] != '1') {
                    continue;
                }
                DrawFilledRect(image,
                               cursor_x + col * safe_scale,
                               y + row * safe_scale,
                               safe_scale,
                               safe_scale,
                               color);
            }
        }
        cursor_x += 6 * safe_scale;
    }
}

void DrawTextShadow(RgbImage& image, int x, int y, const std::string& text, const Color& color, int scale) {
    DrawText(image, x + 1, y + 1, text, Color{0, 0, 0}, scale);
    DrawText(image, x, y, text, color, scale);
}

std::string OverlayTextToken(std::string text) {
    for (char& value : text) {
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>('A' + (value - 'a'));
        } else if (value == '_') {
            value = '-';
        }
    }
    return text;
}

std::string ElementStatusToken(const ls2k::port::BEVElementEvidence& evidence) {
    if (evidence.cross_band.present) {
        return "ELEMENT:CROSS";
    }
    if (evidence.left_circle_corner.present && evidence.right_circle_corner.present) {
        return "ELEMENT:CORNER-LR";
    }
    if (evidence.left_circle_corner.present) {
        return "ELEMENT:CORNER-L";
    }
    if (evidence.right_circle_corner.present) {
        return "ELEMENT:CORNER-R";
    }
    return "ELEMENT:NONE";
}

void DrawFrameBorder(RgbImage& image, int x, int y, int width, int height, const Color& color) {
    DrawLine(image, x, y, x + width - 1, y, color);
    DrawLine(image, x, y, x, y + height - 1, color);
    DrawLine(image, x + width - 1, y, x + width - 1, y + height - 1, color);
    DrawLine(image, x, y + height - 1, x + width - 1, y + height - 1, color);
}

void BlitRawFrame(RgbImage& image, const LegacyCameraFrame& frame, int offset_x, int offset_y) {
    for (int row = 0; row < frame.height; ++row) {
        for (int col = 0; col < frame.width; ++col) {
            const std::size_t index =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(col);
            const std::uint8_t gray = frame.gray[index];
            image.SetPixel(offset_x + col, offset_y + row, Color{gray, gray, gray});
        }
    }
}

void DrawVerticalMarker(RgbImage& image,
                        int x,
                        int y0,
                        int y1,
                        const Color& color) {
    DrawLine(image, x, y0, x, y1, color);
}

bool ProjectSampleToRaw(const ls2k::legacy::BEVProjector& projector,
                        const BEVPathSample& sample,
                        int panel_x,
                        int panel_y,
                        int raw_width,
                        int raw_height,
                        int& out_x,
                        int& out_y) {
    if (!sample.valid) {
        return false;
    }
    ImagePoint point{};
    if (!projector.ProjectVehicleToImage(sample.point, point)) {
        return false;
    }
    const int image_row = static_cast<int>(std::lround(point.row_px));
    const int image_col = static_cast<int>(std::lround(point.col_px));
    if (image_row < 0 || image_col < 0 || image_row >= raw_height || image_col >= raw_width) {
        return false;
    }
    out_x = panel_x + image_col;
    out_y = panel_y + image_row;
    return true;
}

bool ProjectBevPointToRaw(const ls2k::legacy::BEVProjector& projector,
                          const BEVPoint& point,
                          int panel_x,
                          int panel_y,
                          int raw_width,
                          int raw_height,
                          int& out_x,
                          int& out_y) {
    ImagePoint image_point{};
    if (!projector.ProjectVehicleToImage(point, image_point)) {
        return false;
    }
    const int image_row = static_cast<int>(std::lround(image_point.row_px));
    const int image_col = static_cast<int>(std::lround(image_point.col_px));
    if (image_row < 0 || image_col < 0 || image_row >= raw_height || image_col >= raw_width) {
        return false;
    }
    out_x = panel_x + image_col;
    out_y = panel_y + image_row;
    return true;
}

void DrawProjectedPath(RgbImage& image,
                       const ls2k::legacy::BEVProjector& projector,
                       const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
                       int panel_x,
                       int panel_y,
                       int raw_width,
                       int raw_height,
                       const Color& color,
                       int marker_radius) {
    bool have_previous = false;
    int previous_x = 0;
    int previous_y = 0;
    for (const BEVPathSample& sample : path) {
        int x = 0;
        int y = 0;
        if (!ProjectSampleToRaw(projector, sample, panel_x, panel_y, raw_width, raw_height, x, y)) {
            have_previous = false;
            continue;
        }
        if (have_previous) {
            DrawLine(image, previous_x, previous_y, x, y, color);
        }
        DrawSquare(image, x, y, marker_radius, color, true);
        previous_x = x;
        previous_y = y;
        have_previous = true;
    }
}

bool ProjectBevToPanel(const BEVPoint& point,
                       const PanelLayout& layout,
                       float lateral_limit_m,
                       float forward_max_m,
                       int& out_x,
                       int& out_y) {
    if (lateral_limit_m <= 1e-4F || forward_max_m <= 1e-4F) {
        return false;
    }
    const float normalized_x = (point.lateral_m + lateral_limit_m) / (2.0F * lateral_limit_m);
    const float normalized_y = 1.0F - point.forward_m / forward_max_m;
    out_x = layout.bev_x +
            static_cast<int>(std::lround(std::clamp(normalized_x, 0.0F, 1.0F) * (layout.bev_width - 1)));
    out_y = layout.bev_y +
            static_cast<int>(std::lround(std::clamp(normalized_y, 0.0F, 1.0F) * (layout.bev_height - 1)));
    return true;
}

bool ProjectPanelToBev(int panel_x,
                       int panel_y,
                       const PanelLayout& layout,
                       float lateral_limit_m,
                       float forward_max_m,
                       BEVPoint& out_point) {
    if (layout.bev_width <= 1 || layout.bev_height <= 1 || lateral_limit_m <= 1e-4F || forward_max_m <= 1e-4F) {
        return false;
    }
    const float normalized_x =
        static_cast<float>(panel_x - layout.bev_x) / static_cast<float>(layout.bev_width - 1);
    const float normalized_y =
        static_cast<float>(panel_y - layout.bev_y) / static_cast<float>(layout.bev_height - 1);
    out_point.lateral_m = normalized_x * (2.0F * lateral_limit_m) - lateral_limit_m;
    out_point.forward_m = (1.0F - normalized_y) * forward_max_m;
    return true;
}

bool SampleRawFrameBilinear(const LegacyCameraFrame& frame, float row_px, float col_px, std::uint8_t& out_gray) {
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    if (row_px < 0.0F || col_px < 0.0F || row_px > static_cast<float>(frame.height - 1) ||
        col_px > static_cast<float>(frame.width - 1)) {
        return false;
    }

    const int row0 = static_cast<int>(std::floor(row_px));
    const int col0 = static_cast<int>(std::floor(col_px));
    const int row1 = std::min(row0 + 1, frame.height - 1);
    const int col1 = std::min(col0 + 1, frame.width - 1);
    const float row_frac = row_px - static_cast<float>(row0);
    const float col_frac = col_px - static_cast<float>(col0);

    const auto gray_at = [&frame](int row, int col) -> float {
        const std::size_t index =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
            static_cast<std::size_t>(col);
        return static_cast<float>(frame.gray[index]);
    };

    const float top = gray_at(row0, col0) * (1.0F - col_frac) + gray_at(row0, col1) * col_frac;
    const float bottom = gray_at(row1, col0) * (1.0F - col_frac) + gray_at(row1, col1) * col_frac;
    const float gray = top * (1.0F - row_frac) + bottom * row_frac;
    out_gray = static_cast<std::uint8_t>(std::lround(std::clamp(gray, 0.0F, 255.0F)));
    return true;
}

void RenderBevImage(RgbImage& image,
                    const LegacyCameraFrame& frame,
                    const ls2k::legacy::BEVProjector& projector,
                    const PanelLayout& layout,
                    float lateral_limit_m,
                    float forward_max_m) {
    for (int panel_y = layout.bev_y; panel_y < layout.bev_y + layout.bev_height; ++panel_y) {
        for (int panel_x = layout.bev_x; panel_x < layout.bev_x + layout.bev_width; ++panel_x) {
            BEVPoint bev_point{};
            if (!ProjectPanelToBev(panel_x, panel_y, layout, lateral_limit_m, forward_max_m, bev_point)) {
                continue;
            }
            ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage(bev_point, image_point)) {
                continue;
            }
            std::uint8_t gray = 0;
            if (!SampleRawFrameBilinear(frame, image_point.row_px, image_point.col_px, gray)) {
                continue;
            }
            image.SetPixel(panel_x, panel_y, Color{gray, gray, gray});
        }
    }
}

void DrawBevGrid(RgbImage& image,
                 const PanelLayout& layout,
                 float lateral_limit_m,
                 float forward_max_m) {
    const Color grid_color{40, 40, 40};
    const Color axis_color{90, 90, 90};
    DrawFrameBorder(image, layout.bev_x, layout.bev_y, layout.bev_width, layout.bev_height, axis_color);

    for (float lateral = -lateral_limit_m; lateral <= lateral_limit_m + 1e-4F; lateral += 0.2F) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (!ProjectBevToPanel(BEVPoint{0.0F, lateral}, layout, lateral_limit_m, forward_max_m, x0, y0) ||
            !ProjectBevToPanel(BEVPoint{forward_max_m, lateral},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x1,
                               y1)) {
            continue;
        }
        DrawLineAlpha(image,
                      x0,
                      y0,
                      x1,
                      y1,
                      std::abs(lateral) < 1e-4F ? axis_color : grid_color,
                      std::abs(lateral) < 1e-4F ? 0.55F : 0.28F,
                      1);
    }

    for (float forward = 0.0F; forward <= forward_max_m + 1e-4F; forward += 0.25F) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (!ProjectBevToPanel(BEVPoint{forward, -lateral_limit_m},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x0,
                               y0) ||
            !ProjectBevToPanel(BEVPoint{forward, lateral_limit_m},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x1,
                               y1)) {
            continue;
        }
        DrawLineAlpha(image,
                      x0,
                      y0,
                      x1,
                      y1,
                      forward < 1e-4F ? axis_color : grid_color,
                      forward < 1e-4F ? 0.55F : 0.28F,
                      1);
    }
}

void DrawBevPath(RgbImage& image,
                 const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
                 const PanelLayout& layout,
                 float lateral_limit_m,
                 float forward_max_m,
                 const Color& color,
                 int marker_radius) {
    bool have_previous = false;
    int previous_x = 0;
    int previous_y = 0;
    for (const BEVPathSample& sample : path) {
        if (!sample.valid) {
            have_previous = false;
            continue;
        }
        int x = 0;
        int y = 0;
        if (!ProjectBevToPanel(sample.point, layout, lateral_limit_m, forward_max_m, x, y)) {
            have_previous = false;
            continue;
        }
        if (have_previous) {
            DrawLine(image, previous_x, previous_y, x, y, color);
        }
        DrawSquare(image, x, y, marker_radius, color, true);
        previous_x = x;
        previous_y = y;
        have_previous = true;
    }
}

bool GraphNormalConstructionAt(const ls2k::legacy::CorridorGraph& graph,
                               const ls2k::legacy::CorridorIntervalSet& intervals,
                               std::size_t index,
                               BEVPoint& anchor_point,
                               BEVPoint& center_point) {
    if (index >= graph.ordinary_interval_indices.size() || index >= intervals.layers.size()) {
        return false;
    }
    const int interval_index = graph.ordinary_interval_indices[index];
    if (interval_index < 0 ||
        interval_index >= static_cast<int>(intervals.layers[index].intervals.size())) {
        return false;
    }
    const ls2k::port::CorridorInterval& interval =
        intervals.layers[index].intervals[static_cast<std::size_t>(interval_index)];
    const ls2k::legacy::BoundaryAnchorSide anchor_side = graph.ordinary_center_anchor_side[index];
    if (anchor_side == ls2k::legacy::BoundaryAnchorSide::kLeft) {
        anchor_point.forward_m = interval.forward_m;
        anchor_point.lateral_m = interval.lateral_min_m;
    } else if (anchor_side == ls2k::legacy::BoundaryAnchorSide::kRight) {
        anchor_point.forward_m = interval.forward_m;
        anchor_point.lateral_m = interval.lateral_max_m;
    } else {
        return false;
    }
    if (graph.ordinary.sampled_path[index].valid) {
        center_point = graph.ordinary.sampled_path[index].point;
        return true;
    }
    if (!graph.ordinary_raw_centerline[index].valid) {
        return false;
    }
    center_point = graph.ordinary_raw_centerline[index].point;
    return true;
}

void DrawProjectedNormalSegments(RgbImage& image,
                                 const ls2k::legacy::BEVProjector& projector,
                                 const ls2k::legacy::CorridorGraph& graph,
                                 const ls2k::legacy::CorridorIntervalSet& intervals,
                                 const PanelLayout& layout,
                                 const Color& color) {
    for (std::size_t index = 0; index < ls2k::port::kBevTrackSampleCount; ++index) {
        BEVPoint anchor_point{};
        BEVPoint center_point{};
        if (!GraphNormalConstructionAt(graph, intervals, index, anchor_point, center_point)) {
            continue;
        }
        int anchor_x = 0;
        int anchor_y = 0;
        if (!ProjectBevPointToRaw(projector,
                                  anchor_point,
                                  layout.raw_x,
                                  layout.raw_y,
                                  layout.raw_width,
                                  layout.raw_height,
                                  anchor_x,
                                  anchor_y)) {
            continue;
        }
        int center_x = 0;
        int center_y = 0;
        if (!ProjectBevPointToRaw(projector,
                                  center_point,
                                  layout.raw_x,
                                  layout.raw_y,
                                  layout.raw_width,
                                  layout.raw_height,
                                  center_x,
                                  center_y)) {
            continue;
        }
        DrawSquare(image, anchor_x, anchor_y, 1, color, true);
        DrawSquare(image, center_x, center_y, 1, color, true);
    }
}

void DrawBevNormalSegments(RgbImage& image,
                           const ls2k::legacy::CorridorGraph& graph,
                           const ls2k::legacy::CorridorIntervalSet& intervals,
                           const PanelLayout& layout,
                           float lateral_limit_m,
                           float forward_max_m,
                           const Color& color) {
    for (std::size_t index = 0; index < ls2k::port::kBevTrackSampleCount; ++index) {
        BEVPoint anchor_point{};
        BEVPoint center_point{};
        if (!GraphNormalConstructionAt(graph, intervals, index, anchor_point, center_point)) {
            continue;
        }
        int anchor_x = 0;
        int anchor_y = 0;
        if (!ProjectBevToPanel(anchor_point, layout, lateral_limit_m, forward_max_m, anchor_x, anchor_y)) {
            continue;
        }
        int center_x = 0;
        int center_y = 0;
        if (!ProjectBevToPanel(center_point, layout, lateral_limit_m, forward_max_m, center_x, center_y)) {
            continue;
        }
        DrawLineAlpha(image, anchor_x, anchor_y, center_x, center_y, color, 0.58F, 1);
        DrawSquare(image, anchor_x, anchor_y, 1, color, true);
        DrawSquare(image, center_x, center_y, 1, color, true);
    }
}

Color SampleClassColor(ls2k::port::BEVSampleClass sample_class) {
    switch (sample_class) {
        case ls2k::port::BEVSampleClass::kDrivable:
            return Color{86, 150, 150};
        case ls2k::port::BEVSampleClass::kUnknownLowConfidence:
            return Color{190, 142, 76};
        case ls2k::port::BEVSampleClass::kBackground:
            return Color{70, 78, 88};
        case ls2k::port::BEVSampleClass::kInvalidOutsideImage:
            return Color{110, 110, 110};
    }
    return Color{125, 125, 125};
}

float SampleClassAlpha(ls2k::port::BEVSampleClass sample_class) {
    switch (sample_class) {
        case ls2k::port::BEVSampleClass::kDrivable:
            return 0.58F;
        case ls2k::port::BEVSampleClass::kUnknownLowConfidence:
            return 0.54F;
        case ls2k::port::BEVSampleClass::kBackground:
            return 0.42F;
        case ls2k::port::BEVSampleClass::kInvalidOutsideImage:
            return 0.24F;
    }
    return 0.35F;
}

void DrawBevSamples(RgbImage& image,
                    const ls2k::legacy::BEVSparseSampleGrid& samples,
                    const PanelLayout& layout,
                    float lateral_limit_m,
                    float forward_max_m) {
    for (const ls2k::legacy::BEVSampleLayer& layer : samples.layers) {
        for (const ls2k::port::BEVSample& sample : layer.samples) {
            int x = 0;
            int y = 0;
            if (!ProjectBevToPanel(sample.point, layout, lateral_limit_m, forward_max_m, x, y)) {
                continue;
            }
            DrawSquareAlpha(image,
                            x,
                            y,
                            sample.sample_class == ls2k::port::BEVSampleClass::kDrivable ? 1 : 0,
                            SampleClassColor(sample.sample_class),
                            SampleClassAlpha(sample.sample_class),
                            true);
        }
    }
}

void DrawBevIntervals(RgbImage& image,
                      const ls2k::legacy::CorridorIntervalSet& intervals,
                      const PanelLayout& layout,
                      float lateral_limit_m,
                      float forward_max_m) {
    const Color fill{150, 118, 64};
    const Color edge{220, 172, 88};
    for (const ls2k::legacy::CorridorIntervalLayer& layer : intervals.layers) {
        for (const ls2k::port::CorridorInterval& interval : layer.intervals) {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            if (!ProjectBevToPanel(BEVPoint{interval.forward_m, interval.lateral_min_m},
                                   layout,
                                   lateral_limit_m,
                                   forward_max_m,
                                   x0,
                                   y0) ||
                !ProjectBevToPanel(BEVPoint{interval.forward_m, interval.lateral_max_m},
                                   layout,
                                   lateral_limit_m,
                                   forward_max_m,
                                   x1,
                                   y1)) {
                continue;
            }
            const float alpha = std::clamp(0.25F + interval.confidence * 0.45F, 0.25F, 0.75F);
            DrawLineAlpha(image, x0, y0, x1, y1, fill, alpha, 5);
            if (interval.left_edge_valid) {
                DrawSquare(image, x0, y0, 1, edge, true);
            }
            if (interval.right_edge_valid) {
                DrawSquare(image, x1, y1, 1, edge, true);
            }
        }
    }
}

const ls2k::port::BEVRowProfileEvidence* ClosestRowProfile(
    const ls2k::port::BEVElementEvidence& evidence,
    float forward_m) {
    const ls2k::port::BEVRowProfileEvidence* best = nullptr;
    float best_distance = std::numeric_limits<float>::infinity();
    for (const ls2k::port::BEVRowProfileEvidence& profile : evidence.row_profiles) {
        if (!profile.valid) {
            continue;
        }
        const float distance = std::abs(profile.forward_m - forward_m);
        if (distance < best_distance) {
            best = &profile;
            best_distance = distance;
        }
    }
    return best;
}

void DrawCircleCornerEvidence(RgbImage& image,
                              const ls2k::port::BEVCircleCornerEvidence& corner,
                              const PanelLayout& layout,
                              float lateral_limit_m,
                              float forward_max_m,
                              const Color& color) {
    if (!corner.present) {
        return;
    }
    int x = 0;
    int y = 0;
    if (!ProjectBevToPanel(BEVPoint{corner.forward_m, corner.lateral_m},
                           layout,
                           lateral_limit_m,
                           forward_max_m,
                           x,
                           y)) {
        return;
    }
    DrawCircle(image, x, y, 5, color, false);
    DrawCross(image, x, y, 6, color);
}

void DrawBevElementEvidence(RgbImage& image,
                            const ls2k::port::BEVElementEvidence& evidence,
                            const PanelLayout& layout,
                            float lateral_limit_m,
                            float forward_max_m) {
    if (!evidence.valid) {
        return;
    }
    if (evidence.cross_band.present) {
        const ls2k::port::BEVRowProfileEvidence* profile =
            ClosestRowProfile(evidence, evidence.cross_band.forward_m);
        if (profile != nullptr) {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            if (ProjectBevToPanel(BEVPoint{profile->forward_m, profile->valid_lateral_min_m},
                                  layout,
                                  lateral_limit_m,
                                  forward_max_m,
                                  x0,
                                  y0) &&
                ProjectBevToPanel(BEVPoint{profile->forward_m, profile->valid_lateral_max_m},
                                  layout,
                                  lateral_limit_m,
                                  forward_max_m,
                                  x1,
                                  y1)) {
                DrawLineAlpha(image, x0, y0, x1, y1, Color{255, 92, 48}, 0.85F, 7);
            }
        }
    }
    DrawCircleCornerEvidence(image,
                             evidence.left_circle_corner,
                             layout,
                             lateral_limit_m,
                             forward_max_m,
                             Color{255, 64, 220});
    DrawCircleCornerEvidence(image,
                             evidence.right_circle_corner,
                             layout,
                             lateral_limit_m,
                             forward_max_m,
                             Color{255, 64, 220});
}

void DrawRawCorridorCrossSections(
    RgbImage& image,
    const ls2k::legacy::BEVProjector& projector,
    const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& left_path,
    const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& right_path,
    const PanelLayout& layout,
    const Color& color) {
    for (std::size_t index = 0; index < left_path.size(); ++index) {
        int left_x = 0;
        int left_y = 0;
        int right_x = 0;
        int right_y = 0;
        if (!ProjectSampleToRaw(projector,
                                left_path[index],
                                layout.raw_x,
                                layout.raw_y,
                                layout.raw_width,
                                layout.raw_height,
                                left_x,
                                left_y) ||
            !ProjectSampleToRaw(projector,
                                right_path[index],
                                layout.raw_x,
                                layout.raw_y,
                                layout.raw_width,
                                layout.raw_height,
                                right_x,
                                right_y)) {
            continue;
        }
        DrawLineAlpha(image, left_x, left_y, right_x, right_y, color, 0.22F, 5);
    }
}

void DrawBevVehicle(RgbImage& image,
                    const PanelLayout& layout,
                    float lateral_limit_m,
                    float forward_max_m) {
    int nose_x = 0;
    int nose_y = 0;
    int left_x = 0;
    int left_y = 0;
    int right_x = 0;
    int right_y = 0;
    int center_x = 0;
    int center_y = 0;
    if (!ProjectBevToPanel(BEVPoint{0.24F, 0.0F}, layout, lateral_limit_m, forward_max_m, nose_x, nose_y) ||
        !ProjectBevToPanel(BEVPoint{0.02F, -0.11F}, layout, lateral_limit_m, forward_max_m, left_x, left_y) ||
        !ProjectBevToPanel(BEVPoint{0.02F, 0.11F}, layout, lateral_limit_m, forward_max_m, right_x, right_y) ||
        !ProjectBevToPanel(BEVPoint{0.02F, 0.0F}, layout, lateral_limit_m, forward_max_m, center_x, center_y)) {
        return;
    }
    const Color body{235, 235, 235};
    DrawLine(image, left_x, left_y, nose_x, nose_y, body);
    DrawLine(image, nose_x, nose_y, right_x, right_y, body);
    DrawLine(image, right_x, right_y, left_x, left_y, body);
    DrawCircle(image, center_x, center_y, 2, body, true);
}

bool DrawLookaheadTarget(RgbImage& image,
                         const ls2k::legacy::BEVProjector& projector,
                         const ls2k::port::ControlErrorModelOutput& control,
                         const PanelLayout& layout,
                         float lateral_limit_m,
                         float forward_max_m) {
    if (!control.valid || control.lookahead_distance_m <= 0.05F) {
        return false;
    }
    const BEVPoint target{control.lookahead_distance_m, control.lookahead_lateral_error};
    int target_x = 0;
    int target_y = 0;
    if (ProjectBevToPanel(target, layout, lateral_limit_m, forward_max_m, target_x, target_y)) {
        int origin_x = 0;
        int origin_y = 0;
        if (ProjectBevToPanel(BEVPoint{0.0F, 0.0F}, layout, lateral_limit_m, forward_max_m, origin_x, origin_y)) {
            DrawLineAlpha(image, origin_x, origin_y, target_x, target_y, Color{255, 255, 255}, 0.65F, 1);
        }
        DrawCircle(image, target_x, target_y, 5, Color{255, 255, 255}, false);
        DrawCross(image, target_x, target_y, 4, Color{255, 255, 255});
    }

    BEVPathSample raw_target{};
    raw_target.valid = true;
    raw_target.point = target;
    raw_target.confidence = 1.0F;
    int raw_x = 0;
    int raw_y = 0;
    if (ProjectSampleToRaw(projector,
                           raw_target,
                           layout.raw_x,
                           layout.raw_y,
                           layout.raw_width,
                           layout.raw_height,
                           raw_x,
                           raw_y)) {
        DrawCircle(image, raw_x, raw_y, 5, Color{255, 255, 255}, false);
        DrawCross(image, raw_x, raw_y, 4, Color{255, 255, 255});
    }
    return true;
}

void DrawRuntimeSnapshotFacts(RgbImage& image,
                              const RuntimeSnapshotFacts& facts,
                              const RuntimeParameters& params,
                              const ls2k::legacy::BEVProjector& projector,
                              const PanelLayout& layout,
                              float lateral_limit_m,
                              float forward_max_m) {
    if (!facts.available || !facts.has_near_lateral_error) {
        return;
    }

    const int near_index = std::clamp(params.bev_control_model.near_sample_index,
                                      0,
                                      static_cast<int>(params.bev_topology_sampler.forward_samples_m.size()) - 1);
    const float near_forward = params.bev_topology_sampler.forward_samples_m[static_cast<std::size_t>(near_index)];
    const float heading = facts.has_far_heading_error ? facts.far_heading_error : 0.0F;
    const float visible_range =
        facts.has_visible_range_m ? std::clamp(facts.visible_range_m, 0.2F, forward_max_m) : forward_max_m;
    const Color fact_color{255, 255, 255};
    const Color reference_col_color{255, 240, 64};

    bool have_previous_raw = false;
    int previous_raw_x = 0;
    int previous_raw_y = 0;
    bool have_previous_bev = false;
    int previous_bev_x = 0;
    int previous_bev_y = 0;
    for (float forward = 0.0F; forward <= visible_range + 1e-4F; forward += 0.25F) {
        const float lateral = facts.near_lateral_error + std::tan(heading) * (forward - near_forward);
        const BEVPoint point{forward, lateral};

        int bev_x = 0;
        int bev_y = 0;
        if (ProjectBevToPanel(point, layout, lateral_limit_m, forward_max_m, bev_x, bev_y)) {
            if (have_previous_bev) {
                DrawLineAlpha(image, previous_bev_x, previous_bev_y, bev_x, bev_y, fact_color, 0.90F, 2);
            }
            previous_bev_x = bev_x;
            previous_bev_y = bev_y;
            have_previous_bev = true;
        } else {
            have_previous_bev = false;
        }

        BEVPathSample sample{};
        sample.valid = true;
        sample.point = point;
        sample.confidence = facts.has_track_confidence ? facts.track_confidence : 1.0F;
        int raw_x = 0;
        int raw_y = 0;
        if (ProjectSampleToRaw(projector,
                               sample,
                               layout.raw_x,
                               layout.raw_y,
                               layout.raw_width,
                               layout.raw_height,
                               raw_x,
                               raw_y)) {
            if (have_previous_raw) {
                DrawLineAlpha(image, previous_raw_x, previous_raw_y, raw_x, raw_y, fact_color, 0.92F, 3);
            }
            previous_raw_x = raw_x;
            previous_raw_y = raw_y;
            have_previous_raw = true;
        } else {
            have_previous_raw = false;
        }
    }

    if (facts.has_compat_reference) {
        const int x = layout.raw_x + facts.compatibility_col;
        const int y = layout.raw_y + facts.compatibility_row;
        DrawLineAlpha(image, x, layout.raw_y, x, layout.raw_y + layout.raw_height - 1, reference_col_color, 0.38F, 1);
        DrawCross(image, x, y, 6, reference_col_color);
    }

    if (facts.has_lookahead) {
        const BEVPoint target{facts.lookahead_distance_m, facts.lookahead_lateral_error};
        int bev_x = 0;
        int bev_y = 0;
        if (ProjectBevToPanel(target, layout, lateral_limit_m, forward_max_m, bev_x, bev_y)) {
            DrawCircle(image, bev_x, bev_y, 5, fact_color, false);
            DrawCross(image, bev_x, bev_y, 4, fact_color);
        }

        BEVPathSample raw_target{};
        raw_target.valid = true;
        raw_target.point = target;
        raw_target.confidence = 1.0F;
        int raw_x = 0;
        int raw_y = 0;
        if (ProjectSampleToRaw(projector,
                               raw_target,
                               layout.raw_x,
                               layout.raw_y,
                               layout.raw_width,
                               layout.raw_height,
                               raw_x,
                               raw_y)) {
            DrawCircle(image, raw_x, raw_y, 5, fact_color, false);
            DrawCross(image, raw_x, raw_y, 4, fact_color);
        }
    }

    DrawTextShadow(image, layout.raw_x + 5, layout.raw_y + 5, "RUNTIME FACT", fact_color, 1);
}

int DrawLegendItem(RgbImage& image, int x, int y, const Color& color, const std::string& label) {
    DrawFilledRect(image, x, y + 1, 10, 8, color);
    DrawFrameBorder(image, x, y + 1, 10, 8, Color{210, 210, 210});
    DrawTextShadow(image, x + 14, y, label, Color{225, 225, 225}, 1);
    return x + 14 + static_cast<int>(label.size()) * 6 + 10;
}

float MaxAbsLateral(const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path) {
    float result = 0.0F;
    for (const BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        result = std::max(result, std::abs(sample.point.lateral_m));
    }
    return result;
}

void WriteBmp(const std::string& path, const RgbImage& image) {
    const int row_stride = image.width * 3;
    const int row_padding = (4 - (row_stride % 4)) % 4;
    const int pixel_bytes = (row_stride + row_padding) * image.height;
    const int file_size = 54 + pixel_bytes;

    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to open output bmp: " + path);
    }

    const auto write_u16 = [&output](std::uint16_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
    };
    const auto write_u32 = [&output](std::uint32_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
        output.put(static_cast<char>((value >> 16U) & 0xFFU));
        output.put(static_cast<char>((value >> 24U) & 0xFFU));
    };

    output.put('B');
    output.put('M');
    write_u32(static_cast<std::uint32_t>(file_size));
    write_u16(0);
    write_u16(0);
    write_u32(54);

    write_u32(40);
    write_u32(static_cast<std::uint32_t>(image.width));
    write_u32(static_cast<std::uint32_t>(image.height));
    write_u16(1);
    write_u16(24);
    write_u32(0);
    write_u32(static_cast<std::uint32_t>(pixel_bytes));
    write_u32(2835);
    write_u32(2835);
    write_u32(0);
    write_u32(0);

    const std::array<std::uint8_t, 3> padding{{0, 0, 0}};
    for (int row = image.height - 1; row >= 0; --row) {
        for (int col = 0; col < image.width; ++col) {
            const std::size_t index =
                (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(col)) *
                3U;
            output.put(static_cast<char>(image.pixels[index + 2U]));
            output.put(static_cast<char>(image.pixels[index + 1U]));
            output.put(static_cast<char>(image.pixels[index]));
        }
        output.write(reinterpret_cast<const char*>(padding.data()), row_padding);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: scene_overlay_probe <input.raw> <output.bmp> [params_or_config_snapshot.json]"
                     " [--bev-only] [--warmup <input.raw> ...] [--confirm-cycles <count>]\n";
        return 1;
    }

    try {
        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        const LegacyCameraFrame frame = ReadRawFrame(input_path);

        RuntimeParameters params{};
        std::string params_path;
        bool bev_only = false;
        int confirm_cycles = 1;
        std::vector<std::string> warmup_paths;
        for (int arg_index = 3; arg_index < argc; ++arg_index) {
            const std::string arg = argv[arg_index];
            if (arg == "--bev-only") {
                bev_only = true;
                continue;
            }
            if (arg == "--warmup") {
                if (arg_index + 1 >= argc) {
                    throw std::runtime_error("--warmup requires a raw frame path");
                }
                warmup_paths.push_back(argv[++arg_index]);
                continue;
            }
            if (arg == "--confirm-cycles") {
                if (arg_index + 1 >= argc) {
                    throw std::runtime_error("--confirm-cycles requires a positive integer");
                }
                confirm_cycles = std::stoi(argv[++arg_index]);
                if (confirm_cycles <= 0) {
                    throw std::runtime_error("--confirm-cycles must be positive");
                }
                continue;
            }
            if (!params_path.empty()) {
                throw std::runtime_error("unexpected extra argument: " + arg);
            }
            params_path = arg;
        }
        if (params_path.empty()) {
            if (const char* env_path = std::getenv("LS2K_PARAMS_PATH")) {
                params_path = env_path;
            }
        }
        const std::string params_source = LoadRuntimeParamsJson(params_path, params);
        const RuntimeSnapshotFacts runtime_facts = LoadRuntimeSnapshotFacts(input_path);
        const bool use_runtime_facts = runtime_facts.available;

        ls2k::legacy::BEVProjector projector;
        if (!projector.Configure(params.bev_projector)) {
            throw std::runtime_error("failed to configure BEV projector");
        }

        ls2k::port::LegacySteeringState prior_state{};
        std::uint64_t frame_id = 1;
        for (const std::string& warmup_path : warmup_paths) {
            const LegacyCameraFrame warmup_frame = ReadRawFrame(warmup_path);
            const ls2k::legacy::SteeringAnalysisResult warmup_analysis =
                ls2k::legacy::AnalyzeFrame(warmup_frame, params, prior_state, {}, false, frame_id++, 1);
            if (!warmup_analysis.steering_state_update_valid) {
                throw std::runtime_error("warmup frame did not return a state update: " + warmup_path);
            }
            prior_state = warmup_analysis.steering_state_update;
        }
        ls2k::port::LegacySteeringState analysis_prior_state = prior_state;
        ls2k::legacy::SteeringAnalysisResult analysis{};
        for (int cycle = 0; cycle < confirm_cycles; ++cycle) {
            analysis_prior_state = prior_state;
            analysis =
                ls2k::legacy::AnalyzeFrame(frame, params, prior_state, {}, false, frame_id++, 1);
            if (!analysis.steering_state_update_valid) {
                throw std::runtime_error("analysis frame did not return a state update");
            }
            prior_state = analysis.steering_state_update;
        }

        const ls2k::legacy::BEVTopologyPipelineResult topology =
            ls2k::legacy::RunBEVTopologyPipeline(frame,
                                                 analysis.perception.threshold,
                                                 params,
                                                 analysis_prior_state,
                                                 projector);
        const ls2k::legacy::BEVSparseSampleGrid& sparse_samples = topology.sparse_samples;
        const ls2k::legacy::CorridorIntervalSet& corridor_intervals = topology.corridor_intervals;
        const ls2k::legacy::CorridorGraph& corridor_graph = topology.corridor_graph;
        const ls2k::port::BEVElementEvidence& element_evidence = topology.element_evidence;
        const ls2k::port::BEVTrackEstimate& overlay_track = topology.track_estimate;
        const bool draw_graph_geometry = corridor_graph.valid && corridor_graph.ordinary.valid;
        const bool draw_track_geometry = overlay_track.valid;
        const bool draw_reference_path = draw_track_geometry && analysis.reference_path.valid;
        const bool draw_hypothesis_paths = draw_graph_geometry || draw_reference_path;

        const float lateral_limit_m =
            std::max(std::max(params.bev_geometry.search_lateral_limit_m,
                              std::max(std::abs(params.bev_topology_sampler.lateral_min_m),
                                       std::abs(params.bev_topology_sampler.lateral_max_m))),
                     std::max(MaxAbsLateral(overlay_track.sampled_drivable_left_boundary),
                              MaxAbsLateral(overlay_track.sampled_drivable_right_boundary)) +
                         0.05F);
        const float forward_max_m =
            std::max(params.bev_topology_sampler.forward_samples_m.back(), 0.5F);

        constexpr int kPanelGapPx = 12;
        constexpr int kTitleHeightPx = 18;
        constexpr int kLegendHeightPx = 34;
        PanelLayout layout{};
        layout.raw_x = 0;
        layout.raw_y = kTitleHeightPx;
        layout.raw_width = frame.width;
        layout.raw_height = frame.height;
        layout.bev_x = frame.width + kPanelGapPx;
        layout.bev_y = kTitleHeightPx;
        layout.bev_width = params.bev_projector.debug_grid_width * 2;
        const float metric_scale_px_per_m =
            static_cast<float>(layout.bev_width) / std::max(1e-4F, lateral_limit_m * 2.0F);
        layout.bev_height = std::max(
            1,
            static_cast<int>(std::lround(forward_max_m * metric_scale_px_per_m)));

        if (bev_only) {
            PanelLayout bev_only_layout{};
            bev_only_layout.bev_x = 0;
            bev_only_layout.bev_y = 0;
            bev_only_layout.bev_width = layout.bev_width;
            bev_only_layout.bev_height = layout.bev_height;
            RgbImage bev_image(bev_only_layout.bev_width, bev_only_layout.bev_height, Color{18, 18, 18});
            RenderBevImage(bev_image, frame, projector, bev_only_layout, lateral_limit_m, forward_max_m);
            WriteBmp(output_path, bev_image);
            std::cout << "overlay_algorithm=bev_topology_pipeline"
                      << " overlay_authority=" << (use_runtime_facts ? "runtime_snapshot" : "offline_reanalysis")
                      << " export_mode=bev_only"
                      << " bev_width=" << bev_only_layout.bev_width
                      << " bev_height=" << bev_only_layout.bev_height
                      << " lateral_limit_m=" << lateral_limit_m
                      << " forward_max_m=" << forward_max_m
                      << " projector_id=" << params.bev_projector.projector_id
                      << " params_source=" << params_source
                      << "\n";
            std::cout << "wrote_overlay=" << output_path << "\n";
            return 0;
        }

        RgbImage image(layout.bev_x + layout.bev_width,
                       kTitleHeightPx + std::max(layout.raw_height, layout.bev_height) + kLegendHeightPx,
                       Color{18, 18, 18});
        DrawTextShadow(image, layout.raw_x + 5, 4, "RAW IMAGE", Color{230, 230, 230}, 1);
        DrawTextShadow(image,
                       layout.bev_x + 5,
                       4,
                       use_runtime_facts ? "RUNTIME BEV IMAGE" : "BEV IMAGE/TOPOLOGY",
                       Color{230, 230, 230},
                       1);
        const std::string scene_status =
            "S:" + OverlayTextToken(analysis.perception.active_module + "/" + analysis.perception.scene_phase);
        DrawTextShadow(image, layout.bev_x + 150, 4, scene_status, Color{230, 230, 230}, 1);
        BlitRawFrame(image, frame, layout.raw_x, layout.raw_y);

        if (!use_runtime_facts) {
            DrawRawCorridorCrossSections(image,
                                         projector,
                                         overlay_track.sampled_drivable_left_boundary,
                                         overlay_track.sampled_drivable_right_boundary,
                                         layout,
                                         Color{150, 118, 64});
        }
        DrawFrameBorder(image, layout.raw_x, layout.raw_y, layout.raw_width, layout.raw_height, Color{96, 96, 96});

        DrawVerticalMarker(image,
                           layout.raw_x + frame.width / 2,
                           layout.raw_y,
                           layout.raw_y + frame.height - 1,
                           Color{255, 64, 64});

        if (!use_runtime_facts) {
            DrawProjectedPath(image,
                              projector,
                              overlay_track.sampled_drivable_left_boundary,
                              layout.raw_x,
                              layout.raw_y,
                              frame.width,
                              frame.height,
                              Color{176, 126, 54},
                              1);
            DrawProjectedPath(image,
                              projector,
                              overlay_track.sampled_drivable_right_boundary,
                              layout.raw_x,
                              layout.raw_y,
                              frame.width,
                              frame.height,
                              Color{76, 154, 158},
                              1);
            DrawProjectedPath(image,
                              projector,
                              overlay_track.sampled_left_boundary,
                              layout.raw_x,
                              layout.raw_y,
                              frame.width,
                              frame.height,
                              Color{32, 255, 72},
                              2);
            DrawProjectedPath(image,
                              projector,
                              overlay_track.sampled_right_boundary,
                              layout.raw_x,
                              layout.raw_y,
                              frame.width,
                              frame.height,
                              Color{36, 142, 255},
                              2);
            if (draw_graph_geometry) {
                DrawProjectedNormalSegments(image,
                                            projector,
                                            corridor_graph,
                                            corridor_intervals,
                                            layout,
                                            Color{190, 82, 255});
            }
            if (draw_track_geometry) {
                DrawProjectedPath(image,
                                  projector,
                                  overlay_track.sampled_centerline,
                                  layout.raw_x,
                                  layout.raw_y,
                                  frame.width,
                                  frame.height,
                                  Color{255, 220, 32},
                                  2);
            }
            if (draw_reference_path) {
                DrawProjectedPath(image,
                                  projector,
                                  analysis.reference_path.sampled_path,
                                  layout.raw_x,
                                  layout.raw_y,
                                  frame.width,
                                  frame.height,
                                  Color{240, 240, 240},
                                  3);
            }
        }
        RenderBevImage(image, frame, projector, layout, lateral_limit_m, forward_max_m);
        DrawBevGrid(image, layout, lateral_limit_m, forward_max_m);
        if (!use_runtime_facts) {
            DrawBevSamples(image, sparse_samples, layout, lateral_limit_m, forward_max_m);
            DrawBevIntervals(image, corridor_intervals, layout, lateral_limit_m, forward_max_m);
            DrawBevElementEvidence(image, element_evidence, layout, lateral_limit_m, forward_max_m);
            if (draw_graph_geometry) {
                DrawBevPath(image,
                            corridor_graph.ordinary.sampled_path,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{255, 180, 32},
                            1);
            }
            if (draw_hypothesis_paths && analysis.road_hypotheses.left_arc.valid) {
                DrawBevPath(image,
                            analysis.road_hypotheses.left_arc.sampled_path,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{176, 126, 54},
                            1);
            }
            if (draw_hypothesis_paths && analysis.road_hypotheses.right_arc.valid) {
                DrawBevPath(image,
                            analysis.road_hypotheses.right_arc.sampled_path,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{0, 190, 255},
                            1);
            }
            if (draw_hypothesis_paths && analysis.road_hypotheses.forward_exit.valid) {
                DrawBevPath(image,
                            analysis.road_hypotheses.forward_exit.sampled_path,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{170, 255, 96},
                            1);
            }
            DrawBevPath(image,
                        overlay_track.sampled_drivable_left_boundary,
                        layout,
                        lateral_limit_m,
                        forward_max_m,
                        Color{176, 126, 54},
                        1);
            DrawBevPath(image,
                        overlay_track.sampled_drivable_right_boundary,
                        layout,
                        lateral_limit_m,
                        forward_max_m,
                        Color{76, 154, 158},
                        1);
            DrawBevPath(image,
                        overlay_track.sampled_left_boundary,
                        layout,
                        lateral_limit_m,
                        forward_max_m,
                        Color{32, 255, 72},
                        2);
            DrawBevPath(image,
                        overlay_track.sampled_right_boundary,
                        layout,
                        lateral_limit_m,
                        forward_max_m,
                        Color{36, 142, 255},
                        2);
            if (draw_graph_geometry) {
                DrawBevNormalSegments(image,
                                      corridor_graph,
                                      corridor_intervals,
                                      layout,
                                      lateral_limit_m,
                                      forward_max_m,
                                      Color{190, 82, 255});
            }
            if (draw_track_geometry) {
                DrawBevPath(image,
                            overlay_track.sampled_centerline,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{255, 220, 32},
                            2);
            }
            if (draw_reference_path) {
                DrawBevPath(image,
                            analysis.reference_path.sampled_path,
                            layout,
                            lateral_limit_m,
                            forward_max_m,
                            Color{240, 240, 240},
                            3);
            }
        }
        DrawBevVehicle(image, layout, lateral_limit_m, forward_max_m);
        DrawTextShadow(image,
                       layout.bev_x + 5,
                       layout.bev_y + 5,
                       ElementStatusToken(element_evidence),
                       element_evidence.cross_band.present
                           ? Color{255, 92, 48}
                           : ((element_evidence.left_circle_corner.present ||
                               element_evidence.right_circle_corner.present)
                                  ? Color{255, 86, 255}
                                  : Color{185, 185, 185}),
                       1);
        bool drew_lookahead = false;
        if (use_runtime_facts) {
            DrawRuntimeSnapshotFacts(image, runtime_facts, params, projector, layout, lateral_limit_m, forward_max_m);
            drew_lookahead = runtime_facts.has_lookahead;
        } else {
            drew_lookahead =
                DrawLookaheadTarget(image, projector, analysis.control_output, layout, lateral_limit_m, forward_max_m);
        }

        const int legend_y = layout.raw_y + layout.raw_height + 5;
        int legend_x = 6;
        if (use_runtime_facts) {
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{255, 255, 255}, "RUNTIME FACT");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{255, 240, 64}, "REF COL");
            (void)DrawLegendItem(image,
                                 legend_x,
                                 legend_y,
                                 drew_lookahead ? Color{255, 255, 255} : Color{100, 100, 100},
                                 "LOOKAHEAD");
            DrawTextShadow(image,
                           6,
                           legend_y + 14,
                           runtime_facts.active_module + "/" + runtime_facts.scene_phase + "/" +
                               runtime_facts.reference_mode,
                           Color{225, 225, 225},
                           1);
        } else {
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{86, 150, 150}, "SUPPORT");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{190, 142, 76}, "UNKNOWN");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{70, 78, 88}, "BACKGROUND");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{150, 118, 64}, "INTERVAL");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{32, 255, 72}, "OBS L");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{36, 142, 255}, "OBS R");
            legend_x = DrawLegendItem(image, legend_x, legend_y, Color{255, 92, 48}, "CROSS");
            legend_x = 6;
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{255, 180, 32}, "ORDINARY");
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{255, 128, 0}, "ARC L");
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{0, 190, 255}, "ARC R");
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{170, 255, 96}, "EXIT");
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{190, 82, 255}, "NORMAL");
            legend_x = DrawLegendItem(image, legend_x, legend_y + 14, Color{240, 240, 240}, "FINAL REF");
            (void)DrawLegendItem(image,
                                 legend_x,
                                 legend_y + 14,
                                 drew_lookahead ? Color{255, 255, 255} : Color{100, 100, 100},
                                 "LOOKAHEAD");
        }

        WriteBmp(output_path, image);

        std::cout << "module=" << analysis.perception.active_module
                  << " scene=" << analysis.perception.scene_phase
                  << " reference_mode=" << analysis.perception.reference_mode
                  << " cross_candidate=" << (analysis.scene_observation.cross_candidate ? "true" : "false")
                  << " width_expand_ratio=" << analysis.scene_observation.width_expand_ratio
                  << " cross_bilateral_open_score_m="
                  << analysis.scene_observation.cross_bilateral_open_score_m
                  << " left_open_score=" << analysis.scene_observation.left_open_score
                  << " right_open_score=" << analysis.scene_observation.right_open_score
                  << " near_lateral_error=" << analysis.perception.near_lateral_error
                  << " far_heading_error=" << analysis.perception.far_heading_error
                  << " preview_curvature=" << analysis.perception.preview_curvature
                  << " lookahead_distance_m=" << analysis.control_output.lookahead_distance_m
                  << " lookahead_lateral_error=" << analysis.control_output.lookahead_lateral_error
                  << " lookahead_heading_error=" << analysis.control_output.lookahead_heading_error
                  << " reference_curvature=" << analysis.control_output.reference_curvature
                  << " curvature_command=" << analysis.control_output.curvature_command
                  << " yaw_rate_target=" << analysis.control_output.yaw_rate_target
                  << " visible_range_m=" << analysis.perception.visible_range_m
                  << " track_confidence=" << analysis.perception.track_confidence
                  << " cross_band=" << (analysis.topology_evidence.element_evidence.cross_band.present ? "true" : "false")
                  << " left_corner="
                  << (analysis.topology_evidence.element_evidence.left_circle_corner.present ? "true" : "false")
                  << " right_corner="
                  << (analysis.topology_evidence.element_evidence.right_circle_corner.present ? "true" : "false")
                  << " warmup_count=" << warmup_paths.size()
                  << " confirm_cycles=" << confirm_cycles
                  << " projector_id=" << params.bev_projector.projector_id
                  << " params_source=" << params_source << "\n";
        std::cout << "overlay_algorithm=bev_topology_pipeline"
                  << " overlay_authority=" << (use_runtime_facts ? "runtime_snapshot" : "offline_reanalysis")
                  << " threshold=" << analysis.perception.threshold
                  << " sparse_valid=" << (sparse_samples.valid ? "true" : "false")
                  << " intervals_valid=" << (corridor_intervals.valid ? "true" : "false")
                  << " graph_valid=" << (corridor_graph.valid ? "true" : "false")
                  << " ordinary_candidate_valid=" << (corridor_graph.ordinary.valid ? "true" : "false")
                  << " left_arc_valid=" << (analysis.road_hypotheses.left_arc.valid ? "true" : "false")
                  << " right_arc_valid=" << (analysis.road_hypotheses.right_arc.valid ? "true" : "false")
                  << " forward_exit_valid=" << (analysis.road_hypotheses.forward_exit.valid ? "true" : "false")
                  << "\n";
        if (use_runtime_facts) {
            std::cout << "runtime_snapshot_source=" << runtime_facts.source_path
                      << " runtime_frame_id=" << runtime_facts.frame_id
                      << " runtime_near_lateral_error="
                      << (runtime_facts.has_near_lateral_error ? runtime_facts.near_lateral_error : 0.0F)
                      << " runtime_track_confidence="
                      << (runtime_facts.has_track_confidence ? runtime_facts.track_confidence : 0.0F)
                      << " runtime_reference_mode=" << runtime_facts.reference_mode
                      << "\n";
        }
        std::cout << "wrote_overlay=" << output_path << "\n";
    } catch (const std::exception& error) {
        std::cerr << "scene_overlay_probe failed: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
