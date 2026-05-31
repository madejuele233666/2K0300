#include "platform/steering_media_protocol.hpp"

// 转向媒体协议实现 —— 参数快照和图像帧的编码/解码。
// 使用 JSON 头部 + 二进制负载的复合格式，支持媒体链路传输。

#include <algorithm>
#include <cmath>
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

void AppendFiniteJsonNumber(std::ostringstream& stream, double value) {
    if (std::isfinite(value)) {
        AppendJsonNumber(stream, value);
        return;
    }
    stream << "null";
}

void AppendOptionalJsonNumber(std::ostringstream& stream, bool available, double value) {
    if (!available) {
        stream << "null";
        return;
    }
    AppendFiniteJsonNumber(stream, value);
}

const char* VisualReferenceCandidateKindToken(port::VisualReferenceCandidateKind kind) {
    switch (kind) {
        case port::VisualReferenceCandidateKind::kLine:
            return "line";
        case port::VisualReferenceCandidateKind::kCrossExit:
            return "cross_exit";
        case port::VisualReferenceCandidateKind::kCircleLeft:
            return "circle_left";
        case port::VisualReferenceCandidateKind::kCircleRight:
            return "circle_right";
        case port::VisualReferenceCandidateKind::kRoadblockBypass:
            return "roadblock_bypass";
        case port::VisualReferenceCandidateKind::kMlGrounded:
            return "ml_grounded";
    }
    return "line";
}

const char* ReferenceModeToken(port::ReferenceMode mode) {
    switch (mode) {
        case port::ReferenceMode::kNone:
            return "none";
        case port::ReferenceMode::kIntervalCenter:
            return "interval_center";
        case port::ReferenceMode::kHoldLast:
            return "hold_last";
    }
    return "none";
}

const char* PathPointSourceToken(port::BEVPathPointSource source) {
    switch (source) {
        case port::BEVPathPointSource::kNone:
            return "none";
        case port::BEVPathPointSource::kIntervalCenter:
            return "interval_center";
        case port::BEVPathPointSource::kHold:
            return "hold";
    }
    return "none";
}

std::uint64_t CountPresentPathSamples(const port::BEVReferencePath& path) {
    std::uint64_t count = 0;
    for (const port::BEVPathSample& sample : path.sampled_path) {
        if (sample.present) {
            ++count;
        }
    }
    return count;
}

void AppendPathSamplesJson(std::ostringstream& stream, const port::BEVReferencePath& path) {
    stream << "[";
    bool first = true;
    for (std::size_t index = 0; index < path.sampled_path.size(); ++index) {
        const port::BEVPathSample& sample = path.sampled_path[index];
        if (!sample.present) {
            continue;
        }
        if (!first) {
            stream << ",";
        }
        first = false;
        stream << "{\"index\":" << index;
        stream << ",\"forward_m\":";
        AppendFiniteJsonNumber(stream, sample.point.forward_m);
        stream << ",\"lateral_m\":";
        AppendFiniteJsonNumber(stream, sample.point.lateral_m);
        stream << ",\"confidence\":";
        AppendFiniteJsonNumber(stream, sample.confidence);
        stream << ",\"source\":";
        AppendJsonString(stream, PathPointSourceToken(sample.source));
        stream << "}";
    }
    stream << "]";
}

void AppendVisualReferenceCandidatePathJson(std::ostringstream& stream,
                                            const port::VisualReferenceCandidate& candidate) {
    stream << "{\"present\":";
    AppendJsonBool(stream, candidate.present);
    stream << ",\"kind\":";
    AppendJsonString(stream, VisualReferenceCandidateKindToken(candidate.kind));
    stream << ",\"source\":";
    AppendJsonString(stream, candidate.source);
    stream << ",\"reason\":";
    AppendJsonString(stream, candidate.reason);
    stream << ",\"confidence\":";
    AppendFiniteJsonNumber(stream, candidate.confidence);
    stream << ",\"mode\":";
    AppendJsonString(stream, ReferenceModeToken(candidate.reference_path.mode));
    stream << ",\"sample_count\":"
           << CountPresentPathSamples(candidate.reference_path);
    stream << ",\"samples\":";
    AppendPathSamplesJson(stream, candidate.reference_path);
    stream << "}";
}

