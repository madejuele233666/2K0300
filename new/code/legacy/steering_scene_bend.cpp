#include "legacy/steering_scene_bend.hpp"

#include <cmath>

namespace ls2k::legacy {

SteeringSceneOutput EvaluateBendScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const bool active = context.metrics.bend_severity >= 0.9F ||
                        context.metrics.left_edge_missing_bottom ||
                        context.metrics.right_edge_missing_bottom;
    if (!active) {
        return output;
    }

    output.active = true;
    output.active_module = "bend";
    output.scene_phase = "tracking";
    output.scene_override_source = "lane_geometry";
    output.last_special_scene_correction = "bend_bias";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error * 1.15F;
    if (context.metrics.left_edge_missing_bottom && !context.metrics.right_edge_missing_bottom) {
        output.lateral_error -= 8.0F;
    } else if (context.metrics.right_edge_missing_bottom && !context.metrics.left_edge_missing_bottom) {
        output.lateral_error += 8.0F;
    }
    return output;
}

}  // namespace ls2k::legacy
