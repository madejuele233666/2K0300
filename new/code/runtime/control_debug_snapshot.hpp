#ifndef LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
#define LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP

#include <cstdint>

#include "runtime/control_decision.hpp"
#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

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
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
