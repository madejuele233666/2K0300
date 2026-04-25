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
    stream << "\"lateral_error\":";
    AppendJsonNumber(stream, snapshot.lateral_error);
    stream << ",\"heading_error\":";
    AppendJsonNumber(stream, snapshot.heading_error);
    stream << ",\"curvature\":";
    AppendJsonNumber(stream, snapshot.curvature);
    stream << ",\"track_confidence\":";
    AppendJsonNumber(stream, snapshot.track_confidence);
    stream << ",\"highest_line\":" << snapshot.highest_line;
    stream << ",\"farthest_line\":" << snapshot.farthest_line;
    stream << ",\"steering_reference_col\":" << snapshot.steering_reference_col;
    stream << ",\"track_valid\":";
    AppendJsonBool(stream, snapshot.track_valid);
    stream << ",\"track_seed_col\":" << snapshot.track_seed_col;
    stream << ",\"track_seed_score\":";
    AppendJsonNumber(stream, snapshot.track_seed_score);
    stream << ",\"track_sign\":" << snapshot.track_sign;
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
    stream << ",\"scene_override_source\":";
    AppendJsonString(stream, snapshot.scene_override_source);
    stream << ",\"track_source\":";
    AppendJsonString(stream, snapshot.track_source);
    stream << ",\"roadblock_interface_state\":";
    AppendJsonString(stream, snapshot.roadblock_interface_state);
    stream << ",\"last_special_scene_correction\":";
    AppendJsonString(stream, snapshot.last_special_scene_correction);
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
