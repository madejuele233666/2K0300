#include "legacy/steering_corridor_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ls2k::legacy {
namespace {

constexpr float kNegativeInfinity = -1.0e9F;

port::BEVPathSample MakePathSample(float forward_m, float lateral_m, float confidence) {
    port::BEVPathSample sample{};
    sample.valid = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = std::clamp(confidence, 0.0F, 1.0F);
    return sample;
}

float IntervalOverlapScore(const port::CorridorInterval& a, const port::CorridorInterval& b) {
    const float overlap_min = std::max(a.lateral_min_m, b.lateral_min_m);
    const float overlap_max = std::min(a.lateral_max_m, b.lateral_max_m);
    const float overlap = std::max(0.0F, overlap_max - overlap_min);
    const float denominator = std::max(1e-4F, std::min(a.width_m, b.width_m));
    return std::clamp(overlap / denominator, 0.0F, 1.0F);
}

float PriorCarryScore(const port::LegacySteeringState& prior_state,
                      std::size_t layer_index,
                      const port::CorridorInterval& interval,
                      const port::BEVCorridorGraphParameters& graph_params) {
    const port::BEVTrackEstimate& prior =
        prior_state.last_bev_track.valid ? prior_state.last_bev_track
                                         : prior_state.bev_track_memory.previous_track;
    if (!prior.valid || layer_index >= prior.sampled_centerline.size() ||
        !prior.sampled_centerline[layer_index].valid) {
        return 0.0F;
    }
    const float distance =
        std::abs(prior.sampled_centerline[layer_index].point.lateral_m - interval.lateral_center_m);
    const float normalized = 1.0F - distance / std::max(1e-4F, graph_params.max_center_jump_m);
    return std::clamp(normalized, 0.0F, 1.0F) * graph_params.prior_carry_confidence_scale;
}

bool IntervalAllowed(const port::CorridorInterval& interval,
                     const port::BEVCorridorGraphParameters& graph_params) {
    return interval.width_m >= graph_params.min_interval_width_m &&
           interval.width_m <= graph_params.max_interval_width_m &&
           interval.confidence > 0.0F;
}

float NodeScore(const port::CorridorInterval& interval,
                const port::BEVCorridorGraphParameters& graph_params) {
    const float center_score =
        std::clamp(1.0F - std::abs(interval.lateral_center_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m * 2.0F),
                   0.0F,
                   1.0F);
    const float width_score =
        std::clamp(1.0F - std::abs(interval.width_m - graph_params.nominal_lane_width_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m),
                   0.0F,
                   1.0F);
    return interval.confidence + 0.35F * center_score + 0.25F * width_score;
}

port::CorridorGraphEdge BuildEdge(int from_layer,
                                  int from_interval,
                                  int to_layer,
                                  int to_interval,
                                  const port::CorridorInterval& from,
                                  const port::CorridorInterval& to) {
    port::CorridorGraphEdge edge{};
    edge.from_layer = from_layer;
    edge.from_interval = from_interval;
    edge.to_layer = to_layer;
    edge.to_interval = to_interval;
    edge.overlap_score = IntervalOverlapScore(from, to);
    edge.center_jump_cost = std::abs(to.lateral_center_m - from.lateral_center_m);
    edge.width_change_cost = std::abs(to.width_m - from.width_m);
    edge.curvature_cost = 0.0F;
    edge.confidence = std::clamp((from.confidence + to.confidence) * 0.5F, 0.0F, 1.0F);
    return edge;
}

float ComputeHeading(const port::BEVPathSample& near_sample, const port::BEVPathSample& far_sample) {
    const float delta_forward = far_sample.point.forward_m - near_sample.point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 0.0F;
    }
    return std::atan2(far_sample.point.lateral_m - near_sample.point.lateral_m, delta_forward);
}

float ComputeCurvature(const port::BEVPathSample& first,
                       const port::BEVPathSample& second,
                       const port::BEVPathSample& third) {
    const float ds1 = std::max(1e-4F, second.point.forward_m - first.point.forward_m);
    const float ds2 = std::max(1e-4F, third.point.forward_m - second.point.forward_m);
    const float slope1 = (second.point.lateral_m - first.point.lateral_m) / ds1;
    const float slope2 = (third.point.lateral_m - second.point.lateral_m) / ds2;
    return (slope2 - slope1) / std::max(1e-4F, third.point.forward_m - first.point.forward_m);
}

