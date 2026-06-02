#ifndef LS2K_RUNTIME_CAMERA_FRAME_STORE_HPP
#define LS2K_RUNTIME_CAMERA_FRAME_STORE_HPP

#include <optional>

#include "port/camera_frame_types.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// CameraFrameStore owns latest/history semantics for gray camera frames.
class CameraFrameStore {
public:
    explicit CameraFrameStore(RuntimeState& state) : state_(state) {}

    CameraFrameHandle Submit(const port::LegacyCameraFrameView& view,
                             const port::CameraRawFrameMetadata& metadata);
    std::optional<CameraFrameHandle> TryGetLatestAfter(uint64_t last_seen_frame_id) const;
    CameraFrameHandle LatestHandle() const;
    std::optional<CameraFrameHandle> FindExact(uint64_t frame_id,
                                               uint64_t capture_time_ms) const;
    bool CopyFrame(const CameraFrameHandle& handle, port::LegacyCameraFrame& out) const;
    port::CameraFrameStoreHealth Health() const;

private:
    RuntimeState& state_;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CAMERA_FRAME_STORE_HPP
