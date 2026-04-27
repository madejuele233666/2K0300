#ifndef LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP
#define LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP

#include "legacy/steering_corridor_graph.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

port::RoadHypotheses GenerateRoadHypotheses(const CorridorGraph& graph,
                                            const CorridorIntervalSet& intervals,
                                            const port::LegacySteeringState& prior_state,
                                            const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP
