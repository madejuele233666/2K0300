#ifndef LS2K_SAFETY_CONTROL_APPLY_OBSERVATION_HPP
#define LS2K_SAFETY_CONTROL_APPLY_OBSERVATION_HPP

#include "control/motion_types.hpp"
#include "port/actuator_command_types.hpp"
#include "safety/control_gate.hpp"

namespace ls2k::safety {

enum class ControlApplyOutcome {
    kNotRequested,
    kSuppressedByProfile,
    kHeldDisarmedApplied,
    kEmergencyStopApplied,
    kZeroCommandApplied,
    kDriveCommandApplied,
    kApplyFailed
};

struct ControlCycleInputs {
    ControlGateDecision gate{};
    port::ActuatorCommand command{};
    control::MotionPhase motion_phase = control::MotionPhase::kDisarmed;
    bool apply_ok = false;
    bool apply_suppressed_by_profile = false;
    bool hold_disarmed = false;
    bool previously_armed = false;
};

struct ControlCycleObservation {
    bool veto_active = true;
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;
    control::MotionPhase motion_phase = control::MotionPhase::kDisarmed;
    bool hold_disarmed = false;
    bool motion_reset_ready = false;
    bool requested_nonzero_output = false;
    ControlApplyOutcome apply_outcome = ControlApplyOutcome::kNotRequested;
    int applied_left_drive_pwm = 0;
    int applied_right_drive_pwm = 0;
    int applied_left_brushless_pwm = 0;
    int applied_right_brushless_pwm = 0;
    bool actuators_armed = false;
    bool arming_transition = false;
};

ControlCycleObservation ObserveControlCycle(const ControlCycleInputs& inputs);
bool IsNonZeroDriveCommand(const port::ActuatorCommand& command);
const char* ToString(ControlApplyOutcome outcome);

}  // namespace ls2k::safety

#endif  // LS2K_SAFETY_CONTROL_APPLY_OBSERVATION_HPP
