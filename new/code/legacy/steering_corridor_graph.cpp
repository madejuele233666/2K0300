#include "legacy/steering_corridor_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "legacy/steering_bev_geometry.hpp"

namespace ls2k::legacy {
namespace {

constexpr float kNegativeInfinity = -1.0e9F;

struct LaneObservation {
    bool usable = false;
    bool left_boundary_observed = false;
    bool right_boundary_observed = false;
    float center_forward_m = 0.0F;
    float center_m = 0.0F;
    float lane_width_m = 0.0F;
    float confidence = 0.0F;
};

struct CenterCandidate {
    bool valid = false;
    float forward_m = 0.0F;
    float lateral_m = 0.0F;
};

struct AnchorCandidate {
    bool valid = false;
    BoundaryAnchorSide side = BoundaryAnchorSide::kNone;
    CenterCandidate center{};
    int support_rank = 0;
};

using PathSampleArray = std::array<port::BEVPathSample, port::kBevTrackSampleCount>;

struct BoundaryTraces {
    PathSampleArray left{};
    PathSampleArray right{};
};

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
                      const LaneObservation& observation,
                      const port::BEVCorridorGraphParameters& graph_params) {
    const port::BEVTrackEstimate& prior =
        prior_state.last_bev_track.valid ? prior_state.last_bev_track
                                         : prior_state.bev_track_memory.previous_track;
    if (!prior.valid || layer_index >= prior.sampled_centerline.size() ||
        !prior.sampled_centerline[layer_index].valid) {
        return 0.0F;
    }
    const float distance =
        std::abs(prior.sampled_centerline[layer_index].point.lateral_m - observation.center_m);
    const float normalized = 1.0F - distance / std::max(1e-4F, graph_params.max_center_jump_m);
    return std::clamp(normalized, 0.0F, 1.0F) * graph_params.prior_carry_confidence_scale;
}

LaneObservation ObserveLaneWithEdges(const port::CorridorInterval& interval,
                                     const port::BEVCorridorGraphParameters& graph_params,
                                     bool left_boundary_observed,
                                     bool right_boundary_observed) {
    LaneObservation observation{};
    observation.left_boundary_observed = left_boundary_observed;
    observation.right_boundary_observed = right_boundary_observed;
    observation.center_forward_m = interval.forward_m;

    const float nominal_width = std::max(1e-4F, graph_params.nominal_lane_width_m);
    const auto is_clipped_evidence = [](port::BEVBoundaryEvidenceKind evidence) {
        return evidence == port::BEVBoundaryEvidenceKind::kSearchWindowEdge ||
               evidence == port::BEVBoundaryEvidenceKind::kInvalidOutsideImage ||
               evidence == port::BEVBoundaryEvidenceKind::kImageBorder;
    };
    const bool support_is_clipped =
        is_clipped_evidence(interval.left_boundary_evidence) ||
        is_clipped_evidence(interval.right_boundary_evidence);

    if (observation.left_boundary_observed && observation.right_boundary_observed) {
        observation.lane_width_m = interval.width_m;
        observation.center_m = interval.lateral_center_m;
        observation.confidence = interval.confidence;
    } else if (observation.left_boundary_observed) {
        observation.lane_width_m = nominal_width;
        observation.center_m = interval.lateral_min_m + nominal_width * 0.5F;
        observation.confidence = interval.confidence * 0.65F;
    } else if (observation.right_boundary_observed) {
        observation.lane_width_m = nominal_width;
        observation.center_m = interval.lateral_max_m - nominal_width * 0.5F;
        observation.confidence = interval.confidence * 0.65F;
    } else {
        observation.lane_width_m = nominal_width;
        observation.center_m = interval.lateral_center_m;
        observation.confidence = support_is_clipped ? 0.0F : interval.confidence * 0.45F;
    }

    observation.confidence = std::clamp(observation.confidence, 0.0F, 1.0F);
    observation.usable = observation.confidence > 0.0F;
    return observation;
}

LaneObservation ObserveLane(const port::CorridorInterval& interval,
                            const port::BEVCorridorGraphParameters& graph_params) {
    return ObserveLaneWithEdges(interval,
                                graph_params,
                                interval.left_edge_valid,
                                interval.right_edge_valid);
}

const port::CorridorInterval* SelectedIntervalAt(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    std::size_t layer) {
    if (layer >= indices.size()) {
        return nullptr;
    }
    const int interval_index = indices[layer];
    if (interval_index < 0 ||
        interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
        return nullptr;
    }
    return &intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
}

BoundaryAnchorSide AnchorSide(bool left_edge) {
    return left_edge ? BoundaryAnchorSide::kLeft : BoundaryAnchorSide::kRight;
}

float BoundaryContinuityJumpLimit(const port::BEVCorridorGraphParameters& graph_params) {
    return std::max(0.04F, graph_params.max_center_jump_m * 0.55F);
}

bool HasNearbyBoundarySupport(const PathSampleArray& path,
                              std::size_t layer,
                              const port::BEVCorridorGraphParameters& graph_params) {
    if (layer >= path.size() || !path[layer].valid) {
        return false;
    }
    constexpr std::size_t kMaxLayerGap = 2U;
    const float jump_limit = BoundaryContinuityJumpLimit(graph_params);
    for (std::size_t gap = 1U; gap <= kMaxLayerGap; ++gap) {
        if (layer >= gap && path[layer - gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer - gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
        if (layer + gap < path.size() && path[layer + gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer + gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
    }
    return false;
}

PathSampleArray FilterBoundaryTrace(const PathSampleArray& raw,
                                    const port::BEVCorridorGraphParameters& graph_params) {
    PathSampleArray filtered{};
    for (std::size_t layer = 0; layer < raw.size(); ++layer) {
        if (HasNearbyBoundarySupport(raw, layer, graph_params)) {
            filtered[layer] = raw[layer];
        }
    }
    return filtered;
}

BoundaryTraces BuildBoundaryTraces(const CorridorIntervalSet& intervals,
                                   const std::array<int, port::kBevTrackSampleCount>& indices,
                                   const port::BEVCorridorGraphParameters& graph_params) {
    BoundaryTraces raw{};
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = SelectedIntervalAt(intervals, indices, layer);
        if (interval == nullptr) {
            continue;
        }
        if (interval->left_edge_valid) {
            raw.left[layer] =
                MakePathSample(interval->forward_m, interval->lateral_min_m, interval->confidence);
        }
        if (interval->right_edge_valid) {
            raw.right[layer] =
                MakePathSample(interval->forward_m, interval->lateral_max_m, interval->confidence);
        }
    }
    BoundaryTraces traces{};
    traces.left = FilterBoundaryTrace(raw.left, graph_params);
    traces.right = FilterBoundaryTrace(raw.right, graph_params);
    return traces;
}

int PathSupportRank(const PathSampleArray& path, std::size_t layer) {
    int support_rank = 0;
    for (std::size_t index = layer; index > 0U; --index) {
        if (path[index - 1U].valid) {
            ++support_rank;
            break;
        }
    }
    for (std::size_t index = layer + 1U; index < path.size(); ++index) {
        if (path[index].valid) {
            ++support_rank;
            break;
        }
    }
    return support_rank;
}

bool MakeNormalCenterCandidate(
    const PathSampleArray& boundary_path,
    std::size_t layer,
    const port::BEVCorridorGraphParameters& graph_params,
    bool left_edge,
    AnchorCandidate& candidate) {
    if (layer >= boundary_path.size() || !boundary_path[layer].valid) {
        return false;
    }
    const int support_rank = PathSupportRank(boundary_path, layer);
    if (support_rank <= 0) {
        return false;
    }

    const float half_width = std::max(1e-4F, graph_params.nominal_lane_width_m) * 0.5F;
    const float offset_m = left_edge ? half_width : -half_width;
    const port::BEVPathSample center =
        OffsetPathSampleAlongNormal(boundary_path, layer, offset_m, half_width);
    if (!center.valid) {
        return false;
    }
    candidate.valid = true;
    candidate.side = AnchorSide(left_edge);
    candidate.center.valid = true;
    candidate.center.forward_m = center.point.forward_m;
    candidate.center.lateral_m = center.point.lateral_m;
    candidate.support_rank = support_rank;
    return true;
}

float CandidateContinuityScore(const AnchorCandidate& candidate,
                               const LaneObservation* previous_observation,
                               const port::BEVCorridorGraphParameters& graph_params) {
    if (!candidate.valid || previous_observation == nullptr || !previous_observation->usable) {
        return 0.0F;
    }
    const float jump =
        std::abs(candidate.center.lateral_m - previous_observation->center_m);
    const float normalized =
        1.0F - jump / std::max(1e-4F, graph_params.max_center_jump_m);
    return std::clamp(normalized, 0.0F, 1.0F);
}

float CandidateMidpointScore(const AnchorCandidate& candidate,
                             const LaneObservation& fallback,
                             const port::BEVCorridorGraphParameters& graph_params) {
    if (!candidate.valid) {
        return 0.0F;
    }
    const float half_width = std::max(0.1F, graph_params.nominal_lane_width_m * 0.5F);
    const float delta = std::abs(candidate.center.lateral_m - fallback.center_m);
    return std::clamp(1.0F - delta / half_width, 0.0F, 1.0F);
}

const AnchorCandidate* SelectAnchorCandidate(
    const AnchorCandidate& left_candidate,
    const AnchorCandidate& right_candidate,
    const LaneObservation* previous_observation,
    BoundaryAnchorSide committed_anchor_side,
    BoundaryAnchorSide previous_anchor_side,
    const LaneObservation& fallback,
    const port::BEVCorridorGraphParameters& graph_params,
    bool& forced_choice) {
    forced_choice = false;
    if (left_candidate.valid && !right_candidate.valid) {
        forced_choice = true;
        return &left_candidate;
    }
    if (!left_candidate.valid && right_candidate.valid) {
        forced_choice = true;
        return &right_candidate;
    }
    if (!left_candidate.valid && !right_candidate.valid) {
        return nullptr;
    }

    const AnchorCandidate* committed_candidate = nullptr;
    const AnchorCandidate* opposite_candidate = nullptr;
    if (committed_anchor_side == BoundaryAnchorSide::kLeft) {
        committed_candidate = left_candidate.valid ? &left_candidate : nullptr;
        opposite_candidate = right_candidate.valid ? &right_candidate : nullptr;
    } else if (committed_anchor_side == BoundaryAnchorSide::kRight) {
        committed_candidate = right_candidate.valid ? &right_candidate : nullptr;
        opposite_candidate = left_candidate.valid ? &left_candidate : nullptr;
    }
    if (committed_candidate != nullptr && opposite_candidate != nullptr) {
        const float committed_continuity =
            CandidateContinuityScore(*committed_candidate, previous_observation, graph_params);
        const float opposite_continuity =
            CandidateContinuityScore(*opposite_candidate, previous_observation, graph_params);
        if (opposite_continuity > committed_continuity + 0.35F) {
            return opposite_candidate;
        }
        return committed_candidate;
    }

    const float left_score =
        static_cast<float>(left_candidate.support_rank) * 2.0F +
        CandidateContinuityScore(left_candidate, previous_observation, graph_params) +
        0.25F * CandidateMidpointScore(left_candidate, fallback, graph_params);
    const float right_score =
        static_cast<float>(right_candidate.support_rank) * 2.0F +
        CandidateContinuityScore(right_candidate, previous_observation, graph_params) +
        0.25F * CandidateMidpointScore(right_candidate, fallback, graph_params);
    if (std::abs(left_score - right_score) > 1e-4F) {
        return left_score > right_score ? &left_candidate : &right_candidate;
    }
    if (previous_anchor_side == BoundaryAnchorSide::kLeft) {
        return &left_candidate;
    }
    if (previous_anchor_side == BoundaryAnchorSide::kRight) {
        return &right_candidate;
    }
    return &left_candidate;
}

LaneObservation ApplyNormalCenterInference(
    const BoundaryTraces& boundary_traces,
    std::size_t layer,
    const LaneObservation& fallback,
    const port::BEVCorridorGraphParameters& graph_params,
    const LaneObservation* previous_observation,
    BoundaryAnchorSide committed_anchor_side,
    BoundaryAnchorSide previous_anchor_side,
    BoundaryAnchorSide& anchor_side,
    bool& forced_choice) {
    AnchorCandidate left_candidate{};
    AnchorCandidate right_candidate{};
    anchor_side = BoundaryAnchorSide::kNone;
    (void)MakeNormalCenterCandidate(
        boundary_traces.left, layer, graph_params, true, left_candidate);
    (void)MakeNormalCenterCandidate(
        boundary_traces.right, layer, graph_params, false, right_candidate);
    const AnchorCandidate* candidate = SelectAnchorCandidate(left_candidate,
                                                             right_candidate,
                                                             previous_observation,
                                                             committed_anchor_side,
                                                             previous_anchor_side,
                                                             fallback,
                                                             graph_params,
                                                             forced_choice);
    if (candidate == nullptr) {
        return fallback;
    }

    LaneObservation observation = fallback;
    observation.center_forward_m = candidate->center.forward_m;
    observation.center_m = candidate->center.lateral_m;
    anchor_side = candidate->side;
    return observation;
}

std::array<LaneObservation, port::kBevTrackSampleCount> BuildChainObservations(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    const port::BEVCorridorGraphParameters& graph_params,
    std::array<BoundaryAnchorSide, port::kBevTrackSampleCount>* anchor_sides) {
    std::array<LaneObservation, port::kBevTrackSampleCount> observations{};
    if (anchor_sides != nullptr) {
        anchor_sides->fill(BoundaryAnchorSide::kNone);
    }
    const BoundaryTraces boundary_traces = BuildBoundaryTraces(intervals, indices, graph_params);
    LaneObservation previous_observation{};
    const LaneObservation* previous_observation_ptr = nullptr;
    BoundaryAnchorSide committed_anchor_side = BoundaryAnchorSide::kNone;
    BoundaryAnchorSide previous_anchor_side = BoundaryAnchorSide::kNone;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = SelectedIntervalAt(intervals, indices, layer);
        if (interval == nullptr) {
            continue;
        }
        const LaneObservation fallback =
            ObserveLaneWithEdges(*interval,
                                 graph_params,
                                 boundary_traces.left[layer].valid,
                                 boundary_traces.right[layer].valid);
        BoundaryAnchorSide anchor_side = BoundaryAnchorSide::kNone;
        bool forced_choice = false;
        observations[layer] = ApplyNormalCenterInference(boundary_traces,
                                                         layer,
                                                         fallback,
                                                         graph_params,
                                                         previous_observation_ptr,
                                                         committed_anchor_side,
                                                         previous_anchor_side,
                                                         anchor_side,
                                                         forced_choice);
        if (anchor_sides != nullptr) {
            (*anchor_sides)[layer] = anchor_side;
        }
        if (observations[layer].usable) {
            previous_observation = observations[layer];
            previous_observation_ptr = &previous_observation;
            previous_anchor_side = anchor_side;
            if (!forced_choice && anchor_side != BoundaryAnchorSide::kNone) {
                committed_anchor_side = anchor_side;
            }
        }
    }
    return observations;
}

bool IntervalAllowed(const port::CorridorInterval& interval,
                     const port::BEVCorridorGraphParameters& graph_params) {
    const LaneObservation observation = ObserveLane(interval, graph_params);
    return observation.usable &&
           interval.width_m >= graph_params.min_interval_width_m &&
           interval.width_m <= graph_params.max_interval_width_m &&
           interval.confidence > 0.0F;
}

port::BEVCorridorGraphParameters OrdinaryGraphParams(const port::RuntimeParameters& params) {
    port::BEVCorridorGraphParameters graph_params = params.bev_corridor_graph;
    if (graph_params.nominal_lane_width_m <= 0.0F) {
        graph_params.nominal_lane_width_m = params.bev_geometry.nominal_lane_width_m;
    }
    if (params.bev_geometry.min_lane_width_m > 0.0F) {
        graph_params.min_interval_width_m =
            std::max(graph_params.min_interval_width_m, params.bev_geometry.min_lane_width_m);
    }
    if (params.bev_geometry.max_lane_width_m > 0.0F) {
        graph_params.max_interval_width_m =
            std::min(graph_params.max_interval_width_m, params.bev_geometry.max_lane_width_m);
    }
    return graph_params;
}

float NodeScore(const LaneObservation& observation,
                const port::BEVCorridorGraphParameters& graph_params) {
    const float center_score =
        std::clamp(1.0F - std::abs(observation.center_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m * 2.0F),
                   0.0F,
                   1.0F);
    const float width_score =
        std::clamp(1.0F - std::abs(observation.lane_width_m - graph_params.nominal_lane_width_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m),
                   0.0F,
                   1.0F);
    return observation.confidence + 0.35F * center_score + 0.25F * width_score;
}

port::CorridorGraphEdge BuildEdge(int from_layer,
                                  int from_interval,
                                  int to_layer,
                                  int to_interval,
                                  const port::CorridorInterval& from,
                                  const port::CorridorInterval& to,
                                  const port::BEVCorridorGraphParameters& graph_params) {
    const LaneObservation from_observation = ObserveLane(from, graph_params);
    const LaneObservation to_observation = ObserveLane(to, graph_params);
    port::CorridorGraphEdge edge{};
    edge.from_layer = from_layer;
    edge.from_interval = from_interval;
    edge.to_layer = to_layer;
    edge.to_interval = to_interval;
    edge.overlap_score = IntervalOverlapScore(from, to);
    edge.center_jump_cost = std::abs(to_observation.center_m - from_observation.center_m);
    edge.width_change_cost = std::abs(to_observation.lane_width_m - from_observation.lane_width_m);
    edge.curvature_cost = 0.0F;
    edge.confidence =
        std::clamp((from_observation.confidence + to_observation.confidence) * 0.5F, 0.0F, 1.0F);
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

std::size_t ClampSampleIndex(int index) {
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, index)),
        port::kBevTrackSampleCount - 1U);
}

bool HasPathSampleAt(const port::PathCandidate& candidate, std::size_t layer_index) {
    return layer_index < candidate.sampled_path.size() && candidate.sampled_path[layer_index].valid;
}

bool HasSelectedIntervalAt(const std::array<int, port::kBevTrackSampleCount>& indices,
                           std::size_t layer_index) {
    return layer_index < indices.size() && indices[layer_index] >= 0;
}

bool HasSelectedIntervalAtOrBeyond(const std::array<int, port::kBevTrackSampleCount>& indices,
                                   std::size_t layer_index) {
    for (std::size_t index = layer_index; index < indices.size(); ++index) {
        if (HasSelectedIntervalAt(indices, index)) {
            return true;
        }
    }
    return false;
}

bool HasSelectedIntervalNear(const std::array<int, port::kBevTrackSampleCount>& indices,
                             std::size_t layer_index) {
    constexpr std::size_t kMaxControlAnchorEvidenceGap = 1U;
    const std::size_t begin = layer_index > kMaxControlAnchorEvidenceGap
                                  ? layer_index - kMaxControlAnchorEvidenceGap
                                  : 0U;
    const std::size_t end =
        std::min(indices.size() - 1U, layer_index + kMaxControlAnchorEvidenceGap);
    for (std::size_t index = begin; index <= end; ++index) {
        if (HasSelectedIntervalAt(indices, index)) {
            return true;
        }
    }
    return false;
}

struct BoundarySupportStats {
    bool valid = false;
    int count = 0;
    float first_forward_m = 0.0F;
    float last_forward_m = 0.0F;
    float min_lateral_m = 0.0F;
    float max_lateral_m = 0.0F;
};

BoundarySupportStats SingleSideBoundarySupport(
    const PathSampleArray& boundary,
    const PathSampleArray& opposite_boundary,
    std::size_t start_layer) {
    BoundarySupportStats stats{};
    for (std::size_t layer = start_layer; layer < boundary.size(); ++layer) {
        if (!boundary[layer].valid || opposite_boundary[layer].valid) {
            continue;
        }
        if (!stats.valid) {
            stats.valid = true;
            stats.first_forward_m = boundary[layer].point.forward_m;
            stats.min_lateral_m = boundary[layer].point.lateral_m;
            stats.max_lateral_m = boundary[layer].point.lateral_m;
        }
        stats.last_forward_m = boundary[layer].point.forward_m;
        stats.min_lateral_m = std::min(stats.min_lateral_m, boundary[layer].point.lateral_m);
        stats.max_lateral_m = std::max(stats.max_lateral_m, boundary[layer].point.lateral_m);
        ++stats.count;
    }
    return stats;
}

bool HasLongCurvedSingleSideProjection(const BoundarySupportStats& stats,
                                       const port::BEVCorridorGraphParameters& graph_params) {
    constexpr int kMinDistantBoundarySamples = 4;
    const float min_forward_span_m = std::max(0.18F, graph_params.nominal_lane_width_m * 0.40F);
    const float min_lateral_span_m = std::max(0.10F, graph_params.nominal_lane_width_m * 0.30F);
    return stats.valid && stats.count >= kMinDistantBoundarySamples &&
           stats.last_forward_m - stats.first_forward_m >= min_forward_span_m &&
           stats.max_lateral_m - stats.min_lateral_m >= min_lateral_span_m;
}

bool HasDistantBoundaryProjectionAnchor(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    std::size_t start_layer,
    const port::BEVCorridorGraphParameters& graph_params) {
    if (start_layer >= indices.size()) {
        return false;
    }
    const BoundaryTraces boundary_traces = BuildBoundaryTraces(intervals, indices, graph_params);
    const BoundarySupportStats left_stats =
        SingleSideBoundarySupport(boundary_traces.left, boundary_traces.right, start_layer);
    const BoundarySupportStats right_stats =
        SingleSideBoundarySupport(boundary_traces.right, boundary_traces.left, start_layer);
    return HasLongCurvedSingleSideProjection(left_stats, graph_params) ||
           HasLongCurvedSingleSideProjection(right_stats, graph_params);
}

bool HasEvidenceBackedControlAnchor(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    std::size_t near_index,
    std::size_t curvature_index,
    const port::BEVCorridorGraphParameters& graph_params) {
    if (HasSelectedIntervalNear(indices, near_index)) {
        return true;
    }
    const std::size_t distant_start =
        std::min(indices.size() - 1U, std::max(near_index + 2U, curvature_index));
    return HasDistantBoundaryProjectionAnchor(intervals, indices, distant_start, graph_params);
}

void FillCandidateSummary(port::PathCandidate& candidate,
                          const CorridorIntervalSet& intervals,
                          const std::array<int, port::kBevTrackSampleCount>& indices,
                          const port::RuntimeParameters& params,
                          const port::BEVCorridorGraphParameters& graph_params,
                          std::array<port::BEVPathSample, port::kBevTrackSampleCount>* raw_centerline,
                          std::array<BoundaryAnchorSide, port::kBevTrackSampleCount>* anchor_sides) {
    const std::array<LaneObservation, port::kBevTrackSampleCount> observations =
        BuildChainObservations(intervals, indices, graph_params, anchor_sides);
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
        const LaneObservation& observation = observations[layer];
        if (!observation.usable) {
            continue;
        }
        candidate.sampled_path[layer] =
            MakePathSample(observation.center_forward_m, observation.center_m, observation.confidence);
        if (raw_centerline != nullptr) {
            (*raw_centerline)[layer] = candidate.sampled_path[layer];
        }
        confidence_sum += observation.confidence;
        width_sum += observation.lane_width_m;
        width_sq_sum += observation.lane_width_m * observation.lane_width_m;
        if (first == nullptr) {
            first = &candidate.sampled_path[layer];
            candidate.start_forward_m = observation.center_forward_m;
        }
        mid = last;
        last = &candidate.sampled_path[layer];
        candidate.end_forward_m = observation.center_forward_m;
        ++valid_count;
    }

    const int normalized_count =
        NormalizePathToForwardSamples(candidate.sampled_path,
                                      params.bev_topology_sampler.forward_samples_m,
                                      graph_params.nominal_lane_width_m * 0.5F,
                                      std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                               std::abs(params.bev_geometry.lateral_step_m)) *
                                          2.0F);
    candidate.valid = normalized_count >= 3;
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
    first = nullptr;
    mid = nullptr;
    last = nullptr;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
            candidate.start_forward_m = sample.point.forward_m;
        }
        mid = last;
        last = &sample;
        candidate.end_forward_m = sample.point.forward_m;
    }
    if (first != nullptr && mid != nullptr && last != nullptr && first != last && mid != last) {
        candidate.curvature = ComputeCurvature(*first, *mid, *last);
        candidate.curvature_consistency =
            std::clamp(1.0F - std::abs(candidate.curvature) /
                                 std::max(1e-4F, graph_params.max_curvature_abs),
                       0.0F,
                       1.0F);
    }
}

void ClearPublishedTrackGeometry(port::BEVTrackEstimate& track) {
    track.sampled_left_boundary.fill({});
    track.sampled_centerline.fill({});
    track.sampled_right_boundary.fill({});
    track.sampled_drivable_left_boundary.fill({});
    track.sampled_drivable_right_boundary.fill({});
    track.lane_width_profile_m.fill(0.0F);
    track.drivable_width_profile_m.fill(0.0F);
    track.visible_range_m = 0.0F;
    track.track_confidence = 0.0F;
    track.near_lateral_error = 0.0F;
    track.far_heading_error = 0.0F;
    track.preview_curvature = 0.0F;
}

}  // namespace

CorridorGraph BuildCorridorGraph(const CorridorIntervalSet& intervals,
                                 const port::RuntimeParameters& params,
                                 const port::LegacySteeringState& prior_state) {
    CorridorGraph graph{};
    graph.ordinary_interval_indices.fill(-1);
    graph.ordinary_center_anchor_side.fill(BoundaryAnchorSide::kNone);
    graph.ordinary_raw_centerline.fill({});
    if (!intervals.valid) {
        return graph;
    }

    const port::BEVCorridorGraphParameters graph_params = OrdinaryGraphParams(params);

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
            const LaneObservation observation = ObserveLane(interval, graph_params);
            const float base_score = NodeScore(observation, graph_params) +
                                     PriorCarryScore(prior_state, layer, observation, graph_params);
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
                                                         to,
                                                         graph_params);
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
    FillCandidateSummary(graph.ordinary,
                         intervals,
                         graph.ordinary_interval_indices,
                         params,
                         graph_params,
                         &graph.ordinary_raw_centerline,
                         &graph.ordinary_center_anchor_side);
    const std::size_t near_index = ClampSampleIndex(params.bev_control_model.near_sample_index);
    const std::size_t curvature_index = ClampSampleIndex(params.bev_control_model.curvature_sample_index);
    if (!graph.ordinary.valid) {
        graph.fallback_mode = "insufficient_corridor_samples";
    } else if (!HasPathSampleAt(graph.ordinary, near_index)) {
        graph.ordinary.valid = false;
        graph.fallback_mode = "control_anchor_missing";
    } else if (!HasEvidenceBackedControlAnchor(intervals,
                                               graph.ordinary_interval_indices,
                                               near_index,
                                               curvature_index,
                                               graph_params)) {
        graph.ordinary.valid = false;
        graph.fallback_mode = "control_anchor_missing";
    } else if (!HasSelectedIntervalAtOrBeyond(graph.ordinary_interval_indices, curvature_index)) {
        graph.ordinary.valid = false;
        graph.fallback_mode = "control_horizon_missing";
    }
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
        track.fallback_mode =
            graph.fallback_mode.empty() || graph.fallback_mode == "none" ? "no_corridor_chain"
                                                                         : graph.fallback_mode;
        return track;
    }

    int valid_count = 0;
    float confidence_sum = 0.0F;
    const port::BEVCorridorGraphParameters graph_params = OrdinaryGraphParams(params);
    const std::array<LaneObservation, port::kBevTrackSampleCount> observations =
        BuildChainObservations(intervals, graph.ordinary_interval_indices, graph_params, nullptr);
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        if (!graph.ordinary.sampled_path[layer].valid) {
            continue;
        }
        track.sampled_centerline[layer] = graph.ordinary.sampled_path[layer];
        track.visible_range_m =
            std::max(track.visible_range_m, graph.ordinary.sampled_path[layer].point.forward_m);
    }
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int interval_index = graph.ordinary_interval_indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const port::CorridorInterval& interval =
            intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
        const LaneObservation& observation = observations[layer];
        if (!observation.usable) {
            continue;
        }
        if (observation.left_boundary_observed) {
            track.sampled_left_boundary[layer] =
                MakePathSample(interval.forward_m, interval.lateral_min_m, observation.confidence);
        }
        if (observation.right_boundary_observed) {
            track.sampled_right_boundary[layer] =
                MakePathSample(interval.forward_m, interval.lateral_max_m, observation.confidence);
        }
        track.sampled_drivable_left_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_min_m, interval.confidence);
        track.sampled_drivable_right_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_max_m, interval.confidence);
        track.lane_width_profile_m[layer] = observation.lane_width_m;
        track.drivable_width_profile_m[layer] = interval.width_m;
        confidence_sum += observation.confidence;
        ++valid_count;
    }

    const std::size_t near_index = ClampSampleIndex(params.bev_control_model.near_sample_index);
    const bool has_control_anchor =
        near_index < port::kBevTrackSampleCount && track.sampled_centerline[near_index].valid;
    track.valid = valid_count >= 3 && has_control_anchor;
    track.track_confidence =
        valid_count > 0 ? std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F) : 0.0F;
    if (!track.valid) {
        track.fallback_mode = has_control_anchor ? "insufficient_corridor_samples" : "control_anchor_missing";
        ClearPublishedTrackGeometry(track);
        return track;
    }

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