void FillCandidateSummary(port::PathCandidate& candidate,
                          const CorridorIntervalSet& intervals,
                          const std::array<int, port::kBevTrackSampleCount>& indices,
                          const port::BEVCorridorGraphParameters& graph_params) {
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    float width_sq_sum = 0.0F;
    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* mid = nullptr;
    const port::BEVPathSample* last = nullptr;

    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int interval_index = indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const port::CorridorInterval& interval =
            intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
        candidate.sampled_path[layer] =
            MakePathSample(interval.forward_m, interval.lateral_center_m, interval.confidence);
        confidence_sum += interval.confidence;
        width_sum += interval.width_m;
        width_sq_sum += interval.width_m * interval.width_m;
        if (first == nullptr) {
            first = &candidate.sampled_path[layer];
            candidate.start_forward_m = interval.forward_m;
        }
        mid = last;
        last = &candidate.sampled_path[layer];
        candidate.end_forward_m = interval.forward_m;
        ++valid_count;
    }

    candidate.valid = valid_count >= 3;
    if (!candidate.valid) {
        return;
    }
    candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
    candidate.mean_width_m = width_sum / static_cast<float>(valid_count);
    const float width_variance =
        std::max(0.0F, width_sq_sum / static_cast<float>(valid_count) -
                           candidate.mean_width_m * candidate.mean_width_m);
    const float width_stddev = std::sqrt(width_variance);
    candidate.width_stability =
        std::clamp(1.0F - width_stddev / std::max(1e-4F, candidate.mean_width_m), 0.0F, 1.0F);
    if (first != nullptr && mid != nullptr && last != nullptr && first != last && mid != last) {
        candidate.curvature = ComputeCurvature(*first, *mid, *last);
        candidate.curvature_consistency =
            std::clamp(1.0F - std::abs(candidate.curvature) /
                                 std::max(1e-4F, graph_params.max_curvature_abs),
                       0.0F,
                       1.0F);
    }
}

}  // namespace

CorridorGraph BuildCorridorGraph(const CorridorIntervalSet& intervals,
                                 const port::RuntimeParameters& params,
                                 const port::LegacySteeringState& prior_state) {
    CorridorGraph graph{};
    graph.ordinary_interval_indices.fill(-1);
    if (!intervals.valid) {
        return graph;
    }

    port::BEVCorridorGraphParameters graph_params = params.bev_corridor_graph;
    if (graph_params.nominal_lane_width_m <= 0.0F) {
        graph_params.nominal_lane_width_m = params.bev_geometry.nominal_lane_width_m;
    }

    std::array<std::vector<float>, port::kBevTrackSampleCount> scores{};
    std::array<std::vector<int>, port::kBevTrackSampleCount> predecessors{};
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const std::size_t count = intervals.layers[layer].intervals.size();
        scores[layer].assign(count, kNegativeInfinity);
        predecessors[layer].assign(count, -1);
        for (std::size_t interval_index = 0; interval_index < count; ++interval_index) {
            const port::CorridorInterval& interval = intervals.layers[layer].intervals[interval_index];
            if (!IntervalAllowed(interval, graph_params)) {
                continue;
            }
            const float base_score = NodeScore(interval, graph_params) +
                                     PriorCarryScore(prior_state, layer, interval, graph_params);
            scores[layer][interval_index] = base_score;
        }
    }

    for (std::size_t layer = 1; layer < port::kBevTrackSampleCount; ++layer) {
        const CorridorIntervalLayer& previous_layer = intervals.layers[layer - 1U];
        const CorridorIntervalLayer& current_layer = intervals.layers[layer];
        for (std::size_t to_index = 0; to_index < current_layer.intervals.size(); ++to_index) {
            if (scores[layer][to_index] <= kNegativeInfinity * 0.5F) {
                continue;
            }
            const port::CorridorInterval& to = current_layer.intervals[to_index];
            float best_score = scores[layer][to_index];
            int best_predecessor = -1;
            for (std::size_t from_index = 0; from_index < previous_layer.intervals.size(); ++from_index) {
                if (scores[layer - 1U][from_index] <= kNegativeInfinity * 0.5F) {
                    continue;
                }
                const port::CorridorInterval& from = previous_layer.intervals[from_index];
                port::CorridorGraphEdge edge = BuildEdge(static_cast<int>(layer - 1U),
                                                         static_cast<int>(from_index),
                                                         static_cast<int>(layer),
                                                         static_cast<int>(to_index),
                                                         from,
                                                         to);
                if (edge.center_jump_cost > graph_params.max_center_jump_m ||
                    edge.width_change_cost > graph_params.max_width_change_m) {
                    continue;
                }
                const float transition_score = edge.confidence + edge.overlap_score -
                                               edge.center_jump_cost / graph_params.max_center_jump_m -
                                               0.5F * edge.width_change_cost /
                                                   std::max(1e-4F, graph_params.max_width_change_m);
                const float candidate_score = scores[layer - 1U][from_index] +
                                              scores[layer][to_index] + transition_score;
                graph.edges.push_back(edge);
                if (candidate_score > best_score) {
                    best_score = candidate_score;
                    best_predecessor = static_cast<int>(from_index);
                }
            }
            scores[layer][to_index] = best_score;
            predecessors[layer][to_index] = best_predecessor;
        }
    }

    float best_score = kNegativeInfinity;
    int best_layer = -1;
    int best_interval = -1;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        for (std::size_t interval_index = 0; interval_index < scores[layer].size(); ++interval_index) {
            if (scores[layer][interval_index] > best_score) {
                best_score = scores[layer][interval_index];
                best_layer = static_cast<int>(layer);
                best_interval = static_cast<int>(interval_index);
            }
        }
    }

    while (best_layer >= 0 && best_interval >= 0) {
        graph.ordinary_interval_indices[static_cast<std::size_t>(best_layer)] = best_interval;
        const int previous = predecessors[static_cast<std::size_t>(best_layer)]
                                          [static_cast<std::size_t>(best_interval)];
        --best_layer;
        best_interval = previous;
    }

    graph.ordinary.mode = port::ReferenceMode::kCenterline;
    FillCandidateSummary(graph.ordinary, intervals, graph.ordinary_interval_indices, graph_params);
    graph.valid = graph.ordinary.valid;
    return graph;
}

port::BEVTrackEstimate BuildBEVTrackEstimateFromCorridorGraph(const CorridorIntervalSet& intervals,
                                                              const CorridorGraph& graph,
                                                              const port::RuntimeParameters& params) {
    port::BEVTrackEstimate track{};
    track.calibration_valid = true;
    track.continuity_valid = graph.valid;
    track.source = "bev_corridor_topology";
    if (!graph.valid) {
        track.fallback_mode = "no_corridor_chain";
        return track;
    }

    int valid_count = 0;
    float confidence_sum = 0.0F;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int interval_index = graph.ordinary_interval_indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const port::CorridorInterval& interval =
            intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
        track.sampled_left_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_min_m, interval.confidence);
        track.sampled_centerline[layer] =
            MakePathSample(interval.forward_m, interval.lateral_center_m, interval.confidence);
        track.sampled_right_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_max_m, interval.confidence);
        track.sampled_drivable_left_boundary[layer] = track.sampled_left_boundary[layer];
        track.sampled_drivable_right_boundary[layer] = track.sampled_right_boundary[layer];
        track.lane_width_profile_m[layer] = interval.width_m;
        track.drivable_width_profile_m[layer] = interval.width_m;
        track.visible_range_m = std::max(track.visible_range_m, interval.forward_m);
        confidence_sum += interval.confidence;
        ++valid_count;
    }

    track.valid = valid_count >= 3;
    track.track_confidence =
        valid_count > 0 ? std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F) : 0.0F;
    if (!track.valid) {
        track.fallback_mode = "insufficient_corridor_samples";
        return track;
    }

    const std::size_t near_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.near_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    std::size_t first_index = port::kBevTrackSampleCount;
    std::size_t far_index = near_index;
    for (std::size_t index = 0; index < port::kBevTrackSampleCount; ++index) {
        if (track.sampled_centerline[index].valid) {
            first_index = std::min(first_index, index);
            far_index = index;
        }
    }
    if (near_index < port::kBevTrackSampleCount && track.sampled_centerline[near_index].valid) {
        track.near_lateral_error = track.sampled_centerline[near_index].point.lateral_m;
    }
    if (near_index < far_index && track.sampled_centerline[near_index].valid &&
        track.sampled_centerline[far_index].valid) {
        track.far_heading_error =
            ComputeHeading(track.sampled_centerline[near_index], track.sampled_centerline[far_index]);
    }
    const std::size_t curvature_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    if (first_index < port::kBevTrackSampleCount && near_index < curvature_index &&
        track.sampled_centerline[first_index].valid && track.sampled_centerline[near_index].valid &&
        track.sampled_centerline[curvature_index].valid) {
        track.preview_curvature =
            ComputeCurvature(track.sampled_centerline[first_index],
                             track.sampled_centerline[near_index],
                             track.sampled_centerline[curvature_index]);
    }
    if (track.track_confidence < params.bev_geometry.min_track_confidence) {
        track.fallback_mode = "low_confidence";
    } else if (track.visible_range_m < params.bev_geometry.min_visible_range_m) {
        track.fallback_mode = "short_visible_range";
    }
    return track;
}

}  // namespace ls2k::legacy
