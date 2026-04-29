// 相机桥接实现 —— 基于 zf_device_uvc 库的硬件相机初始化与帧采集。
// 提供 InitializeCamera/CaptureCameraFrame/ShutdownCamera 接口。

#include "platform/true_ls2k0300/bridge.hpp"

#include "zf_device_uvc.h"

namespace ls2k::platform::true_ls2k0300 {

// 初始化 UVC 相机设备
bool InitializeCamera(const std::string& video_path) {
    return uvc_camera_init(video_path.c_str()) == 0;
}

// 采集一帧灰度图像并返回视图（引用供应商的 rgay_image 缓冲区）
CameraFrameView CaptureCameraFrame() {
    CameraFrameView frame{};
    if (wait_image_refresh() != 0 || rgay_image == nullptr) {
        return frame;
    }
    frame.valid = true;
    frame.gray = rgay_image;
    frame.width = UVC_WIDTH;
    frame.height = UVC_HEIGHT;
    return frame;
}

// 关闭相机（当前为无操作）
void ShutdownCamera() {}

}  // namespace ls2k::platform::true_ls2k0300
