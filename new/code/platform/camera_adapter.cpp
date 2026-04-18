#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ls2k::platform {
namespace {

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
        adaptation_hook_ = profile.camera.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.camera.hook;

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
        out.source_width = port::kPhase1UvcWidth;
        out.source_height = port::kPhase1UvcHeight;

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
        if (force_geometry != nullptr && std::string(force_geometry) != "160x120") {
            out.marker = port::CameraGeometryMarker::kNonPhase1Geometry;
            out.source_width = 320;
            out.source_height = 240;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "camera.geometry.override",
                                   "forced non-160x120 geometry marker path",
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
        if (frame.width != port::kPhase1UvcWidth || frame.height != port::kPhase1UvcHeight) {
            out.marker = port::CameraGeometryMarker::kNonPhase1Geometry;
            return out;
        }

        // Phase-1 camera contract:
        // source rows 0..119 -> destination rows 8..127, rows 0..7 duplicate source row 0.
        for (int row = 0; row < port::kPhase1UvcHeight; ++row) {
            uint8_t* dst =
                &out.frame.gray[static_cast<std::size_t>(row + 8) * port::kLegacyFrameWidth];
            std::memcpy(
                dst,
                frame.gray + static_cast<std::size_t>(row) * port::kPhase1UvcWidth,
                port::kLegacyFrameWidth);
        }
        for (int row = 0; row < 8; ++row) {
            uint8_t* dst = &out.frame.gray[static_cast<std::size_t>(row) * port::kLegacyFrameWidth];
            std::memcpy(dst, frame.gray, port::kLegacyFrameWidth);
        }

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
};

}  // namespace

std::unique_ptr<port::ICameraAdapter> MakeCameraAdapter() {
    return std::make_unique<CameraAdapter>();
}

}  // namespace ls2k::platform
