#ifndef LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP

#include "port/bev_reference_types.hpp"
#include "port/reference_lateral_error_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::reference {

/// 计算参考路径的横向误差估计
/// 对参考路径中前导可用采样点进行加权平均，距离越近权重越高
/// @param reference_path BEV参考路径
/// @param usability 参考路径可用性信息
/// @param params 运行时参数（含横向误差远距权重）
/// @return 横向误差估计结果
port::ReferenceLateralErrorEstimate ComputeReferenceLateralError(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::RuntimeParameters& params);

}  // namespace ls2k::reference

#endif  // LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP
