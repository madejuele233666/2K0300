#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_path_math.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

bool NearlyEqual(float left, float right, float tolerance = 1e-5F) {
    return std::abs(left - right) <= tolerance;
}

ls2k::port::BEVPathSample Sample(float forward_m, float lateral_m) {
    ls2k::port::BEVPathSample sample{};
    sample.valid = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = 1.0F;
    return sample;
}

ls2k::port::CorridorInterval Interval(float forward,
                                      float center,
                                      float width,
                                      float confidence = 0.9F,
                                      bool left_edge_valid = true,
                                      bool right_edge_valid = true) {
    ls2k::port::CorridorInterval interval{};
    interval.forward_m = forward;
    interval.lateral_center_m = center;
    interval.width_m = width;
    interval.lateral_min_m = center - width * 0.5F;
    interval.lateral_max_m = center + width * 0.5F;
    interval.left_edge_valid = left_edge_valid;
    interval.right_edge_valid = right_edge_valid;
    interval.valid_sample_ratio = 1.0F;
    interval.confidence = confidence;
    return interval;
}

ls2k::port::CorridorInterval IntervalFromEdges(float forward,
                                               float left_m,
                                               float right_m,
                                               float confidence,
                                               bool left_edge_valid,
                                               bool right_edge_valid) {
    ls2k::port::CorridorInterval interval{};
    interval.forward_m = forward;
    interval.lateral_min_m = left_m;
    interval.lateral_max_m = right_m;
    interval.width_m = right_m - left_m;
    interval.lateral_center_m = (left_m + right_m) * 0.5F;
    interval.left_edge_valid = left_edge_valid;
    interval.right_edge_valid = right_edge_valid;
    interval.valid_sample_ratio = 1.0F;
    interval.confidence = confidence;
    return interval;
}

ls2k::legacy::CorridorIntervalSet StraightIntervals() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::CorridorIntervalSet set{};
    set.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        set.layers[layer].forward_m = forward;
        set.layers[layer].intervals.push_back(Interval(forward, 0.0F, 0.42F));
    }
    return set;
}

void TestStraightOrdinaryChain() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    const auto intervals = StraightIntervals();
    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid, "straight intervals must produce a valid ordinary graph");
    Expect(graph.ordinary.valid, "straight graph must produce ordinary candidate");
    Expect(track.valid, "straight graph must produce BEV track");
    Expect(track.source == "bev_corridor_topology", "track source must identify corridor topology");
    Expect(std::abs(track.sampled_centerline[4].point.lateral_m) < 1e-4F,
           "straight centerline must stay centered");
}

void TestCurvedOrdinaryChainContinuity() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    auto intervals = StraightIntervals();
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float center = static_cast<float>(layer) * 0.015F;
        intervals.layers[layer].intervals[0] =
            Interval(params.bev_topology_sampler.forward_samples_m[layer], center, 0.42F);
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "curved ordinary chain must remain valid");
    Expect(track.sampled_centerline[6].point.lateral_m > track.sampled_centerline[1].point.lateral_m,
           "curved chain must preserve lateral trend");
}

void TestWidthJumpIsDownweighted() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    auto intervals = StraightIntervals();
    intervals.layers[4].intervals.push_back(Interval(params.bev_topology_sampler.forward_samples_m[4],
                                                     0.0F,
                                                     1.10F,
                                                     0.95F));

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);

    Expect(graph.valid, "graph with branch interval must remain valid");
    Expect(graph.ordinary_interval_indices[4] == 0,
           "ordinary graph should prefer stable nominal-width interval over width jump");
}

void TestNarrowFragmentsDoNotBecomeOrdinaryTrack() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 8; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.20F, 0.02F, 0.95F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(!graph.valid, "sub-minimum fragments must not form an ordinary graph");
    Expect(!track.valid, "sub-minimum fragments must not form a controllable track");
}

void TestZeroConfidenceIntervalsDoNotBecomeOrdinaryTrack() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.0F, 0.42F, 0.0F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(!graph.valid, "zero-confidence intervals must remain filtered by observation usability");
    Expect(!track.valid, "zero-confidence intervals must not form a controllable track");
}

void TestOverWideIntervalsDoNotBecomeOrdinaryTrack() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    const float over_wide = params.bev_geometry.max_lane_width_m + 0.10F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.0F, over_wide, 0.95F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(!graph.valid, "over-wide topology intervals must not form an ordinary graph");
    Expect(!track.valid, "over-wide topology intervals must not form a controllable track");
}

void TestFarOnlyIntervalsRemainGraphEvidence() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 8; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.20F, 0.42F, 0.95F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid, "far-only corridor evidence should remain available to reference policy");
    Expect(track.valid, "graph layer must not hard-reject far-only evidence at a fixed near anchor");
    Expect(!track.sampled_centerline[0].valid,
           "far-only graph evidence must not fabricate a near center sample");
}

void TestShortNearOnlyChainNeedsEvidenceBackedControlHorizon() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    const auto curvature_index = static_cast<std::size_t>(params.bev_control_model.curvature_sample_index);
    for (std::size_t layer = 0; layer < curvature_index; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.02F, 0.42F, 0.95F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(!graph.valid, "short near-only chains must not extrapolate through the control horizon");
    Expect(track.fallback_mode == "control_horizon_missing",
           "short near-only chain rejection must identify the missing evidence-backed horizon");
    Expect(!track.valid, "short near-only chains must not produce a controllable white path");
}

void TestDistantEvidenceDoesNotFabricateNearSample() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 3; layer < 8U; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.04F, 0.42F, 0.95F));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid, "distant evidence should stay available for trusted reference compatibility");
    Expect(track.valid, "distant-evidence graph must remain valid when the horizon is observed");
    Expect(!track.sampled_centerline[0].valid,
           "distant evidence must not fabricate the fixed near control anchor");
}

void TestDistantCurvedSingleEdgeCanBackProjectControlAnchor() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 6; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float progress = static_cast<float>(layer - 6U) / 5.0F;
        const float left = -0.12F + progress * 0.28F;
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(
            IntervalFromEdges(forward, left, left + 0.42F, 0.95F, true, false));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid, "long curved single-edge evidence may back-project the near control anchor");
    Expect(track.valid, "long curved single-edge evidence must still produce a controllable bend track");
    Expect(graph.ordinary_center_anchor_side[6] == ls2k::legacy::BoundaryAnchorSide::kLeft,
           "distant curved single-edge projection must stay anchored to the observed boundary");
}

void TestUnobservedEdgesDoNotBecomeLaneBoundaries() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.0F, 0.42F, 0.95F, false, false));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "screen-edge bounded support may remain degraded center support");
    Expect(!track.sampled_left_boundary[1].valid, "unobserved left edge must not become a lane boundary");
    Expect(!track.sampled_right_boundary[1].valid, "unobserved right edge must not become a lane boundary");
    Expect(track.sampled_drivable_left_boundary[1].valid && track.sampled_drivable_right_boundary[1].valid,
           "drivable support extents must remain available separately");
    Expect(track.track_confidence < 0.5F, "support-only track confidence must be degraded");
}

void TestSingleObservedEdgeDoesNotInventOppositeBoundary() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.0F, 0.70F, 0.95F, true, false));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "single observed edge should still provide degraded center support");
    Expect(track.sampled_left_boundary[1].valid, "observed left edge must remain a lane boundary");
    Expect(!track.sampled_right_boundary[1].valid, "unobserved right edge must not become a lane boundary");
    Expect(track.sampled_drivable_right_boundary[1].valid,
           "drivable support may still expose the right support extent separately");
    const float expected_center = intervals.layers[1].intervals[0].lateral_min_m +
                                  params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    Expect(std::abs(track.sampled_centerline[1].point.lateral_m - expected_center) < 1e-4F,
           "single-edge centerline must be inferred from the observed edge and nominal lane width");
}

