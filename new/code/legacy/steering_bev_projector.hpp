#ifndef LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
#define LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP

// BEV 投影器 —— 实现图像坐标系 ↔ 车辆/BEV 坐标系之间的单应性投影。
// 使用 4 对标定点解算 3x3 单应矩阵，支持双向投影。

#include <array>

#include "port/control_types.hpp"

namespace ls2k::legacy {

// BEV 投影器类，管理标定参数和双向投影矩阵
class BEVProjector {
public:
    // 用标定参数配置投影器，计算图像↔BEV 的单应矩阵
    bool Configure(const port::BEVProjectorCalibration& calibration);

    // 是否有有效的已配置投影矩阵
    bool Valid() const {
        return configured_;
    }

    // 获取当前标定参数（只读）
    const port::BEVProjectorCalibration& Calibration() const {
        return calibration_;
    }

    // 图像坐标 → 车辆/BEV 坐标（前向/横向，米）
    bool ProjectImageToVehicle(const port::ImagePoint& image_point, port::BEVPoint& vehicle_point) const;
    // 车辆/BEV 坐标 → 图像坐标（行/列，像素）
    bool ProjectVehicleToImage(const port::BEVPoint& vehicle_point, port::ImagePoint& image_point) const;

private:
    port::BEVProjectorCalibration calibration_{}; //!< 标定参数
    std::array<double, 9> image_to_bev_{}; //!< 图像→BEV 的单应矩阵（3x3）
    std::array<double, 9> bev_to_image_{}; //!< BEV→图像 的单应矩阵（3x3）
    bool configured_ = false;             //!< 是否已成功配置
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
