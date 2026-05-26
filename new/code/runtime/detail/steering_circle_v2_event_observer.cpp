#include "runtime/detail/steering_circle_v2_internal.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::runtime::detail {
namespace {

bool EntryGateReached(const CircleSideExpansionObservation& expansion, CircleDir dir) {
    if (dir == CircleDir::kLeft) {
        return expansion.left_entry_gate_reached;
    }
    if (dir == CircleDir::kRight) {
        return expansion.right_entry_gate_reached;
    }
    return false;
}

float CircleTurnSign(CircleDir dir) {
    if (dir == CircleDir::kLeft) {
        return -1.0F;
    }
    if (dir == CircleDir::kRight) {
        return 1.0F;
    }
    return 0.0F;
}

uint64_t InnerTraceElapsedMs(const CircleV2Memory& prior, CaptureStamp stamp) {
    if (stamp.capture_time_ms < prior.clock.enter_capture_time_ms) {
        return 0;
    }
    return stamp.capture_time_ms - prior.clock.enter_capture_time_ms;
}

bool StallTimeoutReached(uint64_t elapsed_ms, const CircleV2Params& params) {
    const uint64_t timeout_ms =
        static_cast<uint64_t>(std::max(1, params.inner_trace_stall_timeout_ms));
    return elapsed_ms >= timeout_ms;
}

}  // namespace

CircleV2Events ObserveCircleV2Events(const SceneFrameView& frame,
                                     const CircleSideExpansionObservation& expansion,
                                     const CircleV2Memory& prior,
                                     const CircleV2Params& params) {
    CircleV2Events events{};
    switch (prior.phase) {
        case CirclePhase::kIdle:
            events.detected_dir = expansion.detected_dir;
            break;
        case CirclePhase::kApproach:
            events.entry_gate_reached = EntryGateReached(expansion, prior.dir);
            break;
        case CirclePhase::kInnerTrace: {
            events.inner_trace_elapsed_ms = InnerTraceElapsedMs(prior, frame.stamp);
            float yaw_delta = 0.0F;
            if (!frame.motion_arc.TryYawDeltaRad(prior.clock.enter_capture_time_ms,
                                                 frame.stamp.capture_time_ms,
                                                 yaw_delta)) {
                break;
            }
            events.motion_arc_available = true;
            const float directed_turn_angle = CircleTurnSign(prior.dir) * yaw_delta;
            events.directed_turn_angle_rad = directed_turn_angle;
            const float progress_angle =
                std::max(prior.clock.max_directed_turn_angle_rad, directed_turn_angle);
            events.exit_gate_reached =
                progress_angle >= params.exit_yaw_threshold_rad;
            events.inner_trace_stalled =
                !events.exit_gate_reached &&
                StallTimeoutReached(events.inner_trace_elapsed_ms, params) &&
                progress_angle < params.inner_trace_stall_yaw_min_rad;
            break;
        }
        case CirclePhase::kExitTrace:
            break;
    }
    return events;
}

}  // namespace ls2k::runtime::detail
