#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/camera_logic.hpp"
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
}

FixtureAnalysis AnalyzeRawFixture(const RawFixture& fixture,
                                  const ls2k::port::RuntimeParameters& params,
                                  ls2k::port::LegacySteeringState state) {
    const ls2k::port::LegacyCameraFrame frame = ReadRawFrame(fixture.path);
    FixtureAnalysis result{};
    result.analysis = ls2k::legacy::AnalyzeFrame(frame, params, state, false, 1, 1);
    result.metrics =
        ls2k::legacy::ExtractLaneMetrics(frame,
                                         result.analysis.perception.threshold,
                                         params,
                                         state.steering_reference_col);
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

void ExpectNeutralNotWide(const RawFixture& fixture) {
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    const FixtureAnalysis analysis = AnalyzeRawFixture(fixture, params, ls2k::port::LegacySteeringState{});
    const std::string module = analysis.analysis.perception.active_module;
    Expect(module != "special_wide" && module != "circle_entry" && module != "cross",
           std::string(fixture.label) +
               " must stay out of special_wide/circle_entry/cross; " + DescribeAnalysis(analysis));
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

ls2k::legacy::SteeringAnalysisResult RunSyntheticScene(const ls2k::legacy::LaneMetrics& metrics,
                                                       const ls2k::port::LegacySteeringState& prior_state) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;

    ls2k::port::RuntimeParameters params = MakeSceneParams();
    ls2k::legacy::SteeringSceneContext context{frame, params, prior_state, metrics};
    return ls2k::legacy::OrchestrateSteeringScenes(context, false, 1, 1);
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

    ExpectNeutralNotWide(bend_1);
    ExpectNeutralNotWide(bend_2);
    ExpectNeutralNotWide(bend_3);
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

    ls2k::legacy::LaneMetrics zebra_metrics = straight_metrics;
    zebra_metrics.zebra_candidate = true;
    Expect(RunSyntheticScene(zebra_metrics, {}).perception.active_module == "zebra",
           "baseline zebra regression must stay zebra");

    ls2k::legacy::LaneMetrics interior_metrics = straight_metrics;
    interior_metrics.bend_severity = 1.2F;
    ls2k::port::LegacySteeringState prior_circle_entry{};
    prior_circle_entry.active_module = "circle_entry";
    Expect(RunSyntheticScene(interior_metrics, prior_circle_entry).perception.active_module ==
               "circle_interior",
           "baseline circle_interior regression must stay circle_interior");

    ls2k::legacy::LaneMetrics exit_metrics = straight_metrics;
    exit_metrics.bend_severity = 0.7F;
    exit_metrics.lateral_error = 20.0F;
    ls2k::port::LegacySteeringState prior_circle_interior{};
    prior_circle_interior.active_module = "circle_interior";
    const std::string exit_module = RunSyntheticScene(exit_metrics, prior_circle_interior).perception.active_module;
    Expect(exit_module == "circle_exit" || exit_module == "bend",
           "baseline circle_exit regression must stay circle_exit-compatible");
}

}  // namespace

int main() {
    try {
        TestRepresentativeWideSamples();
        TestSyntheticSceneRegression();
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
