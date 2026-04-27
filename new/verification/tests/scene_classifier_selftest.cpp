#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/pid_control.hpp"
#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_control_error_model.hpp"
#include "legacy/steering_observation_assembly.hpp"
#include "legacy/steering_reference_policy.hpp"
#include "legacy/steering_scene_fsm.hpp"

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
    params.bev_scene_fsm.bend_severity_confirm = 0.20F;
    params.bev_scene_fsm.cross_confirm_cycles = 2;
    params.bev_scene_fsm.cross_hold_cycles = 3;
    params.bev_scene_fsm.circle_confirm_cycles = 2;
    params.bev_scene_fsm.circle_release_cycles = 3;
    params.bev_scene_fsm.release_track_confidence_min = 0.55F;
    params.bev_control_model.low_confidence_threshold = 0.35;
    params.bev_control_model.steering_suppression_confidence = 0.12;
    params.bev_control_model.low_visible_range_m = 0.80;
    params.bev_control_model.min_gain_scale = 0.25;
    params.bev_control_model.min_speed_limit_scale = 0.35;
    return params;
}

ls2k::port::BEVTrackEstimate MakeTrack(const ls2k::port::RuntimeParameters& params,
                                       float center_bias_m = 0.04F,
                                       float heading_slope = 0.03F,
                                       float lane_width_m = 0.60F,
                                       float visible_range_m = 2.30F,
                                       float confidence = 0.90F) {
    ls2k::port::BEVTrackEstimate track{};
    track.valid = true;
    track.calibration_valid = true;
    track.continuity_valid = true;
    track.visible_range_m = visible_range_m;
    track.track_confidence = confidence;
    track.source = "unit_test";
    track.fallback_mode = "none";

    const float origin_forward = params.bev_geometry.forward_samples_m[0];
    for (std::size_t index = 0; index < ls2k::port::kBevTrackSampleCount; ++index) {
        const float forward = params.bev_geometry.forward_samples_m[index];
        if (forward > visible_range_m + 1e-4F) {
            continue;
        }

        const float center_lateral = center_bias_m + heading_slope * (forward - origin_forward);
        const float half_width = lane_width_m * 0.5F;
        const float sample_confidence = confidence;

        track.sampled_left_boundary[index].valid = true;
        track.sampled_left_boundary[index].point.forward_m = forward;
        track.sampled_left_boundary[index].point.lateral_m = center_lateral - half_width;
        track.sampled_left_boundary[index].confidence = sample_confidence;

        track.sampled_centerline[index].valid = true;
        track.sampled_centerline[index].point.forward_m = forward;
        track.sampled_centerline[index].point.lateral_m = center_lateral;
        track.sampled_centerline[index].confidence = sample_confidence;

        track.sampled_right_boundary[index].valid = true;
        track.sampled_right_boundary[index].point.forward_m = forward;
        track.sampled_right_boundary[index].point.lateral_m = center_lateral + half_width;
        track.sampled_right_boundary[index].confidence = sample_confidence;

        track.sampled_drivable_left_boundary[index] = track.sampled_left_boundary[index];
        track.sampled_drivable_right_boundary[index] = track.sampled_right_boundary[index];
        track.lane_width_profile_m[index] = lane_width_m;
        track.drivable_width_profile_m[index] = lane_width_m;
    }

    const int near_index = std::clamp(
        params.bev_control_model.near_sample_index, 0, static_cast<int>(ls2k::port::kBevTrackSampleCount) - 1);
    const int far_index = std::clamp(
        params.bev_control_model.far_sample_index, near_index, static_cast<int>(ls2k::port::kBevTrackSampleCount) - 1);

    if (track.sampled_centerline[near_index].valid) {
        track.near_lateral_error = track.sampled_centerline[near_index].point.lateral_m;
    }
    if (track.sampled_centerline[near_index].valid && track.sampled_centerline[far_index].valid) {
        const float delta_forward =
            track.sampled_centerline[far_index].point.forward_m -
            track.sampled_centerline[near_index].point.forward_m;
        if (std::fabs(delta_forward) > 1e-4F) {
            track.far_heading_error = std::atan2(
                track.sampled_centerline[far_index].point.lateral_m -
                    track.sampled_centerline[near_index].point.lateral_m,
                delta_forward);
        }
    }

    return track;
}

ls2k::port::BEVSceneObservation MakeObservation(const ls2k::port::BEVTrackEstimate& track) {
    ls2k::port::BEVSceneObservation observation{};
    observation.valid = track.valid;
    observation.track = track;
    observation.vehicle.frame_id = 1;
    observation.vehicle.capture_time_ms = 1;
    return observation;
}

ls2k::port::BEVTrackEstimate MakeOpeningTrack(const ls2k::port::RuntimeParameters& params,
                                              float left_open_m,
                                              float right_open_m) {
    ls2k::port::BEVTrackEstimate track{};
    track.valid = true;
    track.calibration_valid = true;
    track.continuity_valid = true;
    track.visible_range_m = params.bev_geometry.forward_samples_m.back();
    track.track_confidence = 0.90F;
    track.source = "unit_test";
    track.fallback_mode = "none";

    const float half_width = params.bev_geometry.nominal_lane_width_m * 0.5F;
    const float denominator = static_cast<float>(ls2k::port::kBevTrackSampleCount - 1U);
    for (std::size_t index = 0; index < ls2k::port::kBevTrackSampleCount; ++index) {
        const float ratio = denominator > 0.0F ? static_cast<float>(index) / denominator : 0.0F;
        const float forward = params.bev_geometry.forward_samples_m[index];
        const float left_lateral = -half_width - left_open_m * ratio;
        const float right_lateral = half_width + right_open_m * ratio;
        const float center_lateral = (left_lateral + right_lateral) * 0.5F;

        track.sampled_left_boundary[index] = {true, {forward, left_lateral}, 0.9F};
        track.sampled_centerline[index] = {true, {forward, center_lateral}, 0.9F};
        track.sampled_right_boundary[index] = {true, {forward, right_lateral}, 0.9F};
        track.sampled_drivable_left_boundary[index] = track.sampled_left_boundary[index];
        track.sampled_drivable_right_boundary[index] = track.sampled_right_boundary[index];
        track.lane_width_profile_m[index] = right_lateral - left_lateral;
        track.drivable_width_profile_m[index] = right_lateral - left_lateral;
    }

    return track;
}

ls2k::legacy::ObservationAssemblyResult AssembleSyntheticObservation(
    const ls2k::port::RuntimeParameters& params,
    const ls2k::port::BEVTrackEstimate& track) {
    ls2k::legacy::BEVProjector projector;
    Expect(projector.Configure(params.bev_projector), "default BEV projector calibration must configure");

    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(0U);

    return ls2k::legacy::AssembleObservation(frame, 100, params, {}, {}, false, 1, 1, track, projector);
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

ls2k::port::BEVSceneObservation MakeCircleLeftObservation(
    const ls2k::port::BEVTrackEstimate& track,
    const ls2k::port::RuntimeParameters& params) {
    ls2k::port::BEVSceneObservation observation = MakeObservation(track);
    observation.circle_left_candidate = true;
    observation.left_open_score = params.bev_scene_fsm.circle_open_score_min + 0.12F;
    observation.left_opposite_straight_confidence = 0.98F;
    observation.ordinary_bend_veto = false;
    return observation;
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

ls2k::port::ReferencePolicyState MakeHeldReference(const ls2k::port::BEVTrackEstimate& track,
                                                   float lateral_shift_m) {
    ls2k::port::ReferencePolicyState state{};
    state.valid = true;
    state.mode = ls2k::port::ReferenceMode::kHoldLast;
    for (std::size_t index = 0; index < track.sampled_centerline.size(); ++index) {
        state.last_reference[index] = track.sampled_centerline[index];
        if (state.last_reference[index].valid) {
            state.last_reference[index].point.lateral_m += lateral_shift_m;
        }
    }
    return state;
}

ls2k::legacy::SceneFsmResult PrimeCircleInterior(const ls2k::port::BEVSceneObservation& observation,
                                                 const ls2k::port::RuntimeParameters& params) {
    const ls2k::legacy::SceneFsmResult first = ls2k::legacy::UpdateSceneFsm(observation, params, {});
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateSceneFsm(observation, params, first.state);
    const ls2k::legacy::SceneFsmResult third =
        ls2k::legacy::UpdateSceneFsm(observation, params, second.state);
    return ls2k::legacy::UpdateSceneFsm(observation, params, third.state);
}

void TestOrdinaryBendVeto() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params);
    ls2k::port::BEVSceneObservation observation = MakeObservation(track);
    observation.ordinary_bend_veto = true;
    observation.bend_severity = params.bev_scene_fsm.bend_severity_confirm + 0.15F;

    const ls2k::legacy::SceneFsmResult result =
        ls2k::legacy::UpdateSceneFsm(observation, params, {});
    Expect(result.state.active_scene == ls2k::port::SpecialSceneKind::kBend,
           "ordinary bend veto must own the scene when no stronger special scene is confirmed");
    Expect(result.active_module == "bend", "bend veto must surface active_module=bend");
    Expect(result.scene_phase == "bend_veto", "bend veto must surface scene_phase=bend_veto");
}

