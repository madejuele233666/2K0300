#include "runtime/motion_supervisor.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::runtime {
namespace {

uint64_t SafeElapsedMs(uint64_t now_ms, uint64_t start_ms) {
    return now_ms >= start_ms ? now_ms - start_ms : 0;
}

double ClampRatio(uint64_t elapsed_ms, int window_ms) {
    if (window_ms <= 0) {
        return 1.0;
    }
    return std::clamp(static_cast<double>(elapsed_ms) / static_cast<double>(window_ms), 0.0, 1.0);
}

MotionDecision Finalize(const MotionSupervisorInputs& inputs,
                        MotionSupervisorState next_state,
                        MotionPhase previous_phase,
                        bool hold_disarmed,
                        bool allow_drive,
                        bool reset_controllers,
                        bool consume_reset_request,
                        bool blocked_start,
                        bool reset_ready,
                        double effective_speed_target,
                        double turn_limit_scale) {
    MotionDecision decision{};
    decision.state = next_state;
    decision.previous_phase = previous_phase;
    decision.phase_changed = next_state.phase != previous_phase;
    decision.hold_disarmed = hold_disarmed;
    decision.allow_drive = allow_drive;
    decision.reset_controllers = reset_controllers;
    decision.consume_reset_request = consume_reset_request;
    decision.blocked_start = blocked_start;
    decision.reset_ready = reset_ready;
    decision.effective_speed_target = effective_speed_target;
    decision.turn_limit_scale = std::clamp(turn_limit_scale, 0.0, 1.0);
    decision.pwm_step_limit = inputs.motion_pwm_step_limit;
    if (decision.phase_changed) {
        decision.state.phase_entry_ms = inputs.now_ms;
        if (decision.state.phase == MotionPhase::kFailSafeLatched) {
            decision.state.fail_safe_latched_at_ms = inputs.now_ms;
        }
    }
    return decision;
}

}  // namespace

