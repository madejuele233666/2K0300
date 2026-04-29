#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

// 相机适配器实现 —— 平台级相机硬件适配层。
// 负责初始化相机硬件、采集帧数据、转换为 LegacyCameraFrame 格式。

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace ls2k::platform {
namespace {

std::string GeometryText(int width, int height) {
    std::ostringstream stream;
    stream << width << "x" << height;
    return stream.str();
}

class CameraAdapter final : public port::ICameraAdapter {
public:
    bool Initialize(const port::HardwareProfile& profile,
                    const port::RuntimeParameters& params,
                    port::DiagnosticSink& diagnostics) override {
        if (!port::IsEnabled(profile.camera)) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "camera.disabled",
                              "camera subsystem disabled by hardware profile",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return true;
        }

        enabled_ = true;
        expected_width_ = params.camera_frame_width;
        expected_height_ = params.camera_frame_height;
        adaptation_hook_ = profile.camera.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.camera.hook;

        if (expected_width_ <= 0 || expected_height_ <= 0 ||
            expected_width_ > port::kCompiledCameraFrameWidth ||
            expected_height_ > port::kCompiledCameraFrameHeight) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "camera.geometry.invalid",
                              "configured camera_frame_width/camera_frame_height exceed compiled frame storage",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return false;
        }

        if (adaptation_hook_) {
            // Explicit adaptation-hook mode is treated as an intentional extension path.
            ready_ = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera.init.hook",
                              "camera direct path bypassed; adaptation hook selected: " + hook_name_,
                              port::NowMs()});
            return true;
        }

        ready_ = true_ls2k0300::InitializeCamera(true_ls2k0300::kDefaultCameraPath);
        if (!ready_) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "camera.init.failed",
                              "true_ls2k0300 camera bridge init failed for direct-match path: " +
                                  std::string(true_ls2k0300::kDefaultCameraPath),
                              port::NowMs()});
            return false;
        }

        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "camera.init",
                          "camera initialized through true_ls2k0300 bridge: " +
                              std::string(true_ls2k0300::kDefaultCameraPath),
                          port::NowMs()});
        if (params.exp_light != 65) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera.exposure.unsupported",
                              "true_ls2k0300 direct camera path cannot apply non-default exp_light via the vendor public API; use an adaptation hook or degraded diagnostics-only startup",
                              port::NowMs()});
            ready_ = false;
            return false;
        }
        return true;
    }

    port::CameraCapture Capture(port::DiagnosticSink& diagnostics) override {
        port::CameraCapture out{};
        out.frame_id = ++frame_id_;
        out.capture_time_ms = port::NowMs();
        out.source_width = expected_width_;
        out.source_height = expected_height_;
        out.frame.width = expected_width_;
        out.frame.height = expected_height_;

        if (!enabled_) {
            out.marker = port::CameraGeometryMarker::kAdapterNotReady;
            return out;
        }

        if (adaptation_hook_) {
            out.marker = port::CameraGeometryMarker::kAdaptationHookRouted;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "camera.hook",
                                   "camera routed through adaptation hook: " + hook_name_,
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        const char* force_geometry = std::getenv("LS2K_FORCE_UVC_GEOMETRY");
        if (force_geometry != nullptr &&
            std::string(force_geometry) != GeometryText(expected_width_, expected_height_)) {
            out.marker = port::CameraGeometryMarker::kNonPhase1Geometry;
            out.source_width = expected_width_;
            out.source_height = expected_height_;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "camera.geometry.override",
                                   "forced non-expected geometry marker path",
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        if (!ready_
        ) {
            out.marker = port::CameraGeometryMarker::kAdapterNotReady;
            return out;
        }

        const true_ls2k0300::CameraFrameView frame = true_ls2k0300::CaptureCameraFrame();
        if (!frame.valid || frame.gray == nullptr) {
            out.marker = port::CameraGeometryMarker::kEmptyFrame;
            return out;
        }

        out.source_width = frame.width;
        out.source_height = frame.height;
        if (frame.width != expected_width_ || frame.height != expected_height_) {
            out.marker = port::CameraGeometryMarker::kNonPhase1Geometry;
            return out;
        }

        const std::size_t frame_bytes = out.frame.PixelCount();
        std::memcpy(out.frame.gray.data(), frame.gray, frame_bytes);

        out.has_frame = true;
        out.marker = port::CameraGeometryMarker::kPhase1Adapted;
        return out;
    }

    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        true_ls2k0300::ShutdownCamera();
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "camera.shutdown",
                          "camera adapter shutdown complete",
                          port::NowMs()});
    }

    bool Ready() const override { return ready_; }

private:
    bool enabled_ = false;
    bool ready_ = false;
    bool adaptation_hook_ = false;
    std::string hook_name_ = "direct-match";
    uint64_t frame_id_ = 0;
    int expected_width_ = port::kCompiledCameraFrameWidth;
    int expected_height_ = port::kCompiledCameraFrameHeight;
};

}  // namespace

std::unique_ptr<port::ICameraAdapter> MakeCameraAdapter() {
    return std::make_unique<CameraAdapter>();
}

}  // namespace ls2k::platform
