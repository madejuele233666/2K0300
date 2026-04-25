#include "legacy/steering_scene_circle_exit.hpp"

#include <cmath>

namespace ls2k::legacy {

SteeringSceneOutput EvaluateCircleExitScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool prior_interior = context.prior_state.active_module == "circle_interior" ||
                                context.prior_state.active_module == "circle_exit";
    const bool active = prior_interior && context.metrics.bend_severity < 1.1F &&
                        std::abs(context.metrics.lateral_error) > 10.0F;
    if (!active) {
        return output;
    }

    output.active = true;
    output.active_module = "circle_exit";
    output.scene_phase = "exit";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "circle_exit_bias";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error * 1.1F;
    return output;
}

}  // namespace ls2k::legacy
