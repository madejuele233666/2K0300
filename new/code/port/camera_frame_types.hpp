/**
 * @file camera_frame_types.hpp
 * @brief 相机帧类型定义
 *
 * 定义灰度图像帧的视图/封装结构和相机捕获结果，
 * 用于整个感知流水线中的图像数据传输。
 */

#ifndef LS2K_PORT_CAMERA_FRAME_TYPES_HPP
#define LS2K_PORT_CAMERA_FRAME_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ls2k::port {

constexpr int kCompiledCameraFrameWidth = 320;   ///< 编译时确定的相机帧宽度
constexpr int kCompiledCameraFrameHeight = 240;  ///< 编译时确定的相机帧高度

/**
 * @struct LegacyCameraFrameView
 * @brief 灰度图像帧的非拥有视图
 *
 * 指向外部图像数据的轻量级视图，不持有数据所有权。
 * 用于帧数据处理过程中避免拷贝。
 */
struct LegacyCameraFrameView {
    const uint8_t* gray = nullptr;  ///< 灰度图像数据指针
    int width = 0;                  ///< 图像宽度（像素）
    int height = 0;                 ///< 图像高度（像素）
    int stride = 0;                 ///< 行跨度（字节），通常等于width
    uint64_t frame_id = 0;          ///< 帧序号
    uint64_t capture_time_ms = 0;   ///< 捕获时间戳（毫秒）

    /** @brief 检查视图是否有效（指针非空且尺寸合法） */
    bool Valid() const {
        return gray != nullptr && width > 0 && height > 0 && stride >= width;
    }

    /** @brief 计算总像素数 */
    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

/**
 * @enum CameraGeometryMarker
 * @brief 相机几何状态标记
 *
 * 标记当前帧的几何适配状态，用于调试和适配流水线追踪。
 */
enum class CameraGeometryMarker {
    kPhase1Adapted,      ///< Phase1适配模式已生效
    kNonPhase1Geometry,  ///< 非Phase1的几何配置
    kEmptyFrame,         ///< 空帧（无数据）
    kAdapterNotReady,    ///< 相机适配器未就绪
    kAdaptationHookRouted  ///< 适配Hook已路由
};

/// 相机 frame source 输出格式
enum class CameraFrameFormat {
    kGray,  ///< 已是灰度帧
    kYuyv   ///< YUYV 4:2:2 原始帧
};

/// 相机原始帧元数据
struct CameraRawFrameMetadata {
    std::string source = "none";          ///< frame source 名称
    uint64_t frame_id = 0;                ///< source 内帧序号
    uint64_t capture_time_ms = 0;         ///< 硬件/驱动帧时间，缺省用 dequeue time
    uint64_t dequeue_time_ms = 0;         ///< DQBUF 或 backend 返回时间
    uint32_t v4l2_sequence = 0;           ///< V4L2 sequence
    bool v4l2_timestamp_valid = false;    ///< capture_time 是否来自 V4L2 timestamp
    int drained_buffer_count = 0;         ///< 本次 wait drain 的 buffer 数
    uint64_t poll_wait_us = 0;            ///< poll 等待耗时
    uint64_t dequeue_us = 0;              ///< dequeue/drain 耗时
    uint64_t yuyv_to_gray_us = 0;         ///< YUYV 转灰度耗时
    uint64_t store_submit_us = 0;         ///< 提交到 frame store 耗时
};

/// ICameraFrameSource 输出的 raw/gray frame fact
struct CameraRawFrame {
    bool valid = false;                  ///< 是否有可用帧
    CameraFrameFormat format = CameraFrameFormat::kGray;  ///< 数据格式
    int width = 0;                       ///< 宽度
    int height = 0;                      ///< 高度
    int stride = 0;                      ///< 每行字节数
    std::vector<uint8_t> data{};         ///< 原始帧数据
    CameraRawFrameMetadata metadata{};   ///< 帧元数据
};

/// Camera frame store 健康/统计事实
struct CameraFrameStoreHealth {
    uint64_t submitted_frame_count = 0;     ///< 成功提交帧数
    uint64_t overwritten_frame_count = 0;   ///< 覆盖 ready slot 次数
    uint64_t dropped_frame_count = 0;       ///< 无可写 slot 时丢帧数
    uint64_t lookup_miss_count = 0;         ///< exact lookup miss 次数
};

/**
 * @struct LegacyCameraFrame
 * @brief 灰度图像帧的拥有型封装
 *
 * 持有完整的灰度图像数据缓冲区，并提供创建视图的方法。
 * 使用编译时确定的固定大小（320x240）以减少动态内存分配。
 */
struct LegacyCameraFrame {
    std::array<uint8_t, kCompiledCameraFrameWidth * kCompiledCameraFrameHeight> gray{};  ///< 灰度图像像素缓冲区
    int width = 0;   ///< 图像宽度（像素）
    int height = 0;  ///< 图像高度（像素）

    /** @brief 计算总像素数 */
    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    /**
     * @brief 创建指向此帧的非拥有视图
     * @param frame_id 帧序号
     * @param capture_time_ms 捕获时间戳
     * @return 帧视图对象
     */
    LegacyCameraFrameView View(uint64_t frame_id = 0, uint64_t capture_time_ms = 0) const {
        LegacyCameraFrameView view{};
        view.gray = gray.data();
        view.width = width;
        view.height = height;
        view.stride = width;
        view.frame_id = frame_id;
        view.capture_time_ms = capture_time_ms;
        return view;
    }
};

/**
 * @struct CameraCapture
 * @brief 相机捕获结果
 *
 * 相机适配器每次捕获操作输出的完整结果，
 * 包含是否有新帧、帧视图、几何标记和尺寸信息。
 */
struct CameraCapture {
    bool has_frame = false;                     ///< 是否有新帧可用
    LegacyCameraFrameView view{};               ///< 帧数据视图
    CameraGeometryMarker marker = CameraGeometryMarker::kAdapterNotReady;  ///< 几何状态标记
    int source_width = 0;                       ///< 源图像宽度
    int source_height = 0;                      ///< 源图像高度
    uint64_t frame_id = 0;                      ///< 帧序号
    uint64_t capture_time_ms = 0;               ///< 捕获时间戳
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CAMERA_FRAME_TYPES_HPP