void TestIsolatedObservedEdgeDoesNotPublishOrAnchor() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, 0.0F, 0.42F, 0.95F, false, false));
    }
    intervals.layers[1].intervals[0].left_edge_valid = true;

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "isolated observed-edge support must still allow degraded center support");
    Expect(!track.sampled_left_boundary[1].valid,
           "isolated single-layer left edge must not be published as an observed boundary");
    Expect(graph.ordinary_center_anchor_side[1] == ls2k::legacy::BoundaryAnchorSide::kNone,
           "isolated single-layer left edge must not become a normal-offset anchor");
    Expect(std::abs(track.sampled_centerline[1].point.lateral_m) < 1e-4F,
           "isolated observed edge must fall back to the support interval center");
}

void TestImageClippedSupportDoesNotDriveOrdinaryTrack() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        auto interval = Interval(forward, 0.24F, 0.42F, 0.95F, false, false);
        interval.left_boundary_evidence = ls2k::port::BEVBoundaryEvidenceKind::kUnknownLowConfidence;
        interval.right_boundary_evidence = ls2k::port::BEVBoundaryEvidenceKind::kImageBorder;
        intervals.layers[layer].intervals.push_back(interval);
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(!graph.valid, "image-clipped support-only intervals must not drive an ordinary graph");
    Expect(!track.valid, "image-clipped support-only intervals must not produce a controllable white path");
}

void TestClippedWideSingleEdgeFallsBackToSupportCenter() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;

    constexpr float kLeft = -0.12F;
    constexpr float kRight = 0.50F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        intervals.layers[layer].forward_m = forward;
        auto interval = IntervalFromEdges(forward, kLeft, kRight, 0.95F, true, false);
        interval.left_boundary_evidence =
            ls2k::port::BEVBoundaryEvidenceKind::kObservedDrivableBackground;
        interval.right_boundary_evidence = ls2k::port::BEVBoundaryEvidenceKind::kImageBorder;
        intervals.layers[layer].intervals.push_back(interval);
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    constexpr std::size_t kLayer = 5U;
    constexpr float kSupportCenter = (kLeft + kRight) * 0.5F;
    const float nominal_normal_center = kLeft + params.bev_corridor_graph.nominal_lane_width_m * 0.5F;

    Expect(graph.valid && track.valid, "wide clipped single-edge support must remain usable as degraded support");
    Expect(graph.ordinary_center_anchor_side[kLayer] == ls2k::legacy::BoundaryAnchorSide::kNone,
           "wide clipped single-edge support must not become a normal-offset anchor");
    Expect(std::abs(graph.ordinary_raw_centerline[kLayer].point.lateral_m - kSupportCenter) < 1e-4F,
           "wide clipped single-edge support must use support center for the raw centerline");
    Expect(std::abs(track.sampled_centerline[kLayer].point.lateral_m - kSupportCenter) < 1e-4F,
           "wide clipped single-edge support must preserve the support center after resampling");
    Expect(std::abs(track.sampled_centerline[kLayer].point.lateral_m - nominal_normal_center) > 0.05F,
           "wide clipped single-edge support must not shrink to nominal-width normal offset");
    Expect(track.sampled_left_boundary[kLayer].valid, "real observed boundary may still be published");
    Expect(!track.sampled_right_boundary[kLayer].valid, "image border must not be published as observed boundary");
    Expect(track.sampled_drivable_right_boundary[kLayer].valid,
           "clipped support extent must remain available as debug support");
}

void TestOrdinaryPathDoesNotExtrapolatePastSelectedSupport() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;

    constexpr std::size_t kLastSupportedLayer = 9U;
    for (std::size_t layer = 0; layer <= kLastSupportedLayer; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float center = static_cast<float>(layer) * 0.025F;
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(Interval(forward, center, 0.42F, 0.95F));
    }
    for (std::size_t layer = kLastSupportedLayer + 1U;
         layer < ls2k::port::kBevTrackSampleCount;
         ++layer) {
        intervals.layers[layer].forward_m = params.bev_topology_sampler.forward_samples_m[layer];
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "near supported partial corridor must remain usable");
    Expect(graph.ordinary.sampled_path[kLastSupportedLayer].valid,
           "ordinary candidate must keep the last selected support layer");
    Expect(track.sampled_centerline[kLastSupportedLayer].valid,
           "track must publish the last selected support layer");
    Expect(!graph.ordinary.sampled_path[kLastSupportedLayer + 1U].valid,
           "ordinary candidate must not extrapolate beyond selected corridor support");
    Expect(!track.sampled_centerline[kLastSupportedLayer + 1U].valid,
           "track must not publish a white path beyond selected corridor support");
    Expect(track.visible_range_m <= params.bev_topology_sampler.forward_samples_m[kLastSupportedLayer] + 1e-4F,
           "visible range must be bounded by selected corridor support");
}

