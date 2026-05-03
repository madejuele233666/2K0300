#ifndef LS2K_RUNTIME_RUNTIME_STATE_HPP
#define LS2K_RUNTIME_RUNTIME_STATE_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "port/camera_frame_types.hpp"
#include "port/perception_result.hpp"
#include "port/sensor_sample_types.hpp"
#include "port/steering_state_types.hpp"
#include "runtime/control_decision.hpp"
#include "runtime/control_debug_snapshot.hpp"
#include "runtime/motion_types.hpp"
#include "runtime/tuning_state.hpp"

namespace ls2k::runtime {

inline void ResetSteeringPerceptionMemory(port::SteeringPerceptionMemory& memory) {
    memory = {};
}

inline void ResetSteeringControlMemory(port::SteeringControlMemory& memory) {
    memory = {};
}

enum class OwnedCameraFrameSlotState {
    kFree,
    kReady,
    kEncoding
};

struct CameraFrameHandle {
    bool valid = false;
    std::size_t slot_id = 0;
    uint64_t generation = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
};

struct OwnedCameraFrameSlot {
    std::size_t slot_id = 0;
    uint64_t generation = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    OwnedCameraFrameSlotState state = OwnedCameraFrameSlotState::kFree;
    std::array<std::uint8_t, port::kCompiledCameraFrameWidth * port::kCompiledCameraFrameHeight> gray{};
};

struct CameraCaptureHistory {
    static constexpr std::size_t kCapacity = 8;

    void Push(const CameraFrameHandle& handle) {
        handles[next_index] = handle;
        next_index = (next_index + 1) % kCapacity;
        if (count < kCapacity) {
            ++count;
        }
    }

    const CameraFrameHandle* FindExact(uint64_t frame_id, uint64_t capture_time_ms) const {
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t index = (next_index + kCapacity - 1 - offset) % kCapacity;
            const CameraFrameHandle& handle = handles[index];
            if (handle.valid && handle.frame_id == frame_id && handle.capture_time_ms == capture_time_ms) {
                return &handle;
            }
        }
        return nullptr;
    }

    std::array<CameraFrameHandle, kCapacity> handles{};
    std::size_t next_index = 0;
    std::size_t count = 0;
};

inline CameraFrameHandle MaterializeOwnedCameraFrame(
    std::array<OwnedCameraFrameSlot, 3>& slots,
    std::size_t& next_slot_index,
    const port::LegacyCameraFrameView& view) {
    CameraFrameHandle handle{};
    if (!view.Valid() ||
        view.width > port::kCompiledCameraFrameWidth ||
        view.height > port::kCompiledCameraFrameHeight) {
        return handle;
    }

    constexpr std::size_t kSlotCount = 3;
    std::size_t selected = kSlotCount;
    for (std::size_t attempt = 0; attempt < kSlotCount; ++attempt) {
        const std::size_t index = (next_slot_index + attempt) % kSlotCount;
        if (slots[index].state != OwnedCameraFrameSlotState::kEncoding) {
            selected = index;
            break;
        }
    }
    if (selected == kSlotCount) {
        return handle;
    }
    next_slot_index = (selected + 1) % kSlotCount;

    OwnedCameraFrameSlot& slot = slots[selected];
    slot.slot_id = selected;
    slot.state = OwnedCameraFrameSlotState::kReady;
    slot.generation = slot.generation == UINT64_MAX ? 1 : slot.generation + 1;
    slot.frame_id = view.frame_id;
    slot.capture_time_ms = view.capture_time_ms;
    slot.width = view.width;
    slot.height = view.height;
    slot.stride = view.width;
    for (int row = 0; row < view.height; ++row) {
        const std::uint8_t* src =
            view.gray + static_cast<std::size_t>(row) * static_cast<std::size_t>(view.stride);
        std::uint8_t* dst =
            slot.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot.stride);
        std::copy(src, src + view.width, dst);
    }

    handle.valid = true;
    handle.slot_id = selected;
    handle.generation = slot.generation;
    handle.frame_id = slot.frame_id;
    handle.capture_time_ms = slot.capture_time_ms;
    handle.width = slot.width;
    handle.height = slot.height;
    handle.stride = slot.stride;
    return handle;
}

inline bool CopyOwnedCameraFrameByHandle(const std::array<OwnedCameraFrameSlot, 3>& slots,
                                         const CameraFrameHandle& handle,
                                         port::LegacyCameraFrame& out) {
    if (!handle.valid || handle.slot_id >= slots.size()) {
        return false;
    }
    const OwnedCameraFrameSlot& slot = slots[handle.slot_id];
    if (slot.state == OwnedCameraFrameSlotState::kFree ||
        slot.generation != handle.generation ||
        slot.frame_id != handle.frame_id ||
        slot.capture_time_ms != handle.capture_time_ms ||
        slot.width <= 0 ||
        slot.height <= 0 ||
        slot.width > port::kCompiledCameraFrameWidth ||
        slot.height > port::kCompiledCameraFrameHeight) {
        return false;
    }
    out = {};
    out.width = slot.width;
    out.height = slot.height;
    for (int row = 0; row < slot.height; ++row) {
        const std::uint8_t* src =
            slot.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot.stride);
        std::uint8_t* dst =
            out.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(out.width);
        std::copy(src, src + slot.width, dst);
    }
    return true;
}

struct RuntimeState {
    // Shared runtime channels.
    std::mutex shared_mutex{};
    port::PerceptionResult perception{};
    port::ImuSample imu{};
    port::EncoderDelta encoder{};
    CameraFrameHandle latest_camera_frame{};
    CameraCaptureHistory recent_camera_captures{};
    std::array<OwnedCameraFrameSlot, 3> camera_frame_slots{};
    std::size_t next_camera_frame_slot = 0;
    port::ActuatorCommand last_command{};
    ControlCycleObservation control_observation{};
    ControlDebugSnapshot control_debug_snapshot{};
    port::LowVoltageSample low_voltage_last_sample{};

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
    std::atomic<uint64_t> perception_memory_reset_generation{0};

    std::atomic<uint64_t> control_cycle_count{0};
    std::atomic<uint64_t> perception_publish_count{0};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_RUNTIME_STATE_HPP
