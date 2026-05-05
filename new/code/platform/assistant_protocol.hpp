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

struct AssistantReferenceView {
    std::string mode = "none";
    std::string source = "none";
};

struct AssistantPerceptionHealthView {
    bool projector_ok = false;
    std::string reason = "projector_invalid";
};

struct AssistantEligibilityView {
    bool usable = false;
    std::uint64_t leading_usable_samples = 0;
    double leading_min_forward_m = 0.0;
    double leading_max_forward_m = 0.0;
    std::string reason = "no_reference_facts";
};

struct AssistantLateralErrorView {
    bool computed = false;
    double weighted_lateral_error_m = 0.0;
    std::uint64_t weighted_sample_count = 0;
    double weight_sum = 0.0;
    std::string reason = "reference_unusable";
};

struct AssistantReferenceControlView {
    bool ready = false;
    std::string reason = "reference_unusable";
};

struct AssistantSafetyGateView {
    bool veto_active = true;
    std::string reason = "perception_stale";
};

struct AssistantDegradedView {
    bool active = false;
    std::string reason = "none";
};

struct AssistantYawControlView {
    double turn_output_target = 0.0;
};

struct AssistantActuatorView {
    int raw_turn_output = 0;
    int applied_turn_output = 0;
};

struct AssistantTelemetryView {
    std::string motion_phase = "DISARMED";
    AssistantPerceptionHealthView perception_health{};
    AssistantReferenceView reference{};
    AssistantEligibilityView eligibility{};
    AssistantLateralErrorView lateral_error{};
    AssistantReferenceControlView reference_control{};
    AssistantSafetyGateView safety_gate{};
    AssistantDegradedView degraded{};
    AssistantYawControlView yaw_control{};
    AssistantActuatorView actuator{};
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
