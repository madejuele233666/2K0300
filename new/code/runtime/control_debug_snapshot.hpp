#ifndef LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
#define LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP

// 控制调试快照结构 —— 记录每帧运动控制、BEV 参考路径和内部转向诊断状态。
// 用于调试输出、媒体服务协议和离线分析。

#include <cstdint>
#include <cstddef>
#include <string>

#include "runtime/control_decision.hpp"
#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

struct ReferenceDebugView {
    std::string mode = "none";
    std::string source = "none";
};

struct PerceptionHealthDebugView {
    bool projector_ok = false;
    std::string reason = "projector_invalid";
};

struct ReferenceEligibilityDebugView {
    bool usable = false;
    std::size_t leading_usable_samples = 0;
    double leading_min_forward_m = 0.0;
    double leading_max_forward_m = 0.0;
    double lookahead_distance_m = 0.0;
    std::string reason = "no_reference_facts";
};

struct CurvatureDebugView {
    bool computed = false;
    double lookahead_distance_m = 0.0;
    double curvature_command = 0.0;
    std::string reason = "reference_unusable";
};

struct ReferenceControlDebugView {
    bool ready = false;
    std::string reason = "reference_unusable";
};

struct SafetyGateDebugView {
    bool veto_active = true;
    std::string reason = "perception_stale";
};

struct DegradedDebugView {
    bool active = false;
    std::string reason = "none";
};

struct YawControlDebugView {
    double yaw_rate_target = 0.0;
};

struct SteeringActuatorDebugView {
    int raw_turn_output = 0;
    int applied_turn_output = 0;
};

// 转向公开快照 —— 只包含 reference/control 最小分层合同
struct SteeringDebugSnapshot {
    bool valid = false;
    std::uint64_t frame_id = 0;
    std::uint64_t capture_time_ms = 0;
    int threshold = 0;
    PerceptionHealthDebugView perception_health{};
    ReferenceDebugView reference{};
    ReferenceEligibilityDebugView eligibility{};
    CurvatureDebugView curvature{};
    ReferenceControlDebugView reference_control{};
    SafetyGateDebugView safety_gate{};
    DegradedDebugView degraded{};
    YawControlDebugView yaw_control{};
    SteeringActuatorDebugView actuator{};
};

// 转向内部诊断 —— yaw-loop internals.
struct SteeringInternalDebugSnapshot {
    bool valid = false;
    std::uint64_t frame_id = 0;
    std::uint64_t capture_time_ms = 0;
    double yaw_rate_gain = 0.0;
    double yaw_rate_candidate = 0.0;
    double gyro_z = 0.0;
    double gyro_error = 0.0;
    double gyro_p_term = 0.0;
    double gyro_d_term = 0.0;
};

struct ControlDebugSnapshot {
    bool valid = false;
    uint64_t cycle_count = 0;
    uint64_t timestamp_ms = 0;
    MotionPhase motion_phase = MotionPhase::kDisarmed;
    bool veto_active = true;
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    double effective_speed_target = 0.0;
    double left_speed_target = 0.0;
    double right_speed_target = 0.0;
    double left_measured_speed = 0.0;
    double right_measured_speed = 0.0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    int turn_pwm_command = 0;
    int left_pwm_command = 0;
    int right_pwm_command = 0;
    bool emergency_stop = true;
    SteeringDebugSnapshot steering{};
    SteeringInternalDebugSnapshot steering_internal{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
