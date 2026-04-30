#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/camera_logic.hpp"
#include "legacy/pid_control.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_control_error_model.hpp"
#include "legacy/steering_corridor_intervals.hpp"
#include "legacy/steering_reference_policy.hpp"
#include "legacy/steering_scene_fsm.hpp"
#include "legacy/steering_topology_evidence.hpp"
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

bool NearlyEqual(float left, float right, float tolerance = 1e-4F) {
    return std::fabs(left - right) <= tolerance;
}

ls2k::port::RuntimeParameters MakeParams() {
    ls2k::port::RuntimeParameters params{};
    params.Speed_base = 77.0;
    params.bev_geometry.nominal_lane_width_m = 0.42F;
    params.bev_corridor_graph.nominal_lane_width_m = params.bev_geometry.nominal_lane_width_m;
    params.bev_corridor_graph.max_curvature_abs = 0.90F;
    params.bev_topology_evidence.cross_enter_score = 0.70F;
    params.bev_topology_evidence.cross_release_score = 0.35F;
    params.bev_topology_evidence.circle_enter_score = 0.70F;
    params.bev_topology_evidence.circle_release_score = 0.35F;
    params.bev_topology_evidence.zebra_enter_score = 0.70F;
    params.bev_topology_evidence.zebra_release_score = 0.35F;
    params.bev_topology_evidence.ordinary_release_score = 0.75F;
    params.bev_scene_fsm.cross_confirm_cycles = 2;
    params.bev_scene_fsm.cross_hold_cycles = 3;
    params.bev_scene_fsm.circle_confirm_cycles = 2;
    params.bev_scene_fsm.circle_release_cycles = 2;
    params.bev_scene_fsm.zebra_hold_cycles = 3;
    params.bev_reference_policy.hold_last_max_cycles = 2;
    params.bev_reference_policy.blend_min_cycles = 2;
    params.bev_reference_policy.arc_follow_confidence_min = 0.55F;
    params.bev_control_model.low_confidence_threshold = 0.35;
    params.bev_control_model.steering_suppression_confidence = 0.12;
    params.bev_control_model.low_visible_range_m = 0.108444;
    params.bev_control_model.min_gain_scale = 0.25;
    params.bev_control_model.min_speed_limit_scale = 0.35;
    return params;
}

ls2k::port::PathCandidate MakePath(const ls2k::port::RuntimeParameters& params,
                                   float lateral_bias_m = 0.0F,
                                   float heading_slope = 0.0F,
                                   float curvature = 0.0F,
                                   float confidence = 0.92F) {
    ls2k::port::PathCandidate candidate{};
    candidate.valid = true;
    candidate.mode = ls2k::port::ReferenceMode::kCenterline;
    candidate.confidence = confidence;
    candidate.mean_width_m = params.bev_corridor_graph.nominal_lane_width_m;
    candidate.width_stability = 0.95F;
    candidate.curvature = curvature;
    candidate.curvature_consistency =
        std::clamp(1.0F - std::abs(curvature) /
                             std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs),
                   0.0F,
                   1.0F);
    candidate.start_forward_m = params.bev_geometry.forward_samples_m.front();
    candidate.end_forward_m = params.bev_geometry.forward_samples_m.back();

    const float origin = params.bev_geometry.forward_samples_m.front();
    for (std::size_t index = 0; index < candidate.sampled_path.size(); ++index) {
        const float forward = params.bev_geometry.forward_samples_m[index];
        const float distance = forward - origin;
        candidate.sampled_path[index].valid = true;
        candidate.sampled_path[index].point.forward_m = forward;
        candidate.sampled_path[index].point.lateral_m =
            lateral_bias_m + heading_slope * distance + 0.5F * curvature * distance * distance;
        candidate.sampled_path[index].confidence = confidence;
    }
    return candidate;
}

ls2k::port::PathCandidate ShiftPath(ls2k::port::PathCandidate candidate,
                                    ls2k::port::ReferenceMode mode,
                                    float lateral_offset_m,
                                    float confidence_scale = 0.90F) {
    candidate.mode = mode;
    candidate.confidence = std::clamp(candidate.confidence * confidence_scale, 0.0F, 1.0F);
    for (ls2k::port::BEVPathSample& sample : candidate.sampled_path) {
        if (sample.valid) {
            sample.point.lateral_m += lateral_offset_m;
            sample.confidence = std::clamp(sample.confidence * confidence_scale, 0.0F, 1.0F);
        }
    }
    return candidate;
}