void AppendVisualReferenceCandidatePathSetJson(
    std::ostringstream& stream,
    const port::VisualReferenceCandidatePathSet& candidate_paths) {
    const std::size_t stored_count =
        std::min(candidate_paths.count, candidate_paths.entries.size());
    stream << "{\"count\":" << candidate_paths.count;
    stream << ",\"omitted_count\":" << candidate_paths.omitted_count;
    stream << ",\"items\":[";
    for (std::size_t index = 0; index < stored_count; ++index) {
        if (index > 0) {
            stream << ",";
        }
        AppendVisualReferenceCandidatePathJson(stream, candidate_paths.entries[index]);
    }
    stream << "]}";
}

void AppendCircleV2PointObservationJson(std::ostringstream& stream,
                                        const port::CircleV2PointObservation& point) {
    stream << "{\"available\":";
    AppendJsonBool(stream, point.available);
    stream << ",\"forward_m\":";
    AppendOptionalJsonNumber(stream, point.available, point.point.forward_m);
    stream << ",\"lateral_m\":";
    AppendOptionalJsonNumber(stream, point.available, point.point.lateral_m);
    stream << "}";
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
    stream << ",\"circle_v2\":{\"enabled\":";
    AppendJsonBool(stream, snapshot.circle_v2.enabled);
    stream << ",\"frame_phase\":";
    AppendJsonString(stream, snapshot.circle_v2.frame_phase);
    stream << ",\"next_phase\":";
    AppendJsonString(stream, snapshot.circle_v2.next_phase);
    stream << ",\"dir\":";
    AppendJsonString(stream, snapshot.circle_v2.dir);
    stream << ",\"reference_role\":";
    AppendJsonString(stream, snapshot.circle_v2.reference_role);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.circle_v2.reason);
    stream << ",\"motion_arc_available\":";
    AppendJsonBool(stream, snapshot.circle_v2.motion_arc_available);
    stream << ",\"inner_trace_elapsed_ms\":"
           << snapshot.circle_v2.inner_trace_elapsed_ms;
    stream << ",\"directed_turn_angle_rad\":"
           << snapshot.circle_v2.directed_turn_angle_rad;
    stream << ",\"entry_points\":{\"left\":";
    AppendCircleV2PointObservationJson(stream, snapshot.circle_v2.entry_points.left);
    stream << ",\"right\":";
    AppendCircleV2PointObservationJson(stream, snapshot.circle_v2.entry_points.right);
    stream << "}";
    stream << "}";
    stream << ",\"visual_reference\":{\"present\":";
    AppendJsonBool(stream, snapshot.visual_reference.present);
    stream << ",\"source\":";
    AppendJsonString(stream, snapshot.visual_reference.source);
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.visual_reference.reason);
    stream << ",\"candidate_count\":" << snapshot.visual_reference.candidate_count;
    stream << ",\"rejected_candidate_reason\":";
    AppendJsonString(stream, snapshot.visual_reference.rejected_candidate_reason);
    stream << ",\"path_candidates\":";
    AppendVisualReferenceCandidatePathSetJson(stream, snapshot.visual_reference.candidate_paths);
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
    stream << ",\"tracking_geometry\":{\"computed\":";
    AppendJsonBool(stream, snapshot.tracking_geometry.computed);
    stream << ",\"lateral_offset_m\":";
    AppendJsonNumber(stream, snapshot.tracking_geometry.lateral_offset_m);
    stream << ",\"heading_error_rad\":";
    AppendJsonNumber(stream, snapshot.tracking_geometry.heading_error_rad);
    stream << ",\"curvature_m_inv\":";
    AppendJsonNumber(stream, snapshot.tracking_geometry.curvature_m_inv);
    stream << ",\"sample_count\":" << snapshot.tracking_geometry.sample_count;
    stream << ",\"reason\":";
    AppendJsonString(stream, snapshot.tracking_geometry.reason);
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
    stream << ",\"lateral_term\":";
    AppendJsonNumber(stream, snapshot.yaw_control.lateral_term);
    stream << ",\"heading_term\":";
    AppendJsonNumber(stream, snapshot.yaw_control.heading_term);
    stream << ",\"curvature_term\":";
    AppendJsonNumber(stream, snapshot.yaw_control.curvature_term);
    stream << "}";
    stream << ",\"actuator\":{\"raw_turn_output\":" << snapshot.actuator.raw_turn_output;
    stream << ",\"applied_turn_output\":" << snapshot.actuator.applied_turn_output;
    stream << ",\"left_drive_pwm_command\":" << snapshot.actuator.left_drive_pwm_command;
    stream << ",\"right_drive_pwm_command\":" << snapshot.actuator.right_drive_pwm_command;
    stream << ",\"left_brushless_pwm_command\":" << snapshot.actuator.left_brushless_pwm_command;
    stream << ",\"right_brushless_pwm_command\":" << snapshot.actuator.right_brushless_pwm_command;
    stream << ",\"apply_outcome\":";
    AppendJsonString(stream, snapshot.actuator.apply_outcome);
    stream << "}";
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

