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
