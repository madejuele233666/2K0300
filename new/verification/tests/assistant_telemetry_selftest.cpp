#include <cstdlib>
#include <iostream>
#include <string>

#include "platform/assistant_protocol.hpp"
#include "runtime/assistant_telemetry_view.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

ls2k::runtime::ControlDebugSnapshot MakeSnapshot() {
    ls2k::runtime::ControlDebugSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
    snapshot.steering.element_evidence.cross_exit.present = true;
    snapshot.steering.element_evidence.cross_exit.confidence = 0.82;
    snapshot.steering.element_evidence.cross_exit.forward_min_m = 0.20;
    snapshot.steering.element_evidence.cross_exit.forward_max_m = 0.42;
    snapshot.steering.element_evidence.cross_exit.lateral_min_m = -0.35;
    snapshot.steering.element_evidence.cross_exit.lateral_max_m = 0.36;
    snapshot.steering.element_evidence.cross_exit.sampleable_count = 120;
    snapshot.steering.element_evidence.cross_exit.supporting_white_count = 96;
    snapshot.steering.element_evidence.cross_exit.unknown_count = 3;
    snapshot.steering.element_evidence.cross_exit.reason = "present";
    snapshot.steering.element_evidence.cross_exit.candidate.built = true;
    snapshot.steering.element_evidence.cross_exit.candidate.takeover_enabled = false;
    snapshot.steering.element_evidence.cross_exit.candidate.included_in_arbitration = false;
    snapshot.steering.element_evidence.cross_exit.candidate.reason = "takeover_disabled";
    snapshot.steering.visual_reference.present = true;
    snapshot.steering.visual_reference.source = "roadblock_bypass";
    snapshot.steering.visual_reference.reason = "special_visual_candidate_selected";
    snapshot.steering.visual_reference.candidate_count = 3;
    snapshot.steering.visual_reference.rejected_candidate_reason =
        "none_candidate_not_visual";
    snapshot.steering.reference.mode = "interval_center";
    snapshot.steering.reference.source = "roadblock_bypass";
    snapshot.steering.eligibility.usable = true;
    snapshot.steering.eligibility.leading_usable_samples = 5;
    snapshot.steering.eligibility.leading_min_forward_m = 0.05;
    snapshot.steering.eligibility.leading_max_forward_m = 0.25;
    snapshot.steering.eligibility.reason = "ok";
    snapshot.steering.lateral_error.computed = true;
    snapshot.steering.lateral_error.weighted_lateral_error_m = 0.015;
    snapshot.steering.lateral_error.weighted_sample_count = 4;
    snapshot.steering.lateral_error.weight_sum = 2.5;
    snapshot.steering.lateral_error.reason = "ok";
    snapshot.steering.perception_health.projector_ok = true;
    snapshot.steering.perception_health.reason = "ok";
    snapshot.steering.reference_control.ready = true;
    snapshot.steering.reference_control.reason = "ready";
    snapshot.steering.safety_gate.veto_active = false;
    snapshot.steering.safety_gate.reason = "clear";
    snapshot.steering.degraded.active = false;
    snapshot.steering.degraded.reason = "none";
    snapshot.steering.yaw_control.turn_output_target = 0.12;
    snapshot.steering.actuator.raw_turn_output = 12;
    snapshot.steering.actuator.applied_turn_output = 10;
    snapshot.tuning_mode_enabled = true;
    snapshot.turn_suppressed = false;
    snapshot.effective_speed_target = 1.2;
    snapshot.left_speed_target = 1.1;
    snapshot.right_speed_target = 1.3;
    snapshot.left_measured_speed = 1.0;
    snapshot.right_measured_speed = 1.25;
    snapshot.left_pwm_command = 120;
    snapshot.right_pwm_command = 130;
    return snapshot;
}

void TestSnapshotFactsMapToAssistantView() {
    const ls2k::platform::AssistantTelemetryView telemetry =
        ls2k::runtime::BuildAssistantTelemetryView(MakeSnapshot());
    Expect(telemetry.motion_phase == "RUNNING", "motion phase must be mapped");
    Expect(telemetry.element_evidence.cross_exit.present,
           "cross evidence presence must be copied");
    Expect(telemetry.element_evidence.cross_exit.reason == "present",
           "cross evidence reason must be copied");
    Expect(telemetry.element_evidence.cross_exit.candidate.built,
           "cross candidate build state must be copied");
    Expect(!telemetry.element_evidence.cross_exit.candidate.included_in_arbitration,
           "disabled cross candidate inclusion must be copied");
    Expect(telemetry.visual_reference.present,
           "visual reference presence must be copied");
    Expect(telemetry.visual_reference.source == "roadblock_bypass",
           "visual reference source must be copied");
    Expect(telemetry.visual_reference.reason == "special_visual_candidate_selected",
           "visual reference reason must be copied");
    Expect(telemetry.visual_reference.candidate_count == 3,
           "visual reference candidate count must be copied");
    Expect(telemetry.visual_reference.rejected_candidate_reason ==
               "none_candidate_not_visual",
           "visual reference rejection reason must be copied");
    Expect(telemetry.reference.mode == "interval_center",
           "selected reference mode must be copied");
    Expect(telemetry.reference.source == "roadblock_bypass",
           "selected reference source must be copied");
}

void TestAssistantTelemetryJsonEmitsVisualReferenceFacts() {
    const ls2k::platform::AssistantTelemetryView telemetry =
        ls2k::runtime::BuildAssistantTelemetryView(MakeSnapshot());
    const std::string json = ls2k::platform::EncodeAssistantTelemetry(telemetry);
    Expect(Contains(json, "\"element_evidence\":{\"cross_exit\":{\"present\":true"),
           "assistant telemetry must include element evidence object");
    Expect(Contains(json, "\"candidate\":{\"built\":true"),
           "assistant telemetry must include element candidate summary");
    Expect(Contains(json, "\"included_in_arbitration\":false"),
           "assistant telemetry must expose disabled arbitration inclusion");
    Expect(Contains(json, "\"visual_reference\":{\"present\":true"),
           "assistant telemetry must include visual_reference object");
    Expect(Contains(json, "\"source\":\"roadblock_bypass\""),
           "assistant telemetry must include visual reference source");
    Expect(Contains(json, "\"reason\":\"special_visual_candidate_selected\""),
           "assistant telemetry must include visual reference reason");
    Expect(Contains(json, "\"candidate_count\":3"),
           "assistant telemetry must include visual reference candidate count");
    Expect(Contains(json,
                    "\"rejected_candidate_reason\":\"none_candidate_not_visual\""),
           "assistant telemetry must include rejected candidate reason");
    Expect(Contains(json,
                    "\"reference\":{\"mode\":\"interval_center\",\"source\":\"roadblock_bypass\"}"),
           "assistant telemetry must preserve selected reference facts");
}

}  // namespace

int main() {
    try {
        TestSnapshotFactsMapToAssistantView();
        TestAssistantTelemetryJsonEmitsVisualReferenceFacts();
    } catch (const TestFailure& failure) {
        std::cerr << "assistant_telemetry_selftest failed: " << failure.message
                  << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "assistant_telemetry_selftest passed\n";
    return EXIT_SUCCESS;
}
