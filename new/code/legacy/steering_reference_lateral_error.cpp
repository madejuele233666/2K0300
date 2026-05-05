#include "legacy/steering_reference_lateral_error.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) && std::isfinite(sample.point.lateral_m);
}

port::ReferenceLateralErrorEstimate UncomputedOutput(const std::string& reason) {
    port::ReferenceLateralErrorEstimate output{};
    output.computed = false;
    output.reason = reason;
    return output;
}

std::size_t MaxWeightedSampleIndex(const port::BEVControlModelParameters& params) {
    return static_cast<std::size_t>(
        std::clamp(params.lateral_error_max_weighted_sample_index,
                   0,
                   static_cast<int>(port::kBevReferenceSampleCount - 1U)));
}

float ExponentialReferenceWeight(std::size_t index, std::size_t max_weighted_index, float far_weight) {
    if (max_weighted_index == 0U) {
        return 1.0F;
    }
    const float normalized_index =
        static_cast<float>(index) / static_cast<float>(max_weighted_index);
    return std::pow(far_weight, normalized_index);
}

}  // namespace

port::ReferenceLateralErrorEstimate ComputeReferenceLateralError(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::RuntimeParameters& params) {
    if (!usability.usable) {
        return UncomputedOutput(usability.reason);
    }

    const std::size_t max_weighted_index = MaxWeightedSampleIndex(params.bev_control_model);
    const std::size_t max_weighted_count = max_weighted_index + 1U;
    const std::size_t bounded_count = std::min(
        {usability.leading_usable_samples, reference_path.sampled_path.size(), max_weighted_count});
    float weighted_sum = 0.0F;
    float weight_sum = 0.0F;
    std::size_t used_count = 0;
    const float far_weight = static_cast<float>(params.bev_control_model.lateral_error_far_weight);
    for (std::size_t index = 0; index < bounded_count; ++index) {
        const port::BEVPathSample& sample = reference_path.sampled_path[index];
        if (!IsReferencePointPresent(sample)) {
            break;
        }
        const float weight = ExponentialReferenceWeight(index, max_weighted_index, far_weight);
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
