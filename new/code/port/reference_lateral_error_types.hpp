/**
 * @file reference_lateral_error_types.hpp
 * @brief 参考路径横向误差估计类型定义
 *
 * 定义车辆当前位置与期望参考路径之间横向偏移的加权估计结果。
 * 这是横向控制器的核心输入之一。
 */

#ifndef LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP
#define LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP

#include <cstddef>
#include <string>

namespace ls2k::port {

/**
 * @struct ReferenceLateralErrorEstimate
 * @brief 参考路径横向误差估计
 *
 * 对前向多个采样点的横向误差进行加权平均，
 * 得到当前车辆相对于参考路径的综合横向偏移量。
 */
struct ReferenceLateralErrorEstimate {
    bool computed = false;                  ///< 是否已计算出有效结果
    float weighted_lateral_error_m = 0.0F;  ///< 加权平均横向误差（米）
    std::size_t weighted_sample_count = 0;  ///< 参与加权的采样点数
    float weight_sum = 0.0F;                ///< 权重总和
    std::string reason = "reference_unusable";  ///< 计算失败的原因
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP
