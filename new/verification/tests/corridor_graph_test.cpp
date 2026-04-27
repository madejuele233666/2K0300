#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/steering_corridor_graph.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::port::CorridorInterval Interval(float forward, float center, float width, float confidence = 0.9F) {
    ls2k::port::CorridorInterval interval{};
    interval.forward_m = forward;
    interval.lateral_center_m = center;
    interval.width_m = width;
    interval.lateral_min_m = center - width * 0.5F;
    interval.lateral_max_m = center + width * 0.5F;
    interval.left_edge_valid = true;
    interval.right_edge_valid = true;
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

}  // namespace

int main() {
    try {
        TestStraightOrdinaryChain();
        TestCurvedOrdinaryChainContinuity();
        TestWidthJumpIsDownweighted();
    } catch (const TestFailure& failure) {
        std::cerr << "corridor_graph_test failed: " << failure.message << "\n";
        return 1;
    }
    return 0;
}
