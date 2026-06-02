#include "safety/control_gate.hpp"

namespace ls2k::safety {
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

}  // namespace ls2k::safety
