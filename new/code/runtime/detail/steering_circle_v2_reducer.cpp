#include "runtime/detail/steering_circle_v2_internal.hpp"

#include <algorithm>

namespace ls2k::runtime::detail {
namespace {

void EnterPhase(CircleV2Memory& memory, CirclePhase phase, CaptureStamp stamp) {
    memory.phase = phase;
    memory.clock = {};
    memory.clock.enter_capture_time_ms = stamp.capture_time_ms;
}

void EnterIdle(CircleV2Memory& memory) {
    memory.phase = CirclePhase::kIdle;
    memory.dir = CircleDir::kNone;
    memory.clock = {};
}

CircleV2ReferenceRole RoleForPhase(CirclePhase phase) {
    if (phase == CirclePhase::kInnerTrace) {
        return CircleV2ReferenceRole::kInnerTrace;
    }
    if (phase == CirclePhase::kExitTrace) {
        return CircleV2ReferenceRole::kExitTrace;
    }
    return CircleV2ReferenceRole::kNone;
}

}  // namespace

CircleV2Decision ReduceCircleV2(const CircleV2Memory& prior,
                                const CircleV2Events& events,
                                CaptureStamp stamp,
                                const CircleV2Params& params) {
    CircleV2Memory current = prior;
    CircleV2TelemetryReason reason = CircleV2TelemetryReason::kNone;
    if (prior.phase == CirclePhase::kInnerTrace && events.motion_arc_available) {
        current.clock.max_directed_turn_angle_rad =
            std::max(current.clock.max_directed_turn_angle_rad,
                     events.directed_turn_angle_rad);
    }

    switch (prior.phase) {
        case CirclePhase::kIdle:
            if (events.detected_dir != CircleDir::kNone) {
                current.dir = events.detected_dir;
                EnterPhase(current, CirclePhase::kApproach, stamp);
                reason = events.detected_dir == CircleDir::kLeft
                             ? CircleV2TelemetryReason::kPhase1CueLeft
                             : CircleV2TelemetryReason::kPhase1CueRight;
            }
            break;
        case CirclePhase::kApproach:
            if (events.entry_gate_reached) {
                EnterPhase(current, CirclePhase::kInnerTrace, stamp);
                reason = CircleV2TelemetryReason::kEntryGateReached;
            }
            break;
        case CirclePhase::kInnerTrace:
            if (events.exit_gate_reached) {
                EnterPhase(current, CirclePhase::kExitTrace, stamp);
                reason = CircleV2TelemetryReason::kExitGateReached;
            } else if (events.inner_trace_stalled) {
                EnterIdle(current);
                reason = CircleV2TelemetryReason::kInnerTraceYawStalled;
            }
            break;
        case CirclePhase::kExitTrace:
            break;
    }

    CircleV2Decision decision{};
    decision.reference.dir = current.dir;
    decision.reference.role = RoleForPhase(current.phase);
    decision.reason = reason;

    CircleV2Memory next = current;
    const int hold_frames = std::max(2, params.exit_hold_frames);
    if (current.phase == CirclePhase::kExitTrace &&
        current.clock.phase_frame_index + 1 >= hold_frames) {
        EnterIdle(next);
        if (reason == CircleV2TelemetryReason::kNone) {
            decision.reason = CircleV2TelemetryReason::kExitHoldReleased;
        }
    } else if (current.phase != CirclePhase::kIdle) {
        next.clock.phase_frame_index = current.clock.phase_frame_index + 1;
    }
    decision.next_memory = next;
    return decision;
}

CircleV2Telemetry BuildCircleV2Telemetry(const CircleV2Decision& decision,
                                         const CircleV2Events& events,
                                         const CircleV2Geometry& geometry,
                                         const CircleSideExpansionObservation& expansion) {
    CircleV2Telemetry telemetry{};
    telemetry.next_phase = decision.next_memory.phase;
    telemetry.dir = decision.reference.dir;
    telemetry.reference_role = decision.reference.role;
    telemetry.reason = decision.reason;
    telemetry.motion_arc_available = events.motion_arc_available;
    telemetry.inner_trace_elapsed_ms = events.inner_trace_elapsed_ms;
    telemetry.directed_turn_angle_rad = events.directed_turn_angle_rad;
    telemetry.entry_points.left.available = expansion.left_p_available;
    telemetry.entry_points.left.point = expansion.left_p;
    telemetry.entry_points.right.available = expansion.right_p_available;
    telemetry.entry_points.right.point = expansion.right_p;
    if (decision.reference.role == CircleV2ReferenceRole::kInnerTrace) {
        telemetry.frame_phase = CirclePhase::kInnerTrace;
    } else if (decision.reference.role == CircleV2ReferenceRole::kExitTrace) {
        telemetry.frame_phase = CirclePhase::kExitTrace;
    } else {
        telemetry.frame_phase = decision.next_memory.phase;
    }
    if ((decision.reference.role == CircleV2ReferenceRole::kInnerTrace ||
         decision.reference.role == CircleV2ReferenceRole::kExitTrace) &&
        !geometry.available) {
        telemetry.reason = CircleV2TelemetryReason::kGeometryUnavailable;
    }
    return telemetry;
}

}  // namespace ls2k::runtime::detail
