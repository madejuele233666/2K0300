#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_bottom_tracker.hpp"
#include "legacy/steering_scene_orchestrator.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct FixtureAnalysis {
    ls2k::legacy::SteeringAnalysisResult analysis{};
    ls2k::legacy::LaneMetrics metrics{};
};

struct RawFixture {
    const char* label = "";
    const char* path = "";
};

std::string DescribeAnalysis(const FixtureAnalysis& analysis) {
    const ls2k::legacy::LaneMetrics& metrics = analysis.metrics;
    return "module=" + analysis.analysis.perception.active_module + " scene=" +
           analysis.analysis.perception.scene_phase +
           " candidate=" + analysis.analysis.special_wide_candidate +
           " streak=" + std::to_string(analysis.analysis.special_wide_candidate_streak) +
           " cross=" + std::to_string(analysis.analysis.special_wide_cross_score_last) +
           " circle_l=" + std::to_string(analysis.analysis.special_wide_circle_left_score_last) +
           " circle_r=" + std::to_string(analysis.analysis.special_wide_circle_right_score_last) +
           " upper_span=" + std::to_string(metrics.upper_full_span_ratio) +
           " upper_span_consec=" + std::to_string(metrics.upper_full_span_consecutive_rows_max) +
           " left_curve=" + std::to_string(metrics.left_curvature) +
           " right_curve=" + std::to_string(metrics.right_curvature) +
           " bend_same_sign=" + std::to_string(metrics.same_direction_bend_sign) +
           " bend_extrap=" + std::to_string(metrics.bend_used_current_frame_extrapolation ? 1 : 0) +
           " bend_hist=" + std::to_string(metrics.bend_used_history_fallback ? 1 : 0) +
           " left_touch=" + std::to_string(metrics.left_upper_border_touch_ratio) +
           " right_touch=" + std::to_string(metrics.right_upper_border_touch_ratio) +
           " left_visible=" + std::to_string(metrics.left_visible_confident ? 1 : 0) +
           " right_visible=" + std::to_string(metrics.right_visible_confident ? 1 : 0);
}

ls2k::port::LegacyCameraFrame ReadRawFrame(const char* path) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error(std::string("failed to open raw frame: ") + path);
    }
    input.read(reinterpret_cast<char*>(frame.gray.data()),
               static_cast<std::streamsize>(frame.width * frame.height));
    Expect(static_cast<int>(input.gcount()) == frame.width * frame.height,
           std::string("raw frame size must match compiled geometry for ") + path);
    return frame;
}

ls2k::port::RuntimeParameters MakeSceneParams() {
    ls2k::port::RuntimeParameters params{};
    params.see_max = 24.0;
    params.emergency_threshold = 40;
    return params;
}

void ApplyAnalysisToState(const ls2k::legacy::SteeringAnalysisResult& analysis,
                          ls2k::port::LegacySteeringState& state) {
    const ls2k::port::PerceptionResult& perception = analysis.perception;
    state.highest_line = perception.highest_line;
    state.farthest_line = perception.farthest_line;
    state.steering_reference_col = perception.steering_reference_col;
    state.active_module = perception.active_module;
    state.scene_phase = perception.scene_phase;
    state.scene_override_source = perception.scene_override_source;
    state.roadblock_interface_state = perception.roadblock_interface_state;
    state.last_special_scene_correction = perception.last_special_scene_correction;
    state.roadblock_active = perception.roadblock_active;
    state.special_wide_candidate = analysis.special_wide_candidate;
    state.special_wide_candidate_streak = analysis.special_wide_candidate_streak;
    state.special_wide_cross_score_last = analysis.special_wide_cross_score_last;
    state.special_wide_circle_left_score_last = analysis.special_wide_circle_left_score_last;
    state.special_wide_circle_right_score_last = analysis.special_wide_circle_right_score_last;
    if (analysis.steering_state_update_valid) {
        state.circle_active_direction = analysis.steering_state_update.circle_active_direction;
        state.circle_entry_state = analysis.steering_state_update.circle_entry_state;
        state.circle_exit_state = analysis.steering_state_update.circle_exit_state;
        state.circle_reference_mode = analysis.steering_state_update.circle_reference_mode;
        state.circle_heading_delta_deg = analysis.steering_state_update.circle_heading_delta_deg;
        state.circle_heading_baseline_deg = analysis.steering_state_update.circle_heading_baseline_deg;
        state.circle_last_imu_capture_time_ms =
            analysis.steering_state_update.circle_last_imu_capture_time_ms;
        state.circle_fixsteer_cycles = analysis.steering_state_update.circle_fixsteer_cycles;
        state.circle_handover_cycles = analysis.steering_state_update.circle_handover_cycles;
        state.circle_fallback_reason = analysis.steering_state_update.circle_fallback_reason;
        state.circle_entry_settle_cycles = analysis.steering_state_update.circle_entry_settle_cycles;
        state.circle_entry_loss_cycles = analysis.steering_state_update.circle_entry_loss_cycles;
        state.circle_entry_release_reason =
            analysis.steering_state_update.circle_entry_release_reason;
        state.circle_opposite_edge_confirm_cycles =
            analysis.steering_state_update.circle_opposite_edge_confirm_cycles;
        state.circle_release_cycles = analysis.steering_state_update.circle_release_cycles;
        state.circle_last_stable_reference_col =
            analysis.steering_state_update.circle_last_stable_reference_col;
    }
    state.lane_geometry_previous = state.lane_geometry_recent;
    state.lane_geometry_recent = analysis.lane_geometry_snapshot;
    state.track_history = analysis.track_history_snapshot;
    state.gyro_continuity = analysis.gyro_continuity_state;
}

FixtureAnalysis AnalyzeRawFixture(const RawFixture& fixture,
                                  const ls2k::port::RuntimeParameters& params,
                                  ls2k::port::LegacySteeringState state) {
    const ls2k::port::LegacyCameraFrame frame = ReadRawFrame(fixture.path);
    FixtureAnalysis result{};
    result.analysis = ls2k::legacy::AnalyzeFrame(frame, params, state, {}, false, 1, 1);
    result.metrics = ls2k::legacy::ExtractLaneMetrics(
        frame, result.analysis.perception.threshold, params, state, {}, 1);
    ApplyAnalysisToState(result.analysis, state);
    return result;
}

ls2k::port::LegacySteeringState MakePrimedWideState(const char* candidate) {
    ls2k::port::LegacySteeringState state{};
    state.active_module = "special_wide";
    state.special_wide_candidate = candidate;
    state.special_wide_candidate_streak = 1;
    return state;
}

void ExpectConfirmedModule(const RawFixture& fixture,
                           const char* candidate,
                           const char* expected_module) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    const FixtureAnalysis analysis = AnalyzeRawFixture(fixture, params, MakePrimedWideState(candidate));
    Expect(analysis.analysis.perception.active_module == expected_module,
           std::string(fixture.label) + " must confirm " + expected_module + "; " +
               DescribeAnalysis(analysis));
}

void ExpectNotConfirmedModule(const RawFixture& fixture,
                              const char* candidate,
                              const char* forbidden_module) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    const FixtureAnalysis analysis = AnalyzeRawFixture(fixture, params, MakePrimedWideState(candidate));
    Expect(analysis.analysis.perception.active_module != forbidden_module,
           std::string(fixture.label) + " must not confirm " + forbidden_module + "; " +
               DescribeAnalysis(analysis));
}

void ExpectNeutralBend(const RawFixture& fixture) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    const FixtureAnalysis analysis = AnalyzeRawFixture(fixture, params, ls2k::port::LegacySteeringState{});
    Expect(analysis.analysis.perception.active_module == "bend",
           std::string(fixture.label) + " must enter bend on neutral entry; " + DescribeAnalysis(analysis));
    Expect(analysis.analysis.special_wide_candidate == "none",
           std::string(fixture.label) + " must not nominate a wide candidate; " +
               DescribeAnalysis(analysis));
}

void ExpectNeutralSpecialWide(const RawFixture& fixture, const char* expected_candidate) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    const FixtureAnalysis analysis = AnalyzeRawFixture(fixture, params, ls2k::port::LegacySteeringState{});
    Expect(analysis.analysis.perception.active_module == "special_wide",
           std::string(fixture.label) + " must enter special_wide on neutral entry; " +
               DescribeAnalysis(analysis));
    Expect(analysis.analysis.special_wide_candidate == expected_candidate,
           std::string(fixture.label) + " must nominate " + expected_candidate + "; " +
               DescribeAnalysis(analysis));
}

