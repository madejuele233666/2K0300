#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

// 相机适配器实现 —— 平台级相机硬件适配层。
// 负责初始化相机硬件、采集帧视图并交给运行时立即消费。

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

namespace ls2k::platform {
namespace {

/// @brief 将宽高尺寸格式化为 "WxH" 字符串
/// @param width 宽度
/// @param height 高度
/// @return 格式化的几何描述字符串
std::string GeometryText(int width, int height) {
    std::ostringstream stream;
    stream << width << "x" << height;
    return stream.str();
}

/// @brief 相机适配器类
///
/// 实现 port::ICameraAdapter 接口，封装 true_ls2k0300 桥接层的
/// 相机初始化、帧捕获和关闭操作。支持 direct-match 和 adaptation-hook 两种模式。
class CameraAdapter final : public port::ICameraAdapter {
public:
    /// @brief 初始化相机适配器
    /// @param profile 硬件描述文件（检查相机子系统是否启用及其模式）
    /// @param params 运行时参数（含帧宽高、曝光参数）
    /// @param diagnostics 诊断输出接口
    /// @return 初始化成功返回 true
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

        ready_ = true;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "camera.init",
                          "camera adapter validated; capture ownership is delegated to CameraCaptureWorker",
                          port::NowMs()});
        return true;
    }

    /// @brief 捕获一帧相机图像
    /// @param diagnostics 诊断输出接口
    /// @return 捕获结果（包含帧数据、几何标记和时间戳）
    port::CameraCapture Capture(port::DiagnosticSink& diagnostics) override {
        port::CameraCapture out{};
        out.frame_id = ++frame_id_;
        out.capture_time_ms = port::NowMs();
        out.source_width = expected_width_;
        out.source_height = expected_height_;

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

        out.capture_time_ms = port::NowMs();
        const true_ls2k0300::CameraFrameView frame = true_ls2k0300::CaptureCameraFrame();
        if (!frame.valid || frame.gray == nullptr) {
            out.marker = port::CameraGeometryMarker::kEmptyFrame;
            return out;
        }
        out.capture_time_ms = port::NowMs();

        out.source_width = frame.width;
        out.source_height = frame.height;
        if (frame.width != expected_width_ || frame.height != expected_height_) {
            out.marker = port::CameraGeometryMarker::kNonPhase1Geometry;
            return out;
        }

        out.has_frame = true;
        out.view.gray = frame.gray;
        out.view.width = frame.width;
        out.view.height = frame.height;
        out.view.stride = frame.width;
        out.view.frame_id = out.frame_id;
        out.view.capture_time_ms = out.capture_time_ms;
        out.marker = port::CameraGeometryMarker::kPhase1Adapted;
        return out;
    }

    /// @brief 关闭相机适配器
    /// @param diagnostics 诊断输出接口
    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        true_ls2k0300::ShutdownCamera();
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "camera.shutdown",
                          "camera adapter shutdown complete",
                          port::NowMs()});
    }

    /// @brief 检查相机是否已就绪
    /// @return true 表示相机可用
    bool Ready() const override { return ready_; }

private:
    /// 相机子系统是否启用
    bool enabled_ = false;
    /// 相机是否已就绪
    bool ready_ = false;
    /// 是否使用适配钩子模式
    bool adaptation_hook_ = false;
    /// 适配钩子名称
    std::string hook_name_ = "direct-match";
    /// 帧计数器
    uint64_t frame_id_ = 0;
    /// 期望的帧宽度
    int expected_width_ = port::kCompiledCameraFrameWidth;
    /// 期望的帧高度
    int expected_height_ = port::kCompiledCameraFrameHeight;
};

}  // namespace

/// @brief 创建相机适配器实例
/// @return 新创建的 CameraAdapter 智能指针
std::unique_ptr<port::ICameraAdapter> MakeCameraAdapter() {
    return std::make_unique<CameraAdapter>();
}

}  // namespace ls2k::platform
