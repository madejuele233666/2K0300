#ifndef LS2K_PORT_CAMERA_FRAME_TYPES_HPP
#define LS2K_PORT_CAMERA_FRAME_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ls2k::port {

constexpr int kCompiledCameraFrameWidth = 320;
constexpr int kCompiledCameraFrameHeight = 240;

struct LegacyCameraFrameView {
    const uint8_t* gray = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;

    bool Valid() const {
        return gray != nullptr && width > 0 && height > 0 && stride >= width;
    }

    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

enum class CameraGeometryMarker {
    kPhase1Adapted,
    kNonPhase1Geometry,
    kEmptyFrame,
    kAdapterNotReady,
    kAdaptationHookRouted
};

struct LegacyCameraFrame {
    std::array<uint8_t, kCompiledCameraFrameWidth * kCompiledCameraFrameHeight> gray{};
    int width = 0;
    int height = 0;

    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    LegacyCameraFrameView View(uint64_t frame_id = 0, uint64_t capture_time_ms = 0) const {
        LegacyCameraFrameView view{};
        view.gray = gray.data();
        view.width = width;
        view.height = height;
        view.stride = width;
        view.frame_id = frame_id;
        view.capture_time_ms = capture_time_ms;
        return view;
    }
};

struct CameraCapture {
    bool has_frame = false;
    LegacyCameraFrameView view{};
    CameraGeometryMarker marker = CameraGeometryMarker::kAdapterNotReady;
    int source_width = 0;
    int source_height = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CAMERA_FRAME_TYPES_HPP
