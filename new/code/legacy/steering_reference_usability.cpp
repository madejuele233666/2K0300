#include "legacy/steering_reference_usability.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) && std::isfinite(sample.point.lateral_m);
}

std::size_t ConfiguredMinLeadingSamples(const port::RuntimeParameters& params) {
    constexpr int kMinSamplesForInterpolation = 2;
    const int bounded =
        std::clamp(params.bev_control_model.min_leading_reference_samples,
                   kMinSamplesForInterpolation,
                   static_cast<int>(port::kBevReferenceSampleCount));
    return static_cast<std::size_t>(bounded);
}

}  // namespace

port::ReferenceUsability EvaluateReferenceUsability(const port::BEVReferencePath& reference_path,
                                                    const port::RuntimeParameters& params) {
    port::ReferenceUsability usability{};
    const std::size_t min_leading_samples = ConfiguredMinLeadingSamples(params);

    for (const port::BEVPathSample& sample : reference_path.sampled_path) {
        if (!IsReferencePointPresent(sample)) {
            break;
        }
        if (usability.leading_usable_samples == 0) {
            usability.leading_min_forward_m = sample.point.forward_m;
            usability.leading_max_forward_m = sample.point.forward_m;
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
