#ifndef LS2K_PORT_CAMERA_FRAME_SOURCE_HPP
#define LS2K_PORT_CAMERA_FRAME_SOURCE_HPP

#include <string>

#include "port/camera_frame_types.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::port {

/// 相机帧源抽象，只隐藏 backend，不拥有 latest/history 语义
class ICameraFrameSource {
public:
    virtual ~ICameraFrameSource() = default;

    /// 启动 frame source
    virtual bool Start(const CameraSourceParameters& config,
                       DiagnosticSink& diagnostics) = 0;
    /// 停止 frame source
    virtual void Stop(DiagnosticSink& diagnostics) = 0;
    /// 等待一个 raw/gray frame，timeout 只属于 source/worker 线程
    virtual CameraRawFrame WaitRawFrame(int timeout_ms,
                                        DiagnosticSink& diagnostics) = 0;
    /// 后端是否可用
    virtual bool Ready() const = 0;
    /// 后端名称
    virtual const char* Name() const = 0;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CAMERA_FRAME_SOURCE_HPP
