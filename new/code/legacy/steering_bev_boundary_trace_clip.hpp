#ifndef LS2K_LEGACY_STEERING_BEV_BOUNDARY_TRACE_CLIP_HPP
#define LS2K_LEGACY_STEERING_BEV_BOUNDARY_TRACE_CLIP_HPP

#include <cmath>
#include <cstddef>
#include <vector>

#include "port/bev_geometry_types.hpp"

namespace ls2k::legacy {

struct BEVBoundaryTracePoint {
    std::size_t row_index = 0U;
    port::BEVPoint point{};
};

struct BEVBoundaryTraceClipOptions {
    float max_adjacent_distance_m = 0.0F;
};

inline std::size_t BoundaryTraceRowGap(std::size_t lhs, std::size_t rhs) {
    return lhs >= rhs ? lhs - rhs : rhs - lhs;
}

inline std::vector<BEVBoundaryTracePoint> ClipBoundaryTraceOutliers(
    const std::vector<BEVBoundaryTracePoint>& raw_points,
    const BEVBoundaryTraceClipOptions& options) {
    std::vector<BEVBoundaryTracePoint> clipped;
    if (raw_points.empty()) {
        return clipped;
    }
    clipped.reserve(raw_points.size());
    clipped.push_back(raw_points.front());
    for (std::size_t index = 1U; index < raw_points.size(); ++index) {
        const BEVBoundaryTracePoint& last_kept = clipped.back();
        const BEVBoundaryTracePoint& candidate = raw_points[index];
        const float delta_forward_m =
            candidate.point.forward_m - last_kept.point.forward_m;
        const float delta_lateral_m =
            candidate.point.lateral_m - last_kept.point.lateral_m;
        const float distance_m = std::hypot(delta_forward_m, delta_lateral_m);
        const float allowed_m =
            options.max_adjacent_distance_m *
            static_cast<float>(BoundaryTraceRowGap(candidate.row_index,
                                                   last_kept.row_index));
        if (distance_m <= allowed_m) {
            clipped.push_back(candidate);
        }
    }
    return clipped;
}

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_BOUNDARY_TRACE_CLIP_HPP
