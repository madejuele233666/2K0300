#ifndef _zf_driver_uvc_h
#define _zf_driver_uvc_h


#include "zf_common_typedef.h"
#include <opencv2/opencv.hpp>

#define UVC_WIDTH   160
#define UVC_HEIGHT  120
#define UVC_FPS     130
#define Exposure_Time 78				//曝光时间  12 24 18 35 78
#define Original_Width 320              //初始宽度
#define Original_Height 240             //初始高度
#define Crop_Width 320					//裁剪宽度
#define Crop_Height 138					//裁剪高度

int8 uvc_camera_init(const char *path);
int8 wait_image_refresh();

extern cv::VideoCapture cap;
extern uint8_t *rgay_image;
#endif
