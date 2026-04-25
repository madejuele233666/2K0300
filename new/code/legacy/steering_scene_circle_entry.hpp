#ifndef LS2K_LEGACY_STEERING_SCENE_CIRCLE_ENTRY_HPP
#define LS2K_LEGACY_STEERING_SCENE_CIRCLE_ENTRY_HPP

#include "legacy/steering_scene_common.hpp"

namespace ls2k::legacy {

float ComputeCircleLeftEntryScore(const SteeringSceneContext& context);
float ComputeCircleRightEntryScore(const SteeringSceneContext& context);
SteeringSceneOutput BuildCircleEntrySceneOutput(const SteeringSceneContext& context);
SteeringSceneOutput EvaluateCircleEntryScene(const SteeringSceneContext& context);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_CIRCLE_ENTRY_HPP
