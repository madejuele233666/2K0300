#ifndef LS2K_RUNTIME_ASSISTANT_TELEMETRY_VIEW_HPP
#define LS2K_RUNTIME_ASSISTANT_TELEMETRY_VIEW_HPP

#include "platform/assistant_protocol.hpp"
#include "runtime/control_debug_snapshot.hpp"

namespace ls2k::runtime {

inline platform::AssistantTelemetryView BuildAssistantTelemetryView(
    const ControlDebugSnapshot& snapshot) {
    platform::AssistantTelemetryView telemetry{};
    telemetry.motion_phase = ToString(snapshot.motion_phase);
    telemetry.visual_reference.present = snapshot.steering.visual_reference.present;
    telemetry.visual_reference.source = snapshot.steering.visual_reference.source;
    telemetry.visual_reference.reason = snapshot.steering.visual_reference.reason;
    telemetry.visual_reference.candidate_count =
        snapshot.steering.visual_reference.candidate_count;
    telemetry.visual_reference.rejected_candidate_reason =
        snapshot.steering.visual_reference.rejected_candidate_reason;
    telemetry.reference.mode = snapshot.steering.reference.mode;
    telemetry.reference.source = snapshot.steering.reference.source;
    telemetry.eligibility.usable = snapshot.steering.eligibility.usable;
    telemetry.eligibility.leading_usable_samples =
        snapshot.steering.eligibility.leading_usable_samples;
    telemetry.eligibility.leading_min_forward_m =
        snapshot.steering.eligibility.leading_min_forward_m;
    telemetry.eligibility.leading_max_forward_m =
        snapshot.steering.eligibility.leading_max_forward_m;
    telemetry.eligibility.reason = snapshot.steering.eligibility.reason;
    telemetry.lateral_error.computed = snapshot.steering.lateral_error.computed;
    telemetry.lateral_error.weighted_lateral_error_m =
        snapshot.steering.lateral_error.weighted_lateral_error_m;
    telemetry.lateral_error.weighted_sample_count =
        snapshot.steering.lateral_error.weighted_sample_count;
    telemetry.lateral_error.weight_sum = snapshot.steering.lateral_error.weight_sum;
    telemetry.lateral_error.reason = snapshot.steering.lateral_error.reason;
    telemetry.perception_health.projector_ok =
        snapshot.steering.perception_health.projector_ok;
    telemetry.perception_health.reason = snapshot.steering.perception_health.reason;
    telemetry.reference_control.ready = snapshot.steering.reference_control.ready;
    telemetry.reference_control.reason = snapshot.steering.reference_control.reason;
    telemetry.safety_gate.veto_active = snapshot.steering.safety_gate.veto_active;
    telemetry.safety_gate.reason = snapshot.steering.safety_gate.reason;
    telemetry.degraded.active = snapshot.steering.degraded.active;
    telemetry.degraded.reason = snapshot.steering.degraded.reason;
    telemetry.yaw_control.turn_output_target =
        snapshot.steering.yaw_control.turn_output_target;
    telemetry.actuator.raw_turn_output = snapshot.steering.actuator.raw_turn_output;
    telemetry.actuator.applied_turn_output =
        snapshot.steering.actuator.applied_turn_output;
    telemetry.tuning_mode_enabled = snapshot.tuning_mode_enabled;
    telemetry.turn_suppressed = snapshot.turn_suppressed;
    telemetry.target_speed_override_enabled = snapshot.target_speed_override_enabled;
    telemetry.target_speed_override_value = snapshot.target_speed_override_value;
    telemetry.effective_speed_target = snapshot.effective_speed_target;
    telemetry.left_speed_target = snapshot.left_speed_target;
    telemetry.right_speed_target = snapshot.right_speed_target;
    telemetry.left_measured_speed = snapshot.left_measured_speed;
    telemetry.right_measured_speed = snapshot.right_measured_speed;
    telemetry.left_pwm_command = snapshot.left_pwm_command;
    telemetry.right_pwm_command = snapshot.right_pwm_command;
    return telemetry;
}

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_ASSISTANT_TELEMETRY_VIEW_HPP
