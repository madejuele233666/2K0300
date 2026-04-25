#ifndef LS2K_LEGACY_STEERING_SCENE_ORCHESTRATOR_HPP
#define LS2K_LEGACY_STEERING_SCENE_ORCHESTRATOR_HPP

#include "legacy/steering_scene_common.hpp"

namespace ls2k::legacy {

SteeringAnalysisResult OrchestrateSteeringScenes(const SteeringSceneContext& context,
                                                 bool low_voltage_emergency,
                                                 std::uint64_t frame_id,
                                                 std::uint64_t capture_time_ms);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_ORCHESTRATOR_HPP