void TestSingleObservedEdgeUsesBevNormalOffset() {
    ls2k::port::RuntimeParameters params{};
    params.bev_corridor_graph.max_center_jump_m = 0.80F;
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;

    constexpr float kSlope = 0.60F;
    constexpr float kLeftAtOrigin = -0.35F;
    constexpr float kSupportWidth = 0.70F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float left = kLeftAtOrigin + kSlope * forward;
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(
            IntervalFromEdges(forward, left, left + kSupportWidth, 0.95F, true, false));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    constexpr std::size_t kLayer = 5U;
    const float forward = params.bev_topology_sampler.forward_samples_m[kLayer];
    const float left = kLeftAtOrigin + kSlope * forward;
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    const float tangent_length = std::sqrt(1.0F + kSlope * kSlope);
    const float expected_raw_forward = forward - kSlope / tangent_length * half_width;
    const float expected_raw_lateral = left + 1.0F / tangent_length * half_width;
    const float expected_normalized_lateral = left + tangent_length * half_width;
    const float lateral_shift_only = left + half_width;

    Expect(graph.valid && track.valid, "sloped single-edge chain must remain valid");
    Expect(std::abs(graph.ordinary_raw_centerline[kLayer].point.forward_m - expected_raw_forward) < 1e-4F,
           "single-edge raw inferred center must move along the BEV normal forward component");
    Expect(std::abs(graph.ordinary_raw_centerline[kLayer].point.lateral_m - expected_raw_lateral) < 1e-4F,
           "single-edge raw inferred center must move along the BEV normal lateral component");
    Expect(std::abs(track.sampled_centerline[kLayer].point.forward_m - forward) < 1e-4F,
           "normalized centerline must resample back onto the configured forward layer");
    Expect(std::abs(track.sampled_centerline[kLayer].point.lateral_m - expected_normalized_lateral) < 1e-4F,
           "normalized centerline must preserve the BEV-normal offset curve at the configured forward layer");
    Expect(std::abs(track.sampled_centerline[kLayer].point.lateral_m - lateral_shift_only) > 0.02F,
           "normalized single-edge center must not use fixed lateral-only shifting");
}

