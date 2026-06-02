#ifndef LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
#define LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP

// BEV 投影器 —— 实现图像坐标系与车辆/BEV 坐标系之间的单应性投影。
// 使用 4 对标定点解算 3x3 单应矩阵，支持双向投影。

#include <array>

#include "port/bev_geometry_types.hpp"

namespace ls2k::vision {

/// BEV 投影器类，管理标定参数和双向投影矩阵
class BEVProjector {
public:
    /// 用标定参数配置投影器，计算图像到BEV及BEV到图像的单应矩阵
    /// @param calibration 标定参数（包含4组对应点）
    /// @return 配置是否成功
    bool Configure(const port::BEVProjectorCalibration& calibration);

    /// 检查是否已成功配置有效投影矩阵
    bool Valid() const {
        return configured_;
    }

    /// 获取当前标定参数（只读引用）
    const port::BEVProjectorCalibration& Calibration() const {
        return calibration_;
    }

    /// 将图像坐标投影到车辆/BEV坐标系（输出前向距离和横向偏移，单位米）
    /// @param image_point 图像坐标点（行列像素）
    /// @param vehicle_point [输出] 车辆坐标系下的BEV点（前向/横向，米）
    /// @return 投影是否成功
    bool ProjectImageToVehicle(const port::ImagePoint& image_point, port::BEVPoint& vehicle_point) const;

    /// 将车辆/BEV坐标投影到图像坐标系（输出行列像素坐标）
    /// @param vehicle_point 车辆坐标系下的BEV点（前向/横向，米）
    /// @param image_point [输出] 图像坐标点（行列像素）
    /// @return 投影是否成功
    bool ProjectVehicleToImage(const port::BEVPoint& vehicle_point, port::ImagePoint& image_point) const;

private:
    port::BEVProjectorCalibration calibration_{}; ///< 标定参数
    std::array<double, 9> image_to_bev_{}; ///< 图像到BEV的单应矩阵（3x3，行主序存储）
    std::array<double, 9> bev_to_image_{}; ///< BEV到图像的单应矩阵（3x3，行主序存储）
    bool configured_ = false;             ///< 是否已成功配置
};

}  // namespace ls2k::vision

#endif  // LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