FixtureAnalysis AnalyzeRawFramePath(const std::string& path,
                                    const ls2k::port::RuntimeParameters& params,
                                    ls2k::port::LegacySteeringState state,
                                    uint64_t frame_id) {
    const ls2k::port::LegacyCameraFrame frame = ReadRawFrame(path.c_str());
    FixtureAnalysis result{};
    result.analysis = ls2k::legacy::AnalyzeFrame(frame, params, state, {}, false, frame_id, frame_id);
    result.metrics =
        ls2k::legacy::ExtractLaneMetrics(frame, result.analysis.perception.threshold, params, state, {}, frame_id);
    return result;
}

FixtureAnalysis AnalyzeSequentialFrames(const std::string& directory, const std::vector<int>& frames) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    ls2k::port::LegacySteeringState state{};
    FixtureAnalysis current{};
    for (int frame : frames) {
        const std::string path = directory + "/frame-" +
                                 (frame < 100000 ? std::string(6 - std::to_string(frame).size(), '0') : "") +
                                 std::to_string(frame) + ".raw";
        current = AnalyzeRawFramePath(path, params, state, static_cast<uint64_t>(frame));
        ApplyAnalysisToState(current.analysis, state);
    }
    return current;
}

ls2k::port::LegacyCameraFrame MakeSyntheticLaneFrame(bool left_truncated, bool left_only_one_visible_row) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    for (int row = 80; row <= 184; ++row) {
        int left = 44 + (184 - row) / 5;
        if (left_truncated) {
            left = row == 184 ? 20 : 0;
            if (!left_only_one_visible_row && row >= 156) {
                left = 18 + (184 - row) / 8;
            }
        }
        const int right = 250 - (184 - row) / 7;
        for (int col = std::max(0, left); col <= std::min(frame.width - 1, right); ++col) {
            frame.gray[static_cast<std::size_t>(row) * frame.width + col] = 255;
        }
    }
    return frame;
}

ls2k::port::LegacySteeringState MakeHistoryPrimedState() {
    ls2k::port::LegacySteeringState state{};
    state.steering_reference_col = 160;
    state.lane_geometry_recent.valid = true;
    state.lane_geometry_previous.valid = true;
    state.lane_geometry_recent.left_visible_anchors[0] = {true, 100, 28};
    state.lane_geometry_recent.left_visible_anchors[1] = {true, 132, 34};
    state.lane_geometry_recent.left_visible_anchors[2] = {true, 168, 42};
    state.lane_geometry_recent.right_visible_anchors[0] = {true, 100, 244};
    state.lane_geometry_recent.right_visible_anchors[1] = {true, 132, 238};
    state.lane_geometry_recent.right_visible_anchors[2] = {true, 168, 232};
    state.lane_geometry_previous = state.lane_geometry_recent;
    state.track_history.valid = true;
    state.track_history.center_anchors[0] = {true, 100, 136};
    state.track_history.center_anchors[1] = {true, 132, 136};
    state.track_history.center_anchors[2] = {true, 168, 137};
    state.track_history.lane_width_px = 200.0F;
    state.track_history.heading_px_per_row = -0.12F;
    state.track_history.curvature_px_per_row2 = 0.0F;
    state.track_history.turn_sign = -1;
    state.track_history.track_confidence = 0.8F;
    return state;
}

ls2k::legacy::SteeringAnalysisResult RunSyntheticScene(const ls2k::legacy::LaneMetrics& metrics,
                                                       const ls2k::port::LegacySteeringState& prior_state) {
    ls2k::port::RuntimeParameters params = MakeSceneParams();
    const ls2k::port::ImuSample imu{};
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    const ls2k::legacy::SteeringSceneContext context{frame, params, prior_state, imu, 1, metrics};
    return ls2k::legacy::OrchestrateSteeringScenes(context, false, 1, 1);
}

ls2k::legacy::SteeringAnalysisResult RunSyntheticSceneWithInputs(
    const ls2k::legacy::LaneMetrics& metrics,
    const ls2k::port::LegacySteeringState& prior_state,
    const ls2k::port::RuntimeParameters& params,
    const ls2k::port::ImuSample& imu,
    uint64_t capture_time_ms) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    const ls2k::legacy::SteeringSceneContext context{frame, params, prior_state, imu, capture_time_ms, metrics};
    return ls2k::legacy::OrchestrateSteeringScenes(context, false, capture_time_ms, capture_time_ms);
}

void TestRepresentativeWideSamples() {
    const RawFixture circle_1{"circle-1",
                              "new/verification/static-circle-entry-20260425T053113Z/"
                              "steering-media/frames/frame-000001.raw"};
    const RawFixture circle_2{"circle-2",
                              "new/verification/static-circle-entry-20260425T054335Z/"
                              "steering-media/frames/frame-000003.raw"};
    const RawFixture circle_3{"circle-3",
                              "new/verification/static-circle-entry-20260425T055605Z/"
                              "steering-media/frames/frame-004602.raw"};
    const RawFixture cross_1{"cross-1",
                             "new/verification/static-cross-20260425T061249Z/"
                             "steering-media/frames/frame-056387.raw"};
    const RawFixture cross_2{"cross-2",
                             "new/verification/static-cross-20260425T061406Z/"
                             "steering-media/frames/frame-059950.raw"};
    const RawFixture cross_3{"cross-3",
                             "new/verification/static-cross-20260425T061514Z/"
                             "steering-media/frames/frame-063602.raw"};
    const RawFixture bend_1{"bend-1",
                            "new/verification/static-bend-entry-20260425T072933Z-boardtest/"
                            "steering-media/frames/frame-017752.raw"};
    const RawFixture bend_2{"bend-2",
                            "new/verification/static-bend-entry-20260425T073459Z-boardtest/"
                            "steering-media/frames/frame-034614.raw"};
    const RawFixture bend_3{"bend-3",
                            "new/verification/static-bend-entry-20260425T073835Z-boardtest/"
                            "steering-media/frames/frame-045284.raw"};

    ExpectNeutralSpecialWide(circle_1, "circle_entry");
    ExpectNeutralSpecialWide(circle_2, "circle_entry");
    ExpectNeutralSpecialWide(cross_1, "cross");
    ExpectNeutralSpecialWide(cross_2, "cross");

    ExpectConfirmedModule(circle_1, "circle_entry", "circle_entry");
    ExpectConfirmedModule(circle_2, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(circle_3, "cross", "cross");

    ExpectConfirmedModule(cross_1, "cross", "cross");
    ExpectConfirmedModule(cross_2, "cross", "cross");
    ExpectNotConfirmedModule(cross_3, "circle_entry", "circle_entry");

    ExpectNeutralBend(bend_1);
    ExpectNeutralBend(bend_2);
    ExpectNeutralBend(bend_3);

    ExpectNotConfirmedModule(circle_1, "cross", "cross");
    ExpectNotConfirmedModule(circle_2, "cross", "cross");
    ExpectNotConfirmedModule(cross_1, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(cross_2, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(bend_1, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(bend_2, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(bend_3, "circle_entry", "circle_entry");
    ExpectNotConfirmedModule(bend_1, "cross", "cross");
    ExpectNotConfirmedModule(bend_2, "cross", "cross");
    ExpectNotConfirmedModule(bend_3, "cross", "cross");
}

void TestSyntheticSceneRegression() {
    ls2k::legacy::LaneMetrics straight_metrics{};
    straight_metrics.threshold = 120;
    straight_metrics.valid_row_count = 14;
    straight_metrics.lower_valid_row_count = 4;
    straight_metrics.middle_valid_row_count = 4;
    straight_metrics.upper_valid_row_count = 4;
    straight_metrics.steering_reference_col = 160;
    straight_metrics.lateral_error = 0.0F;
    Expect(RunSyntheticScene(straight_metrics, {}).perception.active_module == "straight",
           "baseline straight regression must stay straight");

    ls2k::legacy::LaneMetrics bend_metrics = straight_metrics;
    bend_metrics.bend_severity = 1.0F;
    Expect(RunSyntheticScene(bend_metrics, {}).perception.active_module == "bend",
           "baseline bend regression must stay bend");

    ls2k::legacy::LaneMetrics ambiguous_bend_metrics = straight_metrics;
    ambiguous_bend_metrics.valid_row_count = 18;
    ambiguous_bend_metrics.lower_valid_row_count = 6;
    ambiguous_bend_metrics.middle_valid_row_count = 6;
    ambiguous_bend_metrics.upper_valid_row_count = 4;
    ambiguous_bend_metrics.lower_width_median = 176;
    ambiguous_bend_metrics.left_edge.circle_curve = true;
    ambiguous_bend_metrics.left_edge.bend_curve = true;
    ambiguous_bend_metrics.right_visible_confident = true;
    ambiguous_bend_metrics.left_curvature = 10.0F;
    ambiguous_bend_metrics.right_curvature = 1.0F;
    ambiguous_bend_metrics.left_open = 20.0F;
    ambiguous_bend_metrics.right_contract = 10.0F;
    ambiguous_bend_metrics.track_valid = true;
    ambiguous_bend_metrics.track_confidence = 0.8F;
    ambiguous_bend_metrics.heading_error = -0.45F;
    ambiguous_bend_metrics.curvature = -0.12F;
    ambiguous_bend_metrics.track_sign = -1;
    ambiguous_bend_metrics.same_direction_bend_sign = -1;
    ambiguous_bend_metrics.bend_severity = 2.3F;
    const ls2k::legacy::SteeringAnalysisResult ambiguous_bend_analysis =
        RunSyntheticScene(ambiguous_bend_metrics, {});
    Expect(ambiguous_bend_analysis.perception.active_module == "bend",
           "weak circle proof must lose to ordinary bend evidence");

    ls2k::legacy::LaneMetrics zebra_metrics = straight_metrics;
    zebra_metrics.zebra_candidate = true;
    Expect(RunSyntheticScene(zebra_metrics, {}).perception.active_module == "zebra",
           "baseline zebra regression must stay zebra");

    ls2k::legacy::LaneMetrics interior_metrics = straight_metrics;
    interior_metrics.bend_severity = 1.2F;
    ls2k::port::LegacySteeringState prior_circle_entry{};
    prior_circle_entry.active_module = "circle_interior";
    prior_circle_entry.circle_active_direction = "left";
    prior_circle_entry.circle_entry_state = "idle";
    Expect(RunSyntheticScene(interior_metrics, prior_circle_entry).perception.active_module ==
               "circle_interior",
           "baseline circle_interior regression must stay circle_interior");

    ls2k::legacy::LaneMetrics exit_metrics = straight_metrics;
    exit_metrics.right_curvature = 0.0F;
    exit_metrics.circle_left_opposite_straight_confidence = 1.0F;
    exit_metrics.circle_left_opposite_straight_rows = 3;
    ls2k::port::LegacySteeringState prior_circle_interior{};
    prior_circle_interior.active_module = "circle_interior";
    prior_circle_interior.circle_active_direction = "left";
    prior_circle_interior.circle_entry_state = "idle";
    prior_circle_interior.circle_heading_delta_deg = 190.0F;
    prior_circle_interior.circle_last_imu_capture_time_ms = 1;
    const ls2k::legacy::SteeringAnalysisResult exit_analysis =
        RunSyntheticScene(exit_metrics, prior_circle_interior);
    Expect(exit_analysis.perception.active_module == "circle_exit",
           "baseline circle_exit regression must stay circle_exit");
    Expect(exit_analysis.perception.scene_phase == "exit_handover",
           "circle exit must begin with exit_handover");

    ls2k::legacy::LaneMetrics fallback_metrics = exit_metrics;
    fallback_metrics.circle_left_opposite_straight_confidence = 0.0F;
    fallback_metrics.circle_left_opposite_straight_rows = 0;
    ls2k::port::LegacySteeringState prior_fallback{};
    prior_fallback.active_module = "circle_exit";
    prior_fallback.circle_active_direction = "left";
    prior_fallback.circle_entry_state = "idle";
    prior_fallback.circle_exit_state = "tracking";
    prior_fallback.circle_heading_delta_deg = 250.0F;
    prior_fallback.circle_last_imu_capture_time_ms = 1;
    prior_fallback.circle_handover_cycles = 4;
    prior_fallback.circle_last_stable_reference_col = 150;
    prior_fallback.circle_opposite_edge_confirm_cycles = 1;
    const ls2k::legacy::SteeringAnalysisResult fallback_analysis =
        RunSyntheticScene(fallback_metrics, prior_fallback);
    Expect(fallback_analysis.perception.scene_phase == "exit_fallback",
           "missing opposite edge must fall back after exit tracking");
    Expect(fallback_analysis.perception.circle_fallback_reason == "opposite_edge_unstable",
           "fallback path must expose opposite_edge_unstable reason");
}

void TestCircleEntryReleaseAndHeadingTracker() {
    ls2k::port::RuntimeParameters params = MakeSceneParams();
    params.circle_entry.release_loss_cycles = 2;

    ls2k::legacy::LaneMetrics lost_entry_metrics{};
    lost_entry_metrics.threshold = 120;
    lost_entry_metrics.valid_row_count = 12;
    lost_entry_metrics.lower_valid_row_count = 4;
    lost_entry_metrics.middle_valid_row_count = 4;
    lost_entry_metrics.upper_valid_row_count = 4;
    lost_entry_metrics.steering_reference_col = 163;
    lost_entry_metrics.lateral_error = -3.0F;
    lost_entry_metrics.bend_severity = 1.1F;

    ls2k::port::LegacySteeringState prior{};
    prior.active_module = "circle_entry";
    prior.circle_active_direction = "left";
    prior.circle_entry_state = "repairing";
    prior.circle_last_stable_reference_col = 150;

    const ls2k::legacy::SteeringAnalysisResult first_loss =
        RunSyntheticSceneWithInputs(lost_entry_metrics, prior, params, {}, 10);
    Expect(first_loss.perception.active_module == "circle_entry",
           "entry release must not trigger before the configured loss cycle budget");
    Expect(!first_loss.perception.circle_entry_signal_active,
           "lost entry frame must expose inactive entry signal");
    Expect(first_loss.steering_state_update.circle_entry_loss_cycles == 1,
           "first lost frame must increment circle_entry_loss_cycles");
    Expect(first_loss.perception.circle_entry_release_reason == "entry_signal_lost",
           "first lost frame must expose entry_signal_lost release reason");

    prior.circle_entry_loss_cycles = first_loss.steering_state_update.circle_entry_loss_cycles;
    prior.circle_entry_release_reason = first_loss.steering_state_update.circle_entry_release_reason;
    const ls2k::legacy::SteeringAnalysisResult second_loss =
        RunSyntheticSceneWithInputs(lost_entry_metrics, prior, params, {}, 20);
    Expect(second_loss.perception.active_module == "bend",
           "entry release must fall back to the ordinary bend scene after finite loss cycles");
    Expect(second_loss.perception.circle_entry_release_reason == "entry_signal_lost",
           "released frame must retain the entry release reason");

    const ls2k::legacy::CircleHeadingIntegrationResult integrated =
        ls2k::legacy::IntegrateCircleHeadingDeltaDeg(0.0F, 1000, ls2k::port::ImuSample{true, 0, 0, 0, 0, 0, 1.0F, 0}, 2000, false);
    Expect(std::fabs(integrated.heading_delta_deg - 57.2958F) < 0.2F,
           "circle heading tracker must convert rad/s IMU input into degrees");
    Expect(integrated.capture_time_ms == 2000, "heading tracker must advance the capture timestamp");

    const ls2k::legacy::CircleHeadingIntegrationResult frozen =
        ls2k::legacy::IntegrateCircleHeadingDeltaDeg(12.0F, 2000, {}, 2100, false);
    Expect(std::fabs(frozen.heading_delta_deg - 12.0F) < 1e-6F,
           "invalid IMU must freeze the integrated circle heading delta");
    Expect(frozen.imu_invalid, "invalid IMU must be reported by the shared heading tracker");
}

void TestCircleEntryReleaseDecoupling() {
    ls2k::legacy::LaneMetrics lost_entry_metrics{};
    lost_entry_metrics.threshold = 120;
    lost_entry_metrics.valid_row_count = 12;
    lost_entry_metrics.lower_valid_row_count = 4;
    lost_entry_metrics.middle_valid_row_count = 4;
    lost_entry_metrics.upper_valid_row_count = 4;
    lost_entry_metrics.steering_reference_col = 160;
    lost_entry_metrics.lateral_error = 0.0F;
    lost_entry_metrics.bend_severity = 1.0F;

    ls2k::port::LegacySteeringState prior{};
    prior.active_module = "circle_entry";
    prior.circle_active_direction = "left";
    prior.circle_entry_state = "repairing";

    ls2k::port::RuntimeParameters base_params = MakeSceneParams();
    base_params.circle_entry.release_loss_cycles = 1;

    ls2k::port::RuntimeParameters exit_variant = base_params;
    exit_variant.circle_exit.exit_complete_deg = 999.0;
    exit_variant.circle_exit.handover_ramp_cycles = 99;
    const ls2k::legacy::SteeringAnalysisResult exit_tuned =
        RunSyntheticSceneWithInputs(lost_entry_metrics, prior, exit_variant, {}, 10);

    ls2k::port::RuntimeParameters fallback_variant = base_params;
    fallback_variant.circle_fallback.fixsteer_bias_scale = 0.05;
    const ls2k::legacy::SteeringAnalysisResult fallback_tuned =
        RunSyntheticSceneWithInputs(lost_entry_metrics, prior, fallback_variant, {}, 10);

    ls2k::port::RuntimeParameters classifier_variant = base_params;
    classifier_variant.scene_wide_classifier.enter_confirm_cycles = 9;
    classifier_variant.scene_wide_classifier.exit_confirm_cycles = 9;
    const ls2k::legacy::SteeringAnalysisResult classifier_tuned =
        RunSyntheticSceneWithInputs(lost_entry_metrics, prior, classifier_variant, {}, 10);

    Expect(exit_tuned.perception.active_module == "bend",
           "entry release must not depend on circle exit parameters");
    Expect(fallback_tuned.perception.active_module == "bend",
           "entry release must not depend on circle fallback parameters");
    Expect(classifier_tuned.perception.active_module == "bend",
           "entry release must not depend on wide-classifier confirm cycles");
}

void TestGeometryRegression() {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();

    {
        ls2k::legacy::LaneMetrics metrics{};
        metrics.valid_row_count = 24;
        metrics.lower_valid_row_count = 8;
        metrics.upper_valid_row_count = 8;
        metrics.left_edge.circle_curve = true;
        metrics.left_edge.bend_curve = true;
        metrics.left_curvature = -24.0F;
        metrics.right_edge.strict_straight = false;
        metrics.right_visible_confident = false;
        metrics.right_upper_border_touch_ratio = 1.0F;
        ls2k::port::LegacyCameraFrame frame{};
        frame.width = ls2k::port::kCompiledCameraFrameWidth;
        frame.height = ls2k::port::kCompiledCameraFrameHeight;
        const ls2k::port::ImuSample imu{};
        const ls2k::legacy::SteeringSceneContext context{frame, params, {}, imu, 1, metrics};
        Expect(!ls2k::legacy::HasCircleLeftEntryStructure(context),
               "border_truncated opposite edge must not satisfy circle proof");
    }

    {
        const ls2k::port::LegacyCameraFrame visible_frame = MakeSyntheticLaneFrame(false, false);
        const ls2k::legacy::LaneMetrics metrics =
            ls2k::legacy::ExtractLaneMetrics(visible_frame, 128, params, MakeHistoryPrimedState(), {}, 1);
        Expect(!metrics.bend_used_history_fallback,
               "history fallback must stay disabled when current frame anchors are sufficient");
    }

    {
        const ls2k::port::LegacyCameraFrame truncated_frame = MakeSyntheticLaneFrame(true, true);
        const ls2k::port::LegacySteeringState prior_state = MakeHistoryPrimedState();
        const ls2k::legacy::LaneMetrics metrics =
            ls2k::legacy::ExtractLaneMetrics(truncated_frame, 128, params, prior_state, {}, 1);
        ls2k::port::LegacyCameraFrame frame{};
        frame.width = ls2k::port::kCompiledCameraFrameWidth;
        frame.height = ls2k::port::kCompiledCameraFrameHeight;
        const ls2k::port::ImuSample imu{};
        const ls2k::legacy::SteeringSceneContext context{frame, params, prior_state, imu, 1, metrics};
        const ls2k::legacy::SteeringAnalysisResult analysis =
            ls2k::legacy::OrchestrateSteeringScenes(context, false, 1, 1);
        Expect(metrics.bend_used_history_fallback,
               "history fallback must engage only when current anchors are insufficient");
        Expect(!ls2k::legacy::HasCircleLeftEntryStructure(context) &&
                   !ls2k::legacy::HasCircleRightEntryStructure(context),
               "history fallback must not promote circle proofs");
        Expect(!ls2k::legacy::HasCrossUpperFullSpanStructure(context),
               "history fallback must not promote cross proofs");
        Expect(analysis.perception.active_module == "bend",
               "history fallback must only influence bend classification");
    }
}

void TestBottomTrackRegressionFrames() {
    const std::string base =
        "new/verification/steering-debug-20260425T092518Z/steering-media/frames";

    const FixtureAnalysis frame_274 = AnalyzeSequentialFrames(base, {274});
    Expect(frame_274.analysis.perception.active_module == "bend",
           "frame-000274 must stay on bend; " + DescribeAnalysis(frame_274));
    Expect(frame_274.analysis.perception.track_sign <= 0,
           "frame-000274 must not flip to right-turn sign; " + DescribeAnalysis(frame_274));
    Expect(frame_274.analysis.perception.track_source != "history_guarded",
           "frame-000274 must stay on current-frame track evidence; " + DescribeAnalysis(frame_274));
    Expect(!frame_274.analysis.perception.sign_flip_blocked ||
               frame_274.analysis.perception.steering_reference_col <= 190,
           "frame-000274 must not jump steering reference into the wrong right-side region; " +
               DescribeAnalysis(frame_274));

    const FixtureAnalysis frame_296 = AnalyzeSequentialFrames(base, {274, 279, 284, 290, 296});
    Expect(frame_296.analysis.perception.active_module == "bend",
           "frame-000296 must stay on bend; " + DescribeAnalysis(frame_296));
    Expect(frame_296.analysis.perception.track_sign <= 0,
           "frame-000296 must not flip to right-turn sign; " + DescribeAnalysis(frame_296));
    Expect(frame_296.analysis.perception.track_source != "history_guarded",
           "frame-000296 must stay on current-frame track evidence; " + DescribeAnalysis(frame_296));
    Expect(!frame_296.analysis.perception.sign_flip_blocked ||
               frame_296.analysis.perception.steering_reference_col <= 190,
           "frame-000296 must not jump steering reference into the wrong right-side region; " +
               DescribeAnalysis(frame_296));
}

void TestTrackSignContinuityRegression() {
    ls2k::port::TrackHistorySnapshot history{};
    history.valid = true;
    history.turn_sign = 0;
    history.last_nonzero_turn_sign = -1;
    history.zero_turn_sign_frames = 2;
    Expect(ls2k::legacy::EffectivePriorTurnSign(history) == -1,
           "short zero-sign gap must preserve the prior non-zero turn sign");

    history.zero_turn_sign_frames = 4;
    Expect(ls2k::legacy::EffectivePriorTurnSign(history) == 0,
           "long zero-sign gap must expire the preserved turn sign");

    ls2k::legacy::LaneMetrics metrics{};
    metrics.track_valid = true;
    metrics.track_sign = 0;
    ls2k::port::LegacySteeringState prior{};
    prior.track_history.valid = true;
    prior.track_history.last_nonzero_turn_sign = -1;
    prior.track_history.zero_turn_sign_frames = 2;
    const ls2k::port::TrackHistorySnapshot carried =
        ls2k::legacy::BuildTrackHistorySnapshot(metrics, prior);
    Expect(carried.last_nonzero_turn_sign == -1 && carried.zero_turn_sign_frames == 3,
           "track history must carry the prior turn sign through a short zero-sign reconstruction gap");
}

}  // namespace

int main() {
    try {
        TestRepresentativeWideSamples();
        TestSyntheticSceneRegression();
        TestCircleEntryReleaseAndHeadingTracker();
        TestCircleEntryReleaseDecoupling();
        TestGeometryRegression();
        TestBottomTrackRegressionFrames();
        TestTrackSignContinuityRegression();
    } catch (const TestFailure& failure) {
        std::cerr << "scene_classifier_selftest failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "scene_classifier_selftest unexpected exception: " << error.what() << "\n";
        return 1;
    }

    std::cout << "scene_classifier_selftest passed\n";
    return 0;
}
