#ifndef LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP

#include "port/bev_reference_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

/// 评估参考路径的可用性
/// 检查参考路径中连续有效的前导采样点数量是否满足最小要求
/// @param reference_path BEV参考路径
/// @param params 运行时参数（含最小前导参考采样点数）
/// @return 可用性评估结果（可用性、前导采样数、前向范围等）
port::ReferenceUsability EvaluateReferenceUsability(const port::BEVReferencePath& reference_path,
                                                    const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP
