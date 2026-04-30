#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/steering_control_error_model.hpp"
#include "legacy/steering_reference_policy.hpp"
#include "legacy/steering_topology_hypotheses.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::port::BEVPathSample Sample(float forward_m, float lateral_m, float confidence = 1.0F) {
    ls2k::port::BEVPathSample sample{};
    sample.valid = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = confidence;
    return sample;
}

ls2k::port::PathCandidate ConstantCandidate(const ls2k::port::RuntimeParameters& params,
                                            float lateral_m,
                                            ls2k::port::ReferenceMode mode =
                                                ls2k::port::ReferenceMode::kCenterline) {
    ls2k::port::PathCandidate candidate{};
    candidate.valid = true;
    candidate.mode = mode;
    candidate.confidence = 1.0F;
    for (std::size_t index = 0; index < candidate.sampled_path.size(); ++index) {
        candidate.sampled_path[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index], lateral_m);
    }
    candidate.start_forward_m = params.bev_topology_sampler.forward_samples_m.front();
    candidate.end_forward_m = params.bev_topology_sampler.forward_samples_m.back();
    return candidate;
}

ls2k::port::PathCandidate SlopedCandidate(const ls2k::port::RuntimeParameters& params,
                                          float intercept,
                                          float slope,
                                          ls2k::port::ReferenceMode mode =
                                              ls2k::port::ReferenceMode::kStableBoundaryOffset) {
    ls2k::port::PathCandidate candidate{};
    candidate.valid = true;
    candidate.mode = mode;
    candidate.confidence = 1.0F;
    for (std::size_t index = 0; index < candidate.sampled_path.size(); ++index) {
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        candidate.sampled_path[index] = Sample(forward, intercept + slope * forward);
    }
    candidate.start_forward_m = params.bev_topology_sampler.forward_samples_m.front();
    candidate.end_forward_m = params.bev_topology_sampler.forward_samples_m.back();
    return candidate;
}

ls2k::port::ReferencePolicyState TrustedPrior(const ls2k::port::RuntimeParameters& params,
                                              float lateral_m) {
    ls2k::port::ReferencePolicyState prior{};
    prior.valid = true;
    prior.mode = ls2k::port::ReferenceMode::kCenterline;
    prior.trusted_confidence = 1.0F;
    for (std::size_t index = 0; index < prior.last_reference.size(); ++index) {
        prior.last_reference[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index], lateral_m);
    }
    return prior;
}

void FillEdge(const ls2k::port::RuntimeParameters& params,
              std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& edge,
              float lateral_m,
              float confidence = 0.90F) {
    for (std::size_t index = 0; index < edge.size(); ++index) {
        edge[index] = Sample(params.bev_topology_sampler.forward_samples_m[index], lateral_m, confidence);
    }
}

ls2k::port::BEVCircleInnerIslandEvidence IslandEvidence(const ls2k::port::RuntimeParameters& params,
                                                        float lateral_m) {
    ls2k::port::BEVCircleInnerIslandEvidence evidence{};
    evidence.present = true;
    evidence.edge_present = true;
    evidence.trace_present = true;
    evidence.score = 0.82F;
    evidence.scan_lateral_m = lateral_m - 0.04F;
    evidence.black_start_forward_m = params.bev_topology_sampler.forward_samples_m[1];
    evidence.black_end_forward_m = params.bev_topology_sampler.forward_samples_m[8];
    evidence.support_lines = 2;
    FillEdge(params, evidence.road_facing_edge, lateral_m);
    return evidence;
}

ls2k::port::SpecialSceneFsmState CrossState(ls2k::port::SpecialScenePhase phase) {
    ls2k::port::SpecialSceneFsmState state{};
    state.active_scene = ls2k::port::SpecialSceneKind::kCross;
    state.phase = phase;
    return state;
}

void TestTrustedDoesNotAlterExpectedPath() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.ordinary = ConstantCandidate(params, -0.12F);
    const ls2k::port::ReferencePolicyState prior = TrustedPrior(params, 0.30F);
    const ls2k::port::SpecialSceneFsmState scene{};
    const ls2k::port::TopologyEvidence evidence{};

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, evidence, scene, prior, params);

    Expect(result.reference_path.valid, "ordinary expected path must remain valid");
    Expect(std::abs(result.reference_path.sampled_path[0].point.lateral_m + 0.12F) < 1.0e-4F,
           "trusted path must not geometrically blend into ordinary expected path");
    Expect(result.trusted_error_reference.valid, "trusted path must be exported only as error reference");
}

void TestCrossNoExitUsesTrustedHoldOnly() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.ordinary = ConstantCandidate(params, 0.22F);
    const ls2k::port::ReferencePolicyState prior = TrustedPrior(params, -0.04F);
    const ls2k::port::TopologyEvidence evidence{};

    const auto held = ls2k::legacy::ResolveReferencePolicy(
        hypotheses, evidence, CrossState(ls2k::port::SpecialScenePhase::kHold), prior, params);
    Expect(held.reference_path.valid, "cross without exit must hold trusted path when available");
    Expect(held.reference_path.mode == ls2k::port::ReferenceMode::kHoldLast,
           "cross no-exit fallback must be hold_last");
    Expect(std::abs(held.reference_path.sampled_path[0].point.lateral_m + 0.04F) < 1.0e-4F,
           "cross no-exit must not fall back to ordinary geometry");

    const ls2k::port::ReferencePolicyState no_prior{};
    const auto invalid = ls2k::legacy::ResolveReferencePolicy(
        hypotheses, evidence, CrossState(ls2k::port::SpecialScenePhase::kHold), no_prior, params);
    Expect(!invalid.reference_path.valid,
           "cross without exit and without trusted path must not fabricate ordinary path");
}

void TestCrossExitStitchesFromExpectedNearPath() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.ordinary = ConstantCandidate(params, 0.11F);
    hypotheses.cross_exit.valid = true;
    hypotheses.cross_exit.mode = ls2k::port::ReferenceMode::kBlendToExit;
    for (std::size_t index = 5; index < hypotheses.cross_exit.sampled_path.size(); ++index) {
        hypotheses.cross_exit.sampled_path[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index], 0.24F);
    }
    ls2k::port::TopologyEvidence evidence{};
    evidence.element_evidence.valid = true;
    evidence.element_evidence.cross_band.present = true;
    evidence.element_evidence.cross_band.forward_m = params.bev_topology_sampler.forward_samples_m[2];

    const auto result = ls2k::legacy::ResolveReferencePolicy(
        hypotheses, evidence, CrossState(ls2k::port::SpecialScenePhase::kEntry), {}, params);

    Expect(result.reference_path.valid, "cross exit must build a stitched reference");
    Expect(std::abs(result.reference_path.sampled_path[0].point.lateral_m - 0.11F) < 1.0e-4F,
           "cross stitch must start from expected near path, not hard-coded zero lateral");
}

void TestCircleCandidateUsesOuterGuard() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_outer_guard_left =
        ConstantCandidate(params, 0.18F, ls2k::port::ReferenceMode::kOuterOffset);
    hypotheses.circle_inner_left =
        ConstantCandidate(params, -0.18F, ls2k::port::ReferenceMode::kStableBoundaryOffset);

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kOrdinary;
    scene.phase = ls2k::port::SpecialScenePhase::kCandidate;
    scene.candidate_scene = ls2k::port::SpecialSceneKind::kCircleLeft;

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, {}, params);
    Expect(result.reference_path.valid, "circle candidate must publish an outer guard when available");
    Expect(result.reference_path.mode == ls2k::port::ReferenceMode::kOuterOffset,
           "circle candidate must use outer-offset mode");
    Expect(std::abs(result.reference_path.sampled_path[0].point.lateral_m - 0.18F) < 1.0e-4F,
           "left circle candidate must use right-side outer guard, not inner circle");
}

