#ifndef LS2K_PLATFORM_ASSISTANT_PROTOCOL_HPP
#define LS2K_PLATFORM_ASSISTANT_PROTOCOL_HPP

// 辅助协议定义 —— 外部助手通信的消息类型、状态视图和编解码接口。
// 基于 JSON 行协议，支持命令下发和遥测上传。

#include <cstdint>
#include <string>

namespace ls2k::platform {

// 助手命令类型枚举
enum class AssistantCommandType {
    kStart,
    kStop,
    kEnableTuningMode,
    kDisableTuningMode,
    kSetTurnSuppressed,
    kSetTargetSpeed,
};

struct AssistantCommand {
    AssistantCommandType type = AssistantCommandType::kStart;
    std::uint64_t seq = 0;
    bool bool_value = false;
    double target_speed_value = 0.0;
    int ttl_ms = 0;
};

enum class AssistantInboundMessageType {
    kCommand,
    kAckRejected,
    kInputRejected,
};

struct AssistantInboundMessage {
    AssistantInboundMessageType type = AssistantInboundMessageType::kInputRejected;
    AssistantCommand command{};
    std::uint64_t seq = 0;
    std::string reason;
};

struct AssistantStatusView {
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    double effective_speed_target = 0.0;
};

struct AssistantTelemetryView {
    std::string motion_phase = "DISARMED";
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string reference_mode = "centerline";
    double near_lateral_error = 0.0;
    double far_heading_error = 0.0;
    double preview_curvature = 0.0;
    double lookahead_distance_m = 0.0;
    double lookahead_lateral_error = 0.0;
    double lookahead_heading_error = 0.0;
    double reference_curvature = 0.0;
    double curvature_command = 0.0;
    double yaw_rate_target = 0.0;
    double visible_range_m = 0.0;
    double track_confidence = 0.0;
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    double effective_speed_target = 0.0;
    double left_speed_target = 0.0;
    double right_speed_target = 0.0;
    double left_measured_speed = 0.0;
    double right_measured_speed = 0.0;
    int left_pwm_command = 0;
    int right_pwm_command = 0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    std::string circle_direction = "none";
    std::string circle_reference_mode = "none";
    double circle_heading_delta_deg = 0.0;
    double circle_yaw_accum_deg = 0.0;
    std::string circle_path_phase = "none";
    double reference_compatibility_error_m = 0.0;
    std::string reference_source = "none";
    bool circle_entry_signal_active = false;
    bool inner_island_memory_active = false;
    int inner_island_memory_age = 0;
    double inner_island_memory_confidence = 0.0;
    bool left_inner_island_present = false;
    bool right_inner_island_present = false;
    bool inner_edge_compatible = false;
    bool inner_island_trace_present = false;
    double inner_island_trace_start_forward_m = 0.0;
    double inner_island_trace_end_forward_m = 0.0;
    double inner_island_trace_confidence = 0.0;
    int inner_island_trace_support_layers = 0;
    int inner_island_trace_gap_layers = 0;
    int inner_island_rejected_far_segments = 0;
};

AssistantInboundMessage DecodeAssistantJsonLine(const std::string& line, double max_target_speed);
std::string EncodeAssistantAck(std::uint64_t seq, bool accepted, const std::string& reason = {});
std::string EncodeAssistantState(const std::string& event,
                                 const std::string& reason,
                                 const AssistantStatusView& status);
std::string EncodeAssistantTelemetry(const AssistantTelemetryView& telemetry);
const char* ToString(AssistantCommandType type);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_ASSISTANT_PROTOCOL_HPP