ls2k::port::RoadHypotheses MakeHypotheses(const ls2k::port::RuntimeParameters& params,
                                          float curvature = 0.0F) {
    ls2k::port::RoadHypotheses hypotheses{};
    hypotheses.ordinary = MakePath(params, 0.0F, 0.0F, curvature);
    hypotheses.forward_exit = hypotheses.ordinary;
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    hypotheses.left_arc =
        ShiftPath(hypotheses.ordinary, ls2k::port::ReferenceMode::kArcFollow, -half_width);
    hypotheses.right_arc =
        ShiftPath(hypotheses.ordinary, ls2k::port::ReferenceMode::kArcFollow, half_width);
    hypotheses.circle_inner_left = hypotheses.left_arc;
    hypotheses.circle_inner_left.mode = ls2k::port::ReferenceMode::kStableBoundaryOffset;
    hypotheses.circle_inner_right = hypotheses.right_arc;
    hypotheses.circle_inner_right.mode = ls2k::port::ReferenceMode::kStableBoundaryOffset;
    hypotheses.circle_outer_guard_left = hypotheses.right_arc;
    hypotheses.circle_outer_guard_left.mode = ls2k::port::ReferenceMode::kOuterOffset;
    hypotheses.circle_outer_guard_right = hypotheses.left_arc;
    hypotheses.circle_outer_guard_right.mode = ls2k::port::ReferenceMode::kOuterOffset;
    hypotheses.circle_exit_left = hypotheses.forward_exit;
    hypotheses.circle_exit_left.mode = ls2k::port::ReferenceMode::kBlendToExit;
    hypotheses.circle_exit_right = hypotheses.circle_exit_left;
    hypotheses.zebra_hold = hypotheses.ordinary;
    hypotheses.zebra_hold.mode = ls2k::port::ReferenceMode::kHoldLast;
    return hypotheses;
}

ls2k::port::CorridorInterval MakeInterval(const ls2k::port::RuntimeParameters& params,
                                          std::size_t index,
                                          float left_m,
                                          float right_m,
                                          float confidence = 0.90F) {
    ls2k::port::CorridorInterval interval{};
    interval.forward_m = params.bev_geometry.forward_samples_m[index];
    interval.lateral_min_m = left_m;
    interval.lateral_max_m = right_m;
    interval.lateral_center_m = (left_m + right_m) * 0.5F;
    interval.width_m = right_m - left_m;
    interval.left_edge_valid = true;
    interval.right_edge_valid = true;
    interval.valid_sample_ratio = 1.0F;
    interval.confidence = confidence;
    return interval;
}

ls2k::legacy::CorridorIntervalSet MakeOpeningIntervals(
    const ls2k::port::RuntimeParameters& params,
    float left_open_m,
    float right_open_m) {
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    const float denominator = static_cast<float>(ls2k::port::kBevTrackSampleCount - 1U);
    for (std::size_t index = 0; index < intervals.layers.size(); ++index) {
        const float ratio = denominator > 0.0F ? static_cast<float>(index) / denominator : 0.0F;
        intervals.layers[index].forward_m = params.bev_geometry.forward_samples_m[index];
        intervals.layers[index].intervals.push_back(
            MakeInterval(params, index, -half_width - left_open_m * ratio, half_width + right_open_m * ratio));
    }
    return intervals;
}

void SetIntervalEdgeValidity(ls2k::legacy::CorridorIntervalSet& intervals,
                             bool left_edge_valid,
                             bool right_edge_valid) {
    for (ls2k::legacy::CorridorIntervalLayer& layer : intervals.layers) {
        for (ls2k::port::CorridorInterval& interval : layer.intervals) {
            interval.left_edge_valid = left_edge_valid;
            interval.right_edge_valid = right_edge_valid;
        }
    }
}

void SetLayerEdgeValidity(ls2k::legacy::CorridorIntervalSet& intervals,
                          std::size_t layer_index,
                          bool left_edge_valid,
                          bool right_edge_valid) {
    if (layer_index >= intervals.layers.size()) {
        return;
    }
    for (ls2k::port::CorridorInterval& interval : intervals.layers[layer_index].intervals) {
        interval.left_edge_valid = left_edge_valid;
        interval.right_edge_valid = right_edge_valid;
    }
}

ls2k::legacy::CorridorIntervalSet MakeZebraIntervals(
    const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.55F;
    for (std::size_t index = 0; index < intervals.layers.size(); ++index) {
        intervals.layers[index].forward_m = params.bev_geometry.forward_samples_m[index];
        if (index < 8U && index % 2U == 0U) {
            intervals.layers[index].intervals.push_back(
                MakeInterval(params, index, -half_width, half_width, 0.95F));
        }
    }
    return intervals;
}

ls2k::port::TopologyEvidence ScoreEvidence(
    const ls2k::port::RoadHypotheses& hypotheses,
    const ls2k::legacy::CorridorIntervalSet& intervals,
    const ls2k::port::RuntimeParameters& params) {
    return ls2k::legacy::ScoreTopologyEvidence(hypotheses, intervals, {}, params, {});
}

bool PathsMatch(
    const std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& left,
    const std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& right,
    float tolerance = 1e-4F) {
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].valid != right[index].valid) {
            return false;
        }
        if (!left[index].valid) {
            continue;
        }
        if (!NearlyEqual(left[index].point.forward_m, right[index].point.forward_m, tolerance) ||
            !NearlyEqual(left[index].point.lateral_m, right[index].point.lateral_m, tolerance)) {
            return false;
        }
    }
    return true;
}

ls2k::port::ReferencePolicyState MakeHeldReference(const ls2k::port::PathCandidate& path,
                                                   float lateral_shift_m) {
    ls2k::port::ReferencePolicyState state{};
    state.valid = true;
    state.mode = ls2k::port::ReferenceMode::kHoldLast;
    state.last_reference = path.sampled_path;
    for (ls2k::port::BEVPathSample& sample : state.last_reference) {
        if (sample.valid) {
            sample.point.lateral_m += lateral_shift_m;
        }
    }
    return state;
}

ls2k::port::BEVTrackEstimate MakeTrackFromPath(const ls2k::port::RuntimeParameters& params,
                                               const ls2k::port::PathCandidate& path) {
    ls2k::port::BEVTrackEstimate track{};
    track.valid = path.valid;
    track.calibration_valid = true;
    track.continuity_valid = path.valid;
    track.track_confidence = path.confidence;
    track.visible_range_m = path.end_forward_m;
    track.source = "bev_corridor_topology";
    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    for (std::size_t index = 0; index < path.sampled_path.size(); ++index) {
        if (!path.sampled_path[index].valid) {
            continue;
        }
        track.sampled_centerline[index] = path.sampled_path[index];
        track.sampled_left_boundary[index] = path.sampled_path[index];
        track.sampled_left_boundary[index].point.lateral_m -= half_width;
        track.sampled_right_boundary[index] = path.sampled_path[index];
        track.sampled_right_boundary[index].point.lateral_m += half_width;
        track.sampled_drivable_left_boundary[index] = track.sampled_left_boundary[index];
        track.sampled_drivable_right_boundary[index] = track.sampled_right_boundary[index];
        track.lane_width_profile_m[index] = params.bev_corridor_graph.nominal_lane_width_m;
        track.drivable_width_profile_m[index] = params.bev_corridor_graph.nominal_lane_width_m;
    }
    const int near_index = std::clamp(
        params.bev_control_model.near_sample_index, 0, static_cast<int>(ls2k::port::kBevTrackSampleCount) - 1);
    const int far_index = std::clamp(
        params.bev_control_model.far_sample_index, near_index, static_cast<int>(ls2k::port::kBevTrackSampleCount) - 1);
    if (track.sampled_centerline[static_cast<std::size_t>(near_index)].valid) {
        track.near_lateral_error =
            track.sampled_centerline[static_cast<std::size_t>(near_index)].point.lateral_m;
    }
    if (track.sampled_centerline[static_cast<std::size_t>(near_index)].valid &&
        track.sampled_centerline[static_cast<std::size_t>(far_index)].valid) {
        const auto& near_sample = track.sampled_centerline[static_cast<std::size_t>(near_index)];
        const auto& far_sample = track.sampled_centerline[static_cast<std::size_t>(far_index)];
        track.far_heading_error =
            std::atan2(far_sample.point.lateral_m - near_sample.point.lateral_m,
                       far_sample.point.forward_m - near_sample.point.forward_m);
    }
    track.preview_curvature = path.curvature;
    return track;
}

void PaintWhiteRun(ls2k::port::LegacyCameraFrame& frame, int row, int left_col, int right_col) {
    if (row < 0 || row >= frame.height) {
        return;
    }
    left_col = std::clamp(left_col, 0, std::max(0, frame.width - 1));
    right_col = std::clamp(right_col, 0, std::max(0, frame.width - 1));
    if (left_col > right_col) {
        return;
    }
    for (int col = left_col; col <= right_col; ++col) {
        frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                   static_cast<std::size_t>(col)] = 220U;
    }
}

void PaintWhiteBand(ls2k::port::LegacyCameraFrame& frame, int center_row, int left_col, int right_col) {
    for (int row = center_row - 1; row <= center_row + 1; ++row) {
        PaintWhiteRun(frame, row, left_col, right_col);
    }
}

ls2k::port::LegacyCameraFrame MakeZebraFrame(const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::BEVProjector projector;
    Expect(projector.Configure(params.bev_projector), "default BEV projector calibration must configure");

    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(0U);
    for (std::size_t index = 0; index < 8U; index += 2U) {
        ls2k::port::ImagePoint image{};
        if (!projector.ProjectVehicleToImage({params.bev_topology_sampler.forward_samples_m[index], 0.0F},
                                             image)) {
            continue;
        }
        const int row = std::clamp(static_cast<int>(std::lround(image.row_px)),
                                   0,
                                   std::max(0, frame.height - 1));
        PaintWhiteBand(frame, row, 0, frame.width - 1);
    }
    return frame;
}

void TestBendRemainsOrdinaryTopology() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::RoadHypotheses hypotheses =
        MakeHypotheses(params, params.bev_corridor_graph.max_curvature_abs * 0.65F);
    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.0F, 0.0F), params);

    const ls2k::legacy::SceneFsmResult result =
        ls2k::legacy::UpdateTopologySceneFsm(evidence, params, {});
    Expect(evidence.bend_veto_score > 0.50F, "curved ordinary corridor must expose bend veto evidence");
    Expect(result.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "bend evidence must not become a formal topology scene");
    Expect(result.active_module == "straight", "ordinary bend must keep the compatible straight module");
}

void TestLargeBendVetoesCircleMisclassification() {
    ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::port::RoadHypotheses hypotheses =
        MakeHypotheses(params, params.bev_corridor_graph.max_curvature_abs);
    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.26F, 0.0F), params);

    Expect(evidence.left_opening_score > 0.20F, "test must exercise one-sided opening evidence");
    Expect(evidence.left_circle_score < params.bev_topology_evidence.circle_enter_score,
           "large ordinary curvature must veto circle evidence");
    const ls2k::legacy::SceneFsmResult first =
        ls2k::legacy::UpdateTopologySceneFsm(evidence, params, {});
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateTopologySceneFsm(evidence, params, first.state);
    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "large bend must not confirm as circle");
}

void TestCrossRequiresBilateralOpeningAndReacquires() {
    ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    const ls2k::port::TopologyEvidence one_sided =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.26F, 0.0F), params);
    Expect(one_sided.cross_score == 0.0F,
           "cross evidence must reject one-sided opening");

    const float below_cross_opening = params.bev_scene_fsm.cross_bilateral_open_min_m * 0.5F;
    const ls2k::port::TopologyEvidence below_gate =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, below_cross_opening, below_cross_opening), params);
    Expect(below_gate.cross_score == 0.0F,
           "cross evidence must reject bilateral opening below the configured metric gate");

    ls2k::port::RoadHypotheses weak_reacquire = hypotheses;
    weak_reacquire.forward_exit.confidence = 0.40F;
    const ls2k::port::TopologyEvidence weak_forward =
        ScoreEvidence(weak_reacquire, MakeOpeningIntervals(params, 0.26F, 0.26F), params);
    Expect(weak_forward.forward_reacquire_score == 0.0F,
           "test must exercise cross evidence before ordinary forward reacquire");
    Expect(weak_forward.cross_score >= params.bev_topology_evidence.cross_enter_score,
           "bilateral opening must still confirm cross scene before a usable exit reference exists");

    const ls2k::port::TopologyEvidence cross =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.26F, 0.26F), params);
    Expect(cross.cross_score >= params.bev_topology_evidence.cross_enter_score,
           "bilateral opening with forward reacquire must remain cross evidence");

    const ls2k::legacy::SceneFsmResult first =
        ls2k::legacy::UpdateTopologySceneFsm(cross, params, {});
    Expect(first.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "cross must wait for confirm cycles");
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateTopologySceneFsm(cross, params, first.state);
    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kCross,
           "confirmed bilateral cross must become active");
    Expect(second.scene_phase == "cross_approach", "confirmed cross must begin with approach phase");

    const ls2k::legacy::SceneFsmResult hold =
        ls2k::legacy::UpdateTopologySceneFsm(cross, params, second.state);
    Expect(hold.scene_phase == "cross_hold", "cross must progress into hold phase");

    ls2k::port::TopologyEvidence ordinary{};
    ordinary.ordinary_score = 0.90F;
    const ls2k::legacy::SceneFsmResult reacquire =
        ls2k::legacy::UpdateTopologySceneFsm(ordinary, params, hold.state);
    Expect(reacquire.scene_phase == "cross_reacquire", "cross release must expose reacquire phase");
    const ls2k::legacy::SceneFsmResult released =
        ls2k::legacy::UpdateTopologySceneFsm(ordinary, params, reacquire.state);
    Expect(released.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "ordinary reacquire must release cross state");
}

void TestCrossWithoutExitUsesTrustedHoldOnly() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::PathCandidate entry_path = MakePath(params, 0.02F, 0.18F, 0.0F);
    const ls2k::port::ReferencePolicyState held = MakeHeldReference(entry_path, 0.0F);
    ls2k::port::RoadHypotheses no_graph_hypotheses{};
    ls2k::port::TopologyEvidence cross{};
    cross.cross_score = 1.0F;
    ls2k::port::SpecialSceneFsmState cross_entry{};
    cross_entry.active_scene = ls2k::port::SpecialSceneKind::kCross;
    cross_entry.phase = ls2k::port::SpecialScenePhase::kEntry;

    const ls2k::legacy::ReferencePolicyResult extension =
        ls2k::legacy::ResolveReferencePolicy(no_graph_hypotheses, cross, cross_entry, held, params);
    Expect(extension.reference_path.valid,
           "cross without exit must hold a trusted entrance reference when available");
    Expect(extension.reference_path.mode == ls2k::port::ReferenceMode::kHoldLast,
           "cross without exit must not synthesize an entry-heading extension");
    Expect(PathsMatch(extension.reference_path.sampled_path, held.last_reference),
           "cross trusted hold must preserve the carried reference geometry");

    const ls2k::legacy::ReferencePolicyResult no_reference =
        ls2k::legacy::ResolveReferencePolicy(no_graph_hypotheses, cross, cross_entry, {}, params);
    Expect(!no_reference.reference_path.valid,
           "cross must not invent a controllable reference without prior entrance or ordinary topology");
}

void TestShortOrdinaryRangeWeakensTopologyEvidence() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    hypotheses.ordinary.end_forward_m = params.bev_geometry.forward_samples_m[7];
    hypotheses.forward_exit = hypotheses.ordinary;

    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.0F, 0.0F), params);

    Expect(evidence.ordinary_score < 0.35F,
           "short ordinary coverage must not look like strong topology safety evidence");
    Expect(evidence.lost_score > 0.65F,
           "short ordinary coverage must raise lost topology evidence");
}

void TestSubthresholdSpecialEvidenceDoesNotHideLostTopology() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    hypotheses.ordinary.confidence = 0.32F;
    hypotheses.forward_exit = hypotheses.ordinary;

    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.12F, 0.0F), params);

    Expect(evidence.left_circle_score > 0.0F &&
               evidence.left_circle_score < params.bev_topology_evidence.circle_enter_score,
           "test must exercise a visible but unconfirmed circle-like topology signal");
    Expect(evidence.lost_score > 0.65F,
           "subthreshold special-scene evidence must not mask lost topology");
}

void TestCurvatureVetoedCircleOpeningStillRaisesLostTopology() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::port::RoadHypotheses hypotheses =
        MakeHypotheses(params, params.bev_corridor_graph.max_curvature_abs * 0.65F);
    hypotheses.ordinary.confidence = 0.58F;
    hypotheses.forward_exit = hypotheses.ordinary;

    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.26F, 0.0F), params);

    Expect(evidence.left_circle_score > 0.0F &&
               evidence.left_circle_score < params.bev_topology_evidence.circle_enter_score,
           "curvature-vetoed circle-like opening must remain below formal circle entry");
    Expect(evidence.ordinary_score < params.bev_topology_evidence.ordinary_release_score,
           "fixture must exercise unresolved ordinary topology");
    Expect(evidence.lost_score > 0.65F,
           "curvature-vetoed circle-like opening must still raise lost topology pressure");
}

void TestUnobservedEdgesRaiseLostTopology() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    ls2k::legacy::CorridorIntervalSet intervals = MakeOpeningIntervals(params, 0.0F, 0.0F);
    SetIntervalEdgeValidity(intervals, false, false);

    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, intervals, params);

    Expect(evidence.ordinary_score > 0.0F,
           "edge-clipped support may still expose weak ordinary topology evidence");
    Expect(evidence.lost_score > 0.65F,
           "edge-clipped support must still raise lost topology safety evidence");
}

void TestSingleSidedNearAnchorStraightTrackWeakensOrdinaryEvidence() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    const ls2k::port::TopologyEvidence bilateral =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.12F, 0.0F), params);
    ls2k::legacy::CorridorIntervalSet intervals = MakeOpeningIntervals(params, 0.12F, 0.0F);
    SetLayerEdgeValidity(intervals,
                         static_cast<std::size_t>(params.bev_control_model.near_sample_index),
                         true,
                         false);

    const ls2k::port::TopologyEvidence evidence =
        ScoreEvidence(hypotheses, intervals, params);

    Expect(evidence.left_circle_score > 0.0F &&
               evidence.left_circle_score < params.bev_topology_evidence.circle_enter_score,
           "single-sided near-anchor fixture must remain an unresolved circle-like topology");
    Expect(evidence.ordinary_score < bilateral.ordinary_score,
           "single-sided near anchor must weaken ordinary topology trust versus bilateral support");
    Expect(evidence.lost_score > bilateral.lost_score,
           "single-sided near anchor must increase lost topology pressure versus bilateral support");
}

void TestCircleProgressionAndDirectionLatch() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    const ls2k::port::TopologyEvidence left_circle =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.26F, 0.0F), params);
    Expect(left_circle.left_circle_score >= params.bev_topology_evidence.circle_enter_score,
           "left circle evidence must be strong enough to enter");

    const ls2k::legacy::SceneFsmResult first =
        ls2k::legacy::UpdateTopologySceneFsm(left_circle, params, {});
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateTopologySceneFsm(left_circle, params, first.state);
    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kCircleLeft,
           "circle must confirm after configured cycles");
    Expect(second.scene_phase == "circle_entry", "circle must start in entry phase");
    Expect(second.state.circle_direction == "left", "confirmed left circle must latch direction");

    ls2k::port::RoadHypotheses entry_hypotheses = hypotheses;
    entry_hypotheses.circle_inner_left = {};
    const ls2k::legacy::ReferencePolicyResult entry_reference =
        ls2k::legacy::ResolveReferencePolicy(entry_hypotheses, left_circle, second.state, {}, params);
    Expect(entry_reference.reference_path.mode == ls2k::port::ReferenceMode::kOuterOffset,
           "circle entry without an allowed inner path must use the outer guard reference");
    Expect(!PathsMatch(entry_reference.reference_path.sampled_path, hypotheses.ordinary.sampled_path),
           "circle entry guard reference must differ from ordinary centerline");

    ls2k::legacy::SceneFsmResult state =
        ls2k::legacy::UpdateTopologySceneFsm(left_circle, params, second.state);
    state = ls2k::legacy::UpdateTopologySceneFsm(left_circle, params, state.state);
    Expect(state.scene_phase == "circle_interior", "circle must progress into interior");

    const ls2k::port::TopologyEvidence right_circle =
        ScoreEvidence(hypotheses, MakeOpeningIntervals(params, 0.0F, 0.26F), params);
    const ls2k::legacy::SceneFsmResult opposite =
        ls2k::legacy::UpdateTopologySceneFsm(right_circle, params, state.state);
    Expect(opposite.state.active_scene == ls2k::port::SpecialSceneKind::kCircleLeft,
           "latched circle direction must not flip on one opposite frame");
    Expect(opposite.state.circle_direction == "left",
           "circle direction latch must retain the entry direction");

    ls2k::port::TopologyEvidence ordinary{};
    ordinary.ordinary_score = 0.90F;
    const ls2k::legacy::SceneFsmResult exit =
        ls2k::legacy::UpdateTopologySceneFsm(ordinary, params, opposite.state);
    Expect(exit.scene_phase == "circle_exit", "lost circle evidence must progress to exit");
    const ls2k::legacy::SceneFsmResult released =
        ls2k::legacy::UpdateTopologySceneFsm(ordinary, params, exit.state);
    Expect(released.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "ordinary reacquire must release circle state");
}

void TestCircleInteriorRejectsStableBoundaryOffsetWithoutInnerIslandMemory() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::legacy::CorridorGraph graph{};
    const ls2k::legacy::CorridorIntervalSet intervals = MakeOpeningIntervals(params, 0.26F, 0.0F);
    const ls2k::port::RoadHypotheses hypotheses =
        ls2k::legacy::GenerateRoadHypotheses(graph, intervals, {}, params);
    Expect(!hypotheses.ordinary.valid,
           "fixture must not rely on an ordinary graph path");
    Expect(hypotheses.left_arc.valid,
           "observed boundary continuity must produce a circle interior reference candidate");
    Expect(hypotheses.left_arc.mode == ls2k::port::ReferenceMode::kStableBoundaryOffset,
           "boundary-derived circle hypothesis must preserve stable-boundary offset semantics");

    ls2k::port::TopologyEvidence left_circle{};
    left_circle.left_circle_score = 1.0F;
    ls2k::port::SpecialSceneFsmState circle_state{};
    circle_state.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    circle_state.phase = ls2k::port::SpecialScenePhase::kInterior;

    const ls2k::legacy::ReferencePolicyResult reference =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, left_circle, circle_state, {}, params);
    Expect(reference.reference_path.valid,
           "circle interior without inner-island memory must remain controllable by outer guard");
    Expect(reference.reference_path.mode == ls2k::port::ReferenceMode::kOuterOffset,
           "circle interior must not use generic stable-boundary offset as an inner-circle path");

    const ls2k::legacy::ReferencePolicyResult no_boundary =
        ls2k::legacy::ResolveReferencePolicy({}, left_circle, circle_state, {}, params);
    Expect(!no_boundary.reference_path.valid,
           "circle must not invent a reference when no arc, stable boundary, exit, or prior prediction exists");
}

void TestArcHypothesesUseBevNormalOffset() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::legacy::CorridorGraph graph{};
    graph.valid = true;
    graph.ordinary = MakePath(params, 0.0F, 0.60F, 0.0F);

    ls2k::legacy::CorridorIntervalSet intervals{};
    const ls2k::port::RoadHypotheses hypotheses =
        ls2k::legacy::GenerateRoadHypotheses(graph, intervals, {}, params);

    constexpr std::size_t kLayer = 5U;
    const float offset = -params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    const float tangent_length = std::sqrt(1.0F + 0.60F * 0.60F);
    const ls2k::port::BEVPathSample& base = graph.ordinary.sampled_path[kLayer];
    const ls2k::port::BEVPathSample& left_arc = hypotheses.left_arc.sampled_path[kLayer];
    const float expected_lateral = base.point.lateral_m + tangent_length * offset;
    const float lateral_only = base.point.lateral_m + offset;

    Expect(hypotheses.left_arc.valid, "left arc hypothesis must remain valid");
    Expect(NearlyEqual(left_arc.point.forward_m, base.point.forward_m),
           "arc-follow reference must be normalized back onto the configured forward layer");
    Expect(NearlyEqual(left_arc.point.lateral_m, expected_lateral),
           "arc-follow reference must preserve the BEV-normal offset curve after normalization");
    Expect(std::abs(left_arc.point.lateral_m - lateral_only) > 0.02F,
           "arc-follow reference must not collapse to lateral-only shifting on a sloped path");
}

void TestExitAndBoundaryArcHypothesesDoNotRequireOrdinaryGraph() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::legacy::CorridorGraph graph{};
    ls2k::legacy::CorridorIntervalSet intervals{};
    intervals.valid = true;
    for (std::size_t index = 5U; index < intervals.layers.size(); ++index) {
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        const float center = 0.04F + static_cast<float>(index - 5U) * 0.025F;
        intervals.layers[index].forward_m = forward;
        intervals.layers[index].intervals.push_back(
            MakeInterval(params,
                         index,
                         center - params.bev_corridor_graph.nominal_lane_width_m * 0.5F,
                         center + params.bev_corridor_graph.nominal_lane_width_m * 0.5F,
                         0.86F));
    }

    const ls2k::port::RoadHypotheses hypotheses =
        ls2k::legacy::GenerateRoadHypotheses(graph, intervals, {}, params);

    Expect(!hypotheses.ordinary.valid,
           "fixture must not depend on a valid ordinary graph");
    Expect(hypotheses.forward_exit.valid,
           "far observed corridor intervals must produce a forward-exit hypothesis");
    Expect(hypotheses.left_arc.valid && hypotheses.right_arc.valid,
           "continuous observed boundaries must produce circle reference hypotheses even without ordinary control anchor");
    Expect(hypotheses.left_arc.mode == ls2k::port::ReferenceMode::kStableBoundaryOffset &&
               hypotheses.right_arc.mode == ls2k::port::ReferenceMode::kStableBoundaryOffset,
           "boundary-only hypotheses must be labelled as stable-boundary offset");
    Expect(hypotheses.forward_exit.sampled_path[5].valid,
           "forward-exit hypothesis must preserve far-layer evidence instead of backfilling a fake near anchor");
}