std::size_t SteeringMediaImagePayloadBytesForFormat(int width, int height, const char* pixel_format) {
    const std::size_t pixels = SteeringMediaImagePayloadBytes(width, height);
    if (pixels == 0) {
        return 0;
    }
    if (pixel_format != nullptr && std::strcmp(pixel_format, "gray1") == 0) {
        return (pixels + 7U) / 8U;
    }
    if (pixel_format != nullptr && std::strcmp(pixel_format, "gray2") == 0) {
        return (pixels + 3U) / 4U;
    }
    if (pixel_format != nullptr && std::strcmp(pixel_format, "gray4") == 0) {
        return (pixels + 1U) / 2U;
    }
    return pixels;
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
    return ValidateSteeringMediaImagePayload(width, height, "gray8", payload_size, error);
}

bool ValidateSteeringMediaImagePayload(int width,
                                       int height,
                                       const char* pixel_format,
                                       std::size_t payload_size,
                                       std::string& error) {
    const std::size_t expected = SteeringMediaImagePayloadBytesForFormat(width, height, pixel_format);
    if (expected == 0) {
        error = "steering image frame dimensions must be positive";
        return false;
    }
    if (pixel_format != nullptr &&
        std::strcmp(pixel_format, "gray8") != 0 &&
        std::strcmp(pixel_format, "gray4") != 0 &&
        std::strcmp(pixel_format, "gray2") != 0 &&
        std::strcmp(pixel_format, "gray1") != 0) {
        error = "steering image pixel_format must be gray8, gray4, gray2, or gray1";
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
    header << ",\"REFERENCE_LATERAL_JUMP_GATE_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.reference_lateral_jump_gate_m);
    header << ",\"BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_geometry.boundary_trace_max_adjacent_distance_m);
    header << ",\"NOMINAL_ROAD_HALF_WIDTH_M\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_geometry.nominal_road_half_width_m);
    header << ",\"SPARSE_ROW_COUNT\":"
           << snapshot.param_snapshot.bev_geometry.sparse_row_count;
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
    header << ",\"LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_control_model.lateral_offset_to_wheel_delta_gain);
    header << ",\"HEADING_ERROR_TO_WHEEL_DELTA_GAIN\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_control_model.heading_error_to_wheel_delta_gain);
    header << ",\"CURVATURE_TO_WHEEL_DELTA_GAIN\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_control_model.curvature_to_wheel_delta_gain);
    header << ",\"MIN_LEADING_REFERENCE_SAMPLES\":"
           << snapshot.param_snapshot.bev_control_model.min_leading_reference_samples;
    header << ",\"TRACKING_FIT_MIN_SAMPLES\":"
           << snapshot.param_snapshot.bev_control_model.tracking_fit_min_samples;
    header << "}";
    header << ",\"BEV_ELEMENT\":{";
    header << "\"CROSS_EXIT_TAKEOVER_ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element.cross_exit_takeover_enabled);
    header << ",\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.cross_wide_row_white_ratio_min);
    header << ",\"CIRCLE_V2_ENABLED\":";
    AppendJsonBool(header, snapshot.param_snapshot.bev_element.circle_v2_enabled);
    header << ",\"CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG\":";
    AppendJsonNumber(header, snapshot.param_snapshot.bev_element.circle_v2_exit_yaw_threshold_deg);
    header << ",\"CIRCLE_V2_EXIT_HOLD_FRAMES\":"
           << snapshot.param_snapshot.bev_element.circle_v2_exit_hold_frames;
    header << ",\"CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS\":"
           << snapshot.param_snapshot.bev_element.circle_v2_inner_trace_stall_timeout_ms;
    header << ",\"CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element
                         .circle_v2_inner_trace_stall_yaw_min_deg);
    header << ",\"CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element
                         .circle_v2_inner_trace_path_offset_m);
    header << ",\"CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element
                         .circle_v2_opposite_straight_confidence_min);
    header << ",\"CIRCLE_V2_ENTRY_BOTTOM_ROW_COUNT\":"
           << snapshot.param_snapshot.bev_element.circle_v2_entry_bottom_row_count;
    header << ",\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MIN_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element
                         .circle_v2_entry_bottom_forward_min_m);
    header << ",\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MAX_M\":";
    AppendJsonNumber(header,
                     snapshot.param_snapshot.bev_element
                         .circle_v2_entry_bottom_forward_max_m);
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
    const char* pixel_format = frame.pixel_format == nullptr ? "gray8" : frame.pixel_format;
    if (!ValidateSteeringMediaImagePayload(frame.width, frame.height, pixel_format, frame.pixel_size, error)) {
        return false;
    }

    std::ostringstream header;
    header << "{";
    header << "\"type\":\"image_frame\"";
    header << ",\"frame_id\":" << frame.frame_id;
    header << ",\"capture_time_ms\":" << frame.capture_time_ms;
    header << ",\"publish_time_ms\":" << frame.publish_time_ms;
    header << ",\"camera_frame\":{";
    header << "\"source\":";
    AppendJsonString(header, frame.camera_metadata.source);
    header << ",\"frame_id\":" << frame.camera_metadata.frame_id;
    header << ",\"capture_time_ms\":" << frame.camera_metadata.capture_time_ms;
    header << ",\"dequeue_time_ms\":" << frame.camera_metadata.dequeue_time_ms;
    header << ",\"width\":" << (frame.source_width > 0 ? frame.source_width : frame.width);
    header << ",\"height\":" << (frame.source_height > 0 ? frame.source_height : frame.height);
    header << ",\"stride\":" << (frame.source_stride > 0
                                      ? frame.source_stride
                                      : (frame.source_width > 0 ? frame.source_width : frame.width));
    header << ",\"v4l2_sequence\":" << frame.camera_metadata.v4l2_sequence;
    header << ",\"v4l2_timestamp_valid\":";
    AppendJsonBool(header, frame.camera_metadata.v4l2_timestamp_valid);
    header << ",\"drained_buffer_count\":" << frame.camera_metadata.drained_buffer_count;
    header << ",\"poll_wait_us\":" << frame.camera_metadata.poll_wait_us;
    header << ",\"dequeue_us\":" << frame.camera_metadata.dequeue_us;
    header << ",\"yuyv_to_gray_us\":" << frame.camera_metadata.yuyv_to_gray_us;
    header << ",\"store_submit_us\":" << frame.camera_metadata.store_submit_us;
    header << ",\"submitted_frame_count\":" << frame.camera_store_health.submitted_frame_count;
    header << ",\"overwritten_frame_count\":" << frame.camera_store_health.overwritten_frame_count;
    header << ",\"dropped_frame_count\":" << frame.camera_store_health.dropped_frame_count;
    header << ",\"lookup_miss_count\":" << frame.camera_store_health.lookup_miss_count;
    header << "}";
    header << ",\"motion_phase\":";
    AppendJsonString(header, frame.motion_phase == nullptr ? "DISARMED" : frame.motion_phase);
    header << ",\"frame_source\":";
    AppendJsonString(header, frame.frame_source == nullptr ? "snapshot_aligned" : frame.frame_source);
    header << ",\"snapshot_alignment\":{";
    header << "\"aligned\":";
    AppendJsonBool(header, frame.steering_snapshot_aligned);
    header << ",\"frame_id\":" << frame.steering_snapshot_frame_id;
    header << ",\"capture_time_ms\":" << frame.steering_snapshot_capture_time_ms;
    header << "}";
    header << ",\"pixel_format\":";
    AppendJsonString(header, pixel_format);
    header << ",\"payload_encoding\":";
    if (std::strcmp(pixel_format, "gray4") == 0) {
        AppendJsonString(header, "gray4_packed");
    } else if (std::strcmp(pixel_format, "gray2") == 0) {
        AppendJsonString(header, "gray2_packed");
    } else if (std::strcmp(pixel_format, "gray1") == 0) {
        AppendJsonString(header, "gray1_packed");
    } else {
        AppendJsonString(header, "raw");
    }
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
