#include "legacy/steering_scene_circle_entry.hpp"

#include <algorithm>
#include <cmath>

#include "legacy/steering_scene_cross.hpp"

namespace ls2k::legacy {
namespace {

float NormalizePositive(float value, float threshold) {
    if (threshold <= 0.0F || value <= 0.0F) {
        return 0.0F;
    }
    return value / threshold;
}

float NormalizeNonNegative(float value, float limit) {
    if (limit <= 0.0F || value <= 0.0F) {
        return 0.0F;
    }
    return value / limit;
}

bool HasDirectionTurn(int lower_mid_dx, int mid_upper_dx, int motion_threshold) {
    if (motion_threshold <= 0) {
        return false;
    }
    const bool lower_positive = lower_mid_dx >= motion_threshold;
    const bool lower_negative = lower_mid_dx <= -motion_threshold;
    const bool upper_positive = mid_upper_dx >= motion_threshold;
    const bool upper_negative = mid_upper_dx <= -motion_threshold;
    return (lower_positive && upper_negative) || (lower_negative && upper_positive);
}

float ComputeCurveSignalScore(int lower_mid_dx,
                              int mid_upper_dx,
                              float curvature,
                              const port::SceneWideClassifierParameters& wide) {
    const float curvature_score =
        NormalizePositive(std::abs(curvature), static_cast<float>(wide.edge_curvature_min_px));
    if (!HasDirectionTurn(lower_mid_dx, mid_upper_dx, wide.edge_motion_min_px)) {
        return curvature_score;
    }
    const float direction_turn_score =
        NormalizePositive(static_cast<float>(std::max(std::abs(lower_mid_dx), std::abs(mid_upper_dx))),
                          static_cast<float>(wide.edge_motion_min_px));
    return std::max(curvature_score, direction_turn_score);
}

float ComputeOppositeStraightScore(bool visible_confident,
                                   float border_touch_ratio,
                                   float curvature,
                                   const port::SceneWideClassifierParameters& wide) {
    if (!visible_confident) {
        return 0.0F;
    }
    const float max_curvature = static_cast<float>(wide.opposite_edge_straight_max_curvature_px);
    if (std::abs(curvature) > max_curvature) {
        return 0.0F;
    }
    const float max_touch_ratio = static_cast<float>(wide.opposite_edge_border_touch_max_ratio);
    if (border_touch_ratio > max_touch_ratio) {
        return 0.0F;
    }

    const float curvature_headroom =
        NormalizeNonNegative(max_curvature - std::abs(curvature), std::max(1.0F, max_curvature));
    const float touch_headroom =
        NormalizeNonNegative(max_touch_ratio - border_touch_ratio, std::max(0.05F, max_touch_ratio));
    return 1.0F + 0.35F * curvature_headroom + 0.2F * touch_headroom;
}

int ContinueCircleEntryStreak(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    if (context.prior_state.special_wide_candidate == "circle_entry") {
        return std::max(wide.enter_confirm_cycles, context.prior_state.special_wide_candidate_streak + 1);
    }
    return wide.enter_confirm_cycles;
}

const char* PickCircleDirection(const SteeringSceneContext& context,
                                float circle_left_score,
                                float circle_right_score) {
    if (context.prior_state.circle_active_direction == "left" && circle_left_score > 0.0F) {
        return "left";
    }
    if (context.prior_state.circle_active_direction == "right" && circle_right_score > 0.0F) {
        return "right";
    }
    if (circle_left_score >= circle_right_score && circle_left_score > 0.0F) {
        return "left";
    }
    if (circle_right_score > 0.0F) {
        return "right";
    }
    return "none";
}

int BuildInnerReference(const SteeringSceneContext& context, bool left_circle) {
    const port::CircleEntryParameters& params = context.params.circle_entry;
    return BuildOffsetReferenceFromEdge(left_circle ? context.metrics.left_edge : context.metrics.right_edge,
                                        left_circle,
                                        params.inner_offset_near_px,
                                        params.inner_offset_far_px,
                                        context.prior_state.circle_last_stable_reference_col);
}

SteeringSceneOutput BuildReleasedOutput(const SteeringSceneContext& context, const char* release_reason) {
    SteeringSceneOutput output{};
    const bool bend_like = LooksLikeOrdinaryBend(context) || context.metrics.bend_severity >= 0.9F ||
                           context.metrics.left_edge_missing_bottom || context.metrics.right_edge_missing_bottom;
    output.active = true;
    output.active_module = bend_like ? "bend" : "straight";
    output.scene_phase = bend_like ? "tracking" : "idle";
    output.scene_override_source = bend_like ? "lane_geometry" : "none";
    output.last_special_scene_correction = bend_like ? "bend_bias" : "none";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = bend_like ? context.metrics.lateral_error * 1.15F : context.metrics.lateral_error;
    if (bend_like) {
        if (context.metrics.left_edge_missing_bottom && !context.metrics.right_edge_missing_bottom) {
            output.lateral_error -= 8.0F;
        } else if (context.metrics.right_edge_missing_bottom && !context.metrics.left_edge_missing_bottom) {
            output.lateral_error += 8.0F;
        }
    }
    output.circle_entry_release_reason = release_reason;
    return output;
}

const char* PickEntryReleaseReason(const SteeringSceneContext& context) {
    if (context.metrics.valid_row_count < context.params.circle_scene.active_valid_rows_min) {
        return "entry_geometry_lost";
    }
    return "entry_signal_lost";
}

}  // namespace

float ComputeCircleLeftEntryScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    if (!HasCircleLeftEntryStructure(context)) {
        return 0.0F;
    }
    const float curve_score = ComputeCurveSignalScore(context.metrics.left_dx_lower_mid,
                                                      context.metrics.left_dx_mid_upper,
                                                      context.metrics.left_curvature,
                                                      wide);
    const float opposite_straight_score =
        ComputeOppositeStraightScore(context.metrics.right_visible_confident,
                                     context.metrics.right_upper_border_touch_ratio,
                                     context.metrics.right_curvature,
                                     wide);
    const float open_score =
        NormalizePositive(context.metrics.left_open, static_cast<float>(wide.circle_open_min_px));
    const float contract_score =
        NormalizePositive(context.metrics.right_contract, static_cast<float>(wide.circle_contract_min_px));
    return static_cast<float>(wide.circle_curve_weight) * curve_score +
           static_cast<float>(wide.circle_opposite_straight_weight) * opposite_straight_score +
           static_cast<float>(wide.circle_weight_open) * open_score +
           static_cast<float>(wide.circle_weight_contract) * contract_score;
}

float ComputeCircleRightEntryScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    if (!HasCircleRightEntryStructure(context)) {
        return 0.0F;
    }
    const float curve_score = ComputeCurveSignalScore(context.metrics.right_dx_lower_mid,
                                                      context.metrics.right_dx_mid_upper,
                                                      context.metrics.right_curvature,
                                                      wide);
    const float opposite_straight_score =
        ComputeOppositeStraightScore(context.metrics.left_visible_confident,
                                     context.metrics.left_upper_border_touch_ratio,
                                     context.metrics.left_curvature,
                                     wide);
    const float open_score =
        NormalizePositive(context.metrics.right_open, static_cast<float>(wide.circle_open_min_px));
    const float contract_score =
        NormalizePositive(context.metrics.left_contract, static_cast<float>(wide.circle_contract_min_px));
    return static_cast<float>(wide.circle_curve_weight) * curve_score +
           static_cast<float>(wide.circle_opposite_straight_weight) * opposite_straight_score +
           static_cast<float>(wide.circle_weight_open) * open_score +
           static_cast<float>(wide.circle_weight_contract) * contract_score;
}

SteeringSceneOutput BuildCircleEntrySceneOutput(const SteeringSceneContext& context) {
    const float circle_left_score = ComputeCircleLeftEntryScore(context);
    const float circle_right_score = ComputeCircleRightEntryScore(context);
    const char* direction = PickCircleDirection(context, circle_left_score, circle_right_score);
    SteeringSceneOutput output{};
    if (std::string(direction) == "none") {
        return output;
    }

    const bool left_circle = IsLeftCircleDirection(direction);
    const CircleHeadingIntegrationResult heading = IntegrateCircleHeadingDeltaDeg(
        context.prior_state.circle_heading_delta_deg,
        context.prior_state.circle_last_imu_capture_time_ms,
        context.imu,
        context.capture_time_ms,
        context.prior_state.circle_active_direction == "none");
    int steering_reference_col = BuildInnerReference(context, left_circle);

    output.active = true;
    output.active_module = "circle_entry";
    output.scene_phase = "repairing";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "circle_entry_inner_offset";
    output.steering_reference_col = steering_reference_col;
    output.lateral_error = static_cast<float>(context.frame.width) / 2.0F -
                           static_cast<float>(steering_reference_col);
    output.special_wide_candidate = "circle_entry";
    output.special_wide_candidate_streak = ContinueCircleEntryStreak(context);
    output.special_wide_cross_score = ComputeCrossSceneScore(context);
    output.special_wide_circle_left_score = circle_left_score;
    output.special_wide_circle_right_score = circle_right_score;
    output.circle_state_valid = true;
    output.circle_active_direction = direction;
    output.circle_entry_state = "repairing";
    output.circle_exit_state = "idle";
    output.circle_reference_mode = "inner_offset";
    output.circle_heading_delta_deg = heading.heading_delta_deg;
    output.circle_heading_baseline_deg = 0.0F;
    output.circle_last_imu_capture_time_ms = heading.capture_time_ms;
    output.circle_fixsteer_cycles = 0;
    output.circle_handover_cycles = 0;
    output.circle_fallback_reason = heading.imu_invalid ? "imu_invalid" : "none";
    output.circle_entry_settle_cycles = 0;
    output.circle_entry_loss_cycles = 0;
    output.circle_entry_signal_active = true;
    output.circle_entry_release_reason = "none";
    output.circle_opposite_edge_confirm_cycles = 0;
    output.circle_release_cycles = 0;
    output.circle_last_stable_reference_col = steering_reference_col;
    return output;
}

