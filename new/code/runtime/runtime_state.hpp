#ifndef LS2K_RUNTIME_RUNTIME_STATE_HPP
#define LS2K_RUNTIME_RUNTIME_STATE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "port/control_types.hpp"
#include "runtime/control_decision.hpp"
#include "runtime/control_debug_snapshot.hpp"
#include "runtime/motion_types.hpp"
#include "runtime/tuning_state.hpp"

namespace ls2k::runtime {

inline void ResetSteeringTrackMemory(port::LegacySteeringState& steering_state) {
    steering_state.last_bev_track = {};
    steering_state.bev_track_memory = {};
    steering_state.lane_geometry_recent = {};
    steering_state.lane_geometry_previous = {};
    steering_state.track_history = {};
    steering_state.gyro_continuity = {};
}

inline void ResetSteeringSceneMemory(port::LegacySteeringState& steering_state) {
    steering_state.active_module = "straight";
    steering_state.scene_phase = "idle";
    steering_state.scene_override_source = "none";
    steering_state.roadblock_active = false;
    steering_state.roadblock_interface_state = "supported_not_implemented";
    steering_state.scene_debug_candidate = "none";
    steering_state.scene_debug_candidate_streak = 0;
    steering_state.scene_cross_candidate_score_last = 0.0F;
    steering_state.scene_circle_left_candidate_score_last = 0.0F;
    steering_state.scene_circle_right_candidate_score_last = 0.0F;
    steering_state.scene_fsm = {};
}

inline void ResetSteeringReferenceMemory(port::LegacySteeringState& steering_state) {
    steering_state.reference_policy = {};
}

inline void ResetSteeringControllerMemory(port::LegacySteeringState& steering_state) {
    steering_state.drive_cycle_count = 0;
    steering_state.controller_memory = {};
}

inline void ResetSteeringRuntimeState(port::LegacySteeringState& steering_state) {
    ResetSteeringTrackMemory(steering_state);
    ResetSteeringSceneMemory(steering_state);
    ResetSteeringReferenceMemory(steering_state);
    ResetSteeringControllerMemory(steering_state);
}

struct CameraCaptureHistory {
    static constexpr std::size_t kCapacity = 8;

    void Push(const port::CameraCapture& capture) {
        captures[next_index] = capture;
        next_index = (next_index + 1) % kCapacity;
        if (count < kCapacity) {
            ++count;
        }
    }

    const port::CameraCapture* FindExact(uint64_t frame_id, uint64_t capture_time_ms) const {
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t index = (next_index + kCapacity - 1 - offset) % kCapacity;
            const port::CameraCapture& capture = captures[index];
            if (capture.frame_id == frame_id && capture.capture_time_ms == capture_time_ms) {
                return &capture;
            }
        }
        return nullptr;
    }

    std::array<port::CameraCapture, kCapacity> captures{};
    std::size_t next_index = 0;
    std::size_t count = 0;
};

struct RuntimeState {
    port::LegacySteeringState steering_state{};

    // Shared runtime channels.
    std::mutex shared_mutex{};
    port::PerceptionResult perception{};
    port::ImuSample imu{};
    port::EncoderDelta encoder{};
    port::CameraCapture latest_camera_capture{};
    CameraCaptureHistory recent_camera_captures{};
    port::ActuatorCommand last_command{};
    ControlCycleObservation control_observation{};
    ControlDebugSnapshot control_debug_snapshot{};

    // Lifecycle flags.
    bool startup_complete = false;
    bool degraded_startup = false;
    bool timer_started = false;
    bool actuators_armed = false;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> exit_requested{false};
    bool automation_start_fired = false;
    MotionIntent motion_intent{};
    MotionSupervisorState motion_state{};
    RuntimeTuningState tuning_state{};
    std::atomic<bool> low_voltage_emergency{false};

    std::atomic<uint64_t> control_cycle_count{0};
    std::atomic<uint64_t> perception_publish_count{0};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_RUNTIME_STATE_HPP
