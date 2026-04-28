#include "legacy/steering_topology_hypotheses.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "legacy/steering_bev_geometry.hpp"

namespace ls2k::legacy {
namespace {

void RefreshCandidateForwardRange(port::PathCandidate& candidate) {
    bool have_sample = false;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (!have_sample) {
            candidate.start_forward_m = sample.point.forward_m;
            have_sample = true;
        }
        candidate.end_forward_m = sample.point.forward_m;
    }
}

void FinalizeCandidate(port::PathCandidate& candidate,
                       int valid_count,
                       float confidence_sum,
                       float width_sum,
                       const port::RuntimeParameters& params) {
    candidate.valid = valid_count >= 3;
    if (!candidate.valid) {
        return;
    }
    candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
    candidate.mean_width_m =
        valid_count > 0 ? width_sum / static_cast<float>(valid_count) : params.bev_corridor_graph.nominal_lane_width_m;
    candidate.width_stability =
        std::clamp(1.0F - std::abs(candidate.mean_width_m - params.bev_corridor_graph.nominal_lane_width_m) /
                             std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m),
                   0.0F,
                   1.0F);
    RefreshCandidateForwardRange(candidate);

    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* mid = nullptr;
    const port::BEVPathSample* last = nullptr;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
        }
        mid = last;
        last = &sample;
    }
    if (first != nullptr && mid != nullptr && last != nullptr && first != mid && mid != last) {
        const float ds1 = std::max(1e-4F, mid->point.forward_m - first->point.forward_m);
        const float ds2 = std::max(1e-4F, last->point.forward_m - mid->point.forward_m);
        const float slope1 = (mid->point.lateral_m - first->point.lateral_m) / ds1;
        const float slope2 = (last->point.lateral_m - mid->point.lateral_m) / ds2;
        candidate.curvature =
            (slope2 - slope1) / std::max(1e-4F, last->point.forward_m - first->point.forward_m);
        candidate.curvature_consistency =
            std::clamp(1.0F - std::abs(candidate.curvature) /
                                 std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs),
                       0.0F,
                       1.0F);
    }
}

const port::CorridorInterval* BestObservedInterval(const CorridorIntervalLayer& layer,
                                                   const port::RuntimeParameters& params) {
    const port::CorridorInterval* best = nullptr;
    const float min_width = std::max(params.bev_corridor_graph.min_interval_width_m,
                                     params.bev_geometry.min_lane_width_m);
    const float max_width = std::min(params.bev_corridor_graph.max_interval_width_m,
                                     params.bev_geometry.max_lane_width_m);
    for (const port::CorridorInterval& interval : layer.intervals) {
        if (!interval.left_edge_valid || !interval.right_edge_valid ||
            interval.width_m < min_width || interval.width_m > max_width) {
            continue;
        }
        if (best == nullptr || interval.confidence > best->confidence) {
            best = &interval;
        }
    }
    return best;
}

