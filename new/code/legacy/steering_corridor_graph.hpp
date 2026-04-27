#ifndef LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP
#define LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP

#include <array>
#include <vector>

#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct CorridorGraph {
    bool valid = false;
    std::array<int, port::kBevTrackSampleCount> ordinary_interval_indices{};
    std::vector<port::CorridorGraphEdge> edges{};
    port::PathCandidate ordinary{};
};

CorridorGraph BuildCorridorGraph(const CorridorIntervalSet& intervals,
                                 const port::RuntimeParameters& params,
                                 const port::LegacySteeringState& prior_state);

port::BEVTrackEstimate BuildBEVTrackEstimateFromCorridorGraph(const CorridorIntervalSet& intervals,
                                                              const CorridorGraph& graph,
                                                              const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP
