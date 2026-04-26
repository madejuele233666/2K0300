#include "legacy/steering_scene_circle_exit.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

float ClampUnit(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

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

float OppositeConfidence(const SteeringSceneContext& context, bool left_circle) {
    return left_circle ? context.metrics.circle_left_opposite_straight_confidence
                       : context.metrics.circle_right_opposite_straight_confidence;
}

int OppositeVisibleRows(const SteeringSceneContext& context, bool left_circle) {
    return left_circle ? context.metrics.circle_left_opposite_straight_rows
                       : context.metrics.circle_right_opposite_straight_rows;
}

bool OppositeStable(const SteeringSceneContext& context, bool left_circle) {
    const port::CircleExitParameters& params = context.params.circle_exit;
    const float curvature = left_circle ? std::abs(context.metrics.right_curvature)
                                        : std::abs(context.metrics.left_curvature);
    return OppositeConfidence(context, left_circle) >= 0.5F &&
           OppositeVisibleRows(context, left_circle) >= params.opposite_edge_min_visible_rows &&
           curvature <= static_cast<float>(params.opposite_edge_max_curvature_px);
}

bool OrdinaryGeometryRecovered(const SteeringSceneContext& context) {
    return context.metrics.track_confidence >= static_cast<float>(context.params.circle_scene.minimum_track_confidence) &&
           std::abs(context.metrics.heading_error) <= 0.12F && std::abs(context.metrics.curvature) <= 0.08F &&
           !MeetsSpecialWidePrecondition(context);
}

SteeringSceneOutput BuildReleasedOutput(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool bend_like = context.metrics.bend_severity >= 0.9F;
    output.active = true;
    output.active_module = bend_like ? "bend" : "straight";
    output.scene_phase = bend_like ? "tracking" : "idle";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = bend_like ? "bend_bias" : "none";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error;
    output.circle_state_valid = true;
    output.circle_active_direction = "none";
    output.circle_entry_state = "idle";
    output.circle_exit_state = "idle";
    output.circle_reference_mode = "none";
    output.circle_heading_delta_deg = 0.0F;
    output.circle_heading_baseline_deg = 0.0F;
    output.circle_last_imu_capture_time_ms = 0;
    output.circle_fixsteer_cycles = 0;
    output.circle_handover_cycles = 0;
    output.circle_fallback_reason = "none";
    output.circle_entry_settle_cycles = 0;
    output.circle_entry_loss_cycles = 0;
    output.circle_entry_signal_active = false;
    output.circle_entry_release_reason = "none";
    output.circle_opposite_edge_confirm_cycles = 0;
    output.circle_release_cycles = 0;
    output.circle_last_stable_reference_col = output.steering_reference_col;
    return output;
}

}  // namespace

SteeringSceneOutput EvaluateCircleExitScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool prior_exit = context.prior_state.circle_active_direction != "none" &&
                            (context.prior_state.circle_exit_state != "idle" ||
                             context.prior_state.active_module == "circle_exit");
    if (!prior_exit) {
        return output;
    }

    const bool left_circle = IsLeftCircleDirection(context.prior_state.circle_active_direction);
    const CircleHeadingIntegrationResult heading = IntegrateCircleHeadingDeltaDeg(
        context.prior_state.circle_heading_delta_deg,
        context.prior_state.circle_last_imu_capture_time_ms,
        context.imu,
        context.capture_time_ms,
        false);
    std::string fallback_reason = heading.imu_invalid ? "imu_invalid" : "none";
    const float heading_delta_deg = heading.heading_delta_deg;
    const int inner_reference_col = BuildInnerReference(context, left_circle);
    const int outer_reference_col = BuildOuterReference(context, left_circle);
    const bool opposite_stable = OppositeStable(context, left_circle);

    std::string exit_state = context.prior_state.circle_exit_state;
    if (exit_state == "idle") {
        exit_state = "handover";
    }

    int handover_cycles = context.prior_state.circle_handover_cycles;
    int fixsteer_cycles = 0;
    int unstable_cycles = 0;
    int release_cycles = context.prior_state.circle_release_cycles;
    std::string reference_mode = "outer_handover";
    const char* scene_phase = "exit_handover";
    const char* last_special_scene_correction = "circle_exit_outer_handover";
    int steering_reference_col = inner_reference_col;

    if (exit_state == "handover") {
        if (opposite_stable) {
            ++handover_cycles;
        }
        const float ramp_weight =
            ClampUnit(static_cast<float>(handover_cycles) /
                      static_cast<float>(std::max(1, context.params.circle_exit.handover_ramp_cycles)));
        steering_reference_col = BlendSteeringReferenceCols(inner_reference_col, outer_reference_col, ramp_weight);
        if (handover_cycles >= context.params.circle_exit.handover_ramp_cycles) {
            exit_state = "tracking";
        } else if (heading_delta_deg >= static_cast<float>(context.params.circle_exit.fixsteer_start_deg) &&
                   !opposite_stable) {
            fallback_reason = "handover_timeout";
            exit_state = "fallback";
        }
    }

    if (exit_state == "tracking") {
        reference_mode = "outer_offset";
        scene_phase = "exit_tracking";
        last_special_scene_correction = "circle_exit_outer_offset";
        steering_reference_col = outer_reference_col;
        unstable_cycles = opposite_stable ? 0 : context.prior_state.circle_opposite_edge_confirm_cycles + 1;
        if (!opposite_stable &&
            heading_delta_deg >= static_cast<float>(context.params.circle_exit.fixsteer_start_deg) &&
            unstable_cycles >= context.params.circle_exit.handover_confirm_cycles) {
            fallback_reason = "opposite_edge_unstable";
            exit_state = "fallback";
            fixsteer_cycles = 0;
        }
    }

    if (exit_state == "fallback") {
        reference_mode = "fixsteer_fallback";
        scene_phase = "exit_fallback";
        last_special_scene_correction = "circle_exit_fixsteer_fallback";
        if (opposite_stable) {
            exit_state = "tracking";
            reference_mode = "outer_offset";
            scene_phase = "exit_tracking";
            last_special_scene_correction = "circle_exit_outer_offset";
            steering_reference_col = outer_reference_col;
            unstable_cycles = 0;
        } else {
            fixsteer_cycles = context.prior_state.circle_fixsteer_cycles + 1;
            const int center = context.frame.width / 2;
            const int prior_reference = context.prior_state.circle_last_stable_reference_col;
            const int carried_bias = static_cast<int>(std::lround(
                static_cast<float>(prior_reference - center) *
                static_cast<float>(context.params.circle_fallback.fixsteer_bias_scale)));
            steering_reference_col = std::clamp(prior_reference + carried_bias,
                                                0,
                                                std::max(0, context.frame.width - 1));
            if (fixsteer_cycles >= context.params.circle_exit.exit_fallback_max_cycles) {
                steering_reference_col = prior_reference;
            }
        }
    }

    if (heading_delta_deg >= static_cast<float>(context.params.circle_exit.exit_complete_deg)) {
        release_cycles = OrdinaryGeometryRecovered(context) ? release_cycles + 1 : 0;
        if (release_cycles >= context.params.circle_exit.exit_release_cycles) {
            return BuildReleasedOutput(context);
        }
    } else {
        release_cycles = 0;
    }

    output.active = true;
    output.active_module = "circle_exit";
    output.scene_phase = scene_phase;
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = last_special_scene_correction;
    output.steering_reference_col = steering_reference_col;
    output.lateral_error = static_cast<float>(context.frame.width) / 2.0F -
                           static_cast<float>(steering_reference_col);
    output.circle_state_valid = true;
    output.circle_active_direction = context.prior_state.circle_active_direction;
    output.circle_entry_state = "idle";
    output.circle_exit_state = exit_state;
    output.circle_reference_mode = reference_mode;
    output.circle_heading_delta_deg = heading_delta_deg;
    output.circle_heading_baseline_deg = 0.0F;
    output.circle_last_imu_capture_time_ms = heading.capture_time_ms;
    output.circle_fixsteer_cycles = fixsteer_cycles;
    output.circle_handover_cycles = handover_cycles;
    output.circle_fallback_reason = fallback_reason;
    output.circle_entry_settle_cycles = 0;
    output.circle_entry_loss_cycles = 0;
    output.circle_entry_signal_active = false;
    output.circle_entry_release_reason = "none";
    output.circle_opposite_edge_confirm_cycles = unstable_cycles;
    output.circle_release_cycles = release_cycles;
    output.circle_last_stable_reference_col =
        reference_mode == "fixsteer_fallback" ? context.prior_state.circle_last_stable_reference_col
                                              : steering_reference_col;
    return output;
}

}  // namespace ls2k::legacy
