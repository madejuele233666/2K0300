#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_scene_orchestrator.hpp"
#include "legacy/steering_scene_roadblock_stub.hpp"
#include "port/control_types.hpp"

namespace {

using ls2k::port::LegacyCameraFrame;
using ls2k::port::LegacySteeringState;
using ls2k::port::PerceptionResult;
using ls2k::port::RuntimeParameters;

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        Fail(message);
    }
}

LegacyCameraFrame MakeBlankFrame() {
    LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    return frame;
}

void FillLane(LegacyCameraFrame& frame, int left_base, int right_base, int drift_per_16_rows = 0) {
    for (int row = 60; row < frame.height - 16; ++row) {
        const int drift = ((frame.height - row) / 16) * drift_per_16_rows;
        const int left = std::max(0, left_base + drift);
        const int right = std::min(frame.width - 1, right_base + drift);
        for (int col = left; col <= right; ++col) {
            frame.gray[static_cast<std::size_t>(row) * frame.width + col] = 255;
        }
    }
}

PerceptionResult Analyze(const LegacyCameraFrame& frame, const LegacySteeringState& prior_state) {
    RuntimeParameters params{};
    params.camera_frame_width = 320;
    params.camera_frame_height = 240;
    params.see_max = 35.0;
    params.emergency_threshold = 40;
    return ls2k::legacy::AnalyzeFrame(frame, params, prior_state, {}, false, 1, 100).perception;
}

void ExpectRoadblockStubState(const PerceptionResult& result) {
    Expect(result.roadblock_interface_state == "supported_not_implemented",
           "roadblock interface must remain stubbed");
    Expect(!result.roadblock_active, "roadblock must not be falsely reported active");
}

void TestDefaultRuntimeOwnedStateBaseline() {
    const LegacySteeringState state{};
    Expect(state.highest_line == 0, "default highest_line must be zero");
    Expect(state.farthest_line == 0, "default farthest_line must be zero");
    Expect(state.steering_reference_col == 160, "default steering_reference_col must be 160");
    Expect(state.active_module == "straight", "default active_module must be straight");
    Expect(state.scene_phase == "idle", "default scene_phase must be idle");
    Expect(state.scene_override_source == "none", "default scene_override_source must be none");
    Expect(state.roadblock_interface_state == "supported_not_implemented",
           "roadblock interface state must advertise supported_not_implemented");
    Expect(state.last_special_scene_correction == "none",
           "default special-scene correction must be none");
    Expect(!state.roadblock_active, "roadblock must start inactive");
    Expect(state.controller_memory.w_target_last == 0.0F, "w_target_last baseline must be zero");
    Expect(state.controller_memory.camera_error_last == 0.0F, "camera_error_last baseline must be zero");
    Expect(state.controller_memory.gyro_error_last == 0.0F, "gyro_error_last baseline must be zero");
    Expect(state.controller_memory.gyro_i_accumulator == 0.0F,
           "gyro_i_accumulator baseline must be zero");
}

void TestStraightSceneOn320x240Input() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 100, 220);
    const PerceptionResult result = Analyze(frame, {});

    Expect(result.active_module == "straight", "straight frame must stay on straight module");
    Expect(result.scene_phase == "idle", "straight frame must stay idle");
    ExpectRoadblockStubState(result);
    Expect(result.highest_line > 0, "straight frame must expose highest_line");
    Expect(result.farthest_line > 0, "straight frame must expose farthest_line");
    Expect(result.steering_reference_col > 120 && result.steering_reference_col < 200,
           "straight frame must keep steering reference near the lane center");
}

void TestBendSceneSplitFromStraight() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 120, 240, -10);
    const PerceptionResult result = Analyze(frame, {});

    Expect(result.active_module == "bend", "moderate bend frame must activate bend module");
    ExpectRoadblockStubState(result);
    Expect(result.scene_override_source == "lane_geometry", "bend must publish lane_geometry override source");
    Expect(result.last_special_scene_correction == "bend_bias",
           "bend must expose bend_bias correction tag");
}

void TestZebraSceneWinsDedicatedModule() {
    RuntimeParameters params{};
    params.camera_frame_width = 320;
    params.camera_frame_height = 240;
    params.see_max = 35.0;
    params.emergency_threshold = 40;

    LegacyCameraFrame frame = MakeBlankFrame();
    ls2k::legacy::LaneMetrics metrics{};
    metrics.threshold = 120;
    metrics.valid_row_count = 14;
    metrics.lower_valid_row_count = 4;
    metrics.middle_valid_row_count = 4;
    metrics.upper_valid_row_count = 4;
    metrics.zebra_candidate = true;

    const ls2k::port::ImuSample imu{};
    const ls2k::legacy::SteeringSceneContext context{frame, params, {}, imu, 100, metrics};
    const PerceptionResult result =
        ls2k::legacy::OrchestrateSteeringScenes(context, false, 1, 100).perception;
    Expect(result.active_module == "zebra", "zebra pattern must activate zebra module");
    ExpectRoadblockStubState(result);
    Expect(result.scene_phase == "hold", "zebra module must publish hold phase");
}

void TestRoadblockStubNeverMisreportsActive() {
    RuntimeParameters params{};
    LegacyCameraFrame frame = MakeBlankFrame();
    const LegacySteeringState prior{};
    const ls2k::port::ImuSample imu{};
    const ls2k::legacy::SteeringSceneContext context{frame, params, prior, imu, 100, {}};
    const ls2k::legacy::SteeringSceneOutput result = ls2k::legacy::EvaluateRoadblockStubScene(context);

    Expect(!result.active, "roadblock stub must never report itself active");
    Expect(std::string(result.active_module) == "straight",
           "roadblock stub must not force a synthetic roadblock module");
    Expect(std::string(result.scene_phase) == "idle",
           "roadblock stub must leave the scene phase idle");
    Expect(std::string(result.last_special_scene_correction) == "none",
           "roadblock stub must not inject a special-scene correction");
}

void TestCircleTransitionUsesPriorRuntimeState() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 150, 250, -12);

    LegacySteeringState prior{};
    prior.active_module = "circle_interior";
    prior.scene_phase = "interior_tracking";
    prior.circle_active_direction = "left";
    prior.circle_entry_state = "idle";
    const PerceptionResult interior = Analyze(frame, prior);
    Expect(interior.active_module == "circle_interior",
           "circle entry carry-over must advance into circle_interior");
    ExpectRoadblockStubState(interior);

    prior.active_module = "circle_interior";
    prior.scene_phase = "interior_tracking";
    prior.circle_heading_delta_deg = 190.0F;
    prior.circle_last_imu_capture_time_ms = 1;
    LegacyCameraFrame exit_frame = MakeBlankFrame();
    FillLane(exit_frame, 128, 228);
    const PerceptionResult exit_result = Analyze(exit_frame, prior);
    Expect(exit_result.active_module == "circle_exit" || exit_result.active_module == "circle_interior",
           "circle interior recovery must not collapse back into straight immediately");
    ExpectRoadblockStubState(exit_result);
}

}  // namespace

int main() {
    try {
        TestDefaultRuntimeOwnedStateBaseline();
        TestStraightSceneOn320x240Input();
        TestBendSceneSplitFromStraight();
        TestZebraSceneWinsDedicatedModule();
        TestRoadblockStubNeverMisreportsActive();
        TestCircleTransitionUsesPriorRuntimeState();
    } catch (const std::exception& error) {
        std::cerr << "steering_alignment_test failed: " << error.what() << "\n";
        return 1;
    }

    std::cout << "steering_alignment_test passed\n";
    return 0;
}
