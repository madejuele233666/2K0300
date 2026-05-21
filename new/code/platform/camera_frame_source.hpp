#ifndef LS2K_PLATFORM_CAMERA_FRAME_SOURCE_HPP
#define LS2K_PLATFORM_CAMERA_FRAME_SOURCE_HPP

#include <memory>

#include "port/camera_frame_source.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::platform {

/// 创建并启动 camera frame source。primary 失败时按配置尝试 fallback。
std::unique_ptr<port::ICameraFrameSource> MakeStartedCameraFrameSource(
    const port::RuntimeParameters& params,
    port::DiagnosticSink& diagnostics);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_CAMERA_FRAME_SOURCE_HPP
