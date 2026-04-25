#include "legacy/steering_scene_circle_entry.hpp"

#include <algorithm>

namespace ls2k::legacy {
namespace {

float NormalizePositive(float value, float threshold) {
    if (threshold <= 0.0F || value <= 0.0F) {
        return 0.0F;
    }
    return value / threshold;
}

}  // namespace

float ComputeCircleLeftEntryScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const float open_score =
        NormalizePositive(context.metrics.left_open, static_cast<float>(wide.circle_open_min_px));
    const float contract_score =
        NormalizePositive(context.metrics.right_contract, static_cast<float>(wide.circle_contract_min_px));
    return static_cast<float>(wide.circle_weight_open) * open_score +
           static_cast<float>(wide.circle_weight_contract) * contract_score;
}

float ComputeCircleRightEntryScore(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const float open_score =
        NormalizePositive(context.metrics.right_open, static_cast<float>(wide.circle_open_min_px));
    const float contract_score =
        NormalizePositive(context.metrics.left_contract, static_cast<float>(wide.circle_contract_min_px));
    return static_cast<float>(wide.circle_weight_open) * open_score +
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

}  // namespace ls2k::legacy
