#ifndef LS2K_RUNTIME_CONTROL_DECISION_HPP
#define LS2K_RUNTIME_CONTROL_DECISION_HPP

#include <cstdint>

#include "port/control_types.hpp"
#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

enum class ControlVetoReason {
    kNone,
    kPerceptionStale,
    kPerceptionEmergencyVeto,
    kLowVoltage,
    kImuInvalid,
    kEncoderInvalid
};

enum class ControlApplyOutcome {
    kNotRequested,
    kSuppressedByProfile,
    kHeldDisarmedApplied,
    kEmergencyStopApplied,
    kZeroCommandApplied,
    kDriveCommandApplied,
    kApplyFailed
};

struct ControlGateInputs {
    bool perception_published = false;
    bool perception_fresh = false;
    uint64_t perception_capture_time_ms = 0;
    bool perception_emergency_veto = true;
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

struct ControlCycleInputs {
    ControlGateDecision gate{};
    port::ActuatorCommand command{};
    MotionPhase motion_phase = MotionPhase::kDisarmed;
    bool apply_ok = false;
    bool apply_suppressed_by_profile = false;
    bool hold_disarmed = false;
    bool previously_armed = false;
};

struct ControlCycleObservation {
    bool veto_active = true;
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;
    MotionPhase motion_phase = MotionPhase::kDisarmed;
    bool hold_disarmed = false;
    bool motion_reset_ready = false;
    bool requested_nonzero_output = false;
    ControlApplyOutcome apply_outcome = ControlApplyOutcome::kNotRequested;
    int applied_left_pwm = 0;
    int applied_right_pwm = 0;
    bool actuators_armed = false;
    bool arming_transition = false;
};

ControlGateDecision EvaluateControlGate(const ControlGateInputs& inputs);
ControlCycleObservation ObserveControlCycle(const ControlCycleInputs& inputs);
bool IsNonZeroDriveCommand(const port::ActuatorCommand& command);
const char* ToString(ControlVetoReason reason);
const char* ToDiagnosticCode(ControlVetoReason reason);
const char* ToString(ControlApplyOutcome outcome);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DECISION_HPP