void TestCircleCandidateBuildsInnerIslandMemoryButKeepsOuterGuard() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_outer_guard_left =
        ConstantCandidate(params, 0.18F, ls2k::port::ReferenceMode::kOuterOffset);
    hypotheses.circle_inner_left =
        SlopedCandidate(params, -0.18F, 0.75F, ls2k::port::ReferenceMode::kCircleInnerIsland);

    ls2k::port::TopologyEvidence evidence{};
    evidence.element_evidence.valid = true;
    evidence.element_evidence.left_inner_island = IslandEvidence(params, -0.24F);

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kOrdinary;
    scene.phase = ls2k::port::SpecialScenePhase::kCandidate;
    scene.candidate_scene = ls2k::port::SpecialSceneKind::kCircleLeft;

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, evidence, scene, {}, params);
    Expect(result.reference_path.mode == ls2k::port::ReferenceMode::kOuterOffset,
           "circle candidate must keep using outer guard after inner-island calibration");
    Expect(result.state.circle_inner_island_memory_active,
           "white-black-white inner-island evidence must establish memory");
    Expect(result.state.circle_inner_island_memory_left,
           "left island evidence must store left-side direction");
}

void TestStableBoundaryInnerCandidateCannotLatch() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_outer_guard_left =
        ConstantCandidate(params, 0.22F, ls2k::port::ReferenceMode::kOuterOffset);
    hypotheses.circle_inner_left =
        SlopedCandidate(params, -0.18F, 0.75F, ls2k::port::ReferenceMode::kStableBoundaryOffset);

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    scene.phase = ls2k::port::SpecialScenePhase::kInterior;

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, {}, params);
    Expect(result.reference_path.mode == ls2k::port::ReferenceMode::kOuterOffset,
           "generic stable-boundary inner candidate must not trigger inner follow");
    Expect(!result.state.circle_inner_latched,
           "generic stable-boundary inner candidate must not latch circle inner state");
}

void TestCircleInnerLateSwitchAndExitGate() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_outer_guard_left =
        ConstantCandidate(params, 0.18F, ls2k::port::ReferenceMode::kOuterOffset);
    hypotheses.circle_inner_left =
        SlopedCandidate(params, -0.18F, 0.75F, ls2k::port::ReferenceMode::kCircleInnerIsland);
    hypotheses.circle_exit_left =
        SlopedCandidate(params, -0.05F, 0.75F, ls2k::port::ReferenceMode::kBlendToExit);

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    scene.phase = ls2k::port::SpecialScenePhase::kInterior;

    const auto inner =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, {}, params);
    Expect(inner.reference_path.valid, "late-switch inner path must be valid");
    Expect(inner.state.circle_inner_latched, "late-switch inner path must latch");
    Expect(inner.reference_path.mode == ls2k::port::ReferenceMode::kCircleInnerIsland,
           "late-switch path must use inner-island memory mode");

    ls2k::port::ReferencePolicyState prior = inner.state;
    scene.phase = ls2k::port::SpecialScenePhase::kExit;
    scene.circle_yaw_accum_deg = params.bev_path_policy.circle_exit_yaw_deg - 20.0F;
    const auto before_yaw =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, prior, params);
    Expect(before_yaw.reference_path.mode == ls2k::port::ReferenceMode::kCircleInnerIsland,
           "circle exit must not switch before yaw gate");

    prior = before_yaw.state;
    scene.circle_yaw_accum_deg = params.bev_path_policy.circle_exit_yaw_deg + 1.0F;
    const auto exit =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, prior, params);
    Expect(exit.reference_path.mode == ls2k::port::ReferenceMode::kBlendToExit,
           "circle exit must switch after yaw and tangent gate");
    Expect(exit.state.circle_exit_latched, "circle exit must latch after switching");
}

void TestCircleInnerLatchDoesNotFallBackToOuterGuard() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_outer_guard_left =
        ConstantCandidate(params, 0.24F, ls2k::port::ReferenceMode::kOuterOffset);

    ls2k::port::ReferencePolicyState prior = TrustedPrior(params, -0.08F);
    prior.mode = ls2k::port::ReferenceMode::kStableBoundaryOffset;
    prior.circle_inner_latched = true;

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    scene.phase = ls2k::port::SpecialScenePhase::kInterior;

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, prior, params);
    Expect(result.reference_path.valid, "latched inner loss should fall back to trusted prediction");
    Expect(result.reference_path.mode == ls2k::port::ReferenceMode::kLostPrediction,
           "latched inner loss must not switch back to outer guard");
    Expect(result.state.circle_inner_latched, "inner latch must survive a missing inner candidate frame");
    Expect(std::abs(result.reference_path.sampled_path[0].point.lateral_m + 0.08F) < 1.0e-4F,
           "latched inner fallback must use trusted path geometry");
}

