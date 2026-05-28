#include "legacy/steering_reference_usability.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

/// 检查参考路径采样点是否存在且坐标有限
bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) && std::isfinite(sample.point.lateral_m);
}

/// 从运行时参数中获取配置的最小前导参考采样点数（钳制在有效范围内）
std::size_t ConfiguredMinLeadingSamples(const port::RuntimeParameters& params) {
    constexpr int kMinSamplesForControl = 3;
    const int bounded =
        std::clamp(params.bev_control_model.min_leading_reference_samples,
                   kMinSamplesForControl,
                   static_cast<int>(port::kBevReferenceSampleCount));
    return static_cast<std::size_t>(bounded);
}

}  // namespace

/// EvaluateReferenceUsability 实现
/// 统计参考路径中第一个连续有效采样段的真实点数及其前向范围。
/// 近端采样点缺失本身不使路径不可用；连续真实点数不足才不可用。
port::ReferenceUsability EvaluateReferenceUsability(const port::BEVReferencePath& reference_path,
                                                    const port::RuntimeParameters& params) {
    port::ReferenceUsability usability{};
    const std::size_t min_leading_samples = ConfiguredMinLeadingSamples(params);
    bool segment_started = false;

    for (const port::BEVPathSample& sample : reference_path.sampled_path) {
        if (!IsReferencePointPresent(sample)) {
            if (segment_started) {
                break;
            }
            continue;
        }
        if (usability.leading_usable_samples == 0) {
            usability.leading_min_forward_m = sample.point.forward_m;
            usability.leading_max_forward_m = sample.point.forward_m;
            segment_started = true;
        } else {
            usability.leading_min_forward_m =
                std::min(usability.leading_min_forward_m, sample.point.forward_m);
            usability.leading_max_forward_m =
                std::max(usability.leading_max_forward_m, sample.point.forward_m);
        }
        ++usability.leading_usable_samples;
    }

    if (usability.leading_usable_samples == 0) {
        usability.reason = "no_reference_facts";
        return usability;
    }
    if (usability.leading_usable_samples < min_leading_samples) {
        usability.reason = "insufficient_leading_reference";
        return usability;
    }

    usability.usable = true;
    usability.reason = "ok";
    return usability;
}

}  // namespace ls2k::legacy
