#include "zf_device_uvc.h"


#include <opencv2/imgproc/imgproc.hpp>  // for cv::cvtColor
#include <opencv2/highgui/highgui.hpp> // for cv::VideoCapture
#include <opencv2/opencv.hpp>

#include <iostream> // for std::cerr
#include <fstream>  // for std::ofstream
#include <iostream>
#include <opencv2/opencv.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using namespace cv;
uint8_t g_cropped_buffer[Crop_Width * Crop_Height] __attribute__((aligned(32))); // 裁剪缓存

cv::Mat frame_rgb;      // 构建opencv对象 彩色
cv::Mat roi_buffer;             // ROI缓存（避免重复内存分配）
cv::Mat resized_buffer;         // 缩放缓存（避免重复内存分配）
cv::Mat gray;
cv::Mat resized;
cv::Mat cropped;
int Crop_x;                         //裁剪初始坐标x
int Crop_y;                         //裁剪初始坐标y
cv::Rect roi;                           //裁剪区域类

uint8_t *rgay_image;    // 灰度图像数组指针

VideoCapture cap;

int8 uvc_camera_init(const char *path)
{
    cap.open(path);
    if(!cap.isOpened())
    {
        printf("find uvc camera error.\r\n");
        return -1;
    } 
    else 
    {
        printf("find uvc camera Successfully.\r\n");
    }

    cap.set(CAP_PROP_FRAME_WIDTH, Original_Width);     // 设置摄像头宽度
    cap.set(CAP_PROP_FRAME_HEIGHT, Original_Height);    // 设置摄像头高度
    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P','G'));

    cap.set(CAP_PROP_AUTO_EXPOSURE, 1);           //关闭自动曝光
    cap.set(CAP_PROP_EXPOSURE, Exposure_Time);   //设置曝光时间
    cap.set(CAP_PROP_FPS, UVC_FPS);              //设置屏幕帧率

    // // 计算裁剪所需参数（已放到全局变量中）
    Crop_x = (Original_Width - Crop_Width) / 2 ;
    Crop_y = Original_Height - Crop_Height;
    roi = Rect(Crop_x, Crop_y, Crop_Width, Crop_Height);

    //printf("exposure time:%.2f\n",cap.get(CAP_PROP_EXPOSURE));
    // rgay_image = new uint8_t[UVC_WIDTH * UVC_HEIGHT];  // 为灰度图像分配内存
    // roi_buffer.create(Crop_Height, Crop_Width, CV_8UC3);   // 优化内存布局
    // resized_buffer.create(UVC_HEIGHT, UVC_WIDTH, CV_8UC3);

    return 0;
}


int8 wait_image_refresh()
{
    try 
    {
        // 阻塞式等待图像刷新
        cap >> frame_rgb;
        if (frame_rgb.empty()) 
        {
            std::cerr << "未获取到有效图像帧" << std::endl;
            return -1;
        }
    } 
    catch (const cv::Exception& e) 
    {  
        std::cerr << "OpenCV 异常: " << e.what() << std::endl;
        return -1;
    }

    cropped = frame_rgb(roi).clone();
    cv::resize(cropped, resized, cv::Size(UVC_WIDTH, UVC_HEIGHT));
    cv::cvtColor(resized, gray, COLOR_BGR2GRAY);

    // cv::Mat roi(frame_rgb, cv::Rect(Crop_x, Crop_y, Crop_Width, Crop_Height));
    // cv::resize(roi, resized_buffer, resized_buffer.size(), 0, 0, cv::INTER_NEAREST);
    // cv::Mat gray(UVC_HEIGHT, UVC_WIDTH, CV_8UC1, rgay_image);
    // cv::cvtColor(resized_buffer, gray, cv::COLOR_BGR2GRAY);

    rgay_image = reinterpret_cast<uint8_t *>(gray.ptr(0));

    return 0;
}