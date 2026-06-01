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
#include <cmath>
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
    float lateral_m = 0.0F;  ///< 横向距离（米），沿车辆横轴方向（右正左负）
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
        {ImagePoint{222.0F, 33.5F},
         ImagePoint{222.0F, 298.5F},
         ImagePoint{81.0F, 116.0F},
         ImagePoint{81.0F, 217.0F}}};
    std::array<BEVPoint, kBevCalibrationPointCount> target_points{  ///< BEV平面上的对应目标点（米坐标）
        {BEVPoint{0.061F, -0.21F},
         BEVPoint{0.061F, 0.21F},
         BEVPoint{0.6006F, -0.21F},
         BEVPoint{0.6006F, 0.21F}}};
    int debug_grid_width = 160;   ///< 调试栅格宽度（像素）
    int debug_grid_height = 128;  ///< 调试栅格高度（像素）
    std::string projector_id = "bev_projector_square_aspect_20260531T043107Z";  ///< 投影器唯一标识
    std::string projector_hash = "bev-projector-square-aspect-frame-3096-20260531T043107Z";   ///< 投影器哈希版本
};

/**
 * @struct BEVGeometryParameters
 * @brief BEV几何参数
 *
 * 定义参考路径的前向采样网格和横向搜索范围。
 * forward_samples_m 数组定义了 BEV 投影后的车辆坐标系前向采样位置。
 * 消费方直接按米制 forward_m 使用这些值，不需要再额外做 BEV 转换。
 * sparse_row_count 只控制启用前缀长度，不重新分布采样行。
 */
struct BEVGeometryParameters {
    std::array<float, kBevReferenceSampleCount> forward_samples_m{  ///< 24个前向采样位置（米），从近到远
        {0.100000F,
         0.165217F,
         0.230435F,
         0.295652F,
         0.360870F,
         0.426087F,
         0.491304F,
         0.556522F,
         0.621739F,
         0.686957F,
         0.752174F,
         0.817391F,
         0.882609F,
         0.947826F,
         1.013043F,
         1.078261F,
         1.143478F,
         1.208696F,
         1.273913F,
         1.339130F,
         1.404348F,
         1.469565F,
         1.534783F,
         1.600000F}};
    int sparse_row_count = static_cast<int>(kBevReferenceSampleCount);  ///< 启用原始前向采样行的前 N 行
    float search_lateral_limit_m = 1.60F;  ///< 横向搜索范围限制（米）
    float lateral_step_m = 0.02F;          ///< 横向搜索步长（米）
    float reference_lateral_jump_gate_m = 1000.0F;  ///< 参考路径横向跳变门限（米），默认失效化
    float boundary_trace_max_adjacent_distance_m = 0.15F;  ///< 边界 trace 相邻保留点最大距离（米）
    float nominal_road_half_width_m = 0.19F;  ///< 普通道路模型使用的名义半路宽（米）
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
    int hold_last_max_cycles = 32;  ///< 最近一次识别结果的最大保持周期数；0 表示禁用 hold
};

inline bool IsValidBEVClassificationParameters(
    const BEVClassificationParameters& params) {
    return std::isfinite(params.unknown_confidence_min) &&
           std::isfinite(params.white_confidence_min) &&
           params.unknown_confidence_min > 0.0F &&
           params.white_confidence_min >= params.unknown_confidence_min &&
           params.white_confidence_min <= 1.0F &&
           params.hold_last_max_cycles >= 0;
}

/**
 * @struct BEVControlModelParameters
 * @brief BEV控制模型参数
 *
 * 将从感知到控制的映射参数化，包括横向误差的加权和PID增益等。
 */
struct BEVControlModelParameters {
    double lateral_error_far_weight = 0.0;  ///< 远端横向误差权重
    double lateral_offset_to_wheel_delta_gain = 600.0;  ///< 横向位置项到轮速差值的增益系数
    double heading_error_to_wheel_delta_gain = 140.0;  ///< 航向误差项到轮速差值的增益系数
    double curvature_to_wheel_delta_gain = 100.0;  ///< nominal speed下曲率前馈项到轮速差值的增益系数
    double lateral_error_to_wheel_delta_gain = 600.0;  ///< 旧参数名兼容别名，映射到 lateral offset gain
    int min_leading_reference_samples = 3;  ///< 最小前导参考采样点数量
    int tracking_fit_min_samples = 3;       ///< 跟踪几何拟合最小采样点数量
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_GEOMETRY_TYPES_HPP