void TestCrossWhitelist() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params);
    ls2k::port::BEVSceneObservation observation = MakeObservation(track);
    observation.cross_candidate = true;
    observation.width_expand_ratio = params.bev_scene_fsm.cross_expand_ratio_min + 0.30F;
    observation.ordinary_bend_veto = true;
    observation.bend_severity = params.bev_scene_fsm.bend_severity_confirm + 0.05F;

    const ls2k::legacy::SceneFsmResult first =
        ls2k::legacy::UpdateSceneFsm(observation, params, {});
    Expect(first.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "cross candidate must wait for confirm cycles before owning the scene");

    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateSceneFsm(observation, params, first.state);
    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kCross,
           "cross candidate must beat ordinary bend veto after confirm");
    Expect(second.active_module == "cross", "confirmed cross must expose active_module=cross");
}

void TestCrossRequiresBilateralOpeningEvidence() {
    ls2k::port::RuntimeParameters params = MakeParams();
    params.bev_scene_fsm.cross_bilateral_open_min_m = 0.04F;

    const ls2k::port::BEVTrackEstimate bend_like_track =
        MakeOpeningTrack(params, 0.18F, 0.0F);
    const ls2k::legacy::ObservationAssemblyResult bend_like =
        AssembleSyntheticObservation(params, bend_like_track);

    Expect(bend_like.observation.width_expand_ratio >= params.bev_scene_fsm.cross_expand_ratio_min,
           "one-sided bend-like opening must still exercise width expansion");
    Expect(bend_like.observation.cross_bilateral_open_score_m <
               params.bev_scene_fsm.cross_bilateral_open_min_m,
           "one-sided bend-like opening must expose insufficient bilateral opening evidence");
    Expect(!bend_like.observation.cross_bilateral_open,
           "one-sided bend-like opening must not satisfy bilateral opening evidence");
    Expect(bend_like.observation.left_open_score >= params.bev_scene_fsm.cross_bilateral_open_min_m,
           "one-sided bend-like opening must expose the opening side");
    Expect(bend_like.observation.right_open_score < params.bev_scene_fsm.cross_bilateral_open_min_m,
           "one-sided bend-like opening must not invent an opposite-side opening");
    Expect(!bend_like.observation.cross_candidate,
           "BEV cross candidate must require bilateral opening evidence, not width expansion alone");

    const ls2k::port::BEVTrackEstimate cross_like_track =
        MakeOpeningTrack(params, 0.07F, 0.07F);
    const ls2k::legacy::ObservationAssemblyResult cross_like =
        AssembleSyntheticObservation(params, cross_like_track);

    Expect(cross_like.observation.cross_bilateral_open_score_m >=
               params.bev_scene_fsm.cross_bilateral_open_min_m,
           "symmetric BEV opening must expose enough bilateral opening evidence");
    Expect(cross_like.observation.cross_bilateral_open,
           "symmetric BEV opening must satisfy bilateral opening evidence");
    Expect(cross_like.observation.cross_candidate,
           "symmetric BEV opening must remain eligible for cross confirmation");
}

void TestCircleConfirmedAndLatchedProgression() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params, 0.05F, 0.02F, 0.60F);
    const ls2k::port::BEVSceneObservation observation = MakeCircleLeftObservation(track, params);

    const ls2k::legacy::SceneFsmResult first = ls2k::legacy::UpdateSceneFsm(observation, params, {});
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateSceneFsm(observation, params, first.state);
    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kCircleLeft,
           "circle must confirm after the configured candidate streak");
    Expect(second.scene_phase == "circle_entry", "confirmed circle must begin in entry phase");

    const ls2k::legacy::ReferencePolicyResult entry_reference =
        ls2k::legacy::ResolveReferencePolicy(track, observation, second.state, {}, params);
    Expect(entry_reference.reference_path.mode == ls2k::port::ReferenceMode::kInnerOffset,
           "circle entry must switch the reference policy to inner_offset");
    Expect(!PathsMatch(entry_reference.reference_path.sampled_path, track.sampled_centerline),
           "circle reference path must differ from the ordinary centerline");

    const ls2k::legacy::SceneFsmResult third =
        ls2k::legacy::UpdateSceneFsm(observation, params, second.state);
    const ls2k::legacy::SceneFsmResult fourth =
        ls2k::legacy::UpdateSceneFsm(observation, params, third.state);
    Expect(fourth.state.phase == ls2k::port::SpecialScenePhase::kInterior,
           "confirmed circle must latch into interior progression");

    ls2k::port::BEVSceneObservation lost_signal = observation;
    lost_signal.circle_left_candidate = false;
    lost_signal.left_open_score = 0.0F;
    lost_signal.left_opposite_straight_confidence = 0.0F;
    const ls2k::legacy::SceneFsmResult lost_once =
        ls2k::legacy::UpdateSceneFsm(lost_signal, params, fourth.state);
    Expect(lost_once.state.active_scene == ls2k::port::SpecialSceneKind::kCircleLeft &&
               lost_once.state.latched,
           "circle must stay latched after one lost candidate frame");
}

void TestCircleCandidateBeatsOrdinaryBendVeto() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params, 0.04F, 0.02F, 0.60F);
    ls2k::port::BEVSceneObservation observation = MakeObservation(track);
    observation.circle_right_candidate = true;
    observation.right_open_score = params.bev_scene_fsm.circle_open_score_min + 0.02F;
    observation.right_opposite_straight_confidence = 0.20F;
    observation.ordinary_bend_veto = true;
    observation.bend_severity = observation.right_open_score + 0.50F;

    const ls2k::legacy::SceneFsmResult first =
        ls2k::legacy::UpdateSceneFsm(observation, params, {});
    const ls2k::legacy::SceneFsmResult second =
        ls2k::legacy::UpdateSceneFsm(observation, params, first.state);

    Expect(second.state.active_scene == ls2k::port::SpecialSceneKind::kCircleRight,
           "confirmed circle candidate must beat ordinary bend veto ownership");
    Expect(second.active_module == "circle",
           "circle candidate must not be downgraded to bend by bend severity score");
}

void TestCircleRejectsCurvedOppositeBoundary() {
    ls2k::port::RuntimeParameters params = MakeParams();
    params.bev_scene_fsm.circle_opposite_heading_abs_max = 0.05F;

    const ls2k::port::BEVTrackEstimate bend_like_track =
        MakeOpeningTrack(params, 0.22F, -0.28F);
    const ls2k::legacy::ObservationAssemblyResult bend_like =
        AssembleSyntheticObservation(params, bend_like_track);

    Expect(bend_like.observation.left_open_score >= params.bev_scene_fsm.circle_open_score_min,
           "bend-like sample must exercise one-sided circle-open evidence");
    Expect(bend_like.observation.right_boundary_heading_abs_rad >
               params.bev_scene_fsm.circle_opposite_heading_abs_max,
           "bend-like sample must expose a curved opposite boundary");
    Expect(!bend_like.observation.circle_left_opposite_straight,
           "bend-like sample must not satisfy opposite-straight evidence");
    Expect(!bend_like.observation.circle_left_candidate,
           "circle-left candidate must reject one-sided openings when the opposite boundary is not straight");

    const ls2k::port::BEVTrackEstimate circle_like_track =
        MakeOpeningTrack(params, 0.22F, 0.0F);
    const ls2k::legacy::ObservationAssemblyResult circle_like =
        AssembleSyntheticObservation(params, circle_like_track);

    Expect(circle_like.observation.right_boundary_heading_abs_rad <=
               params.bev_scene_fsm.circle_opposite_heading_abs_max,
           "circle-like sample must expose a straight opposite boundary");
    Expect(circle_like.observation.circle_left_opposite_straight,
           "circle-like sample must satisfy opposite-straight evidence");
    Expect(circle_like.observation.circle_left_candidate,
           "circle-left candidate must remain eligible when one side opens and the opposite boundary is straight");
}

void TestCircleReleaseConditions() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params, 0.05F, 0.02F, 0.60F);
    const ls2k::port::BEVSceneObservation circle_observation = MakeCircleLeftObservation(track, params);
    ls2k::legacy::SceneFsmResult state = PrimeCircleInterior(circle_observation, params);

    ls2k::port::BEVSceneObservation lost_signal = circle_observation;
    lost_signal.circle_left_candidate = false;
    lost_signal.left_open_score = 0.0F;
    lost_signal.left_opposite_straight_confidence = 0.0F;

    for (int cycle = 0; cycle < params.bev_scene_fsm.circle_release_cycles; ++cycle) {
        state = ls2k::legacy::UpdateSceneFsm(lost_signal, params, state.state);
    }

    Expect(state.state.phase == ls2k::port::SpecialScenePhase::kExit,
           "circle must progress into exit after finite release cycles");
    Expect(state.scene_phase == "circle_exit", "circle exit must surface scene_phase=circle_exit");

    ls2k::port::BEVSceneObservation recovered = lost_signal;
    recovered.track.track_confidence = params.bev_scene_fsm.release_track_confidence_min + 0.10F;
    const ls2k::legacy::SceneFsmResult cleared =
        ls2k::legacy::UpdateSceneFsm(recovered, params, state.state);
    Expect(cleared.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "recovered ordinary track must release the latched circle state");
    Expect(cleared.scene_phase == "idle", "released circle must return to idle scene phase");
}

void TestLowConfidenceDegradation() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::legacy::BEVProjector projector;
    Expect(projector.Configure(params.bev_projector), "default BEV projector calibration must configure");

    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;

    const ls2k::port::BEVTrackEstimate track =
        MakeTrack(params, 0.02F, 0.01F, 0.60F, 0.55F, 0.30F);
    const ls2k::legacy::ObservationAssemblyResult assembly = ls2k::legacy::AssembleObservation(
        frame, 96, params, {}, {}, false, 7, 9, track, projector);

    Expect(assembly.constraints.low_confidence_degraded,
           "short visible range must degrade control constraints");
    Expect(assembly.constraints.speed_limit_scale < 1.0,
           "low confidence degradation must reduce speed limit scale");
    Expect(assembly.constraints.turn_limit_scale < 1.0,
           "low confidence degradation must reduce turn limit scale");
    Expect(assembly.constraints.primary_reason == "short_visible_range",
           "short visible range must be the primary degrade reason");

    const ls2k::legacy::ReferencePolicyResult ordinary_reference =
        ls2k::legacy::ResolveReferencePolicy(track, assembly.observation, {}, {}, params);

    ls2k::port::ControlErrorModelInput input{};
    input.track = track;
    input.reference_path = ordinary_reference.reference_path;
    input.vehicle = assembly.vehicle;
    input.constraints = assembly.constraints;

    const ls2k::port::ControlErrorModelOutput control =
        ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(control.degraded, "control error model must preserve degraded low-confidence status");
    Expect(!control.steering_suppressed,
           "degraded low visibility alone must not force full steering suppression");
    Expect(control.degrade_reason == "short_visible_range",
           "control error model must surface the low-visibility degrade reason");
}

void TestLookaheadCurvatureControlModel() {
    ls2k::port::RuntimeParameters params = MakeParams();
    params.bev_control_model.lookahead_visible_range_ratio = 0.35;
    params.bev_control_model.lookahead_min_m = 1.20;
    params.bev_control_model.lookahead_max_m = 2.00;
    params.bev_control_model.pure_pursuit_gain = 1.0;
    params.bev_control_model.heading_curvature_gain = 0.35;
    params.bev_control_model.curvature_feedforward_gain = 0.20;
    params.bev_control_model.curvature_command_limit = 0.12;

    const ls2k::port::BEVTrackEstimate right_offset_track =
        MakeTrack(params, 0.05F, 0.0F, 0.60F, 4.50F, 0.90F);
    const ls2k::port::BEVSceneObservation ordinary_observation =
        MakeObservation(right_offset_track);
    const ls2k::legacy::ReferencePolicyResult right_reference =
        ls2k::legacy::ResolveReferencePolicy(right_offset_track, ordinary_observation, {}, {}, params);

    ls2k::port::ControlErrorModelInput input{};
    input.track = right_offset_track;
    input.reference_path = right_reference.reference_path;
    const ls2k::port::ControlErrorModelOutput right_control =
        ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(right_control.valid, "right-offset reference path must produce a valid control command");
    Expect(right_control.lookahead_distance_m > 1.19F && right_control.lookahead_distance_m < 2.01F,
           "lookahead distance must be clamped inside the configured range");
    Expect(right_control.curvature_command > 0.0F,
           "right-side BEV offset must produce positive curvature command");

    ls2k::port::BEVTrackEstimate left_offset_track = right_offset_track;
    for (ls2k::port::BEVPathSample& sample : left_offset_track.sampled_centerline) {
        if (sample.valid) {
            sample.point.lateral_m *= -1.0F;
        }
    }
    const ls2k::port::BEVSceneObservation left_observation =
        MakeObservation(left_offset_track);
    const ls2k::legacy::ReferencePolicyResult left_reference =
        ls2k::legacy::ResolveReferencePolicy(left_offset_track, left_observation, {}, {}, params);
    input.track = left_offset_track;
    input.reference_path = left_reference.reference_path;
    const ls2k::port::ControlErrorModelOutput left_control =
        ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(left_control.curvature_command < 0.0F,
           "left-side BEV offset must produce negative curvature command");

    ls2k::port::BEVTrackEstimate short_visible_track =
        MakeTrack(params, 0.05F, 0.0F, 0.60F, 1.25F, 0.90F);
    input.track = short_visible_track;
    input.reference_path = ls2k::legacy::ResolveReferencePolicy(
                               short_visible_track, MakeObservation(short_visible_track), {}, {}, params)
                               .reference_path;
    const ls2k::port::ControlErrorModelOutput short_visible_control =
        ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(NearlyEqual(short_visible_control.lookahead_distance_m, 1.20F, 1e-4F),
           "short visible range must clamp lookahead to LOOKAHEAD_MIN_M");

    params.bev_control_model.lookahead_visible_range_ratio = 1.0;
    input.track = right_offset_track;
    input.reference_path = right_reference.reference_path;
    const ls2k::port::ControlErrorModelOutput long_visible_control =
        ls2k::legacy::ComputeControlErrorModel(input, params);
    Expect(NearlyEqual(long_visible_control.lookahead_distance_m, 2.00F, 1e-4F),
           "long visible range must clamp lookahead to LOOKAHEAD_MAX_M");
}

