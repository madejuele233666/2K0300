#ifndef LS2K_LEGACY_STEERING_SCENE_FSM_HPP
#define LS2K_LEGACY_STEERING_SCENE_FSM_HPP

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

struct SceneFsmResult {
    port::SpecialSceneFsmState state{};
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
};

SceneFsmResult UpdateSceneFsm(const port::BEVSceneObservation& observation,
                              const port::RuntimeParameters& params,
                              const port::SpecialSceneFsmState& prior_state);
SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state);
const char* ToString(port::SpecialSceneKind kind);
const char* ToString(port::SpecialScenePhase phase);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_FSM_HPP