void TestZebraHoldAndConstraintsUseTopologyPath() {
    ls2k::port::RuntimeParameters params = MakeParams();
    params.bev_topology_sampler.lateral_min_m = -0.45F;
    params.bev_topology_sampler.lateral_max_m = 0.45F;
    const ls2k::port::TopologyEvidence zebra =
        ScoreEvidence({}, MakeZebraIntervals(params), params);
    Expect(zebra.zebra_score >= params.bev_topology_evidence.zebra_enter_score,
           "BEV-local alternating intervals must produce zebra evidence");

    const ls2k::legacy::SceneFsmResult scene =
        ls2k::legacy::UpdateTopologySceneFsm(zebra, params, {});
    Expect(scene.state.active_scene == ls2k::port::SpecialSceneKind::kZebra,
           "zebra evidence must enter zebra hold through FSM");
    Expect(scene.scene_phase == "zebra_hold", "zebra FSM phase must be zebra_hold");

    ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    const ls2k::port::ReferencePolicyState held =
        MakeHeldReference(hypotheses.ordinary, 0.10F);
    const ls2k::legacy::ReferencePolicyResult reference =
        ls2k::legacy::ResolveReferencePolicy(hypotheses, zebra, scene.state, held, params);
    Expect(reference.reference_path.mode == ls2k::port::ReferenceMode::kHoldLast,
           "zebra hold must express behavior through held reference");
    Expect(PathsMatch(reference.reference_path.sampled_path, held.last_reference),
           "zebra held reference must come from reference policy memory");

    ls2k::port::LegacySteeringState prior{};
    prior.reference_policy = held;
    const ls2k::port::LegacyCameraFrame frame = MakeZebraFrame(params);
    const ls2k::legacy::SteeringAnalysisResult analysis =
        ls2k::legacy::AnalyzeFrame(frame, params, prior, {}, false, 21, 34);
    Expect(analysis.steering_state_update_valid,
           "runtime synthetic zebra fixture must remain analyzable through the shared pipeline");
}

void TestLostPredictionExpires() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::PathCandidate last_path = MakePath(params);
    ls2k::port::ReferencePolicyState prior = MakeHeldReference(last_path, -0.08F);
    ls2k::port::TopologyEvidence lost{};
    lost.lost_score = 1.0F;
    const ls2k::legacy::SceneFsmResult scene =
        ls2k::legacy::UpdateTopologySceneFsm(lost, params, {});
    Expect(scene.scene_phase == "lost_prediction", "lost topology must enter lost prediction phase");

    const ls2k::legacy::ReferencePolicyResult prediction =
        ls2k::legacy::ResolveReferencePolicy({}, lost, scene.state, prior, params);
    Expect(prediction.reference_path.mode == ls2k::port::ReferenceMode::kLostPrediction,
           "lost scene must reuse recent reference for bounded prediction");
    Expect(PathsMatch(prediction.reference_path.sampled_path, prior.last_reference),
           "lost prediction must use carried reference path");

    prior.lost_prediction_cycles = params.bev_reference_policy.hold_last_max_cycles;
    const ls2k::legacy::ReferencePolicyResult expired =
        ls2k::legacy::ResolveReferencePolicy({}, lost, scene.state, prior, params);
    Expect(!expired.reference_path.valid,
           "lost prediction must expire after configured hold cycles");
}

void TestRuntimeLostPredictionReferenceCanSupportControlBeforeExpiry() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::PathCandidate last_path = MakePath(params, 0.04F);
    ls2k::port::LegacySteeringState prior{};
    prior.reference_policy = MakeHeldReference(last_path, 0.0F);

    ls2k::port::LegacyCameraFrame blank{};
    blank.width = ls2k::port::kCompiledCameraFrameWidth;
    blank.height = ls2k::port::kCompiledCameraFrameHeight;
    blank.gray.fill(0U);

    const ls2k::legacy::SteeringAnalysisResult prediction =
        ls2k::legacy::AnalyzeFrame(blank, params, prior, {}, false, 31, 55);
    Expect(prediction.reference_path.mode == ls2k::port::ReferenceMode::kLostPrediction,
           "runtime lost frame must use reference-policy prediction while it is fresh");
    Expect(prediction.control_output.valid,
           "fresh lost prediction must provide a reference-support track to the control model");
    Expect(!prediction.control_output.steering_suppressed,
           "fresh lost prediction must not be suppressed before the configured hold expiry");

    prior.reference_policy.lost_prediction_cycles = params.bev_reference_policy.hold_last_max_cycles;
    prior.lost_prediction_cycles = params.bev_reference_policy.hold_last_max_cycles;
    const ls2k::legacy::SteeringAnalysisResult expired =
        ls2k::legacy::AnalyzeFrame(blank, params, prior, {}, false, 32, 60);
    Expect(!expired.reference_path.valid,
           "runtime lost prediction must stop carrying reference after hold expiry");
    Expect(expired.control_output.steering_suppressed,
           "expired lost prediction must suppress steering through the standard control output");
}