void TestObservedEdgesPreferLocalAnchorContinuity() {
    ls2k::port::RuntimeParameters params{};
    params.bev_geometry.max_lane_width_m = 10.0F;
    params.bev_corridor_graph.max_interval_width_m = 10.0F;
    params.bev_corridor_graph.max_width_change_m = 10.0F;
    params.bev_corridor_graph.max_center_jump_m = 10.0F;
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;

    constexpr float kLeftSlope = 0.70F;
    constexpr float kRightSlope = 0.70F;
    constexpr float kLeftAtOrigin = -0.30F;
    constexpr float kRightAtOrigin = 0.12F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float left = kLeftAtOrigin + kLeftSlope * forward;
        const float right = kRightAtOrigin + kRightSlope * forward;
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(
            IntervalFromEdges(forward,
                              left,
                              right,
                              0.95F,
                              layer < 8U,
                              layer >= 2U));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    constexpr std::size_t kBothObservedLayer = 6U;
    constexpr std::size_t kRightOnlyLayer = 9U;
    const float forward_both = params.bev_topology_sampler.forward_samples_m[kBothObservedLayer];
    const float left_both = kLeftAtOrigin + kLeftSlope * forward_both;
    const float right_both = kRightAtOrigin + kRightSlope * forward_both;
    const float forward_right_only = params.bev_topology_sampler.forward_samples_m[kRightOnlyLayer];
    const float right_right_only = kRightAtOrigin + kRightSlope * forward_right_only;
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    const float tangent_length = std::sqrt(1.0F + kLeftSlope * kLeftSlope);
    const float left_center_forward = forward_both - kLeftSlope / tangent_length * half_width;
    const float left_center_lateral = left_both + 1.0F / tangent_length * half_width;
    const float right_center_forward = forward_right_only + kRightSlope / tangent_length * half_width;
    const float right_center_lateral = right_right_only - 1.0F / tangent_length * half_width;
    const float same_layer_midpoint = (left_both + right_both) * 0.5F;

    Expect(graph.valid && track.valid, "locally anchored observed-edge chain must remain valid");
    Expect(graph.ordinary_center_anchor_side[kBothObservedLayer] == ls2k::legacy::BoundaryAnchorSide::kLeft,
           "when both edges are locally supported, anchor choice must stay continuous with the earlier left track");
    Expect(std::abs(graph.ordinary_raw_centerline[kBothObservedLayer].point.forward_m - left_center_forward) < 1e-4F,
           "continuous local anchor must keep the center on the left boundary normal while both edges remain observed");
    Expect(std::abs(graph.ordinary_raw_centerline[kBothObservedLayer].point.lateral_m - left_center_lateral) < 1e-4F,
           "continuous local anchor lateral must come from the left boundary normal");
    Expect(std::abs(graph.ordinary_raw_centerline[kBothObservedLayer].point.lateral_m - same_layer_midpoint) > 0.02F,
           "continuous local anchor must not collapse back to the same-forward midpoint");
    Expect(graph.ordinary_center_anchor_side[kRightOnlyLayer] == ls2k::legacy::BoundaryAnchorSide::kRight,
           "anchor choice must switch once the left boundary is no longer observed");
    Expect(std::abs(graph.ordinary_raw_centerline[kRightOnlyLayer].point.forward_m - right_center_forward) < 1e-4F,
           "right-only support must project the center from the right boundary normal");
    Expect(std::abs(graph.ordinary_raw_centerline[kRightOnlyLayer].point.lateral_m - right_center_lateral) < 1e-4F,
           "right-only support lateral must come from the right boundary normal");

    float previous_forward = -1.0F;
    for (const ls2k::port::BEVPathSample& sample : track.sampled_centerline) {
        if (!sample.valid) {
            continue;
        }
        Expect(sample.point.forward_m > previous_forward,
               "normalized centerline must remain strictly forward-monotonic");
        previous_forward = sample.point.forward_m;
    }
}

