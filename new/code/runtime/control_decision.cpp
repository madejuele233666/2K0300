#include "runtime/control_decision.hpp"

namespace ls2k::runtime {
namespace {

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

ControlGateDecision EvaluateControlGate(const ControlGateInputs& inputs) {
    if (IsPerceptionStale(inputs)) {
        return {true, ControlVetoReason::kPerceptionStale};
    }
    if (inputs.low_voltage_emergency) {
        return {true, ControlVetoReason::kLowVoltage};
    }
    if (inputs.perception_emergency_veto) {
        return {true, ControlVetoReason::kPerceptionEmergencyVeto};
    }
    if (!inputs.imu_valid) {
        return {true, ControlVetoReason::kImuInvalid};
    }
    if (!inputs.encoder_valid) {
        return {true, ControlVetoReason::kEncoderInvalid};
    }
    return {false, ControlVetoReason::kNone};
}

bool IsNonZeroDriveCommand(const port::ActuatorCommand& command) {
    return !command.emergency_stop && (command.left_pwm != 0 || command.right_pwm != 0);
}

ControlCycleObservation ObserveControlCycle(const ControlCycleInputs& inputs) {
    ControlCycleObservation observation{};
    observation.veto_active = inputs.gate.veto_active;
    observation.veto_reason = inputs.gate.veto_reason;
    observation.motion_phase = inputs.motion_phase;
    observation.hold_disarmed = inputs.hold_disarmed;
    observation.requested_nonzero_output = IsNonZeroDriveCommand(inputs.command);
    observation.applied_left_pwm = inputs.command.left_pwm;
    observation.applied_right_pwm = inputs.command.right_pwm;

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

const char* ToString(ControlVetoReason reason) {
    switch (reason) {
        case ControlVetoReason::kNone:
            return "none";
        case ControlVetoReason::kPerceptionStale:
            return "perception_stale";
        case ControlVetoReason::kPerceptionEmergencyVeto:
            return "perception_emergency_veto";
        case ControlVetoReason::kLowVoltage:
            return "low_voltage";
        case ControlVetoReason::kImuInvalid:
            return "imu_invalid";
        case ControlVetoReason::kEncoderInvalid:
            return "encoder_invalid";
    }
    return "unknown";
}

const char* ToDiagnosticCode(ControlVetoReason reason) {
    switch (reason) {
        case ControlVetoReason::kPerceptionStale:
            return "control.veto.perception_stale";
        case ControlVetoReason::kPerceptionEmergencyVeto:
            return "control.veto.perception_emergency";
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
