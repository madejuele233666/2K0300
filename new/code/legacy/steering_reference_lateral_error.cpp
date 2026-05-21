#include "legacy/steering_reference_lateral_error.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

/// 检查参考路径采样点是否存在且坐标有限
bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) && std::isfinite(sample.point.lateral_m);
}

/// 创建未计算的输出（包含原因说明）
port::ReferenceLateralErrorEstimate UncomputedOutput(const std::string& reason) {
    port::ReferenceLateralErrorEstimate output{};
    output.computed = false;
    output.reason = reason;
    return output;
}

/// 计算线性参考权重（近端权重为1.0，远端由far_weight参数控制）
/// @param index 采样点索引（0为最近端）
/// @param far_weight 最远端的权重值
/// @return 该索引处的线性插值权重
float LinearReferenceWeight(std::size_t index, float far_weight) {
    constexpr float kDenominator = static_cast<float>(port::kBevReferenceSampleCount - 1U);
    return 1.0F + (far_weight - 1.0F) * static_cast<float>(index) / kDenominator;
}

}  // namespace

/// ComputeReferenceLateralError 实现
/// 对参考路径的前导可用采样点进行加权平均，距离越近权重越高
/// 使用线性权重（近端1.0，远端由参数控制）
port::ReferenceLateralErrorEstimate ComputeReferenceLateralError(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::RuntimeParameters& params) {
    if (!usability.usable) {
        return UncomputedOutput(usability.reason);
    }

    const std::size_t bounded_count = std::min(usability.leading_usable_samples,
                                               reference_path.sampled_path.size());
    float weighted_sum = 0.0F;
    float weight_sum = 0.0F;
    std::size_t used_count = 0;
    const float far_weight = static_cast<float>(params.bev_control_model.lateral_error_far_weight);
    for (std::size_t index = 0; index < bounded_count; ++index) {
        const port::BEVPathSample& sample = reference_path.sampled_path[index];
        if (!IsReferencePointPresent(sample)) {
            break;
        }
        const float weight = LinearReferenceWeight(index, far_weight);
        weighted_sum += weight * sample.point.lateral_m;
        weight_sum += weight;
        ++used_count;
    }

    if (used_count == 0 || weight_sum <= 1.0e-6F) {
        return UncomputedOutput("lateral_error_unavailable");
    }

    port::ReferenceLateralErrorEstimate output{};
    output.computed = true;
    output.weighted_lateral_error_m = weighted_sum / weight_sum;
    output.weighted_sample_count = used_count;
    output.weight_sum = weight_sum;
    output.reason = "ok";
    return output;
}

}  // namespace ls2k::legacy