void TestSceneOnlyAffectsControllerThroughReferenceOrConstraints() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::RoadHypotheses hypotheses = MakeHypotheses(params);
    const ls2k::port::BEVTrackEstimate track = MakeTrackFromPath(params, hypotheses.ordinary);
    ls2k::port::BEVReferencePath ordinary_reference{};
    ordinary_reference.valid = true;
    ordinary_reference.mode = ls2k::port::ReferenceMode::kCenterline;
    ordinary_reference.sampled_path = hypotheses.ordinary.sampled_path;

    ls2k::port::ControlErrorModelInput ordinary_input{};
    ordinary_input.track = track;
    ordinary_input.reference_path = ordinary_reference;
    const ls2k::port::ControlErrorModelOutput ordinary_control =
        ls2k::legacy::ComputeControlErrorModel(ordinary_input, params);
    Expect(ordinary_control.valid, "ordinary reference must produce valid control output");

    ls2k::port::PerceptionResult ordinary_perception{};
    ordinary_perception.active_module = "straight";
    ordinary_perception.scene_phase = "idle";
    ordinary_perception.reference_mode = "centerline";
    ordinary_perception.visible_range_m = ordinary_control.visible_range_m;
    ordinary_perception.control_model = ordinary_control;
    ls2k::port::PerceptionResult cross_label_only = ordinary_perception;
    cross_label_only.active_module = "cross";
    cross_label_only.scene_phase = "cross_hold";

    ls2k::legacy::LegacyPidControl ordinary_pid;
    ordinary_pid.Configure(params);
    ls2k::legacy::LegacyPidControl label_pid;
    label_pid.Configure(params);
    ls2k::port::LegacySteeringControllerMemory ordinary_memory{};
    ls2k::port::LegacySteeringControllerMemory label_memory{};
    const ls2k::legacy::CameraTurnComputation ordinary_turn =
        ordinary_pid.ComputeTurnTarget(ordinary_perception, params.Speed_base, ordinary_memory);
    const ls2k::legacy::CameraTurnComputation label_turn =
        label_pid.ComputeTurnTarget(cross_label_only, params.Speed_base, label_memory);
    Expect(NearlyEqual(ordinary_turn.w_target, label_turn.w_target, 1e-6F),
           "scene labels alone must not directly perturb controller output");

    ls2k::port::BEVReferencePath shifted_reference = ordinary_reference;
    for (ls2k::port::BEVPathSample& sample : shifted_reference.sampled_path) {
        if (sample.valid) {
            sample.point.lateral_m += 0.16F;
        }
    }
    ordinary_input.reference_path = shifted_reference;
    const ls2k::port::ControlErrorModelOutput shifted_control =
        ls2k::legacy::ComputeControlErrorModel(ordinary_input, params);
    ls2k::port::PerceptionResult shifted_perception = ordinary_perception;
    shifted_perception.active_module = "circle";
    shifted_perception.scene_phase = "circle_entry";
    shifted_perception.reference_mode = "arc_follow";
    shifted_perception.control_model = shifted_control;

    ls2k::legacy::LegacyPidControl shifted_pid;
    shifted_pid.Configure(params);
    ls2k::port::LegacySteeringControllerMemory shifted_memory{};
    const ls2k::legacy::CameraTurnComputation shifted_turn =
        shifted_pid.ComputeTurnTarget(shifted_perception, params.Speed_base, shifted_memory);
    Expect(!NearlyEqual(shifted_control.curvature_command, ordinary_control.curvature_command, 1e-3F),
           "reference path change must change curvature command");
    Expect(!NearlyEqual(shifted_turn.w_target, ordinary_turn.w_target, 1e-3F),
           "controller output may change after reference curvature changes");
}

}  // namespace

int main() {
    try {
        TestBendRemainsOrdinaryTopology();
        TestLargeBendVetoesCircleMisclassification();
        TestCrossRequiresBilateralOpeningAndReacquires();
        TestCrossWithoutExitUsesTrustedHoldOnly();
        TestShortOrdinaryRangeWeakensTopologyEvidence();
        TestSubthresholdSpecialEvidenceDoesNotHideLostTopology();
        TestCurvatureVetoedCircleOpeningStillRaisesLostTopology();
        TestUnobservedEdgesRaiseLostTopology();
        TestSingleSidedNearAnchorStraightTrackWeakensOrdinaryEvidence();
        TestCircleProgressionAndDirectionLatch();
        TestCircleInteriorRejectsStableBoundaryOffsetWithoutInnerIslandMemory();
        TestArcHypothesesUseBevNormalOffset();
        TestExitAndBoundaryArcHypothesesDoNotRequireOrdinaryGraph();
        TestZebraHoldAndConstraintsUseTopologyPath();
        TestLostPredictionExpires();
        TestRuntimeLostPredictionReferenceCanSupportControlBeforeExpiry();
        TestSceneOnlyAffectsControllerThroughReferenceOrConstraints();
    } catch (const TestFailure& failure) {
        std::cerr << "scene_classifier_selftest failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "scene_classifier_selftest unexpected exception: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "scene_classifier_selftest passed\n";
    return EXIT_SUCCESS;
}