void TestNoisySingleEdgeBendUsesStableNormalFit() {
    ls2k::port::RuntimeParameters params{};
    params.bev_corridor_graph.max_center_jump_m = 0.80F;
    params.bev_corridor_graph.max_interval_width_m = 1.20F;
    ls2k::port::LegacySteeringState prior{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;

    const std::array<float, ls2k::port::kBevTrackSampleCount> left_edges{
        {-0.13F,
         -0.14F,
         -0.15F,
         -0.17F,
         -0.19F,
         -0.18F,
         -0.17F,
         -0.16F,
         -0.15F,
         -0.15F,
         -0.15F,
         -0.13F,
         -0.11F,
         -0.09F,
         -0.07F,
         -0.04F,
         -0.01F,
         0.04F,
         0.11F,
         0.13F,
         0.15F,
         0.14F,
         0.13F,
         0.12F}};
    constexpr float kObservedSupportWidth = 0.44F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float left = left_edges[layer];
        intervals.layers[layer].forward_m = forward;
        intervals.layers[layer].intervals.push_back(
            IntervalFromEdges(forward, left, left + kObservedSupportWidth, 0.95F, true, false));
    }

    const auto graph = ls2k::legacy::BuildCorridorGraph(intervals, params, prior);
    const auto track = ls2k::legacy::BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);

    Expect(graph.valid && track.valid, "noisy single-edge bend must remain usable");
    float previous_raw_forward = -1.0F;
    for (std::size_t layer = 0; layer < ls2k::port::kBevTrackSampleCount; ++layer) {
        Expect(graph.ordinary_center_anchor_side[layer] == ls2k::legacy::BoundaryAnchorSide::kLeft,
               "single-edge bend must keep the observed left boundary as its anchor");
        const float inferred_forward = graph.ordinary_raw_centerline[layer].point.forward_m;
        Expect(inferred_forward + 0.06F > previous_raw_forward,
               "single-edge normal fit must not produce a large local forward backtrack");
        previous_raw_forward = inferred_forward;
    }
    Expect(track.sampled_centerline[0].point.lateral_m < 0.12F,
           "resampling must not amplify clustered near-field support into a lateral spike");
    for (std::size_t layer = 1; layer + 1U < ls2k::port::kBevTrackSampleCount; ++layer) {
        const float previous_delta = track.sampled_centerline[layer].point.lateral_m -
                                     track.sampled_centerline[layer - 1U].point.lateral_m;
        const float next_delta = track.sampled_centerline[layer + 1U].point.lateral_m -
                                 track.sampled_centerline[layer].point.lateral_m;
        Expect(std::abs(next_delta - previous_delta) < 0.12F,
               "single-edge bend centerline must not contain a local resampling spike");
    }
}

void TestSharedPathCurvatureHelper() {
    const ls2k::port::BEVPathSample first = Sample(0.0F, 0.0F);
    const ls2k::port::BEVPathSample mid = Sample(1.0F, 0.0F);
    const ls2k::port::BEVPathSample straight_last = Sample(2.0F, 0.0F);
    const ls2k::port::BEVPathSample left_last = Sample(2.0F, 0.5F);
    const ls2k::port::BEVPathSample right_last = Sample(2.0F, -0.5F);

    Expect(NearlyEqual(ls2k::legacy::PathCurvatureFromThreeSamples(first, mid, straight_last), 0.0F),
           "shared curvature helper must return zero for collinear samples");
    Expect(NearlyEqual(ls2k::legacy::PathCurvatureFromThreeSamples(first, mid, left_last), 0.25F),
           "shared curvature helper must preserve the original positive curvature formula");
    Expect(NearlyEqual(ls2k::legacy::PathCurvatureFromThreeSamples(first, mid, right_last), -0.25F),
           "shared curvature helper must preserve the original negative curvature formula");
}

}  // namespace

int main() {
    try {
        TestStraightOrdinaryChain();
        TestCurvedOrdinaryChainContinuity();
        TestWidthJumpIsDownweighted();
        TestNarrowFragmentsDoNotBecomeOrdinaryTrack();
        TestZeroConfidenceIntervalsDoNotBecomeOrdinaryTrack();
        TestOverWideIntervalsDoNotBecomeOrdinaryTrack();
        TestFarOnlyIntervalsRemainGraphEvidence();
        TestShortNearOnlyChainNeedsEvidenceBackedControlHorizon();
        TestDistantEvidenceDoesNotFabricateNearSample();
        TestDistantCurvedSingleEdgeCanBackProjectControlAnchor();
        TestUnobservedEdgesDoNotBecomeLaneBoundaries();
        TestSingleObservedEdgeDoesNotInventOppositeBoundary();
        TestIsolatedObservedEdgeDoesNotPublishOrAnchor();
        TestImageClippedSupportDoesNotDriveOrdinaryTrack();
        TestClippedWideSingleEdgeFallsBackToSupportCenter();
        TestOrdinaryPathDoesNotExtrapolatePastSelectedSupport();
        TestSingleObservedEdgeUsesBevNormalOffset();
        TestObservedEdgesPreferLocalAnchorContinuity();
        TestNoisySingleEdgeBendUsesStableNormalFit();
        TestSharedPathCurvatureHelper();
    } catch (const TestFailure& failure) {
        std::cerr << "corridor_graph_test failed: " << failure.message << "\n";
        return 1;
    }
    return 0;
}
