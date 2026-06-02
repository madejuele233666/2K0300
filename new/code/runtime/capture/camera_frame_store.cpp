#include "runtime/capture/camera_frame_store.hpp"

#include <algorithm>
#include <chrono>

namespace ls2k::runtime {
namespace {

uint64_t NowUs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

CameraFrameHandle CameraFrameStore::Submit(const port::LegacyCameraFrameView& view,
                                           const port::CameraRawFrameMetadata& metadata) {
    const uint64_t submit_begin_us = NowUs();
    CameraFrameHandle handle{};
    if (!view.Valid() ||
        view.width > port::kCompiledCameraFrameWidth ||
        view.height > port::kCompiledCameraFrameHeight) {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        ++state_.camera_frame_store_health.dropped_frame_count;
        return handle;
    }

    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    constexpr std::size_t kSlotCount = 3;
    std::size_t selected = kSlotCount;
    for (std::size_t attempt = 0; attempt < kSlotCount; ++attempt) {
        const std::size_t index = (state_.next_camera_frame_slot + attempt) % kSlotCount;
        if (state_.camera_frame_slots[index].state != OwnedCameraFrameSlotState::kEncoding) {
            selected = index;
            break;
        }
    }
    if (selected == kSlotCount) {
        ++state_.camera_frame_store_health.dropped_frame_count;
        return handle;
    }
    state_.next_camera_frame_slot = (selected + 1) % kSlotCount;

    OwnedCameraFrameSlot& slot = state_.camera_frame_slots[selected];
    if (slot.state == OwnedCameraFrameSlotState::kReady) {
        ++state_.camera_frame_store_health.overwritten_frame_count;
    }
    slot.slot_id = selected;
    slot.state = OwnedCameraFrameSlotState::kReady;
    slot.generation = slot.generation == UINT64_MAX ? 1 : slot.generation + 1;
    slot.frame_id = view.frame_id;
    slot.capture_time_ms = view.capture_time_ms;
    slot.width = view.width;
    slot.height = view.height;
    slot.stride = view.width;
    slot.metadata = metadata;
    slot.metadata.frame_id = view.frame_id;
    slot.metadata.capture_time_ms = view.capture_time_ms;
    for (int row = 0; row < view.height; ++row) {
        const std::uint8_t* src =
            view.gray + static_cast<std::size_t>(row) * static_cast<std::size_t>(view.stride);
        std::uint8_t* dst =
            slot.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot.stride);
        std::copy(src, src + view.width, dst);
    }
    slot.metadata.store_submit_us = NowUs() - submit_begin_us;

    handle.valid = true;
    handle.slot_id = selected;
    handle.generation = slot.generation;
    handle.frame_id = slot.frame_id;
    handle.capture_time_ms = slot.capture_time_ms;
    handle.width = slot.width;
    handle.height = slot.height;
    handle.stride = slot.stride;
    handle.metadata = slot.metadata;
    state_.latest_camera_frame = handle;
    state_.recent_camera_captures.Push(handle);
    ++state_.camera_frame_store_health.submitted_frame_count;
    return handle;
}

std::optional<CameraFrameHandle> CameraFrameStore::TryGetLatestAfter(
    uint64_t last_seen_frame_id) const {
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    if (!state_.latest_camera_frame.valid ||
        state_.latest_camera_frame.frame_id == last_seen_frame_id) {
        return std::nullopt;
    }
    return state_.latest_camera_frame;
}

CameraFrameHandle CameraFrameStore::LatestHandle() const {
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    return state_.latest_camera_frame;
}

std::optional<CameraFrameHandle> CameraFrameStore::FindExact(uint64_t frame_id,
                                                             uint64_t capture_time_ms) const {
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    const CameraFrameHandle* handle =
        state_.recent_camera_captures.FindExact(frame_id, capture_time_ms);
    if (handle == nullptr) {
        ++state_.camera_frame_store_health.lookup_miss_count;
        return std::nullopt;
    }
    return *handle;
}

bool CameraFrameStore::CopyFrame(const CameraFrameHandle& handle,
                                 port::LegacyCameraFrame& out) const {
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    return CopyOwnedCameraFrameByHandle(state_.camera_frame_slots, handle, out);
}

port::CameraFrameStoreHealth CameraFrameStore::Health() const {
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    return state_.camera_frame_store_health;
}

}  // namespace ls2k::runtime
