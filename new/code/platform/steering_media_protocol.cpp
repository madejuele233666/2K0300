#include "platform/steering_media_protocol.hpp"

#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ls2k::platform {
namespace {

void AppendJsonString(std::ostringstream& stream, const std::string& value) {
    stream << '"';
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
                           << std::setfill(' ');
                } else {
                    stream << ch;
                }
                break;
        }
    }
    stream << '"';
}

void AppendJsonNumber(std::ostringstream& stream, double value) {
    stream << std::setprecision(12) << value;
}

void AppendJsonBool(std::ostringstream& stream, bool value) {
    stream << (value ? "true" : "false");
}

std::string BuildSteeringSnapshotJson(const SteeringMediaSnapshotView& snapshot) {
    std::ostringstream stream;
    stream << "{";
    stream << "\"near_lateral_error\":";
    AppendJsonNumber(stream, snapshot.near_lateral_error);
    stream << ",\"far_heading_error\":";
    AppendJsonNumber(stream, snapshot.far_heading_error);
    stream << ",\"preview_curvature\":";
    AppendJsonNumber(stream, snapshot.preview_curvature);
    stream << ",\"lookahead_distance_m\":";
    AppendJsonNumber(stream, snapshot.lookahead_distance_m);
    stream << ",\"lookahead_lateral_error\":";
    AppendJsonNumber(stream, snapshot.lookahead_lateral_error);
    stream << ",\"lookahead_heading_error\":";
    AppendJsonNumber(stream, snapshot.lookahead_heading_error);
    stream << ",\"reference_curvature\":";
    AppendJsonNumber(stream, snapshot.reference_curvature);
    stream << ",\"curvature_command\":";
    AppendJsonNumber(stream, snapshot.curvature_command);
    stream << ",\"yaw_rate_target\":";
    AppendJsonNumber(stream, snapshot.yaw_rate_target);
    stream << ",\"visible_range_m\":";
    AppendJsonNumber(stream, snapshot.visible_range_m);
    stream << ",\"scene_evidence\":{";
    stream << "\"width_expand_ratio\":";
    AppendJsonNumber(stream, snapshot.scene_width_expand_ratio);
    stream << ",\"cross_bilateral_open_score_m\":";
    AppendJsonNumber(stream, snapshot.scene_cross_bilateral_open_score_m);
    stream << ",\"cross_bilateral_open\":";
    AppendJsonBool(stream, snapshot.scene_cross_bilateral_open);
    stream << ",\"cross_candidate\":";
    AppendJsonBool(stream, snapshot.scene_cross_candidate);
    stream << ",\"zebra_candidate\":";
    AppendJsonBool(stream, snapshot.scene_zebra_candidate);
    stream << ",\"circle_left_candidate\":";
    AppendJsonBool(stream, snapshot.scene_circle_left_candidate);
    stream << ",\"circle_right_candidate\":";
    AppendJsonBool(stream, snapshot.scene_circle_right_candidate);
    stream << ",\"left_open_score\":";
    AppendJsonNumber(stream, snapshot.scene_left_open_score);
    stream << ",\"right_open_score\":";
    AppendJsonNumber(stream, snapshot.scene_right_open_score);
    stream << ",\"left_contract_score\":";
    AppendJsonNumber(stream, snapshot.scene_left_contract_score);
    stream << ",\"right_contract_score\":";
    AppendJsonNumber(stream, snapshot.scene_right_contract_score);
    stream << ",\"left_boundary_heading_abs_rad\":";
    AppendJsonNumber(stream, snapshot.scene_left_boundary_heading_abs_rad);
    stream << ",\"right_boundary_heading_abs_rad\":";
    AppendJsonNumber(stream, snapshot.scene_right_boundary_heading_abs_rad);
    stream << ",\"circle_left_opposite_straight\":";
    AppendJsonBool(stream, snapshot.scene_circle_left_opposite_straight);
    stream << ",\"circle_right_opposite_straight\":";
    AppendJsonBool(stream, snapshot.scene_circle_right_opposite_straight);
    stream << "}";
    stream << ",\"track_confidence\":";
    AppendJsonNumber(stream, snapshot.track_confidence);
    stream << ",\"track_valid\":";
    AppendJsonBool(stream, snapshot.track_valid);
    stream << ",\"sign_flip_blocked\":";
    AppendJsonBool(stream, snapshot.sign_flip_blocked);
    stream << ",\"imu_grace_active\":";
    AppendJsonBool(stream, snapshot.imu_grace_active);
    stream << ",\"gyro_heading_delta_deg\":";
    AppendJsonNumber(stream, snapshot.gyro_heading_delta_deg);
    stream << ",\"gyro_consistency_score\":";
    AppendJsonNumber(stream, snapshot.gyro_consistency_score);
    stream << ",\"threshold\":" << snapshot.threshold;
    stream << ",\"threshold_veto\":";
    AppendJsonBool(stream, snapshot.threshold_veto);
    stream << ",\"active_module\":";
    AppendJsonString(stream, snapshot.active_module);
    stream << ",\"scene_phase\":";
    AppendJsonString(stream, snapshot.scene_phase);
    stream << ",\"reference_mode\":";
    AppendJsonString(stream, snapshot.reference_mode);
    stream << ",\"scene_override_source\":";
    AppendJsonString(stream, snapshot.scene_override_source);
    stream << ",\"circle_direction\":";
    AppendJsonString(stream, snapshot.circle_direction);
    stream << ",\"circle_reference_mode\":";
    AppendJsonString(stream, snapshot.circle_reference_mode);
    stream << ",\"circle_heading_delta_deg\":";
    AppendJsonNumber(stream, snapshot.circle_heading_delta_deg);
    stream << ",\"circle_entry_signal_active\":";
    AppendJsonBool(stream, snapshot.circle_entry_signal_active);
    stream << ",\"roadblock_interface_state\":";
    AppendJsonString(stream, snapshot.roadblock_interface_state);
    stream << ",\"roadblock_active\":";
    AppendJsonBool(stream, snapshot.roadblock_active);
    stream << ",\"resolved_fuzzy_p\":";
    AppendJsonNumber(stream, snapshot.resolved_fuzzy_p);
    stream << ",\"camera_p_term\":";
    AppendJsonNumber(stream, snapshot.camera_p_term);
    stream << ",\"camera_d_term\":";
    AppendJsonNumber(stream, snapshot.camera_d_term);
    stream << ",\"w_target\":";
    AppendJsonNumber(stream, snapshot.w_target);
    stream << ",\"gyro_z\":";
    AppendJsonNumber(stream, snapshot.gyro_z);
    stream << ",\"gyro_error\":";
    AppendJsonNumber(stream, snapshot.gyro_error);
    stream << ",\"gyro_p_term\":";
    AppendJsonNumber(stream, snapshot.gyro_p_term);
    stream << ",\"gyro_d_term\":";
    AppendJsonNumber(stream, snapshot.gyro_d_term);
    stream << ",\"raw_turn_output\":" << snapshot.raw_turn_output;
    stream << ",\"applied_turn_output\":" << snapshot.applied_turn_output;
    stream << "}";
    return stream.str();
}

bool EncodeEnvelope(const std::string& header_json,
                    const std::uint8_t* payload_data,
                    std::size_t payload_size,
                    std::vector<std::uint8_t>& encoded,
                    std::string& error) {
    if (header_json.empty()) {
        error = "steering media header must not be empty";
        return false;
    }
    if (header_json.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        payload_size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        error = "steering media envelope exceeds 32-bit length prefix";
        return false;
    }
    const std::uint32_t header_len = static_cast<std::uint32_t>(header_json.size());
    const std::uint32_t payload_len = static_cast<std::uint32_t>(payload_size);
    encoded.assign(8 + header_len + payload_len, 0);
    encoded[0] = static_cast<std::uint8_t>((header_len >> 24) & 0xFFU);
    encoded[1] = static_cast<std::uint8_t>((header_len >> 16) & 0xFFU);
    encoded[2] = static_cast<std::uint8_t>((header_len >> 8) & 0xFFU);
    encoded[3] = static_cast<std::uint8_t>(header_len & 0xFFU);
    encoded[4] = static_cast<std::uint8_t>((payload_len >> 24) & 0xFFU);
    encoded[5] = static_cast<std::uint8_t>((payload_len >> 16) & 0xFFU);
    encoded[6] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFFU);
    encoded[7] = static_cast<std::uint8_t>(payload_len & 0xFFU);
    std::memcpy(encoded.data() + 8, header_json.data(), header_len);
    if (payload_len > 0 && payload_data != nullptr) {
        std::memcpy(encoded.data() + 8 + header_len, payload_data, payload_len);
    }
    error.clear();
    return true;
}

}  // namespace

std::size_t SteeringMediaImagePayloadBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

bool ValidateSteeringMediaImagePayload(int width,
                                       int height,
                                       std::size_t payload_size,
                                       std::string& error) {
    const std::size_t expected = SteeringMediaImagePayloadBytes(width, height);
    if (expected == 0) {
        error = "steering image frame dimensions must be positive";
        return false;
    }
    if (payload_size != expected) {
        error = "steering image payload must be exactly " + std::to_string(expected) + " bytes";
        return false;
    }
    error.clear();
    return true;
}

bool EncodeSteeringMediaConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                       std::vector<std::uint8_t>& encoded,
                                       std::string& error) {
    std::ostringstream header;
    header << "{";
    header << "\"type\":\"config_snapshot\"";
    header << ",\"publish_time_ms\":" << snapshot.publish_time_ms;
    header << ",\"media_publish_interval_ms\":" << snapshot.media_publish_interval_ms;
    header << ",\"param_snapshot\":{";
    header << "\"pid_turn_camera_p\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_camera_p);
    header << ",\"pid_turn_camera_p_scale\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_camera_p_scale);
    header << ",\"pid_turn_camera_d\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_camera_d);
    header << ",\"pid_turn_camera_use_fuzzy\":";
    AppendJsonBool(header, snapshot.param_snapshot.pid_turn_camera_use_fuzzy);
    header << ",\"pid_turn_gyro_camera_p\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_gyro_camera_p);
    header << ",\"pid_turn_gyro_camera_i\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_gyro_camera_i);
    header << ",\"pid_turn_gyro_camera_d\":";
    AppendJsonNumber(header, snapshot.param_snapshot.pid_turn_gyro_camera_d);
    header << ",\"p_mode\":" << snapshot.param_snapshot.p_mode;
    header << ",\"speed_base\":";
    AppendJsonNumber(header, snapshot.param_snapshot.speed_base);
    header << ",\"control_period_ms\":" << snapshot.param_snapshot.control_period_ms;
    header << ",\"raw_turn_output_limit\":" << snapshot.param_snapshot.raw_turn_output_limit;
    header << ",\"BEV_PROJECTOR\":{";
    header << "\"VALID\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_projector.valid);
    header << ",\"PROJECTOR_ID\":";
    AppendJsonString(header, snapshot.param_snapshot.bev_projector.projector_id);
    header << ",\"PROJECTOR_HASH\":";
    AppendJsonString(header, snapshot.param_snapshot.bev_projector.projector_hash);
    header << ",\"DEBUG_GRID_WIDTH\":" << snapshot.param_snapshot.bev_projector.debug_grid_width;
    header << ",\"DEBUG_GRID_HEIGHT\":" << snapshot.param_snapshot.bev_projector.debug_grid_height;
    for (std::size_t index = 0; index < port::kBevCalibrationPointCount; ++index) {
        header << ",\"SOURCE_ROW_" << index << "\":";
        AppendJsonNumber(header, snapshot.param_snapshot.bev_projector.source_points[index].row_px);
        header << ",\"SOURCE_COL_" << index << "\":";
        AppendJsonNumber(header, snapshot.param_snapshot.bev_projector.source_points[index].col_px);
        header << ",\"TARGET_FORWARD_" << index << "\":";
        AppendJsonNumber(header, snapshot.param_snapshot.bev_projector.target_points[index].forward_m);
        header << ",\"TARGET_LATERAL_" << index << "\":";
        AppendJsonNumber(header, snapshot.param_snapshot.bev_projector.target_points[index].lateral_m);
    }
    header << "}";
    header << ",\"BEV_GEOMETRY\":{";
    for (std::size_t index = 0; index < port::kBevTrackSampleCount; ++index) {
        if (index > 0) {
            header << ",";
        }
        header << "\"FORWARD_SAMPLE_" << index << "\":";
        AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.forward_samples_m[index]);
    }
    header << ",\"SEARCH_LATERAL_LIMIT_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.search_lateral_limit_m);
    header << ",\"LATERAL_STEP_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.lateral_step_m);
    header << ",\"NOMINAL_LANE_WIDTH_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.nominal_lane_width_m);
    header << ",\"MIN_LANE_WIDTH_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.min_lane_width_m);
    header << ",\"MAX_LANE_WIDTH_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.max_lane_width_m);
    header << ",\"MIN_VISIBLE_RANGE_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.min_visible_range_m);
    header << ",\"MIN_TRACK_CONFIDENCE\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.min_track_confidence);
    header << ",\"CONTINUITY_BREAK_THRESHOLD_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.continuity_break_threshold_m);
    header << ",\"SAMPLE_ROW_STEP_PX\":" << snapshot.param_snapshot.bev_geometry.sample_row_step_px;
    header << ",\"IMAGE_BORDER_TRUNCATION_MARGIN_PX\":"
           << snapshot.param_snapshot.bev_geometry.image_border_truncation_margin_px;
    header << "}";
    header << ",\"BEV_SCENE_FSM\":{";
    header << "\"BEND_SEVERITY_CONFIRM\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.bend_severity_confirm);
    header << ",\"CROSS_EXPAND_RATIO_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.cross_expand_ratio_min);
    header << ",\"CROSS_BILATERAL_OPEN_MIN_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.cross_bilateral_open_min_m);
    header << ",\"CROSS_CONFIRM_CYCLES\":" << snapshot.param_snapshot.bev_scene_fsm.cross_confirm_cycles;
    header << ",\"CROSS_HOLD_CYCLES\":" << snapshot.param_snapshot.bev_scene_fsm.cross_hold_cycles;
    header << ",\"ZEBRA_TRANSITION_DENSITY_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.zebra_transition_density_min);
    header << ",\"ZEBRA_HOLD_CYCLES\":" << snapshot.param_snapshot.bev_scene_fsm.zebra_hold_cycles;
    header << ",\"CIRCLE_OPEN_SCORE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.circle_open_score_min);
    header << ",\"CIRCLE_CONTRACT_SCORE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.circle_contract_score_min);
    header << ",\"CIRCLE_OPPOSITE_HEADING_ABS_MAX\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.circle_opposite_heading_abs_max);
    header << ",\"CIRCLE_CONFIRM_CYCLES\":" << snapshot.param_snapshot.bev_scene_fsm.circle_confirm_cycles;
    header << ",\"CIRCLE_RELEASE_CYCLES\":" << snapshot.param_snapshot.bev_scene_fsm.circle_release_cycles;
    header << ",\"RELEASE_TRACK_CONFIDENCE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_scene_fsm.release_track_confidence_min);
    header << "}";
    header << ",\"BEV_CONTROL_MODEL\":{";
    header << "\"NEAR_SAMPLE_INDEX\":" << snapshot.param_snapshot.bev_control_model.near_sample_index;
    header << ",\"FAR_SAMPLE_INDEX\":" << snapshot.param_snapshot.bev_control_model.far_sample_index;
    header << ",\"CURVATURE_SAMPLE_INDEX\":" << snapshot.param_snapshot.bev_control_model.curvature_sample_index;
    header << ",\"LOOKAHEAD_VISIBLE_RANGE_RATIO\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.lookahead_visible_range_ratio);
    header << ",\"LOOKAHEAD_MIN_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.lookahead_min_m);
    header << ",\"LOOKAHEAD_MAX_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.lookahead_max_m);
    header << ",\"PURE_PURSUIT_GAIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.pure_pursuit_gain);
    header << ",\"HEADING_CURVATURE_GAIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.heading_curvature_gain);
    header << ",\"CURVATURE_FEEDFORWARD_GAIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.curvature_feedforward_gain);
    header << ",\"CURVATURE_COMMAND_LIMIT\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.curvature_command_limit);
    header << ",\"CURVATURE_TO_W_TARGET_GAIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.curvature_to_w_target_gain);
    header << ",\"LOW_CONFIDENCE_THRESHOLD\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.low_confidence_threshold);
    header << ",\"STEERING_SUPPRESSION_CONFIDENCE\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.steering_suppression_confidence);
    header << ",\"LOW_VISIBLE_RANGE_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.low_visible_range_m);
    header << ",\"MIN_GAIN_SCALE\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.min_gain_scale);
    header << ",\"MIN_SPEED_LIMIT_SCALE\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.min_speed_limit_scale);
    header << ",\"MAX_REFERENCE_BIAS_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.max_reference_bias_m);
    header << "}";
    header << "}}";
    return EncodeEnvelope(header.str(), nullptr, 0, encoded, error);
}

