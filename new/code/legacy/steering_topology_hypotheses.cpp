#include "legacy/steering_topology_hypotheses.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

port::PathCandidate ShiftCandidate(const port::PathCandidate& base,
                                   port::ReferenceMode mode,
                                   float lateral_offset,
                                   float confidence_scale) {
    port::PathCandidate shifted = base;
    shifted.mode = mode;
    shifted.confidence = std::clamp(base.confidence * confidence_scale, 0.0F, 1.0F);
    for (port::BEVPathSample& sample : shifted.sampled_path) {
        if (sample.valid) {
            sample.point.lateral_m += lateral_offset;
            sample.confidence = std::clamp(sample.confidence * confidence_scale, 0.0F, 1.0F);
        }
    }
    return shifted;
}

port::PathCandidate PickBranchCandidate(const CorridorGraph& graph,
                                        const CorridorIntervalSet& intervals,
                                        bool left_branch,
                                        const port::RuntimeParameters& params) {
    port::PathCandidate candidate{};
    candidate.mode = left_branch ? port::ReferenceMode::kStableBoundaryOffset
                                 : port::ReferenceMode::kStableBoundaryOffset;

    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int ordinary_index = graph.ordinary_interval_indices[layer];
        const auto& layer_intervals = intervals.layers[layer].intervals;
        if (ordinary_index < 0 || ordinary_index >= static_cast<int>(layer_intervals.size())) {
            continue;
        }
        const port::CorridorInterval& ordinary =
            layer_intervals[static_cast<std::size_t>(ordinary_index)];
        const port::CorridorInterval* best = nullptr;
        for (std::size_t interval_index = 0; interval_index < layer_intervals.size(); ++interval_index) {
            if (static_cast<int>(interval_index) == ordinary_index) {
                continue;
            }
            const port::CorridorInterval& interval = layer_intervals[interval_index];
            const bool side_ok = left_branch ? interval.lateral_center_m < ordinary.lateral_center_m
                                             : interval.lateral_center_m > ordinary.lateral_center_m;
            if (!side_ok) {
                continue;
            }
            if (best == nullptr ||
                (left_branch ? interval.lateral_center_m < best->lateral_center_m
                             : interval.lateral_center_m > best->lateral_center_m)) {
                best = &interval;
            }
        }
        if (best == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = best->forward_m;
        sample.point.lateral_m = best->lateral_center_m;
        sample.confidence = best->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += best->confidence;
        width_sum += best->width_m;
        candidate.start_forward_m = valid_count == 0 ? best->forward_m : candidate.start_forward_m;
        candidate.end_forward_m = best->forward_m;
        ++valid_count;
    }
    candidate.valid = valid_count >= 2;
    if (candidate.valid) {
        candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
        candidate.mean_width_m = width_sum / static_cast<float>(valid_count);
        candidate.width_stability =
            std::clamp(1.0F - std::abs(candidate.mean_width_m - params.bev_corridor_graph.nominal_lane_width_m) /
                                 std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m),
                       0.0F,
                       1.0F);
    }
    return candidate;
}

}  // namespace

port::RoadHypotheses GenerateRoadHypotheses(const CorridorGraph& graph,
                                            const CorridorIntervalSet& intervals,
                                            const port::LegacySteeringState& prior_state,
                                            const port::RuntimeParameters& params) {
    (void)prior_state;
    port::RoadHypotheses hypotheses{};
    hypotheses.ordinary = graph.ordinary;
    hypotheses.ordinary.mode = port::ReferenceMode::kCenterline;
    hypotheses.forward_exit = graph.ordinary;
    hypotheses.forward_exit.mode = port::ReferenceMode::kCenterline;

    const float arc_offset = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    if (graph.ordinary.valid) {
        hypotheses.left_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, -arc_offset, 0.80F);
        hypotheses.right_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, arc_offset, 0.80F);
        hypotheses.zebra_hold = graph.ordinary;
        hypotheses.zebra_hold.mode = port::ReferenceMode::kHoldLast;
    }

    hypotheses.left_branch = PickBranchCandidate(graph, intervals, true, params);
    hypotheses.right_branch = PickBranchCandidate(graph, intervals, false, params);
    return hypotheses;
}

}  // namespace ls2k::legacy
