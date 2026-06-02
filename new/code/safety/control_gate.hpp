#ifndef LS2K_SAFETY_CONTROL_GATE_HPP
#define LS2K_SAFETY_CONTROL_GATE_HPP

#include <cstdint>

namespace ls2k::safety {

enum class ControlVetoReason {
    kNone,
    kPerceptionStale,
    kPerceptionInvalid,
    kReferenceControlNotReady,
    kLowVoltage,
    kImuInvalid,
    kEncoderInvalid
};

struct ControlGateInputs {
    bool perception_published = false;
    bool perception_fresh = false;
    uint64_t perception_capture_time_ms = 0;
    uint64_t perception_publish_time_ms = 0;
    bool perception_projector_ok = false;
    bool reference_control_ready = false;
    bool low_voltage_emergency = false;
    bool imu_valid = false;
    bool encoder_valid = false;
    uint64_t now_ms = 0;
    int perception_stale_ms = 0;
};

struct ControlGateDecision {
    bool veto_active = true;
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;
};

ControlGateDecision EvaluateControlGate(const ControlGateInputs& inputs);
const char* ToString(ControlVetoReason reason);
const char* ToDiagnosticCode(ControlVetoReason reason);

}  // namespace ls2k::safety

#endif  // LS2K_SAFETY_CONTROL_GATE_HPP
