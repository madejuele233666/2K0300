#include "legacy/steering_scene_cross.hpp"

#include <algorithm>

#include "legacy/steering_scene_circle_entry.hpp"

namespace ls2k::legacy {
namespace {

float NormalizePositive(float value, float threshold) {
    if (threshold <= 0.0F || value <= 0.0F) {
        return 0.0F;
    }
    return value / threshold;
}

}  // namespace

float ComputeCrossSceneScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const float full_span_score =
        NormalizePositive(context.metrics.upper_full_span_ratio,
                          static_cast<float>(wide.cross_upper_full_span_min_ratio));
    const float both_open_score =
        NormalizePositive(std::min(context.metrics.left_open, context.metrics.right_open),
                          static_cast<float>(wide.circle_open_min_px));
    return static_cast<float>(wide.cross_weight_full_span) * full_span_score +
           static_cast<float>(wide.cross_weight_both_open) * both_open_score;
}

SteeringSceneOutput BuildCrossSceneOutput(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    output.active = true;
    output.active_module = "cross";
    output.scene_phase = "hold";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "cross_hold";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error;
    return output;
}

SteeringSceneOutput EvaluateCrossScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    if (context.prior_state.active_module != "cross" || context.metrics.zebra_candidate ||
        !MeetsSpecialWidePrecondition(context)) {
        return output;
    }

    const float cross_score = ComputeCrossSceneScore(context);
    const float circle_score =
        std::max(ComputeCircleLeftEntryScore(context), ComputeCircleRightEntryScore(context));
    if (cross_score < circle_score) {
        return output;
    }
    return BuildCrossSceneOutput(context);
}

}  // namespace ls2k::legacy
