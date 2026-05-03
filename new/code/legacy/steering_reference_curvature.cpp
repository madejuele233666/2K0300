#include "legacy/steering_reference_curvature.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

constexpr float kForwardEpsilonM = 1.0e-4F;

bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) && std::isfinite(sample.point.lateral_m);
}

bool InterpolateReferenceAt(const std::array<port::BEVPathSample, port::kBevReferenceSampleCount>& path,
                            const port::ReferenceUsability& usability,
                            port::BEVPathSample& output) {
    if (!usability.usable || usability.leading_usable_samples == 0) {
        return false;
    }

    const float forward_m = usability.lookahead_distance_m;
    if (forward_m < usability.leading_min_forward_m - kForwardEpsilonM ||
        forward_m > usability.leading_max_forward_m + kForwardEpsilonM) {
        return false;
    }

    const port::BEVPathSample* previous = nullptr;
    const port::BEVPathSample* next = nullptr;
    const std::size_t bounded_count = std::min(usability.leading_usable_samples, path.size());
    for (std::size_t index = 0; index < bounded_count; ++index) {
        const port::BEVPathSample& sample = path[index];
        if (!IsReferencePointPresent(sample)) {
            break;
        }
        if (sample.point.forward_m <= forward_m + kForwardEpsilonM) {
            previous = &sample;
        }
        if (sample.point.forward_m >= forward_m - kForwardEpsilonM) {
            next = &sample;
            break;
        }
    }

    if (previous == nullptr && next == nullptr) {
        return false;
    }
    if (previous == nullptr) {
        output = *next;
        output.point.forward_m = forward_m;
        return true;
    }
    if (next == nullptr) {
        output = *previous;
        output.point.forward_m = forward_m;
        return true;
    }

    const float span = next->point.forward_m - previous->point.forward_m;
    if (std::abs(span) < kForwardEpsilonM) {
        output = *next;
        output.point.forward_m = forward_m;
        return true;
    }

    const float t = std::clamp((forward_m - previous->point.forward_m) / span, 0.0F, 1.0F);
    output.present = true;
    output.point.forward_m = forward_m;
    output.point.lateral_m = previous->point.lateral_m +
                             t * (next->point.lateral_m - previous->point.lateral_m);
    output.confidence = std::clamp(previous->confidence +
                                       t * (next->confidence - previous->confidence),
                                   0.0F,
                                   1.0F);
    return true;
}

float ClampMagnitude(float value, double limit) {
    const float abs_limit = static_cast<float>(std::max(0.0, limit));
    return std::clamp(value, -abs_limit, abs_limit);
}

port::ReferenceCurvatureEstimate UncomputedOutput(const std::string& reason) {
    port::ReferenceCurvatureEstimate output{};
    output.computed = false;
    output.reason = reason;
    return output;
}

}  // namespace

port::ReferenceCurvatureEstimate ComputeReferenceCurvature(const port::BEVReferencePath& reference_path,
                                                           const port::ReferenceUsability& usability,
                                                           const port::RuntimeParameters& params) {
    if (!usability.usable) {
        return UncomputedOutput(usability.reason);
    }

    port::BEVPathSample lookahead{};
    if (!InterpolateReferenceAt(reference_path.sampled_path, usability, lookahead)) {
        return UncomputedOutput("reference_lookahead_unavailable");
    }

    const port::BEVControlModelParameters& model = params.bev_control_model;
    const float lateral = lookahead.point.lateral_m;
    const float lookahead_m = usability.lookahead_distance_m;
    const float denominator = std::max(1.0e-4F, lookahead_m * lookahead_m + lateral * lateral);
    const float curvature =
        static_cast<float>(model.pure_pursuit_gain) * (2.0F * lateral / denominator);

    if (!std::isfinite(curvature)) {
        return UncomputedOutput("curvature_command_unavailable");
    }

    port::ReferenceCurvatureEstimate output{};
    output.computed = true;
    output.lookahead_distance_m = lookahead_m;
    output.curvature_command = ClampMagnitude(curvature, model.curvature_command_limit);
    output.reason = "ok";
    return output;
}

}  // namespace ls2k::legacy
