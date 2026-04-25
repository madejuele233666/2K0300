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
    SteeringSceneOutput output{};
    output.active = true;
    output.active_module = "circle_entry";
    output.scene_phase = "entry";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "circle_entry_bias";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error * 1.25F;
    return output;
}

SteeringSceneOutput EvaluateCircleEntryScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    if (context.prior_state.active_module != "circle_entry" || context.metrics.zebra_candidate) {
        return output;
    }
    if (LooksLikeOrdinaryBend(context)) {
        return output;
    }

    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const float cross_score = ComputeCrossSceneScore(context);
    const float circle_left_score = ComputeCircleLeftEntryScore(context);
    const float circle_right_score = ComputeCircleRightEntryScore(context);
    const float circle_score = std::max(circle_left_score, circle_right_score);
    if (circle_score <= 0.0F) {
        return output;
    }
    if (cross_score - circle_score >= static_cast<float>(wide.to_cross_margin)) {
        return output;
    }

    output = BuildCircleEntrySceneOutput(context);
    output.special_wide_candidate = "circle_entry";
    output.special_wide_candidate_streak = ContinueCircleEntryStreak(context);
    output.special_wide_cross_score = cross_score;
    output.special_wide_circle_left_score = circle_left_score;
    output.special_wide_circle_right_score = circle_right_score;
    return output;
}

}  // namespace ls2k::legacy
