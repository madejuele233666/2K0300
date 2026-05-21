/**
 * @file reference_usability_types.hpp
 * @brief 参考路径可用性类型定义
 *
 * 定义BEV参考路径是否可用于横向控制的质量评估结果。
 * 可用性评估在横向误差估计之前执行，确保参考路径有足够的前导有效采样点。
 */

#ifndef LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP
#define LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP

#include <cstddef>
#include <string>

namespace ls2k::port {

/**
 * @struct ReferenceUsability
 * @brief 参考路径可用性评估结果
 *
 * 检查参考路径前向方向上的有效采样点是否足够，
 * 并给出可用范围的最小/最大前向距离。
 */
struct ReferenceUsability {
    bool usable = false;                    ///< 参考路径是否可用
    std::size_t leading_usable_samples = 0; ///< 前向有效采样点数量
    float leading_min_forward_m = 0.0F;     ///< 前向有效范围的最小距离（米）
    float leading_max_forward_m = 0.0F;     ///< 前向有效范围的最大距离（米）
    std::string reason = "no_reference_facts";  ///< 不可用的原因
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP
