#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "legacy/camera_logic.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct RawFrameFixture {
    std::uint64_t frame_id = 0;
    std::string path{};
    int width = 0;
    int height = 0;
};

struct FixtureAnalysis {
    ls2k::legacy::SteeringAnalysisResult analysis{};
    ls2k::legacy::LaneMetrics metrics{};
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
           " left_open=" + std::to_string(metrics.left_open) +
           " right_open=" + std::to_string(metrics.right_open) +
           " left_contract=" + std::to_string(metrics.left_contract) +
           " right_contract=" + std::to_string(metrics.right_contract) +
           " widths=" + std::to_string(metrics.lower_width_median) + "/" +
           std::to_string(metrics.middle_width_median) + "/" + std::to_string(metrics.upper_width_median);
}

std::string ExtractQuotedValue(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\": \"";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos) {
        return {};
    }
    return line.substr(value_start, value_end - value_start);
}

std::uint64_t ExtractUintValue(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\": ";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) {
        return 0;
    }
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find_first_of(",}", value_start);
    return static_cast<std::uint64_t>(std::stoull(line.substr(value_start, value_end - value_start)));
}

int ExtractIntValue(const std::string& line, const std::string& key) {
    return static_cast<int>(ExtractUintValue(line, key));
}

std::vector<RawFrameFixture> LoadRawFixture(const std::string& fixture_dir) {
    const std::string metadata_path = fixture_dir + "/steering-media/frame_metadata.jsonl";
    std::ifstream input(metadata_path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open metadata: " + metadata_path);
    }

    std::vector<RawFrameFixture> frames;
    std::string line;
    while (std::getline(input, line)) {
        RawFrameFixture frame{};
        frame.frame_id = ExtractUintValue(line, "frame_id");
        frame.path = ExtractQuotedValue(line, "frame_path");
        frame.width = ExtractIntValue(line, "width");
        frame.height = ExtractIntValue(line, "height");
        if (!frame.path.empty() && frame.width > 0 && frame.height > 0) {
            frames.push_back(frame);
        }
    }
    return frames;
}

ls2k::port::LegacyCameraFrame ReadRawFrame(const RawFrameFixture& fixture) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = fixture.width;
    frame.height = fixture.height;

    std::ifstream input(fixture.path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open raw frame: " + fixture.path);
    }
    input.read(reinterpret_cast<char*>(frame.gray.data()),
               static_cast<std::streamsize>(fixture.width * fixture.height));
    Expect(static_cast<int>(input.gcount()) == fixture.width * fixture.height,
           "raw frame size must match metadata for " + fixture.path);
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

FixtureAnalysis AnalyzeFixtureFrame(const RawFrameFixture& fixture,
                                    const ls2k::port::RuntimeParameters& params,
                                    ls2k::port::LegacySteeringState& state) {
    const ls2k::port::LegacyCameraFrame frame = ReadRawFrame(fixture);
    FixtureAnalysis result{};
    result.analysis =
        ls2k::legacy::AnalyzeFrame(frame, params, state, false, fixture.frame_id, fixture.frame_id);
    result.metrics = ls2k::legacy::ExtractLaneMetrics(
        frame, result.analysis.perception.threshold, params, state.steering_reference_col);
    ApplyAnalysisToState(result.analysis, state);
    return result;
}

void ExpectFirstTwoFrames(const std::string& fixture_dir,
                          const std::string& expected_second_module,
                          const std::string& label) {
    const std::vector<RawFrameFixture> frames = LoadRawFixture(fixture_dir);
    Expect(frames.size() >= 2, label + " must contain at least two raw frames");

    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    ls2k::port::LegacySteeringState state{};

    const auto first = AnalyzeFixtureFrame(frames[0], params, state);
    Expect(first.analysis.perception.active_module == "special_wide",
           label + " first frame must enter special_wide; " + DescribeAnalysis(first));
    Expect(first.analysis.perception.scene_phase == "sus",
           label + " first frame must enter sus phase; " + DescribeAnalysis(first));

    const auto second = AnalyzeFixtureFrame(frames[1], params, state);
    Expect(second.analysis.perception.active_module == expected_second_module,
           label + " second frame must confirm " + expected_second_module + "; " +
               DescribeAnalysis(second));
}

void ExpectThirdFrameHold(const std::string& fixture_dir,
                          const std::string& expected_module,
                          const std::string& label) {
    const std::vector<RawFrameFixture> frames = LoadRawFixture(fixture_dir);
    Expect(frames.size() >= 3, label + " must contain at least three raw frames");

    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    ls2k::port::LegacySteeringState state{};

    (void)AnalyzeFixtureFrame(frames[0], params, state);
    (void)AnalyzeFixtureFrame(frames[1], params, state);
    const auto third = AnalyzeFixtureFrame(frames[2], params, state);
    Expect(third.analysis.perception.active_module == expected_module,
           label + " third frame must keep " + expected_module + "; " + DescribeAnalysis(third));
}

void TestCircleFixtures() {
    ExpectFirstTwoFrames("new/verification/static-circle-entry-20260425T053113Z",
                         "circle_entry",
                         "circle-1");
    ExpectFirstTwoFrames("new/verification/static-circle-entry-20260425T054335Z",
                         "circle_entry",
                         "circle-2");
    ExpectThirdFrameHold("new/verification/static-circle-entry-20260425T054335Z",
                         "circle_entry",
                         "circle-2");

    const std::vector<RawFrameFixture> frames =
        LoadRawFixture("new/verification/static-circle-entry-20260425T055605Z");
    Expect(frames.size() >= 2, "circle-3 must contain at least two raw frames");
    const ls2k::port::RuntimeParameters params = MakeSceneParams();
    ls2k::port::LegacySteeringState state{};
    (void)AnalyzeFixtureFrame(frames[0], params, state);
    const auto second = AnalyzeFixtureFrame(frames[1], params, state);
    Expect(second.analysis.perception.active_module != "cross",
           "circle-3 must not confirm cross; " + DescribeAnalysis(second));
    Expect(second.analysis.perception.active_module == "special_wide" ||
               second.analysis.perception.active_module == "circle_entry",
           "circle-3 may stay in special_wide or enter circle_entry; " + DescribeAnalysis(second));
}

void TestCrossFixtures() {
    ExpectFirstTwoFrames("new/verification/static-cross-20260425T061249Z", "cross", "cross-1");
    ExpectFirstTwoFrames("new/verification/static-cross-20260425T061406Z", "cross", "cross-2");
    ExpectThirdFrameHold("new/verification/static-cross-20260425T061406Z", "cross", "cross-2");
    ExpectFirstTwoFrames("new/verification/static-cross-20260425T061514Z", "cross", "cross-3");
}

}  // namespace

int main() {
    try {
        TestCircleFixtures();
        TestCrossFixtures();
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
