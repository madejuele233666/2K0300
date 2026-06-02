#ifndef LS2K_RUNTIME_CAMERA_CAPTURE_WORKER_HPP
#define LS2K_RUNTIME_CAMERA_CAPTURE_WORKER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include "port/camera_frame_source.hpp"
#include "port/runtime_parameter_types.hpp"
#include "runtime/capture/camera_frame_store.hpp"

namespace ls2k::runtime {

/// Convert one YUYV frame to the fixed gray frame container.
inline bool YuyvToGray(const std::uint8_t* yuyv,
                       int width,
                       int height,
                       int bytesperline,
                       port::LegacyCameraFrame& out) {
    if (yuyv == nullptr ||
        width <= 0 ||
        height <= 0 ||
        width > port::kCompiledCameraFrameWidth ||
        height > port::kCompiledCameraFrameHeight ||
        bytesperline < width * 2) {
        return false;
    }
    out = {};
    out.width = width;
    out.height = height;
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* src =
            yuyv + static_cast<std::size_t>(row) * static_cast<std::size_t>(bytesperline);
        std::uint8_t* dst =
            out.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
        for (int col = 0; col < width; col += 2) {
            const int src_col = col * 2;
            dst[col] = src[src_col];
            if (col + 1 < width) {
                dst[col + 1] = src[src_col + 2];
            }
        }
    }
    return true;
}

/// Blocking camera producer. It only submits frame facts to CameraFrameStore.
class CameraCaptureWorker {
public:
    CameraCaptureWorker(CameraFrameStore& frame_store,
                        port::DiagnosticSink& diagnostics);
    ~CameraCaptureWorker();

    bool Start(const port::RuntimeParameters& params);
    void Stop();
    bool Running() const;

private:
    void ThreadMain();
    bool ConvertRawFrame(const port::CameraRawFrame& raw,
                         port::LegacyCameraFrame& out,
                         port::CameraRawFrameMetadata& metadata);

    CameraFrameStore& frame_store_;
    port::DiagnosticSink& diagnostics_;
    port::RuntimeParameters params_{};
    std::unique_ptr<port::ICameraFrameSource> source_{};
    std::thread thread_{};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CAMERA_CAPTURE_WORKER_HPP
