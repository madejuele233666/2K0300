#include "platform/steering_media_protocol.hpp"

// 转向媒体协议实现 —— 参数快照和图像帧的编码/解码。
// 使用 JSON 头部 + 二进制负载的复合格式，支持媒体链路传输。

#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "platform/visual_element_evidence_json.hpp"

namespace ls2k::platform {
namespace {

// JSON 字符串转义追加（处理控制字符和引号）
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

// JSON 数值追加（12 位精度）
void AppendJsonNumber(std::ostringstream& stream, double value) {
    stream << std::setprecision(12) << value;
}

// JSON 布尔值追加
void AppendJsonBool(std::ostringstream& stream, bool value) {
    stream << (value ? "true" : "false");
}

// 构建转向快照 JSON —— 将 SteeringMediaSnapshotView 序列化为 JSON 对象
std::string BuildSteeringSnapshotJson(const SteeringMediaSnapshotView& snapshot) {
    std::ostringstream stream;
    stream << "{";
    stream << "\"perception_health\":{\"projector_ok\":";
    AppendJsonBool(stream, snapshot.perception_health.projector_ok);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.perception_health.reason);
    stream << "}";
    stream << ",\"element_evidence\":";
    AppendVisualElementEvidenceJson(stream, snapshot.element_evidence);
    stream << ",\"visual_reference\":{\"present\":";
    AppendJsonBool(stream, snapshot.visual_reference.present);
    stream << ",\"source\":";
    AppendJsonString(stream, snapshot.visual_reference.source);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.visual_reference.reason);
    stream << ",\"candidate_count\":" << snapshot.visual_reference.candidate_count;
    stream << ",\"rejected_candidate_reason\":";
    AppendJsonString(stream, snapshot.visual_reference.rejected_candidate_reason);
    stream << "}";
    stream << ",\"reference\":{\"mode\":";
    AppendJsonString(stream, snapshot.reference.mode);
    stream << ",\"source\":";
    AppendJsonString(stream, snapshot.reference.source);
    stream << "}";
    stream << ",\"eligibility\":{\"usable\":";
    AppendJsonBool(stream, snapshot.eligibility.usable);
    stream << ",\"leading_usable_samples\":" << snapshot.eligibility.leading_usable_samples;
    stream << ",\"leading_min_forward_m\":";
    AppendJsonNumber(stream, snapshot.eligibility.leading_min_forward_m);
    stream << ",\"leading_max_forward_m\":";
    AppendJsonNumber(stream, snapshot.eligibility.leading_max_forward_m);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.eligibility.reason);
    stream << "}";
    stream << ",\"lateral_error\":{\"computed\":";
    AppendJsonBool(stream, snapshot.lateral_error.computed);
    stream << ",\"weighted_lateral_error_m\":";
    AppendJsonNumber(stream, snapshot.lateral_error.weighted_lateral_error_m);
    stream << ",\"weighted_sample_count\":" << snapshot.lateral_error.weighted_sample_count;
    stream << ",\"weight_sum\":";
    AppendJsonNumber(stream, snapshot.lateral_error.weight_sum);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.lateral_error.reason);
    stream << "}";
    stream << ",\"reference_control\":{\"ready\":";
    AppendJsonBool(stream, snapshot.reference_control.ready);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.reference_control.reason);
    stream << "}";
    stream << ",\"safety_gate\":{\"veto_active\":";
    AppendJsonBool(stream, snapshot.safety_gate.veto_active);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.safety_gate.reason);
    stream << "}";
    stream << ",\"degraded\":{\"active\":";
    AppendJsonBool(stream, snapshot.degraded.active);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.degraded.reason);
    stream << "}";
    stream << ",\"yaw_control\":{\"turn_output_target\":";
    AppendJsonNumber(stream, snapshot.yaw_control.turn_output_target);
    stream << "}";
    stream << ",\"actuator\":{\"raw_turn_output\":" << snapshot.actuator.raw_turn_output;
    stream << ",\"applied_turn_output\":" << snapshot.actuator.applied_turn_output << "}";
    stream << ",\"threshold\":" << snapshot.threshold;
    stream << "}";
    return stream.str();
}

// 编码媒体信封 —— 4 字节头部长度 + 4 字节负载长度 + JSON 头部 + 二进制负载
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

// 计算灰度图像负载字节数
std::size_t SteeringMediaImagePayloadBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

// 校验图像负载尺寸是否与声明分辨率一致
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

// 编码参数配置快照为媒体信封格式
bool EncodeSteeringMediaConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                       std::vector<std::uint8_t>& encoded,
                                       std::string& error) {
    std::ostringstream header;
    header << "{";
    header << "\"type\":\"config_snapshot\"";
    header << ",\"publish_time_ms\":" << snapshot.publish_time_ms;
    header << ",\"media_publish_interval_ms\":" << snapshot.media_publish_interval_ms;
    header << ",\"param_snapshot\":{";
    header << "\"running_speed_target\":";
    AppendJsonNumber(header, snapshot.param_snapshot.running_speed_target);
    header << ",\"yaw_rate_pid\":{";
    header << "\"p\":";
    AppendJsonNumber(header, snapshot.param_snapshot.yaw_rate_pid_p);
    header << ",\"i\":";
    AppendJsonNumber(header, snapshot.param_snapshot.yaw_rate_pid_i);
    header << ",\"d\":";
    AppendJsonNumber(header, snapshot.param_snapshot.yaw_rate_pid_d);
    header << "}";
    header << ",\"control_period_ms\":" << snapshot.param_snapshot.control_period_ms;
    header << ",\"low_voltage_sample_interval_ms\":"
           << snapshot.param_snapshot.low_voltage_sample_interval_ms;
    header << ",\"low_voltage_raw_threshold\":"
           << snapshot.param_snapshot.low_voltage_raw_threshold;
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
    for (std::size_t index = 0; index < port::kBevReferenceSampleCount; ++index) {
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
    header << "}";
    header << ",\"BEV_CLASSIFICATION\":{";
    header << "\"WHITE_CONFIDENCE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_classification.white_confidence_min);
    header << ",\"UNKNOWN_CONFIDENCE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_classification.unknown_confidence_min);
    header << ",\"HOLD_LAST_MAX_CYCLES\":"
           << snapshot.param_snapshot.bev_classification.hold_last_max_cycles;
    header << "}";
    header << ",\"BEV_CONTROL_MODEL\":{";
    header << "\"LATERAL_ERROR_FAR_WEIGHT\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_control_model.lateral_error_far_weight);
    header << ",\"LATERAL_ERROR_TO_WHEEL_DELTA_GAIN\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_control_model.lateral_error_to_wheel_delta_gain);
    header << ",\"MIN_LEADING_REFERENCE_SAMPLES\":"
           << snapshot.param_snapshot.bev_control_model.min_leading_reference_samples;
    header << "}";
    header << ",\"BEV_ELEMENT\":{";
    header << "\"CROSS_EXIT_TAKEOVER_ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element.cross_exit_takeover_enabled);
    header << "}";
    header << ",\"BEV_ELEMENT_RASTER\":{";
    header << "\"ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element_raster.enabled);
    header << ",\"WIDTH\":" << snapshot.param_snapshot.bev_element_raster.width;
    header << "}";
    header << "}}";
    return EncodeEnvelope(header.str(), nullptr, 0, encoded, error);
}

// 编码图像帧为媒体信封格式（JSON 头部 + 灰度像素数据）
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

// 解码媒体信封 —— 解析 8 字节前缀 + JSON 头部 + 二进制负载
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
