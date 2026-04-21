#ifndef LS2K_RUNTIME_RUNTIME_STATE_HPP
#define LS2K_RUNTIME_RUNTIME_STATE_HPP

#include <atomic>
#include <cstdint>
#include <mutex>

#include "port/control_types.hpp"
#include "runtime/control_decision.hpp"
#include "runtime/control_debug_snapshot.hpp"
#include "runtime/motion_types.hpp"
#include "runtime/tuning_state.hpp"

namespace ls2k::runtime {

struct RuntimeState {
    // Retained and explicitly initialized carry-over patterns from legacy ISR/runtime.
    float W_Target_last = 0.0F;
    int bcount = 0;
    bool circle_find = false;
    bool zebra_flag = false;
    bool cross_flag = false;

    // Shared runtime channels.
    std::mutex shared_mutex{};
    port::PerceptionResult perception{};
    port::ImuSample imu{};
    port::EncoderDelta encoder{};
    port::CameraCapture latest_camera_capture{};
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
