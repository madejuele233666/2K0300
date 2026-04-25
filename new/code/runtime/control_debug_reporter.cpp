#include "runtime/control_debug_reporter.hpp"

#include <algorithm>
#include <sstream>

namespace ls2k::runtime {
namespace {

const char* BoolToken(bool value) {
    return value ? "true" : "false";
}

}  // namespace

void ControlDebugReporter::Configure(const port::RuntimeParameters& params) {
    interval_ms_ = std::max(1, params.control_snapshot_emit_interval_ms);
}

void ControlDebugReporter::Reset() {
    last_emit_ms_ = 0;
}

void ControlDebugReporter::MaybeEmit(const ControlDebugSnapshot& snapshot, port::DiagnosticSink& diagnostics) {
    if (!snapshot.valid) {
        return;
    }
    const uint64_t now_ms = snapshot.timestamp_ms == 0 ? port::NowMs() : snapshot.timestamp_ms;
    if (last_emit_ms_ != 0 && now_ms >= last_emit_ms_ &&
        now_ms - last_emit_ms_ < static_cast<uint64_t>(interval_ms_)) {
        return;
    }
    last_emit_ms_ = now_ms;

    std::ostringstream message;
    message << "phase=" << ToString(snapshot.motion_phase)
            << " veto=" << (snapshot.veto_active ? "true" : "false")
            << " reason=" << ToString(snapshot.veto_reason)
            << " tuning_mode=" << (snapshot.tuning_mode_enabled ? "true" : "false")
            << " turn_suppressed=" << (snapshot.turn_suppressed ? "true" : "false")
            << " override_enabled=" << (snapshot.target_speed_override_enabled ? "true" : "false")
            << " override_value="
            << (snapshot.target_speed_override_enabled ? std::to_string(snapshot.target_speed_override_value)
                                                       : std::string("null"))
            << " effective_speed_target=" << snapshot.effective_speed_target
            << " left_target=" << snapshot.left_speed_target
            << " right_target=" << snapshot.right_speed_target
            << " left_measured=" << snapshot.left_measured_speed
            << " right_measured=" << snapshot.right_measured_speed
            << " raw_turn=" << snapshot.raw_turn_output
            << " applied_turn=" << snapshot.applied_turn_output
            << " left_pwm=" << snapshot.left_pwm_command
            << " right_pwm=" << snapshot.right_pwm_command
            << " emergency_stop=" << (snapshot.emergency_stop ? "true" : "false");
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.snapshot",
                      message.str(),
                      now_ms});

    if (!snapshot.steering.valid) {
        return;
    }

    std::ostringstream steering_message;
    steering_message << "phase=" << ToString(snapshot.motion_phase)
                     << " frame_id=" << snapshot.steering.frame_id
                     << " capture_time_ms=" << snapshot.steering.capture_time_ms
                     << " lateral_error=" << snapshot.steering.lateral_error
                     << " heading_error=" << snapshot.steering.heading_error
                     << " curvature=" << snapshot.steering.curvature
                     << " track_confidence=" << snapshot.steering.track_confidence
                     << " highest_line=" << snapshot.steering.highest_line
                     << " farthest_line=" << snapshot.steering.farthest_line
                     << " steering_reference_col=" << snapshot.steering.steering_reference_col
                     << " track_valid=" << BoolToken(snapshot.steering.track_valid)
                     << " track_seed_col=" << snapshot.steering.track_seed_col
                     << " track_seed_score=" << snapshot.steering.track_seed_score
                     << " track_sign=" << snapshot.steering.track_sign
                     << " sign_flip_blocked=" << BoolToken(snapshot.steering.sign_flip_blocked)
                     << " imu_grace_active=" << BoolToken(snapshot.steering.imu_grace_active)
                     << " gyro_heading_delta_deg=" << snapshot.steering.gyro_heading_delta_deg
                     << " gyro_consistency_score=" << snapshot.steering.gyro_consistency_score
                     << " threshold=" << snapshot.steering.threshold
                     << " threshold_veto=" << BoolToken(snapshot.steering.threshold_veto)
                     << " active_module=" << snapshot.steering.active_module
                     << " scene_phase=" << snapshot.steering.scene_phase
                     << " scene_override_source=" << snapshot.steering.scene_override_source
                     << " track_source=" << snapshot.steering.track_source
                     << " roadblock_interface_state=" << snapshot.steering.roadblock_interface_state
                     << " last_special_scene_correction="
                     << snapshot.steering.last_special_scene_correction
                     << " roadblock_active=" << BoolToken(snapshot.steering.roadblock_active)
                     << " resolved_fuzzy_p=" << snapshot.steering.resolved_fuzzy_p
                     << " camera_p_term=" << snapshot.steering.camera_p_term
                     << " camera_d_term=" << snapshot.steering.camera_d_term
                     << " w_target=" << snapshot.steering.w_target
                     << " gyro_z=" << snapshot.steering.gyro_z
                     << " gyro_error=" << snapshot.steering.gyro_error
                     << " gyro_p_term=" << snapshot.steering.gyro_p_term
                     << " gyro_d_term=" << snapshot.steering.gyro_d_term
                     << " raw_turn_output=" << snapshot.steering.raw_turn_output
                     << " applied_turn_output=" << snapshot.steering.applied_turn_output;
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.steering_snapshot",
                      steering_message.str(),
                      now_ms});
}

}  // namespace ls2k::runtime