void TestCrossEntranceDrivableSpanFeedsSceneEvidence() {
    ls2k::port::RuntimeParameters params = MakeParams();
    ls2k::legacy::BEVProjector projector;
    Expect(projector.Configure(params.bev_projector), "default BEV projector calibration must configure");

    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(0U);

    for (std::size_t index = 0; index < params.bev_geometry.forward_samples_m.size(); ++index) {
        const float forward = params.bev_geometry.forward_samples_m[index];
        ls2k::port::ImagePoint center_image{};
        ls2k::port::ImagePoint left_image{};
        ls2k::port::ImagePoint right_image{};
        if (!projector.ProjectVehicleToImage({forward, 0.0F}, center_image) ||
            !projector.ProjectVehicleToImage(
                {forward, -params.bev_geometry.nominal_lane_width_m * 0.5F}, left_image) ||
            !projector.ProjectVehicleToImage(
                {forward, params.bev_geometry.nominal_lane_width_m * 0.5F}, right_image)) {
            continue;
        }

        const int row = std::clamp(static_cast<int>(std::lround(center_image.row_px)),
                                   0,
                                   std::max(0, frame.height - 1));
        if (index >= 3U && index <= 5U) {
            PaintWhiteRun(frame, row, 0, frame.width - 1);
            continue;
        }

        const int left_col = std::clamp(static_cast<int>(std::lround(left_image.col_px)),
                                        0,
                                        std::max(0, frame.width - 1));
        const int right_col = std::clamp(static_cast<int>(std::lround(right_image.col_px)),
                                         0,
                                         std::max(0, frame.width - 1));
        PaintWhiteRun(frame, row, left_col, right_col);
    }

    const ls2k::port::BEVTrackEstimate track =
        ls2k::legacy::ComputeBevTrackEstimate(frame, 100, params, {}, projector);
    const ls2k::legacy::ObservationAssemblyResult assembly = ls2k::legacy::AssembleObservation(
        frame, 100, params, {}, {}, false, 11, 13, track, projector);

    Expect(track.valid, "cross-entrance synthetic frame must retain an ordinary BEV track");

    bool saw_overwide_drivable_span = false;
    for (std::size_t index = 0; index < track.drivable_width_profile_m.size(); ++index) {
        if (track.drivable_width_profile_m[index] <= params.bev_geometry.max_lane_width_m) {
            continue;
        }
        saw_overwide_drivable_span = true;
        Expect(!track.sampled_left_boundary[index].valid && !track.sampled_right_boundary[index].valid,
               "overwide drivable span must not be reauthored as lane boundaries");
    }
    Expect(saw_overwide_drivable_span,
           "cross-entrance geometry must retain overwide drivable span evidence");
    Expect(assembly.observation.width_expand_ratio >= params.bev_scene_fsm.cross_expand_ratio_min,
           "cross-entrance drivable span must feed width expansion evidence");
    Expect(assembly.observation.cross_bilateral_open_score_m >=
               params.bev_scene_fsm.cross_bilateral_open_min_m,
           "cross-entrance drivable span must feed bilateral opening evidence");
    Expect(assembly.observation.cross_candidate,
           "cross-entrance scene observation must become eligible for cross confirmation");
    Expect(!assembly.observation.circle_left_candidate && !assembly.observation.circle_right_candidate,
           "bilateral cross opening must not also surface circle candidates");
}

void TestBorderTruncatedBoundaryDoesNotBecomeBevEdge() {
    ls2k::port::RuntimeParameters params = MakeParams();
    params.bev_geometry.image_border_truncation_margin_px = 2;
    ls2k::legacy::BEVProjector projector;
    Expect(projector.Configure(params.bev_projector), "default BEV projector calibration must configure");

    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(0U);

    for (float forward : params.bev_geometry.forward_samples_m) {
        ls2k::port::ImagePoint center_image{};
        ls2k::port::ImagePoint left_image{};
        if (!projector.ProjectVehicleToImage({forward, 0.0F}, center_image) ||
            !projector.ProjectVehicleToImage(
                {forward, -params.bev_geometry.nominal_lane_width_m * 0.5F}, left_image)) {
            continue;
        }
        const int row = std::clamp(static_cast<int>(std::lround(center_image.row_px)),
                                   0,
                                   std::max(0, frame.height - 1));
        const int left_col = std::clamp(static_cast<int>(std::lround(left_image.col_px)),
                                        0,
                                        std::max(0, frame.width - 1));
        PaintWhiteRun(frame, row, left_col, frame.width - 1);
    }

    const ls2k::port::BEVTrackEstimate track =
        ls2k::legacy::ComputeBevTrackEstimate(frame, 100, params, {}, projector);

    Expect(track.valid, "single visible edge with a truncated opposite side must still produce a BEV track");
    Expect(track.fallback_mode == "single_edge_reconstruction",
           "single-edge centerline reconstruction must be visible in the BEV track fallback mode");

    int checked_samples = 0;
    for (std::size_t index = 0; index < track.sampled_centerline.size(); ++index) {
        if (!track.sampled_centerline[index].valid) {
            continue;
        }
        ++checked_samples;
        Expect(track.sampled_left_boundary[index].valid,
               "the observed non-border boundary must remain authoritative");
        Expect(!track.sampled_right_boundary[index].valid,
               "an image-border truncation must not be exported as a BEV right boundary");
        Expect(track.sampled_centerline[index].confidence < 1.0F,
               "single-edge reconstructed centerline must be lower confidence than dual-edge geometry");
    }
    Expect(checked_samples >= 3,
           "border-truncated synthetic frame must exercise multiple valid BEV geometry samples");
}

void TestResetStateIsolation() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params, 0.07F, 0.01F, 0.60F);
    const ls2k::port::BEVSceneObservation observation = MakeObservation(track);

    ls2k::port::SpecialSceneFsmState cross_state{};
    cross_state.active_scene = ls2k::port::SpecialSceneKind::kCross;
    cross_state.phase = ls2k::port::SpecialScenePhase::kHold;

    const ls2k::port::ReferencePolicyState held_reference = MakeHeldReference(track, 0.18F);
    const ls2k::legacy::ReferencePolicyResult held =
        ls2k::legacy::ResolveReferencePolicy(track, observation, cross_state, held_reference, params);
    Expect(held.reference_path.mode == ls2k::port::ReferenceMode::kHoldLast,
           "cross hold must reuse the carried reference path");
    Expect(!PathsMatch(held.reference_path.sampled_path, track.sampled_centerline),
           "held reference path must differ from the ordinary centerline");

    const ls2k::legacy::SceneFsmResult reset_scene =
        ls2k::legacy::UpdateSceneFsm(observation, params, {});
    const ls2k::legacy::ReferencePolicyResult reset_reference =
        ls2k::legacy::ResolveReferencePolicy(track, observation, reset_scene.state, {}, params);
    Expect(reset_scene.state.active_scene == ls2k::port::SpecialSceneKind::kOrdinary,
           "reset state must clear prior scene latches");
    Expect(reset_reference.reference_path.mode == ls2k::port::ReferenceMode::kCenterline,
           "reset state must clear held reference policy memory");
    Expect(PathsMatch(reset_reference.reference_path.sampled_path, track.sampled_centerline),
           "ordinary reset reference must rebuild directly from the centerline");
}

