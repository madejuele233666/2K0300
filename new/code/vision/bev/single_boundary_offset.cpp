#include "vision/bev/single_boundary_offset.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace ls2k::vision {
namespace {

constexpr float kForwardEpsilon = 1.0e-5F;
constexpr float kLateralEpsilon = 1.0e-5F;

bool FinitePoint(const port::BEVPoint& point) {
    return std::isfinite(point.forward_m) && std::isfinite(point.lateral_m);
}

std::optional<std::vector<port::BEVPoint>> NormalizeTrace(
    const std::vector<port::BEVPoint>& boundary_trace) {
    std::vector<port::BEVPoint> points;
    points.reserve(boundary_trace.size());
    for (const port::BEVPoint& point : boundary_trace) {
        if (FinitePoint(point)) {
            points.push_back(point);
        }
    }
    std::sort(points.begin(), points.end(), [](const port::BEVPoint& lhs,
                                               const port::BEVPoint& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });

    std::vector<port::BEVPoint> normalized;
    normalized.reserve(points.size());
    for (const port::BEVPoint& point : points) {
        if (normalized.empty()) {
            normalized.push_back(point);
            continue;
        }
        const float dy = point.forward_m - normalized.back().forward_m;
        if (std::fabs(dy) <= kForwardEpsilon) {
            if (std::fabs(point.lateral_m - normalized.back().lateral_m) >
                kLateralEpsilon) {
                return std::nullopt;
            }
            continue;
        }
        normalized.push_back(point);
    }
    if (normalized.size() < 2U) {
        return std::nullopt;
    }
    return normalized;
}

}  // namespace

std::vector<port::BEVPoint> BuildSingleBoundaryOffsetReference(
    const std::vector<port::BEVPoint>& boundary_trace,
    const std::vector<float>& target_forward_samples,
    float signed_normal_offset_m) {
    std::vector<port::BEVPoint> output;
    if (!std::isfinite(signed_normal_offset_m)) {
        return output;
    }

    const std::optional<std::vector<port::BEVPoint>> normalized =
        NormalizeTrace(boundary_trace);
    if (!normalized.has_value()) {
        return output;
    }
    const std::vector<port::BEVPoint>& points = *normalized;
    std::size_t segment = 0U;
    output.reserve(target_forward_samples.size());

    for (float target_forward : target_forward_samples) {
        if (!std::isfinite(target_forward)) {
            break;
        }
        if (target_forward < points.front().forward_m - kForwardEpsilon ||
            target_forward > points.back().forward_m + kForwardEpsilon) {
            break;
        }
        while (segment + 1U < points.size() &&
               target_forward > points[segment + 1U].forward_m + kForwardEpsilon) {
            ++segment;
        }
        if (segment + 1U >= points.size()) {
            if (std::fabs(target_forward - points.back().forward_m) <=
                kForwardEpsilon) {
                segment = points.size() - 2U;
            } else {
                break;
            }
        }

        const port::BEVPoint& start = points[segment];
        const port::BEVPoint& end = points[segment + 1U];
        const float dy = end.forward_m - start.forward_m;
        if (std::fabs(dy) <= kForwardEpsilon) {
            break;
        }
        const float t =
            std::clamp((target_forward - start.forward_m) / dy, 0.0F, 1.0F);
        const float slope = (end.lateral_m - start.lateral_m) / dy;
        const float edge_lateral =
            start.lateral_m + t * (end.lateral_m - start.lateral_m);
        const float target_lateral =
            edge_lateral +
            signed_normal_offset_m * std::sqrt(1.0F + slope * slope);
        if (!std::isfinite(slope) || !std::isfinite(target_lateral)) {
            break;
        }
        output.push_back(port::BEVPoint{target_forward, target_lateral});
    }
    return output;
}

}  // namespace ls2k::vision
