#include "platform/steering_media_protocol.hpp"

// 转向媒体协议实现 —— 参数快照和图像帧的编码/解码。
// 使用 JSON 头部 + 二进制负载的复合格式，支持媒体链路传输。

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "platform/visual_element_evidence_json.hpp"

namespace ls2k::platform {
namespace {

/**
 * 向 JSON 输出流追加转义后的字符串（处理控制字符、引号和反斜杠）。
 * @param stream 输出流
 * @param value 待追加的原始字符串
 */
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

/**
 * 向 JSON 输出流追加数值（12 位有效数字精度）。
 * @param stream 输出流
 * @param value 待追加的数值
 */
void AppendJsonNumber(std::ostringstream& stream, double value) {
    stream << std::setprecision(12) << value;
}

/**
 * 向 JSON 输出流追加布尔值（"true" / "false"）。
 * @param stream 输出流
 * @param value 待追加的布尔值
 */
void AppendJsonBool(std::ostringstream& stream, bool value) {
    stream << (value ? "true" : "false");
}

/**
 * 构建转向快照 JSON —— 将 SteeringMediaSnapshotView 序列化为 JSON 对象字符串。
 * @param snapshot 转向快照视图数据
 * @return JSON 格式的快照字符串
 */
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

/**
 * 编码媒体信封 —— 构建 8 字节前缀（4 字节头部长度 + 4 字节负载长度）+ JSON 头部 + 二进制负载。
 * @param header_json JSON 格式的头部数据
 * @param payload_data 二进制负载数据指针（可为 nullptr）
 * @param payload_size 二进制负载数据大小（字节）
 * @param encoded 输出参数，编码后的完整媒体信封
 * @param error 输出参数，编码失败时的错误描述
 * @return true 表示编码成功
 */
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

/**
 * 计算灰度图像负载的字节数（每个像素 1 字节）。
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 * @return 负载字节数，若宽或高 <= 0 则返回 0
 */
std::size_t SteeringMediaImagePayloadBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

/**
 * 校验图像负载尺寸是否与声明分辨率一致。
 * @param width 声明的图像宽度
 * @param height 声明的图像高度
 * @param payload_size 实际负载大小（字节）
 * @param error 输出参数，校验失败时的错误描述
 * @return true 表示校验通过
 */
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

/**
 * 编码参数配置快照为媒体信封格式。
 * 生成 JSON 格式的头部，包含速度目标、PID 参数、BEV 配置等全部运行时参数。
 * @param snapshot 参数配置快照
 * @param encoded 输出参数，编码后的完整媒体信封数据
 * @param error 输出参数，编码失败时的错误描述
 * @return true 表示编码成功
 */
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
    header << ",\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.cross_wide_row_white_ratio_min);
    header << ",\"CIRCLE_EVIDENCE_ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element.circle_evidence_enabled);
    header << ",\"CIRCLE_MIN_SUPPORT_ROWS\":"
           << snapshot.param_snapshot.bev_element.circle_min_support_rows;
    header << ",\"CIRCLE_MIN_SAMPLEABLE_PER_ROW\":"
           << snapshot.param_snapshot.bev_element.circle_min_sampleable_per_row;
    header << ",\"CIRCLE_OPEN_EXPANSION_MIN_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_open_expansion_min_m);
    header << ",\"CIRCLE_OPENING_EXPANSION_RATIO_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_opening_expansion_ratio_min);
    header << ",\"CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element.circle_opposite_straight_drift_max_m);
    header << ",\"CIRCLE_OPPOSITE_SHRINK_RATIO_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_opposite_shrink_ratio_min);
    header << ",\"CIRCLE_PRESENT_CONFIDENCE_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_present_confidence_min);
    header << ",\"CIRCLE_ENTRY_TAKEOVER_ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element.circle_entry_takeover_enabled);
    header << ",\"CIRCLE_ENTRY_MIN_FRONTIER_POINTS\":"
           << snapshot.param_snapshot.bev_element.circle_entry_min_frontier_points;
    header << ",\"CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element.circle_entry_direction_min_lateral_m);
    header << ",\"CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element.circle_entry_max_interpolation_gap_m);
    header << ",\"CIRCLE_ENTRY_MAX_JOIN_JUMP_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_entry_max_join_jump_m);
    header << "}";
    header << ",\"BEV_ELEMENT_RASTER\":{";
    header << "\"ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element_raster.enabled);
    header << ",\"WIDTH\":" << snapshot.param_snapshot.bev_element_raster.width;
    header << "}";
    header << "}}";
    return EncodeEnvelope(header.str(), nullptr, 0, encoded, error);
}

/**
 * 编码图像帧为媒体信封格式（JSON 头部 + 灰度像素负载）。
 * 头部包含帧 ID、时间戳、分辨率、降采样系数、运动阶段和关联的转向快照。
 * @param frame 图像帧数据
 * @param encoded 输出参数，编码后的完整媒体信封
 * @param error 输出参数，编码失败时的错误描述
 * @return true 表示编码成功
 */
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
    header << ",\"source_width\":"
           << (frame.source_width > 0 ? frame.source_width : frame.width);
    header << ",\"source_height\":"
           << (frame.source_height > 0 ? frame.source_height : frame.height);
    header << ",\"downsample\":" << std::max(1, frame.downsample);
    header << ",\"steering_snapshot\":";
    header << BuildSteeringSnapshotJson(frame.steering_snapshot);
    header << "}";
    return EncodeEnvelope(header.str(), frame.pixel_data, frame.pixel_size, encoded, error);
}

/**
 * 解码媒体信封 —— 解析 8 字节长度前缀（4 字节头部长度 + 4 字节负载长度），
 * 然后提取 JSON 头部和二进制负载。
 * @param data 原始媒体信封数据
 * @param size 数据总大小（字节）
 * @param header_json 输出参数，解析得到的 JSON 头部
 * @param payload 输出参数，解析得到的二进制负载
 * @param error 输出参数，解码失败时的错误描述
 * @return true 表示解码成功
 */
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
