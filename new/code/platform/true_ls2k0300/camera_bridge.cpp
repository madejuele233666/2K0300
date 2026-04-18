#include "platform/true_ls2k0300/bridge.hpp"

#include "zf_device_uvc.h"

namespace ls2k::platform::true_ls2k0300 {

bool InitializeCamera(const std::string& video_path) {
    return uvc_camera_init(video_path.c_str()) == 0;
}

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

void ShutdownCamera() {}

}  // namespace ls2k::platform::true_ls2k0300
