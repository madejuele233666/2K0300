#ifndef LS2K_RUNTIME_MOTION_TYPES_HPP
#define LS2K_RUNTIME_MOTION_TYPES_HPP

#include <cstdint>

namespace ls2k::runtime {

enum class MotionPhase {
    kDisarmed,
    kStartRequested,
    kSpinup,
    kRunning,
    kStopping,
    kFailSafeLatched
};

struct MotionIntent {
    bool start_requested = false;
    bool stop_requested = false;
    bool reset_fault_requested = false;
};

struct MotionSupervisorState {
    MotionPhase phase = MotionPhase::kDisarmed;
    uint64_t phase_entry_ms = 0;
    uint64_t fail_safe_latched_at_ms = 0;
    int clean_gate_cycles = 0;
};

struct MotionSupervisorInputs {
    MotionSupervisorState state{};
    MotionIntent intent{};
    bool startup_complete = false;
    bool gate_clear = false;
    uint64_t now_ms = 0;
    double running_speed_target = 0.0;
    int encoder_mean_abs = 0;
    int motion_unveto_confirm_cycles = 0;
    int motion_spinup_ms = 0;
    double motion_turn_limit_spinup = 1.0;
    int motion_pwm_step_limit = 0;
    int motion_stop_ms = 0;
    int motion_stop_encoder_threshold = 0;
    int motion_fault_rearm_hold_ms = 0;
};

struct MotionDecision {
    MotionSupervisorState state{};
    MotionPhase previous_phase = MotionPhase::kDisarmed;
    bool phase_changed = false;
    bool hold_disarmed = true;
    bool allow_drive = false;
    bool reset_controllers = false;
    bool consume_reset_request = false;
    bool blocked_start = false;
    bool reset_ready = false;
    double effective_speed_target = 0.0;
    double turn_limit_scale = 1.0;
    int pwm_step_limit = 0;
};

bool IsDrivePhase(MotionPhase phase);
const char* ToString(MotionPhase phase);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_MOTION_TYPES_HPP
