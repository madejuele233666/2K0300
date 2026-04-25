#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "legacy/steering_bottom_tracker.hpp"
#include "legacy/steering_gyro_continuity.hpp"
#include "runtime/control_decision.hpp"

namespace {

using ls2k::legacy::BottomTrackRequest;
using ls2k::legacy::BottomTrackResult;
using ls2k::legacy::ComputeGyroContinuityConstraint;
using ls2k::legacy::GyroContinuityConstraint;
using ls2k::legacy::TrackBottomConnectedLane;
using ls2k::legacy::TrackSource;
using ls2k::port::ImuSample;
using ls2k::port::LegacyCameraFrame;
using ls2k::port::LegacySteeringState;

struct TestFailure {
    std::string message;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

LegacyCameraFrame MakeBlankFrame() {
    LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    return frame;
}

void FillLane(LegacyCameraFrame& frame,
              int left_bottom,
              int width,
              int shift_left_per_band,
              int row_start = 80,
              int row_end = 184) {
    for (int row = row_start; row <= row_end; ++row) {
        const int upward_bands = (row_end - row) / 8;
        const int left = std::max(0, left_bottom - upward_bands * shift_left_per_band);
        const int right = std::min(frame.width - 1, left + width);
        for (int col = left; col <= right; ++col) {
            frame.gray[static_cast<std::size_t>(row) * frame.width + col] = 255;
        }
    }
}

void FillBrightBlob(LegacyCameraFrame& frame, int left, int top, int right, int bottom) {
    for (int row = top; row <= bottom; ++row) {
        for (int col = left; col <= right; ++col) {
            frame.gray[static_cast<std::size_t>(row) * frame.width + col] = 255;
        }
    }
}

LegacySteeringState MakePriorState(int turn_sign = -1) {
    LegacySteeringState state{};
    state.steering_reference_col = 140;
    state.track_history.valid = true;
    state.track_history.center_anchors[0] = {true, 96, 126};
    state.track_history.center_anchors[1] = {true, 136, 136};
    state.track_history.center_anchors[2] = {true, 176, 146};
    state.track_history.lane_width_px = 110.0F;
    state.track_history.heading_px_per_row = -0.28F;
    state.track_history.curvature_px_per_row2 = -0.02F;
    state.track_history.turn_sign = turn_sign;
    state.track_history.track_confidence = 0.85F;
    state.gyro_continuity.last_valid_capture_time_ms = 1000;
    state.gyro_continuity.filtered_yaw_rate = -4.0F;
    state.gyro_continuity.heading_delta_deg_150ms = -5.5F;
    return state;
}

BottomTrackResult RunTracker(const LegacyCameraFrame& frame,
                             const LegacySteeringState& prior_state,
                             const ImuSample& imu,
                             uint64_t capture_time_ms) {
    const GyroContinuityConstraint continuity =
        ComputeGyroContinuityConstraint(prior_state, imu, capture_time_ms);
    return TrackBottomConnectedLane(
        BottomTrackRequest{frame, 128, 35, 184, 4, prior_state, continuity});
}

void TestTrackerIgnoresFarBrightBlob() {
    LegacyCameraFrame frame = MakeBlankFrame();
    FillLane(frame, 88, 114, 5);
    FillBrightBlob(frame, 220, 24, 312, 84);

    const BottomTrackResult track = RunTracker(frame, MakePriorState(-1), {true, 0, 0, 1, 0, 0, -4, 1100}, 1100);
    Require(track.valid, "bottom-connected tracker must stay valid on the synthetic bend");
    Require(track.track_sign <= 0, "far bright blob must not flip the bend into a right turn");
    Require(track.source != TrackSource::history_guarded,
            "far bright blob case must still be driven by current-frame connected evidence");
    Require(track.lateral_error > 0.0F, "synthetic left bend must keep the centerline on the left half");
}

void TestImuCannotCreateTrack() {
    LegacyCameraFrame frame = MakeBlankFrame();
    const BottomTrackResult track = RunTracker(frame, MakePriorState(-1), {true, 0, 0, 1, 0, 0, 7, 1100}, 1100);
    Require(!track.valid, "IMU continuity must not synthesize a track on a blank frame");
}

void TestGraceWindowAndGatePolicy() {
    const LegacySteeringState prior = MakePriorState(-1);
    const GyroContinuityConstraint grace =
        ComputeGyroContinuityConstraint(prior, ImuSample{}, 1140);
    Require(grace.imu_grace_active, "missing IMU within 150 ms must enter pure-visual grace");

    ls2k::runtime::ControlGateInputs grace_gate{};
    grace_gate.perception_published = true;
    grace_gate.perception_fresh = true;
    grace_gate.perception_capture_time_ms = 1140;
    grace_gate.perception_publish_time_ms = 1140;
    grace_gate.perception_emergency_veto = false;
    grace_gate.low_voltage_emergency = false;
    grace_gate.imu_valid = grace.imu_grace_active;
    grace_gate.encoder_valid = true;
    grace_gate.now_ms = 1140;
    grace_gate.perception_stale_ms = 120;
    Require(!ls2k::runtime::EvaluateControlGate(grace_gate).veto_active,
            "pure-visual grace must keep control eligible while IMU is temporarily absent");

    const GyroContinuityConstraint expired =
        ComputeGyroContinuityConstraint(prior, ImuSample{}, 1201);
    Require(!expired.imu_grace_active, "missing IMU past 150 ms must clear grace");

    ls2k::runtime::ControlGateInputs expired_gate = grace_gate;
    expired_gate.now_ms = 1201;
    expired_gate.perception_capture_time_ms = 1201;
    expired_gate.perception_publish_time_ms = 1201;
    expired_gate.imu_valid = false;
    const auto veto = ls2k::runtime::EvaluateControlGate(expired_gate);
    Require(veto.veto_active && veto.veto_reason == ls2k::runtime::ControlVetoReason::kImuInvalid,
            "expired grace must restore imu_invalid veto");
}

void TestFlipRequiresTwoFramesWithImu() {
    const auto first =
        ls2k::legacy::EvaluateTrackSignFlip({-1, 1, 4, 0.75F, 0, 0, false});
    Require(first.blocked, "first contradictory frame must block the direction flip");
    Require(first.resolved_sign == -1, "first contradictory frame must hold the prior left-turn sign");
    Require(first.pending_sign == 1 && first.pending_frames == 1,
            "first contradictory frame must retain the opposing candidate for the next frame");

    const auto second =
        ls2k::legacy::EvaluateTrackSignFlip({-1, 1, 4, 0.75F, first.pending_sign, first.pending_frames, false});
    Require(!second.blocked, "second supported IMU-backed frame must allow the flip");
    Require(second.resolved_sign == 1, "second supported IMU-backed frame must admit the right-turn sign");
}

void TestFlipRequiresThreeFramesWithoutImu() {
    int pending_sign = 0;
    int pending_frames = 0;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        const auto track =
            ls2k::legacy::EvaluateTrackSignFlip({-1, 1, 4, 0.75F, pending_sign, pending_frames, true});
        if (attempt < 3) {
            Require(track.blocked,
                    "pure-visual grace must keep blocking until the third confirming frame");
            Require(track.resolved_sign == -1,
                    "pure-visual grace must hold the prior direction before the third frame");
        } else {
            Require(!track.blocked,
                    "third pure-visual confirming frame must finally allow the flip");
            Require(track.resolved_sign == 1,
                    "third pure-visual confirming frame must admit the right-turn sign");
        }
        pending_sign = track.pending_sign;
        pending_frames = track.pending_frames;
    }
}

}  // namespace

int main() {
    try {
        TestTrackerIgnoresFarBrightBlob();
        TestImuCannotCreateTrack();
        TestGraceWindowAndGatePolicy();
        TestFlipRequiresTwoFramesWithImu();
        TestFlipRequiresThreeFramesWithoutImu();
    } catch (const TestFailure& failure) {
        std::cerr << "steering_track_fusion_selftest failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "steering_track_fusion_selftest unexpected exception: " << error.what() << "\n";
        return 1;
    }

    std::cout << "steering_track_fusion_selftest passed\n";
    return 0;
}
