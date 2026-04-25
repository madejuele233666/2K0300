#include "legacy/steering_scene_circle_entry.hpp"

#include <algorithm>

#include "legacy/steering_scene_cross.hpp"

namespace ls2k::legacy {
namespace {

float NormalizePositive(float value, float threshold) {
    if (threshold <= 0.0F || value <= 0.0F) {
        return 0.0F;
    }
    return value / threshold;
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
    if (context.metrics.right_contract <= 0.0F) {
        return 0.0F;
    }
    const float contract_score =
        NormalizePositive(context.metrics.right_contract, static_cast<float>(wide.circle_contract_min_px));
    const float open_score =
        NormalizePositive(context.metrics.left_open, static_cast<float>(wide.circle_open_min_px));
    return static_cast<float>(wide.circle_weight_contract) * contract_score +
           static_cast<float>(wide.circle_weight_open) * open_score;
}

float ComputeCircleRightEntryScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    if (context.metrics.left_contract <= 0.0F) {
        return 0.0F;
    }
    const float contract_score =
        NormalizePositive(context.metrics.left_contract, static_cast<float>(wide.circle_contract_min_px));
    const float open_score =
        NormalizePositive(context.metrics.right_open, static_cast<float>(wide.circle_open_min_px));
    return static_cast<float>(wide.circle_weight_contract) * contract_score +
           static_cast<float>(wide.circle_weight_open) * open_score;
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