void TestSceneOnlyAffectsControllerThroughReferenceOrConstraints() {
    const ls2k::port::RuntimeParameters params = MakeParams();
    const ls2k::port::BEVTrackEstimate track = MakeTrack(params, 0.04F, 0.03F, 0.60F);
    const ls2k::port::BEVSceneObservation ordinary_observation = MakeObservation(track);

    const ls2k::legacy::ReferencePolicyResult ordinary_reference =
        ls2k::legacy::ResolveReferencePolicy(track, ordinary_observation, {}, {}, params);

    ls2k::port::ControlErrorModelInput ordinary_input{};
    ordinary_input.track = track;
    ordinary_input.reference_path = ordinary_reference.reference_path;

    const ls2k::port::ControlErrorModelOutput ordinary_control =
        ls2k::legacy::ComputeControlErrorModel(ordinary_input, params);
    Expect(ordinary_control.valid, "ordinary centerline reference must produce valid control errors");

    ls2k::port::PerceptionResult ordinary_perception{};
    ordinary_perception.visible_range_m = ordinary_control.visible_range_m;
    ordinary_perception.active_module = "straight";
    ordinary_perception.scene_phase = "idle";
    ordinary_perception.reference_mode = "centerline";
    ordinary_perception.control_model = ordinary_control;

    ls2k::port::PerceptionResult bend_perception = ordinary_perception;
    bend_perception.active_module = "bend";
    bend_perception.scene_phase = "bend_veto";

    ls2k::legacy::LegacyPidControl ordinary_pid;
    ordinary_pid.Configure(params);
    ls2k::legacy::LegacyPidControl bend_pid;
    bend_pid.Configure(params);

    ls2k::port::LegacySteeringControllerMemory ordinary_memory{};
    ls2k::port::LegacySteeringControllerMemory bend_memory{};

    const ls2k::legacy::CameraTurnComputation ordinary_turn =
        ordinary_pid.ComputeTurnTarget(ordinary_perception, params.Speed_base, ordinary_memory);
    const ls2k::legacy::CameraTurnComputation bend_turn =
        bend_pid.ComputeTurnTarget(bend_perception, params.Speed_base, bend_memory);

    Expect(NearlyEqual(ordinary_turn.w_target, bend_turn.w_target, 1e-6F),
           "scene labels alone must not directly perturb controller output");
    Expect(NearlyEqual(ordinary_turn.camera_p_term, bend_turn.camera_p_term, 1e-6F),
           "scene labels alone must not directly perturb controller camera terms");

    const ls2k::port::BEVSceneObservation circle_observation =
        MakeCircleLeftObservation(track, params);
    const ls2k::legacy::SceneFsmResult first_circle =
        ls2k::legacy::UpdateSceneFsm(circle_observation, params, {});
    const ls2k::legacy::SceneFsmResult confirmed_circle =
        ls2k::legacy::UpdateSceneFsm(circle_observation, params, first_circle.state);
    const ls2k::legacy::ReferencePolicyResult circle_reference = ls2k::legacy::ResolveReferencePolicy(
        track, circle_observation, confirmed_circle.state, {}, params);

    ls2k::port::ControlErrorModelInput circle_input = ordinary_input;
    circle_input.reference_path = circle_reference.reference_path;
    const ls2k::port::ControlErrorModelOutput circle_control =
        ls2k::legacy::ComputeControlErrorModel(circle_input, params);

    ls2k::port::PerceptionResult circle_perception = ordinary_perception;
    circle_perception.visible_range_m = circle_control.visible_range_m;
    circle_perception.active_module = "circle";
    circle_perception.scene_phase = "circle_entry";
    circle_perception.reference_mode = "inner_offset";
    circle_perception.control_model = circle_control;

    ls2k::legacy::LegacyPidControl circle_pid;
    circle_pid.Configure(params);
    ls2k::port::LegacySteeringControllerMemory circle_memory{};
    const ls2k::legacy::CameraTurnComputation circle_turn =
        circle_pid.ComputeTurnTarget(circle_perception, params.Speed_base, circle_memory);

    Expect(!NearlyEqual(circle_control.curvature_command, ordinary_control.curvature_command, 1e-3F),
           "scene influence must appear through reference policy curvature changes");
    Expect(!NearlyEqual(circle_turn.w_target, ordinary_turn.w_target, 1e-3F),
           "controller output must change only after the reference path changes");
}

}  // namespace

int main() {
    try {
        TestOrdinaryBendVeto();
        TestCrossWhitelist();
        TestCrossRequiresBilateralOpeningEvidence();
        TestCircleConfirmedAndLatchedProgression();
        TestCircleCandidateBeatsOrdinaryBendVeto();
        TestCircleRejectsCurvedOppositeBoundary();
        TestCircleReleaseConditions();
        TestLowConfidenceDegradation();
        TestLookaheadCurvatureControlModel();
        TestCrossEntranceDrivableSpanFeedsSceneEvidence();
        TestBorderTruncatedBoundaryDoesNotBecomeBevEdge();
        TestResetStateIsolation();
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