MotionDecision MotionSupervisor::Evaluate(const MotionSupervisorInputs& inputs) const {
    MotionSupervisorState next_state = inputs.state;
    MotionPhase phase = next_state.phase;
    const uint64_t phase_elapsed_ms = SafeElapsedMs(inputs.now_ms, next_state.phase_entry_ms);
    const bool start_requested = inputs.intent.start_requested && !inputs.intent.stop_requested;

    if (!inputs.startup_complete) {
        next_state.phase = MotionPhase::kDisarmed;
        next_state.clean_gate_cycles = 0;
        return Finalize(inputs,
                        next_state,
                        phase,
                        true,
                        false,
                        phase != MotionPhase::kDisarmed,
                        false,
                        false,
                        false,
                        0.0,
                        0.0);
    }

    if ((phase == MotionPhase::kSpinup || phase == MotionPhase::kRunning || phase == MotionPhase::kStopping) &&
        !inputs.gate_clear) {
        next_state.phase = MotionPhase::kFailSafeLatched;
        next_state.clean_gate_cycles = 0;
        return Finalize(inputs,
                        next_state,
                        phase,
                        false,
                        false,
                        true,
                        false,
                        false,
                        false,
                        0.0,
                        0.0);
    }

    switch (phase) {
        case MotionPhase::kDisarmed:
            next_state.clean_gate_cycles = 0;
            if (start_requested) {
                next_state.phase = MotionPhase::kStartRequested;
            }
            return Finalize(inputs,
                            next_state,
                            phase,
                            true,
                            false,
                            false,
                            false,
                            false,
                            false,
                            0.0,
                            0.0);
        case MotionPhase::kStartRequested: {
            if (!start_requested) {
                next_state.phase = MotionPhase::kDisarmed;
                next_state.clean_gate_cycles = 0;
                return Finalize(inputs,
                                next_state,
                                phase,
                                true,
                                false,
                                false,
                                false,
                                false,
                                false,
                                0.0,
                                0.0);
            }

            if (!inputs.gate_clear) {
                next_state.clean_gate_cycles = 0;
                return Finalize(inputs,
                                next_state,
                                phase,
                                true,
                                false,
                                false,
                                false,
                                true,
                                false,
                                0.0,
                                0.0);
            }

            next_state.clean_gate_cycles =
                std::min(next_state.clean_gate_cycles + 1, std::max(1, inputs.motion_unveto_confirm_cycles));
            if (next_state.clean_gate_cycles >= std::max(1, inputs.motion_unveto_confirm_cycles)) {
                next_state.phase = MotionPhase::kSpinup;
                next_state.clean_gate_cycles = 0;
                return Finalize(inputs,
                                next_state,
                                phase,
                                false,
                                true,
                                true,
                                false,
                                false,
                                false,
                                0.0,
                                inputs.motion_turn_limit_spinup);
            }

            return Finalize(inputs,
                            next_state,
                            phase,
                            true,
                            false,
                            false,
                            false,
                            false,
                            false,
                            0.0,
                            0.0);
        }
        case MotionPhase::kSpinup: {
            if (inputs.intent.stop_requested) {
                next_state.phase = MotionPhase::kStopping;
                next_state.clean_gate_cycles = 0;
                return Finalize(inputs,
                                next_state,
                                phase,
                                false,
                                true,
                                false,
                                false,
                                false,
                                false,
                                inputs.running_speed_target,
                                inputs.motion_turn_limit_spinup);
            }

            const double spinup_ratio = ClampRatio(phase_elapsed_ms, inputs.motion_spinup_ms);
            const double effective_speed = inputs.running_speed_target * spinup_ratio;
            if (phase_elapsed_ms >= static_cast<uint64_t>(std::max(0, inputs.motion_spinup_ms))) {
                next_state.phase = MotionPhase::kRunning;
                return Finalize(inputs,
                                next_state,
                                phase,
                                false,
                                true,
                                false,
                                false,
                                false,
                                false,
                                inputs.running_speed_target,
                                1.0);
            }

            return Finalize(inputs,
                            next_state,
                            phase,
                            false,
                            true,
                            false,
                            false,
                            false,
                            false,
                            effective_speed,
                            inputs.motion_turn_limit_spinup);
        }
        case MotionPhase::kRunning:
            if (inputs.intent.stop_requested) {
                next_state.phase = MotionPhase::kStopping;
            }
            return Finalize(inputs,
                            next_state,
                            phase,
                            false,
                            true,
                            false,
                            false,
                            false,
                            false,
                            next_state.phase == MotionPhase::kStopping ? inputs.running_speed_target
                                                                       : inputs.running_speed_target,
                            1.0);
        case MotionPhase::kStopping: {
            const double stop_ratio = 1.0 - ClampRatio(phase_elapsed_ms, inputs.motion_stop_ms);
            const double effective_speed = inputs.running_speed_target * std::max(0.0, stop_ratio);
            const bool stop_time_satisfied = phase_elapsed_ms >= static_cast<uint64_t>(std::max(0, inputs.motion_stop_ms));
            const bool encoder_quiet =
                inputs.encoder_mean_abs <= std::max(0, inputs.motion_stop_encoder_threshold);
            if (stop_time_satisfied && encoder_quiet) {
                next_state.phase = MotionPhase::kDisarmed;
                next_state.clean_gate_cycles = 0;
                return Finalize(inputs,
                                next_state,
                                phase,
                                true,
                                false,
                                true,
                                false,
                                false,
                                false,
                                0.0,
                                0.0);
            }
            return Finalize(inputs,
                            next_state,
                            phase,
                            false,
                            true,
                            false,
                            false,
                            false,
                            false,
                            effective_speed,
                            1.0);
        }
        case MotionPhase::kFailSafeLatched: {
            next_state.clean_gate_cycles = 0;
            if (!inputs.gate_clear) {
                return Finalize(inputs,
                                next_state,
                                phase,
                                false,
                                false,
                                false,
                                false,
                                false,
                                false,
                                0.0,
                                0.0);
            }

            const uint64_t latch_elapsed_ms =
                SafeElapsedMs(inputs.now_ms, next_state.fail_safe_latched_at_ms);
            const bool rearm_hold_satisfied =
                latch_elapsed_ms >= static_cast<uint64_t>(std::max(0, inputs.motion_fault_rearm_hold_ms));
            const bool reset_ready = rearm_hold_satisfied;
            if (reset_ready && inputs.intent.reset_fault_requested) {
                next_state.phase = MotionPhase::kDisarmed;
                return Finalize(inputs,
                                next_state,
                                phase,
                                true,
                                false,
                                true,
                                true,
                                false,
                                true,
                                0.0,
                                0.0);
            }

            return Finalize(inputs,
                            next_state,
                            phase,
                            true,
                            false,
                            false,
                            false,
                            false,
                            reset_ready,
                            0.0,
                            0.0);
        }
    }

    next_state.phase = MotionPhase::kDisarmed;
    next_state.clean_gate_cycles = 0;
    return Finalize(inputs,
                    next_state,
                    phase,
                    true,
                    false,
                    true,
                    false,
                    false,
                    false,
                    0.0,
                    0.0);
}

bool IsDrivePhase(MotionPhase phase) {
    return phase == MotionPhase::kSpinup || phase == MotionPhase::kRunning ||
           phase == MotionPhase::kStopping;
}

const char* ToString(MotionPhase phase) {
    switch (phase) {
        case MotionPhase::kDisarmed:
            return "DISARMED";
        case MotionPhase::kStartRequested:
            return "START_REQUESTED";
        case MotionPhase::kSpinup:
            return "SPINUP";
        case MotionPhase::kRunning:
            return "RUNNING";
        case MotionPhase::kStopping:
            return "STOPPING";
        case MotionPhase::kFailSafeLatched:
            return "FAIL_SAFE_LATCHED";
    }
    return "UNKNOWN";
}

}  // namespace ls2k::runtime
