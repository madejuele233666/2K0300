#include "runtime/steering_circle_v2_scene.hpp"

#include "runtime/detail/steering_circle_v2_internal.hpp"

namespace ls2k::runtime {

const char* ToString(CircleDir dir) {
    switch (dir) {
        case CircleDir::kNone:
            return "none";
        case CircleDir::kLeft:
            return "left";
        case CircleDir::kRight:
            return "right";
    }
    return "none";
}

const char* ToString(CirclePhase phase) {
    switch (phase) {
        case CirclePhase::kIdle:
            return "idle";
        case CirclePhase::kApproach:
            return "approach";
        case CirclePhase::kInnerTrace:
            return "inner_trace";
        case CirclePhase::kExitTrace:
            return "exit_trace";
    }
    return "idle";
}

const char* ToString(CircleV2ReferenceRole role) {
    switch (role) {
        case CircleV2ReferenceRole::kNone:
            return "none";
        case CircleV2ReferenceRole::kInnerTrace:
            return "inner_trace";
        case CircleV2ReferenceRole::kExitTrace:
            return "exit_trace";
    }
    return "none";
}

const char* ToString(CircleV2TelemetryReason reason) {
    switch (reason) {
        case CircleV2TelemetryReason::kNone:
            return "none";
        case CircleV2TelemetryReason::kPhase1CueLeft:
            return "phase1_cue_left";
        case CircleV2TelemetryReason::kPhase1CueRight:
            return "phase1_cue_right";
        case CircleV2TelemetryReason::kEntryGateReached:
            return "entry_gate_reached";
        case CircleV2TelemetryReason::kExitGateReached:
            return "exit_gate_reached";
        case CircleV2TelemetryReason::kInnerTraceYawStalled:
            return "inner_trace_yaw_stalled";
        case CircleV2TelemetryReason::kExitHoldReleased:
            return "exit_hold_released";
        case CircleV2TelemetryReason::kGeometryUnavailable:
            return "geometry_unavailable";
    }
    return "none";
}

void ResetCircleV2Memory(CircleV2Memory& memory) {
    memory = {};
}

CircleV2StepResult CircleV2Scene::Step(const SceneFrameView& frame,
                                       const CircleV2Memory& prior,
                                       const CircleV2Params& params) const {
    const detail::CircleSideExpansionObservation expansion =
        detail::ObserveCircleSideExpansion(frame, params);
    const detail::CircleV2Events events =
        detail::ObserveCircleV2Events(frame, expansion, prior, params);
    const detail::CircleV2Decision decision =
        detail::ReduceCircleV2(prior, events, frame.stamp, params);
    const detail::CircleV2Geometry geometry =
        detail::ObserveCircleV2Geometry(frame, decision.reference, expansion, params);

    CircleV2StepResult result{};
    result.next_memory = decision.next_memory;
    result.telemetry = detail::BuildCircleV2Telemetry(decision, events, geometry, expansion);
    result.reference_plan =
        detail::ComposeCircleV2Reference(decision.reference, geometry, params);
    return result;
}

}  // namespace ls2k::runtime
