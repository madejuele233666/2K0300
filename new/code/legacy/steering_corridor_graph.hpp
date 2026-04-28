#ifndef LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP
#define LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP

#include <array>
#include <string>
#include <vector>

#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

enum class BoundaryAnchorSide : int {
    kNone = 0,
    kLeft = -1,
    kRight = 1
};

struct CorridorGraph {
    bool valid = false;
    std::string fallback_mode = "none";
    std::array<int, port::kBevTrackSampleCount> ordinary_interval_indices{};
    std::array<BoundaryAnchorSide, port::kBevTrackSampleCount> ordinary_center_anchor_side{};
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> ordinary_raw_centerline{};
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
