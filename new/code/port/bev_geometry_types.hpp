/**
 * @file bev_geometry_types.hpp
 * @brief BEV几何类型定义
 *
 * 定义BEV（鸟瞰视角）投影所需的几何数据类型，包括：
 * - 图像坐标点与BEV坐标点的结构
 * - 投影标定参数（四点透视变换）
 * - 前向采样网格参数
 * - 分类与控制模型参数
 */

#ifndef LS2K_PORT_BEV_GEOMETRY_TYPES_HPP
#define LS2K_PORT_BEV_GEOMETRY_TYPES_HPP

#include <array>
#include <cstddef>
#include <string>

namespace ls2k::port {

/**
 * @struct ImagePoint
 * @brief 图像坐标系中的点（行/列坐标，单位为像素）
 */
struct ImagePoint {
    float row_px = 0.0F;  ///< 行坐标（像素）
    float col_px = 0.0F;  ///< 列坐标（像素）
};

/**
 * @struct BEVPoint
 * @brief BEV坐标系中的点（前向/横向，单位为米）
 */
struct BEVPoint {
    float forward_m = 0.0F;  ///< 前向距离（米），沿车辆纵轴方向
    float lateral_m = 0.0F;  ///< 横向距离（米），沿车辆横轴方向（左正右负）
};

constexpr std::size_t kBevCalibrationPointCount = 4;  ///< 透视变换标定点数量
constexpr std::size_t kBevReferenceSampleCount = 24;   ///< 参考路径采样点数量

/**
 * @struct BEVProjectorCalibration
 * @brief BEV投影器标定参数
 *
 * 包含从图像到BEV平面的四点透视变换标定数据，
 * 以及投影器的标识信息和调试网格尺寸。
 */
struct BEVProjectorCalibration {
    bool valid = true;  ///< 标定是否有效
    std::array<ImagePoint, kBevCalibrationPointCount> source_points{  ///< 源图像上的四个标定点（像素坐标）
        {ImagePoint{220.0F, 19.0F},
         ImagePoint{220.0F, 300.0F},
         ImagePoint{68.0F, 121.0F},
         ImagePoint{68.0F, 204.0F}}};
    std::array<BEVPoint, kBevCalibrationPointCount> target_points{  ///< BEV平面上的对应目标点（米坐标）
        {BEVPoint{0.061F, -0.21F},
         BEVPoint{0.061F, 0.21F},
         BEVPoint{0.610F, -0.21F},
         BEVPoint{0.610F, 0.21F}}};
    int debug_grid_width = 160;   ///< 调试栅格宽度（像素）
    int debug_grid_height = 128;  ///< 调试栅格高度（像素）
    std::string projector_id = "bev_projector_true_bev_long_straight_v6";  ///< 投影器唯一标识
    std::string projector_hash = "bev-projector-long-straight-20260506";   ///< 投影器哈希版本
};

/**
 * @struct BEVGeometryParameters
 * @brief BEV几何参数
 *
 * 定义参考路径的前向采样网格和横向搜索范围。
 * forward_samples_m 数组定义了从车头到最远探测距离的24个前向采样位置。
 */
struct BEVGeometryParameters {
    std::array<float, kBevReferenceSampleCount> forward_samples_m{  ///< 24个前向采样位置（米），从近到远
        {0.061000F,
         0.123565F,
         0.186130F,
         0.248696F,
         0.311261F,
         0.373826F,
         0.436391F,
         0.498957F,
         0.561522F,
         0.624087F,
         0.686652F,
         0.749217F,
         0.811783F,
         0.874348F,
         0.936913F,
         0.999478F,
         1.062043F,
         1.124609F,
         1.187174F,
         1.249739F,
         1.312304F,
         1.374870F,
         1.437435F,
         1.500000F}};
    float search_lateral_limit_m = 1.60F;  ///< 横向搜索范围限制（米）
    float lateral_step_m = 0.02F;          ///< 横向搜索步长（米）
    float nominal_road_half_width_m = 0.21F;  ///< 普通道路模型使用的名义半路宽（米）
};

/**
 * @struct BEVClassificationParameters
 * @brief BEV分类参数
 *
 * 控制视觉元素分类的置信度阈值和历史保持策略。
 */
struct BEVClassificationParameters {
    float white_confidence_min = 0.55F;  ///< 白色元素的最小置信度阈值
    float unknown_confidence_min = 0.25F;  ///< 无法确定类别的置信度阈值
    int hold_last_max_cycles = 32;  ///< 最近一次识别结果的最大保持周期数
};

/**
 * @struct BEVControlModelParameters
 * @brief BEV控制模型参数
 *
 * 将从感知到控制的映射参数化，包括横向误差的加权和PID增益等。
 */
struct BEVControlModelParameters {
    double lateral_error_far_weight = 0.0;  ///< 远端横向误差权重
    double lateral_error_to_wheel_delta_gain = 500.0;  ///< 横向误差到轮速差值的增益系数
    int min_leading_reference_samples = 3;  ///< 最小前导参考采样点数量
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_GEOMETRY_TYPES_HPP
