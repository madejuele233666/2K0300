#include "legacy/steering_scene_circle_interior.hpp"

namespace ls2k::legacy {

SteeringSceneOutput EvaluateCircleInteriorScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool prior_circle = context.prior_state.active_module == "circle_entry" ||
                              context.prior_state.active_module == "circle_interior";
    const bool active = prior_circle && context.metrics.bend_severity >= 1.1F;
    if (!active) {
        return output;
    }

    output.active = true;
    output.active_module = "circle_interior";
    output.scene_phase = "interior";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "circle_interior_bias";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error * 1.35F;
    return output;
}

}  // namespace ls2k::legacy
