#include "legacy/steering_scene_circle_interior.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

int BuildInnerReference(const SteeringSceneContext& context, bool left_circle) {
    const int offset = context.params.circle_interior.inner_offset_px;
    return BuildOffsetReferenceFromEdge(left_circle ? context.metrics.left_edge : context.metrics.right_edge,
                                        left_circle,
                                        offset,
                                        offset,
                                        context.prior_state.circle_last_stable_reference_col);
}

int BuildOuterReference(const SteeringSceneContext& context, bool left_circle) {
    const port::CircleExitParameters& params = context.params.circle_exit;
    return BuildOffsetReferenceFromEdge(left_circle ? context.metrics.right_edge : context.metrics.left_edge,
                                        !left_circle,
                                        params.outer_offset_near_px,
                                        params.outer_offset_far_px,
                                        context.prior_state.circle_last_stable_reference_col);
}

float InnerConfidence(const SteeringSceneContext& context, bool left_circle) {
    return left_circle ? context.metrics.circle_left_inner_confidence
                       : context.metrics.circle_right_inner_confidence;
}

float OppositeConfidence(const SteeringSceneContext& context, bool left_circle) {
    return left_circle ? context.metrics.circle_left_opposite_straight_confidence
                       : context.metrics.circle_right_opposite_straight_confidence;
}

int OppositeVisibleRows(const SteeringSceneContext& context, bool left_circle) {
    return left_circle ? context.metrics.circle_left_opposite_straight_rows
                       : context.metrics.circle_right_opposite_straight_rows;
}

bool OppositeStableForExit(const SteeringSceneContext& context, bool left_circle) {
    const port::CircleExitParameters& exit = context.params.circle_exit;
    const float curvature = left_circle ? std::abs(context.metrics.right_curvature)
                                        : std::abs(context.metrics.left_curvature);
    return OppositeConfidence(context, left_circle) >= 0.5F &&
           OppositeVisibleRows(context, left_circle) >= exit.opposite_edge_min_visible_rows &&
           curvature <= static_cast<float>(exit.opposite_edge_max_curvature_px);
}

int RequiredOppositeConfirmCyclesForExit(const SteeringSceneContext& context,
                                         float heading_delta_deg) {
    const int configured_cycles =
        std::max(1, context.params.circle_exit.opposite_edge_straight_confirm_cycles);
    const bool handover_angle_reached =
        heading_delta_deg >= static_cast<float>(context.params.circle_exit.handover_start_deg);
    if (!handover_angle_reached) {
        return configured_cycles;
    }

    // Once the angle gate is already satisfied, allow the first strong opposite-edge
    // observation to start the handover ramp instead of waiting an extra interior frame.
    return std::max(1, configured_cycles - 1);
}

}  // namespace

SteeringSceneOutput EvaluateCircleInteriorScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool prior_circle_interior = context.prior_state.circle_active_direction != "none" &&
                                       context.prior_state.circle_entry_state == "idle" &&
                                       context.prior_state.circle_exit_state == "idle";
    if (!prior_circle_interior ||
        context.metrics.valid_row_count < context.params.circle_scene.active_valid_rows_min) {
        return output;
    }

    const bool left_circle = IsLeftCircleDirection(context.prior_state.circle_active_direction);
    const CircleHeadingIntegrationResult heading = IntegrateCircleHeadingDeltaDeg(
        context.prior_state.circle_heading_delta_deg,
        context.prior_state.circle_last_imu_capture_time_ms,
        context.imu,
        context.capture_time_ms,
        false);
    const float heading_delta_deg = heading.heading_delta_deg;

    int steering_reference_col = BuildInnerReference(context, left_circle);
    std::string reference_mode = "inner_offset";
    const bool can_blend = context.params.circle_interior.blend_enable &&
                           InnerConfidence(context, left_circle) >=
                               static_cast<float>(context.params.circle_interior.blend_min_confidence) &&
                           OppositeConfidence(context, left_circle) >=
                               static_cast<float>(context.params.circle_interior.blend_min_confidence);
    if (can_blend) {
        const int outer_reference_col = BuildOuterReference(context, left_circle);
        steering_reference_col = BlendSteeringReferenceCols(steering_reference_col, outer_reference_col, 0.25F);
        reference_mode = "dual_edge_blend";
    }

    int opposite_confirm_cycles =
        OppositeStableForExit(context, left_circle) ? context.prior_state.circle_opposite_edge_confirm_cycles + 1 : 0;
    const bool enter_exit =
        heading_delta_deg >= static_cast<float>(context.params.circle_exit.handover_start_deg) &&
        opposite_confirm_cycles >= RequiredOppositeConfirmCyclesForExit(context, heading_delta_deg);
    if (enter_exit) {
        output.active = true;
        output.active_module = "circle_exit";
        output.scene_phase = "exit_handover";
        output.scene_override_source = "scene_module";
        output.last_special_scene_correction = "circle_exit_outer_handover";
        output.steering_reference_col = steering_reference_col;
        output.lateral_error = static_cast<float>(context.frame.width) / 2.0F -
                               static_cast<float>(steering_reference_col);
        output.circle_state_valid = true;
        output.circle_active_direction = context.prior_state.circle_active_direction;
        output.circle_entry_state = "idle";
        output.circle_exit_state = "handover";
        output.circle_reference_mode = "outer_handover";
        output.circle_heading_delta_deg = heading_delta_deg;
        output.circle_heading_baseline_deg = 0.0F;
        output.circle_last_imu_capture_time_ms = heading.capture_time_ms;
        output.circle_fixsteer_cycles = 0;
        output.circle_handover_cycles = 0;
        output.circle_fallback_reason = heading.imu_invalid ? "imu_invalid" : "none";
        output.circle_entry_settle_cycles = 0;
        output.circle_entry_loss_cycles = 0;
        output.circle_entry_signal_active = false;
        output.circle_entry_release_reason = "none";
        output.circle_opposite_edge_confirm_cycles = opposite_confirm_cycles;
        output.circle_release_cycles = 0;
        output.circle_last_stable_reference_col = steering_reference_col;
        return output;
    }

    output.active = true;
    output.active_module = "circle_interior";
    output.scene_phase = "interior_tracking";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "circle_interior_inner_offset";
    output.steering_reference_col = steering_reference_col;
    output.lateral_error = static_cast<float>(context.frame.width) / 2.0F -
                           static_cast<float>(steering_reference_col);
    output.circle_state_valid = true;
    output.circle_active_direction = context.prior_state.circle_active_direction;
    output.circle_entry_state = "idle";
    output.circle_exit_state = "idle";
    output.circle_reference_mode = reference_mode;
    output.circle_heading_delta_deg = heading_delta_deg;
    output.circle_heading_baseline_deg = 0.0F;
    output.circle_last_imu_capture_time_ms = heading.capture_time_ms;
    output.circle_fixsteer_cycles = 0;
    output.circle_handover_cycles = 0;
    output.circle_fallback_reason = heading.imu_invalid ? "imu_invalid" : "none";
    output.circle_entry_settle_cycles = 0;
    output.circle_entry_loss_cycles = 0;
    output.circle_entry_signal_active = false;
    output.circle_entry_release_reason = "none";
    output.circle_opposite_edge_confirm_cycles = opposite_confirm_cycles;
    output.circle_release_cycles = 0;
    output.circle_last_stable_reference_col = steering_reference_col;
    return output;
}

}  // namespace ls2k::legacy
