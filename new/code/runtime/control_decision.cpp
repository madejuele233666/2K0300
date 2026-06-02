#include "runtime/control_decision.hpp"

namespace ls2k::runtime {
namespace {

/// 检查感知数据是否过时：根据发布时间戳和过期阈值进行判定
/// @param inputs  控制门输入
/// @return        感知数据是否过时
bool IsPerceptionStale(const ControlGateInputs& inputs) {
    if (!inputs.perception_published || !inputs.perception_fresh) {
        return true;
    }
    const uint64_t perception_observed_time_ms =
        inputs.perception_publish_time_ms != 0 ? inputs.perception_publish_time_ms : inputs.perception_capture_time_ms;
    if (inputs.now_ms <= perception_observed_time_ms) {
        return false;
    }
    const uint64_t stale_window_ms = static_cast<uint64_t>(inputs.perception_stale_ms <= 0 ? 0
                                                                                            : inputs.perception_stale_ms);
    return inputs.now_ms - perception_observed_time_ms > stale_window_ms;
}

}  // namespace

/// 评估控制门：优先级依次检查低电压紧急、感知过期、感知无效、参考控制未就绪、IMU无效、编码器无效
/// @param inputs  控制门输入
/// @return        门控决策（是否 veto 及原因）
ControlGateDecision EvaluateControlGate(const ControlGateInputs& inputs) {
    if (inputs.low_voltage_emergency) {
        return {true, ControlVetoReason::kLowVoltage};
    }
    if (IsPerceptionStale(inputs)) {
        return {true, ControlVetoReason::kPerceptionStale};
    }
    if (!inputs.perception_projector_ok) {
        return {true, ControlVetoReason::kPerceptionInvalid};
    }
    if (!inputs.reference_control_ready) {
        return {true, ControlVetoReason::kReferenceControlNotReady};
    }
    if (!inputs.imu_valid) {
        return {true, ControlVetoReason::kImuInvalid};
    }
    if (!inputs.encoder_valid) {
        return {true, ControlVetoReason::kEncoderInvalid};
    }
    return {false, ControlVetoReason::kNone};
}

/// 判断是否为非零驱动命令（非紧急停止且任一通道 PWM 不为零）
/// @param command  执行器命令
/// @return         是否是非零驱动命令
bool IsNonZeroDriveCommand(const port::ActuatorCommand& command) {
    return !command.emergency_stop &&
           (command.left_drive_pwm != 0 || command.right_drive_pwm != 0 ||
            command.left_brushless_pwm != 0 || command.right_brushless_pwm != 0);
}

/// 观察控制周期：分析 gate 状态、命令施加结果、运动阶段和 arming 转换
/// @param inputs  控制周期输入
/// @return        控制周期观察结果
ControlCycleObservation ObserveControlCycle(const ControlCycleInputs& inputs) {
    ControlCycleObservation observation{};
    observation.veto_active = inputs.gate.veto_active;
    observation.veto_reason = inputs.gate.veto_reason;
    observation.motion_phase = inputs.motion_phase;
    observation.hold_disarmed = inputs.hold_disarmed;
    observation.requested_nonzero_output = IsNonZeroDriveCommand(inputs.command);
    observation.applied_left_drive_pwm = inputs.command.left_drive_pwm;
    observation.applied_right_drive_pwm = inputs.command.right_drive_pwm;
    observation.applied_left_brushless_pwm = inputs.command.left_brushless_pwm;
    observation.applied_right_brushless_pwm = inputs.command.right_brushless_pwm;

    if (inputs.apply_suppressed_by_profile) {
        observation.apply_outcome = ControlApplyOutcome::kSuppressedByProfile;
    } else if (inputs.hold_disarmed) {
        observation.apply_outcome = ControlApplyOutcome::kHeldDisarmedApplied;
    } else if (!inputs.apply_ok) {
        observation.apply_outcome = ControlApplyOutcome::kApplyFailed;
    } else if (inputs.command.emergency_stop) {
        observation.apply_outcome = ControlApplyOutcome::kEmergencyStopApplied;
    } else if (observation.requested_nonzero_output) {
        observation.apply_outcome = ControlApplyOutcome::kDriveCommandApplied;
    } else {
        observation.apply_outcome = ControlApplyOutcome::kZeroCommandApplied;
    }

    observation.actuators_armed = !inputs.apply_suppressed_by_profile && !inputs.hold_disarmed &&
                                  inputs.apply_ok && !inputs.command.emergency_stop;
    observation.arming_transition = observation.actuators_armed != inputs.previously_armed;
    return observation;
}

/// 将 ControlVetoReason 枚举转换为可读字符串
/// @param reason  控制 veto 原因
/// @return        字符串描述
const char* ToString(ControlVetoReason reason) {
    switch (reason) {
        case ControlVetoReason::kNone:
            return "none";
        case ControlVetoReason::kPerceptionStale:
            return "perception_stale";
        case ControlVetoReason::kPerceptionInvalid:
            return "perception_invalid";
        case ControlVetoReason::kReferenceControlNotReady:
            return "reference_control_not_ready";
        case ControlVetoReason::kLowVoltage:
            return "low_voltage";
        case ControlVetoReason::kImuInvalid:
            return "imu_invalid";
        case ControlVetoReason::kEncoderInvalid:
            return "encoder_invalid";
    }
    return "unknown";
}

/// 将 ControlVetoReason 枚举转换为诊断代码字符串（用于诊断消息 code 字段）
/// @param reason  控制 veto 原因
/// @return        诊断代码字符串
const char* ToDiagnosticCode(ControlVetoReason reason) {
    switch (reason) {
        case ControlVetoReason::kPerceptionStale:
            return "control.veto.perception_stale";
        case ControlVetoReason::kPerceptionInvalid:
            return "control.veto.perception_invalid";
        case ControlVetoReason::kReferenceControlNotReady:
            return "control.veto.reference_control_not_ready";
        case ControlVetoReason::kLowVoltage:
            return "control.veto.low_voltage";
        case ControlVetoReason::kImuInvalid:
            return "control.veto.imu_invalid";
        case ControlVetoReason::kEncoderInvalid:
            return "control.veto.encoder_invalid";
        case ControlVetoReason::kNone:
            return "control.veto.none";
    }
    return "control.veto.unknown";
}

/// 将 ControlApplyOutcome 枚举转换为可读字符串
/// @param outcome  控制施加结果
/// @return         字符串描述
const char* ToString(ControlApplyOutcome outcome) {
    switch (outcome) {
        case ControlApplyOutcome::kNotRequested:
            return "not_requested";
        case ControlApplyOutcome::kSuppressedByProfile:
            return "suppressed_by_profile";
        case ControlApplyOutcome::kHeldDisarmedApplied:
            return "held_disarmed_applied";
        case ControlApplyOutcome::kEmergencyStopApplied:
            return "emergency_stop_applied";
        case ControlApplyOutcome::kZeroCommandApplied:
            return "zero_command_applied";
        case ControlApplyOutcome::kDriveCommandApplied:
            return "drive_command_applied";
        case ControlApplyOutcome::kApplyFailed:
            return "apply_failed";
    }
    return "unknown";
}

}  // namespace ls2k::runtime
