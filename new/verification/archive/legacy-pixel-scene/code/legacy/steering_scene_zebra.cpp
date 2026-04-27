#include "legacy/steering_scene_zebra.hpp"

namespace ls2k::legacy {

SteeringSceneOutput EvaluateZebraScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    if (!context.metrics.zebra_candidate) {
        return output;
    }

    output.active = true;
    output.active_module = "zebra";
    output.scene_phase = "hold";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "zebra_hold";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error * 0.85F;
    return output;
}

}  // namespace ls2k::legacy
