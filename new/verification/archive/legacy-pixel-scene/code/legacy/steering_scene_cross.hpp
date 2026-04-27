#ifndef LS2K_LEGACY_STEERING_SCENE_CROSS_HPP
#define LS2K_LEGACY_STEERING_SCENE_CROSS_HPP

#include "legacy/steering_scene_common.hpp"

namespace ls2k::legacy {

float ComputeCrossSceneScore(const SteeringSceneContext& context);
SteeringSceneOutput BuildCrossSceneOutput(const SteeringSceneContext& context);
SteeringSceneOutput EvaluateCrossScene(const SteeringSceneContext& context);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_CROSS_HPP