void TestCircleExitLatchSurvivesMissingExitCandidate() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.circle_inner_left =
        ConstantCandidate(params, -0.20F, ls2k::port::ReferenceMode::kStableBoundaryOffset);

    ls2k::port::ReferencePolicyState prior = TrustedPrior(params, -0.06F);
    prior.mode = ls2k::port::ReferenceMode::kBlendToExit;
    prior.circle_inner_latched = true;
    prior.circle_exit_latched = true;

    ls2k::port::SpecialSceneFsmState scene{};
    scene.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    scene.phase = ls2k::port::SpecialScenePhase::kExit;
    scene.circle_yaw_accum_deg = params.bev_path_policy.circle_exit_yaw_deg + 20.0F;

    const auto result =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, {}, scene, prior, params);
    Expect(result.reference_path.valid, "latched exit loss should fall back to trusted prediction");
    Expect(result.reference_path.mode == ls2k::port::ReferenceMode::kLostPrediction,
           "latched exit loss must not return to inner-follow geometry");
    Expect(result.state.circle_exit_latched, "exit latch must survive a missing exit candidate frame");
}

void TestCircleInnerUsesCompleteTracedEdgeOnly() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::LegacySteeringState prior{};
    prior.reference_policy.circle_inner_island_memory_active = true;
    prior.reference_policy.circle_inner_island_memory_left = true;
    prior.reference_policy.circle_inner_island_memory_confidence = 0.85F;
    prior.reference_policy.circle_inner_island_black_start_forward_m =
        params.bev_topology_sampler.forward_samples_m[10];
    prior.reference_policy.circle_inner_island_black_end_forward_m =
        params.bev_topology_sampler.forward_samples_m[20];
    prior.reference_policy.circle_inner_island_edge[11] =
        Sample(params.bev_topology_sampler.forward_samples_m[11], -0.24F);
    prior.reference_policy.circle_inner_island_edge[14] =
        Sample(params.bev_topology_sampler.forward_samples_m[14], -0.22F);
    prior.reference_policy.circle_inner_island_edge[19] =
        Sample(params.bev_topology_sampler.forward_samples_m[19], -0.23F);

    ls2k::port::BEVElementEvidence elements{};
    elements.valid = true;
    elements.left_inner_island.edge_present = true;
    elements.left_inner_island.trace_present = true;
    elements.left_inner_island.score = 0.35F;
    for (std::size_t index = 2; index <= 10; ++index) {
        elements.left_inner_island.road_facing_edge[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index],
                   -0.30F - 0.02F * static_cast<float>(index - 2U));
    }

    const ls2k::legacy::CorridorGraph graph{};
    const ls2k::legacy::CorridorIntervalSet intervals{};
    const ls2k::port::RoadHypotheses hypotheses =
        ls2k::legacy::GenerateRoadHypotheses(graph, intervals, prior, params, &elements);

    Expect(hypotheses.circle_inner_left.valid,
           "inner-island memory must allow current edge-only local inner path");
    Expect(hypotheses.circle_inner_left.sampled_path[2].valid &&
               hypotheses.circle_inner_left.sampled_path[10].valid,
           "complete traced inner edge must be retained by the path layer");
    const float expected_offset =
        std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.5F,
                 std::abs(params.bev_topology_sampler.lateral_step_m) * 2.0F);
    Expect(std::abs(hypotheses.circle_inner_left.sampled_path[2].point.lateral_m -
                    (-0.30F + expected_offset)) < 1.0e-4F,
           "circle inner path must be generated from the traced current edge");
}

