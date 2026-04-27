#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/control_debug_reporter.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct RecordingDiagnostics final : ls2k::port::DiagnosticSink {
    std::vector<ls2k::port::DiagnosticEvent> events{};

    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }
};

const ls2k::port::DiagnosticEvent* FindEvent(const RecordingDiagnostics& diagnostics, const std::string& code) {
    for (const auto& event : diagnostics.events) {
        if (event.code == code) {
            return &event;
        }
    }
    return nullptr;
}

void TestSteeringSnapshotRemainsUsefulWhenAssistantDisabled() {
    ls2k::runtime::ControlDebugReporter reporter;
    ls2k::port::RuntimeParameters params{};
    params.control_snapshot_emit_interval_ms = 1;
    params.assistant_enabled = false;
    params.steering_media_enabled = false;
    reporter.Configure(params);

    ls2k::runtime::ControlDebugSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.timestamp_ms = 100;
    snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
    snapshot.veto_active = false;
    snapshot.tuning_mode_enabled = false;
    snapshot.turn_suppressed = false;
    snapshot.raw_turn_output = 123;
    snapshot.applied_turn_output = 117;
    snapshot.steering.valid = true;
    snapshot.steering.frame_id = 88;
    snapshot.steering.capture_time_ms = 77;
    snapshot.steering.lateral_error = 12.5;
    snapshot.steering.threshold = 101;
    snapshot.steering.threshold_veto = false;
    snapshot.steering.active_module = "bend";
    snapshot.steering.scene_phase = "tracking";
    snapshot.steering.scene_override_source = "lane_geometry";
    snapshot.steering.scene_width_expand_ratio = 1.32;
    snapshot.steering.scene_cross_bilateral_open_score_m = 0.01;
    snapshot.steering.scene_cross_bilateral_open = false;
    snapshot.steering.scene_cross_candidate = false;
    snapshot.steering.scene_circle_left_candidate = false;
    snapshot.steering.scene_circle_right_candidate = false;
    snapshot.steering.scene_left_open_score = 0.20;
    snapshot.steering.scene_right_open_score = 0.0;
    snapshot.steering.scene_left_boundary_heading_abs_rad = 0.10;
    snapshot.steering.scene_right_boundary_heading_abs_rad = 0.28;
    snapshot.steering.roadblock_interface_state = "supported_not_implemented";
    snapshot.steering.roadblock_active = false;
    snapshot.steering.resolved_fuzzy_p = 22.0;
    snapshot.steering.camera_p_term = 11.0;
    snapshot.steering.camera_d_term = 2.0;
    snapshot.steering.w_target = 13.0;
    snapshot.steering.gyro_z = 1.0;
    snapshot.steering.gyro_error = 12.0;
    snapshot.steering.gyro_p_term = 12.0;
    snapshot.steering.gyro_d_term = 3.0;
    snapshot.steering.raw_turn_output = 123;
    snapshot.steering.applied_turn_output = 117;
    snapshot.steering.circle_entry_signal_active = false;
    snapshot.steering.lookahead_distance_m = 1.4;
    snapshot.steering.lookahead_lateral_error = 0.03;
    snapshot.steering.lookahead_heading_error = 0.02;
    snapshot.steering.reference_curvature = 0.01;
    snapshot.steering.curvature_command = 0.04;
    snapshot.steering.yaw_rate_target = 0.0;

    RecordingDiagnostics diagnostics;
    reporter.MaybeEmit(snapshot, diagnostics);

    const auto* control_event = FindEvent(diagnostics, "control.snapshot");
    const auto* steering_event = FindEvent(diagnostics, "control.steering_snapshot");
    Expect(control_event != nullptr, "assistant-disabled path must still emit control.snapshot");
    Expect(steering_event != nullptr, "assistant-disabled path must still emit control.steering_snapshot");
    Expect(steering_event->message.find("compatibility.") == std::string::npos,
           "steering snapshot must not export deprecated compatibility fields");
    Expect(steering_event->message.find("curvature_command=0.04") != std::string::npos,
           "steering snapshot must expose BEV curvature command");
    Expect(steering_event->message.find("lookahead_distance_m=1.4") != std::string::npos,
           "steering snapshot must expose BEV lookahead distance");
    Expect(steering_event->message.find("active_module=bend") != std::string::npos,
           "steering snapshot must keep active_module for assistant-disabled review");
    Expect(steering_event->message.find("scene_phase=tracking") != std::string::npos,
           "steering snapshot must keep scene_phase for assistant-disabled review");
    Expect(steering_event->message.find("scene_override_source=lane_geometry") != std::string::npos,
           "steering snapshot must keep scene_override_source for assistant-disabled review");
    Expect(steering_event->message.find("scene_evidence.cross_bilateral_open_score_m=0.01") !=
               std::string::npos,
           "steering snapshot must expose BEV cross evidence for assistant-disabled review");
    Expect(steering_event->message.find("scene_evidence.right_boundary_heading_abs_rad=0.28") !=
               std::string::npos,
           "steering snapshot must expose BEV boundary heading evidence for assistant-disabled review");
    Expect(steering_event->message.find("roadblock_interface_state=supported_not_implemented") !=
               std::string::npos,
           "steering snapshot must disclose the roadblock stub state");
    Expect(steering_event->message.find("raw_turn_output=123") != std::string::npos,
           "steering snapshot must keep raw_turn_output for assistant-disabled review");
    Expect(steering_event->message.find("applied_turn_output=117") != std::string::npos,
           "steering snapshot must keep applied_turn_output for assistant-disabled review");
}

}  // namespace

int main() {
    try {
        TestSteeringSnapshotRemainsUsefulWhenAssistantDisabled();
    } catch (const TestFailure& failure) {
        std::cerr << "control_debug_snapshot_contract_test failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "control_debug_snapshot_contract_test unexpected exception: " << error.what() << "\n";
        return 1;
    }

    std::cout << "control_debug_snapshot_contract_test passed\n";
    return 0;
}