bool EncodeSteeringMediaImageFrame(const SteeringMediaImageFrame& frame,
                                   std::vector<std::uint8_t>& encoded,
                                   std::string& error) {
    if (frame.pixel_data == nullptr) {
        error = "steering image frame payload is missing";
        return false;
    }
    if (!ValidateSteeringMediaImagePayload(frame.width, frame.height, frame.pixel_size, error)) {
        return false;
    }

    std::ostringstream header;
    header << "{";
    header << "\"type\":\"image_frame\"";
    header << ",\"frame_id\":" << frame.frame_id;
    header << ",\"capture_time_ms\":" << frame.capture_time_ms;
    header << ",\"publish_time_ms\":" << frame.publish_time_ms;
    header << ",\"motion_phase\":";
    AppendJsonString(header, frame.motion_phase == nullptr ? "DISARMED" : frame.motion_phase);
    header << ",\"pixel_format\":\"gray8\"";
    header << ",\"width\":" << frame.width;
    header << ",\"height\":" << frame.height;
    header << ",\"steering_snapshot\":";
    header << BuildSteeringSnapshotJson(frame.steering_snapshot);
    header << "}";
    return EncodeEnvelope(header.str(), frame.pixel_data, frame.pixel_size, encoded, error);
}

bool DecodeSteeringMediaEnvelope(const std::uint8_t* data,
                                 std::size_t size,
                                 std::string& header_json,
                                 std::vector<std::uint8_t>& payload,
                                 std::string& error) {
    if (data == nullptr || size < 8) {
        error = "steering media envelope is shorter than the 8-byte prefix";
        return false;
    }
    const std::uint32_t header_len = (static_cast<std::uint32_t>(data[0]) << 24) |
                                     (static_cast<std::uint32_t>(data[1]) << 16) |
                                     (static_cast<std::uint32_t>(data[2]) << 8) |
                                     static_cast<std::uint32_t>(data[3]);
    const std::uint32_t payload_len = (static_cast<std::uint32_t>(data[4]) << 24) |
                                      (static_cast<std::uint32_t>(data[5]) << 16) |
                                      (static_cast<std::uint32_t>(data[6]) << 8) |
                                      static_cast<std::uint32_t>(data[7]);
    const std::size_t expected_size = 8U + static_cast<std::size_t>(header_len) +
                                      static_cast<std::size_t>(payload_len);
    if (expected_size != size) {
        error = "steering media envelope length prefix mismatch";
        return false;
    }

    header_json.assign(reinterpret_cast<const char*>(data + 8), header_len);
    payload.assign(data + 8 + header_len, data + expected_size);
    error.clear();
    return true;
}

}  // namespace ls2k::platform