void TestCircleInnerEdgeOnlyRequiresMemory() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::BEVElementEvidence elements{};
    elements.valid = true;
    elements.left_inner_island.edge_present = true;
    elements.left_inner_island.trace_present = true;
    elements.left_inner_island.score = 0.35F;
    for (std::size_t index = 2; index <= 5; ++index) {
        elements.left_inner_island.road_facing_edge[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index], -0.30F);
    }

    const ls2k::legacy::CorridorGraph graph{};
    const ls2k::legacy::CorridorIntervalSet intervals{};
    const ls2k::port::RoadHypotheses hypotheses =
        ls2k::legacy::GenerateRoadHypotheses(graph, intervals, {}, params, &elements);
    Expect(!hypotheses.circle_inner_left.valid,
           "current edge-only inner path must not exist before inner-island memory is calibrated");
}

ls2k::port::BEVReferencePath ReferencePath(const ls2k::port::RuntimeParameters& params,
                                           float lateral_m,
                                           bool truncate_far = false) {
    ls2k::port::BEVReferencePath path{};
    path.valid = true;
    for (std::size_t index = 0; index < path.sampled_path.size(); ++index) {
        if (truncate_far && index > 1U) {
            continue;
        }
        path.sampled_path[index] =
            Sample(params.bev_topology_sampler.forward_samples_m[index], lateral_m);
    }
    return path;
}

ls2k::port::BEVTrackEstimate TrackForControl() {
    ls2k::port::BEVTrackEstimate track{};
    track.valid = true;
    track.calibration_valid = true;
    track.visible_range_m = 1.0F;
    track.track_confidence = 1.0F;
    return track;
}

void TestTrustedErrorBlendsErrorsButNotPath() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::ControlErrorModelInput input{};
    input.track = TrackForControl();
    input.reference_path = ReferencePath(params, 0.20F);
    input.trusted_error_reference = ReferencePath(params, 0.0F);
    input.trusted_error_confidence = 1.0F;

    const auto output = ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(output.valid, "control model output must be valid");
    Expect(output.trusted_error_active, "trusted reference must activate error protection");
    Expect(std::abs(input.reference_path.sampled_path[0].point.lateral_m - 0.20F) < 1.0e-4F,
           "trusted error protection must not mutate expected reference path");
    Expect(output.raw_near_lateral_error > output.near_lateral_error,
           "trusted error must pull near lateral error toward trusted path");
    Expect(output.trusted_error_weight_near > output.trusted_error_weight_lookahead,
           "trusted error weight must be stronger near than at lookahead");
}

void TestTrustedMissingForwardLeavesRawTerm() {
    ls2k::port::RuntimeParameters params{};
    ls2k::port::ControlErrorModelInput input{};
    input.track = TrackForControl();
    input.reference_path = ReferencePath(params, 0.20F);
    input.trusted_error_reference = ReferencePath(params, 0.0F, true);
    input.trusted_error_confidence = 1.0F;

    const auto output = ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(output.valid, "control model output must stay valid with short trusted path");
    Expect(output.trusted_error_weight_near > 0.0F,
           "short trusted path should still protect near error");
    Expect(output.trusted_error_weight_lookahead == 0.0F,
           "missing trusted lookahead interpolation must not create a fallback geometry");
    Expect(std::abs(output.lookahead_lateral_error - output.raw_lookahead_lateral_error) < 1.0e-5F,
           "missing trusted lookahead must leave raw lookahead error unchanged");
}

}  // namespace

int main() {
    try {
        TestTrustedDoesNotAlterExpectedPath();
        TestCrossNoExitUsesTrustedHoldOnly();
        TestCrossExitStitchesFromExpectedNearPath();
        TestCircleCandidateUsesOuterGuard();
        TestCircleCandidateBuildsInnerIslandMemoryButKeepsOuterGuard();
        TestStableBoundaryInnerCandidateCannotLatch();
        TestCircleInnerLateSwitchAndExitGate();
        TestCircleInnerLatchDoesNotFallBackToOuterGuard();
        TestCircleExitLatchSurvivesMissingExitCandidate();
        TestCircleInnerUsesCompleteTracedEdgeOnly();
        TestCircleInnerEdgeOnlyRequiresMemory();
        TestTrustedErrorBlendsErrorsButNotPath();
        TestTrustedMissingForwardLeavesRawTerm();
    } catch (const TestFailure& failure) {
        std::cerr << "reference_policy_control_error_test failed: " << failure.message << "\n";
        return 1;
    }
    std::cout << "reference_policy_control_error_test passed\n";
    return 0;
}
