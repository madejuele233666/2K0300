#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/camera_logic.hpp"
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
    Expect(state.active_module == "straight", "default active_module must be straight");
    Expect(state.scene_phase == "idle", "default scene_phase must be idle");
    Expect(state.scene_override_source == "none", "default scene_override_source must be none");
    Expect(state.roadblock_interface_state == "supported_not_implemented",
           "roadblock interface state must advertise supported_not_implemented");
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
    Expect(result.track_valid, "straight frame must expose a valid BEV-compatible track result");
}

void TestBendSceneSplitFromStraight() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 120, 240, -10);
    const PerceptionResult result = Analyze(frame, {});

    Expect(result.active_module == "straight" || result.active_module == "bend",
           "moderate synthetic frame must stay in ordinary BEV ownership");
    ExpectRoadblockStubState(result);
}

void TestCircleTransitionUsesPriorRuntimeState() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 150, 250, -12);

    LegacySteeringState prior{};
    prior.active_module = "circle_interior";
    prior.scene_phase = "interior_tracking";
    prior.scene_fsm.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    prior.scene_fsm.phase = ls2k::port::SpecialScenePhase::kInterior;
    prior.scene_fsm.latched = true;
    prior.scene_fsm.circle_direction = "left";
    const PerceptionResult interior = Analyze(frame, prior);
    Expect(interior.active_module == "circle" || interior.active_module == "straight" ||
               interior.active_module == "bend",
           "BEV FSM carry-over must remain within formal module ownership");
    ExpectRoadblockStubState(interior);

    prior.active_module = "circle_interior";
    prior.scene_phase = "interior_tracking";
    prior.scene_fsm.active_scene = ls2k::port::SpecialSceneKind::kCircleLeft;
    prior.scene_fsm.phase = ls2k::port::SpecialScenePhase::kInterior;
    prior.scene_fsm.latched = true;
    prior.scene_fsm.circle_direction = "left";
    LegacyCameraFrame exit_frame = MakeBlankFrame();
    FillLane(exit_frame, 128, 228);
    const PerceptionResult exit_result = Analyze(exit_frame, prior);
    Expect(exit_result.active_module == "circle" || exit_result.active_module == "straight" ||
               exit_result.active_module == "bend",
           "circle interior recovery must remain within formal module ownership");
    ExpectRoadblockStubState(exit_result);
}

void TestFormalModulesDoNotExposeSpecialWide() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 40, 280);
    const PerceptionResult result = Analyze(frame, {});

    Expect(result.active_module != "special_wide",
           "formal BEV scene ownership must not expose special_wide");
    Expect(result.perception_tag == "bev_first",
           "formal scene ownership must flow through the BEV-first AnalyzeFrame path");
    Expect(result.scene_override_source == "none" || result.scene_override_source == "scene_fsm",
           "scene override source must come from the BEV FSM boundary");
}

}  // namespace

int main() {
    try {
        TestDefaultRuntimeOwnedStateBaseline();
        TestStraightSceneOn320x240Input();
        TestBendSceneSplitFromStraight();
        TestCircleTransitionUsesPriorRuntimeState();
        TestFormalModulesDoNotExposeSpecialWide();
    } catch (const std::exception& error) {
        std::cerr << "steering_alignment_test failed: " << error.what() << "\n";
        return 1;
    }

    std::cout << "steering_alignment_test passed\n";
    return 0;
}