SteeringSceneOutput EvaluateCircleEntryScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool prior_circle_entry = context.prior_state.circle_active_direction != "none" &&
                                    context.prior_state.circle_exit_state == "idle" &&
                                    context.prior_state.circle_entry_state != "idle";
    if (!prior_circle_entry) {
        return output;
    }

    const bool left_circle = IsLeftCircleDirection(context.prior_state.circle_active_direction);
    const float circle_left_score = ComputeCircleLeftEntryScore(context);
    const float circle_right_score = ComputeCircleRightEntryScore(context);
    const bool entry_signal_active =
        context.metrics.valid_row_count >= context.params.circle_scene.active_valid_rows_min &&
        (left_circle ? circle_left_score > 0.0F : circle_right_score > 0.0F);

    const CircleHeadingIntegrationResult heading = IntegrateCircleHeadingDeltaDeg(
        context.prior_state.circle_heading_delta_deg,
        context.prior_state.circle_last_imu_capture_time_ms,
        context.imu,
        context.capture_time_ms,
        false);
    const float heading_delta_deg = heading.heading_delta_deg;

    int steering_reference_col = BuildInnerReference(context, left_circle);
    std::string reference_mode = "inner_offset";

    const bool repair_complete = heading_delta_deg >=
                                 static_cast<float>(context.params.circle_entry.repair_over_deg);
    const bool progress_evidence = repair_complete || context.prior_state.circle_entry_state == "settle";
    int settle_cycles = context.prior_state.circle_entry_settle_cycles;
    int loss_cycles =
        (!entry_signal_active && !progress_evidence) ? context.prior_state.circle_entry_loss_cycles + 1 : 0;
    const char* release_reason = (!entry_signal_active && !progress_evidence) ? PickEntryReleaseReason(context)
                                                                               : "none";
    if (loss_cycles >= context.params.circle_entry.release_loss_cycles) {
        return BuildReleasedOutput(context, release_reason);
    }

    std::string entry_state = context.prior_state.circle_entry_state;
    const char* active_module = "circle_entry";
    const char* scene_phase = "repairing";
    const char* last_special_scene_correction = "circle_entry_inner_offset";

    if (entry_state == "repairing") {
        settle_cycles = repair_complete && !entry_signal_active ? settle_cycles + 1 : 0;
        if (settle_cycles >= context.params.circle_entry.settle_confirm_cycles) {
            entry_state = "settle";
            settle_cycles = 0;
        }
    }

    if (entry_state == "settle") {
        scene_phase = "entry_settle";
        const float inner_confidence = left_circle ? context.metrics.circle_left_inner_confidence
                                                   : context.metrics.circle_right_inner_confidence;
        settle_cycles =
            inner_confidence >= 0.65F && context.metrics.track_confidence >=
                                             static_cast<float>(context.params.circle_scene.minimum_track_confidence)
                ? settle_cycles + 1
                : 0;
        if (settle_cycles >= context.params.circle_entry.settle_confirm_cycles) {
            active_module = "circle_interior";
            scene_phase = "interior_tracking";
            entry_state = "idle";
            settle_cycles = 0;
            last_special_scene_correction = reference_mode == "dual_edge_blend"
                                                ? "circle_interior_inner_offset"
                                                : "circle_interior_inner_offset";
        }
    }

    output.active = true;
    output.active_module = active_module;
    output.scene_phase = scene_phase;
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = last_special_scene_correction;
    output.steering_reference_col = steering_reference_col;
    output.lateral_error = static_cast<float>(context.frame.width) / 2.0F -
                           static_cast<float>(steering_reference_col);
    output.circle_state_valid = true;
    output.circle_active_direction = context.prior_state.circle_active_direction;
    output.circle_entry_state = entry_state;
    output.circle_exit_state = "idle";
    output.circle_reference_mode = reference_mode;
    output.circle_heading_delta_deg = heading_delta_deg;
    output.circle_heading_baseline_deg = 0.0F;
    output.circle_last_imu_capture_time_ms = heading.capture_time_ms;
    output.circle_fixsteer_cycles = 0;
    output.circle_handover_cycles = 0;
    output.circle_fallback_reason = heading.imu_invalid ? "imu_invalid" : "none";
    output.circle_entry_settle_cycles = settle_cycles;
    output.circle_entry_loss_cycles = loss_cycles;
    output.circle_entry_signal_active = entry_signal_active;
    output.circle_entry_release_reason = release_reason;
    output.circle_opposite_edge_confirm_cycles = 0;
    output.circle_release_cycles = 0;
    output.circle_last_stable_reference_col = steering_reference_col;
    return output;
}

}  // namespace ls2k::legacy
