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

float LinearReferenceWeight(std::size_t index, float far_weight) {
    constexpr float kDenominator = static_cast<float>(port::kBevReferenceSampleCount - 1U);
    return 1.0F + (far_weight - 1.0F) * static_cast<float>(index) / kDenominator;
}

}  // namespace

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