port::PathCandidate BuildForwardExitCandidate(const CorridorIntervalSet& intervals,
                                              const port::RuntimeParameters& params) {
    port::PathCandidate candidate{};
    candidate.mode = port::ReferenceMode::kBlendToExit;
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    const std::size_t start_layer =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    for (std::size_t layer = start_layer; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = BestObservedInterval(intervals.layers[layer], params);
        if (interval == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = interval->forward_m;
        sample.point.lateral_m = interval->lateral_center_m;
        sample.confidence = interval->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += interval->confidence;
        width_sum += interval->width_m;
        ++valid_count;
    }
    FinalizeCandidate(candidate, valid_count, confidence_sum, width_sum, params);
    return candidate;
}

port::PathCandidate BuildBoundaryOffsetCandidate(const CorridorIntervalSet& intervals,
                                                 bool left_boundary,
                                                 const port::RuntimeParameters& params) {
    port::PathCandidate candidate{};
    candidate.mode = port::ReferenceMode::kStableBoundaryOffset;
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* best = nullptr;
        for (const port::CorridorInterval& interval : intervals.layers[layer].intervals) {
            const bool edge_valid = left_boundary ? interval.left_edge_valid : interval.right_edge_valid;
            if (!edge_valid) {
                continue;
            }
            if (best == nullptr || interval.confidence > best->confidence) {
                best = &interval;
            }
        }
        if (best == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = best->forward_m;
        sample.point.lateral_m = left_boundary ? best->lateral_min_m : best->lateral_max_m;
        sample.confidence = best->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += best->confidence;
        width_sum += std::max(1e-4F, best->width_m);
        ++valid_count;
    }
    if (valid_count < 3) {
        return candidate;
    }

    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    OffsetPathAlongNormals(candidate.sampled_path,
                           left_boundary ? half_width : -half_width,
                           half_width);
    (void)NormalizePathToForwardSamples(candidate.sampled_path,
                                        params.bev_topology_sampler.forward_samples_m,
                                        half_width,
                                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                 std::abs(params.bev_geometry.lateral_step_m)) *
                                            2.0F);
    FinalizeCandidate(candidate, valid_count, confidence_sum, width_sum, params);
    if (candidate.valid) {
        candidate.confidence = std::clamp(candidate.confidence * 0.75F, 0.0F, 1.0F);
        for (port::BEVPathSample& sample : candidate.sampled_path) {
            if (sample.valid) {
                sample.confidence = std::clamp(sample.confidence * 0.75F, 0.0F, 1.0F);
            }
        }
    }
    return candidate;
}

port::PathCandidate ShiftCandidate(const port::PathCandidate& base,
                                   port::ReferenceMode mode,
                                   float lateral_offset,
                                   float confidence_scale,
                                   const port::RuntimeParameters& params) {
    port::PathCandidate shifted = base;
    shifted.mode = mode;
    shifted.confidence = std::clamp(base.confidence * confidence_scale, 0.0F, 1.0F);
    OffsetPathAlongNormals(shifted.sampled_path, lateral_offset, std::abs(lateral_offset));
    (void)NormalizePathToForwardSamples(shifted.sampled_path,
                                        params.bev_topology_sampler.forward_samples_m,
                                        std::abs(lateral_offset),
                                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                 std::abs(params.bev_geometry.lateral_step_m)) *
                                            2.0F);
    for (port::BEVPathSample& sample : shifted.sampled_path) {
        if (sample.valid) {
            sample.confidence = std::clamp(sample.confidence * confidence_scale, 0.0F, 1.0F);
        }
    }
    RefreshCandidateForwardRange(shifted);
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
        const float ordinary_center =
            graph.ordinary.sampled_path[layer].valid ? graph.ordinary.sampled_path[layer].point.lateral_m
                                                     : ordinary.lateral_center_m;
        const port::CorridorInterval* best = nullptr;
        for (std::size_t interval_index = 0; interval_index < layer_intervals.size(); ++interval_index) {
            if (static_cast<int>(interval_index) == ordinary_index) {
                continue;
            }
            const port::CorridorInterval& interval = layer_intervals[interval_index];
            if (!interval.left_edge_valid && !interval.right_edge_valid) {
                continue;
            }
            const bool side_ok = left_branch ? interval.lateral_center_m < ordinary_center
                                             : interval.lateral_center_m > ordinary_center;
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
    if (!hypotheses.forward_exit.valid) {
        hypotheses.forward_exit = BuildForwardExitCandidate(intervals, params);
    }

    const float arc_offset = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    if (graph.ordinary.valid) {
        hypotheses.left_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, -arc_offset, 0.80F, params);
        hypotheses.right_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, arc_offset, 0.80F, params);
        hypotheses.zebra_hold = graph.ordinary;
        hypotheses.zebra_hold.mode = port::ReferenceMode::kHoldLast;
    }
    if (!hypotheses.left_arc.valid) {
        hypotheses.left_arc = BuildBoundaryOffsetCandidate(intervals, true, params);
    }
    if (!hypotheses.right_arc.valid) {
        hypotheses.right_arc = BuildBoundaryOffsetCandidate(intervals, false, params);
    }

    hypotheses.left_branch = PickBranchCandidate(graph, intervals, true, params);
    hypotheses.right_branch = PickBranchCandidate(graph, intervals, false, params);
    return hypotheses;
}

}  // namespace ls2k::legacy
