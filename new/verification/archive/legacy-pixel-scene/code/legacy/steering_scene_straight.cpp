#include "legacy/steering_scene_straight.hpp"

namespace ls2k::legacy {

SteeringSceneOutput EvaluateStraightScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    output.active = true;
    output.active_module = "straight";
    output.scene_phase = "idle";
    output.scene_override_source = "none";
    output.last_special_scene_correction = "none";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error;
    return output;
}

}  // namespace ls2k::legacy
