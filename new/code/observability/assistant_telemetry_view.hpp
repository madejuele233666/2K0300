#ifndef LS2K_OBSERVABILITY_ASSISTANT_TELEMETRY_VIEW_HPP
#define LS2K_OBSERVABILITY_ASSISTANT_TELEMETRY_VIEW_HPP

#include "transport/assistant_protocol.hpp"
#include "observability/control_debug_snapshot.hpp"

namespace ls2k::observability {

/// 构建辅助遥测视图：将 ControlDebugSnapshot 中的字段映射到 AssistantTelemetryView
/// @param snapshot  控制调试快照（包含运动、感知、转向等全部调试信息）
/// @return          填充好的 AssistantTelemetryView 结构体
inline transport::AssistantTelemetryView BuildAssistantTelemetryView(
    const ControlDebugSnapshot& snapshot) {
    transport::AssistantTelemetryView telemetry{};
    telemetry.motion_phase = ToString(snapshot.motion_phase);
    telemetry.element_evidence = snapshot.steering.element_evidence;
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
    telemetry.tracking_geometry.computed = snapshot.steering.tracking_geometry.computed;
    telemetry.tracking_geometry.lateral_offset_m =
        snapshot.steering.tracking_geometry.lateral_offset_m;
    telemetry.tracking_geometry.heading_error_rad =
        snapshot.steering.tracking_geometry.heading_error_rad;
    telemetry.tracking_geometry.curvature_m_inv =
        snapshot.steering.tracking_geometry.curvature_m_inv;
    telemetry.tracking_geometry.sample_count =
        snapshot.steering.tracking_geometry.sample_count;
    telemetry.tracking_geometry.reason = snapshot.steering.tracking_geometry.reason;
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
    telemetry.yaw_control.lateral_term = snapshot.steering.yaw_control.lateral_term;
    telemetry.yaw_control.heading_term = snapshot.steering.yaw_control.heading_term;
    telemetry.yaw_control.curvature_term = snapshot.steering.yaw_control.curvature_term;
    telemetry.tuning_mode_enabled = snapshot.tuning_mode_enabled;
    telemetry.turn_suppressed = snapshot.turn_suppressed;
    telemetry.target_speed_override_enabled = snapshot.target_speed_override_enabled;
    telemetry.target_speed_override_value = snapshot.target_speed_override_value;
    telemetry.effective_speed_target = snapshot.effective_speed_target;
    telemetry.left_speed_target = snapshot.left_speed_target;
    telemetry.right_speed_target = snapshot.right_speed_target;
    telemetry.left_measured_speed = snapshot.left_measured_speed;
    telemetry.right_measured_speed = snapshot.right_measured_speed;
    telemetry.raw_turn_output = snapshot.raw_turn_output;
    telemetry.applied_turn_output = snapshot.applied_turn_output;
    telemetry.left_drive_pwm_command = snapshot.left_drive_pwm_command;
    telemetry.right_drive_pwm_command = snapshot.right_drive_pwm_command;
    telemetry.left_brushless_pwm_command = snapshot.left_brushless_pwm_command;
    telemetry.right_brushless_pwm_command = snapshot.right_brushless_pwm_command;
    telemetry.actuator_apply_outcome = ToString(snapshot.apply_outcome);
    return telemetry;
}

}  // namespace ls2k::observability

#endif  // LS2K_OBSERVABILITY_ASSISTANT_TELEMETRY_VIEW_HPP
