#ifndef LS2K_PORT_CIRCLE_V2_TYPES_HPP
#define LS2K_PORT_CIRCLE_V2_TYPES_HPP

#include <cstdint>

#include "port/bev_reference_types.hpp"

namespace ls2k::port {

enum class CircleDir {
    kNone,
    kLeft,
    kRight,
};

enum class CirclePhase {
    kIdle,
    kApproach,
    kInnerTrace,
    kExitTrace,
};

enum class CircleV2ReferenceRole {
    kNone,
    kInnerTrace,
    kExitTrace,
};

enum class CircleV2TelemetryReason {
    kNone,
    kPhase1CueLeft,
    kPhase1CueRight,
    kEntryGateReached,
    kExitGateReached,
    kInnerTraceYawStalled,
    kExitHoldReleased,
    kGeometryUnavailable,
};

struct CircleV2StageClock {
    uint64_t enter_capture_time_ms = 0;
    int phase_frame_index = 0;
    float max_directed_turn_angle_rad = 0.0F;
};

struct CircleV2Memory {
    CirclePhase phase = CirclePhase::kIdle;
    CircleDir dir = CircleDir::kNone;
    CircleV2StageClock clock{};
};

struct CircleV2Params {
    float exit_yaw_threshold_rad = 5.75958653158F;
    int exit_hold_frames = 60;
    int inner_trace_stall_timeout_ms = 4000;
    float inner_trace_stall_yaw_min_rad = 0.28797932658F;
    float inner_trace_path_offset_m = 0.0F;
    float opposite_straight_confidence_min = 0.50F;
};

struct CircleV2ReferencePlan {
    CircleDir dir = CircleDir::kNone;
    CircleV2ReferenceRole role = CircleV2ReferenceRole::kNone;
    BEVReferencePath reference_path{};
};

struct CircleV2PointObservation {
    bool available = false;
    BEVPoint point{};
};

struct CircleV2EntryPointObservation {
    CircleV2PointObservation left{};
    CircleV2PointObservation right{};
};

struct CircleV2Telemetry {
    CirclePhase frame_phase = CirclePhase::kIdle;
    CirclePhase next_phase = CirclePhase::kIdle;
    CircleDir dir = CircleDir::kNone;
    CircleV2ReferenceRole reference_role = CircleV2ReferenceRole::kNone;
    CircleV2TelemetryReason reason = CircleV2TelemetryReason::kNone;
    bool motion_arc_available = false;
    uint64_t inner_trace_elapsed_ms = 0;
    float directed_turn_angle_rad = 0.0F;
    CircleV2EntryPointObservation entry_points{};
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CIRCLE_V2_TYPES_HPP
