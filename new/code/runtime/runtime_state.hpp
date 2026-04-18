#ifndef LS2K_RUNTIME_RUNTIME_STATE_HPP
#define LS2K_RUNTIME_RUNTIME_STATE_HPP

#include <atomic>
#include <cstdint>
#include <mutex>

#include "port/control_types.hpp"
#include "runtime/control_decision.hpp"

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
    port::ActuatorCommand last_command{};
    ControlCycleObservation control_observation{};

    // Lifecycle flags.
    bool startup_complete = false;
    bool degraded_startup = false;
    bool timer_started = false;
    bool actuators_armed = false;
    bool stop_requested = false;
    std::atomic<bool> low_voltage_emergency{false};

    std::atomic<uint64_t> control_cycle_count{0};
    std::atomic<uint64_t> perception_publish_count{0};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_RUNTIME_STATE_HPP
