#ifndef LS2K_LEGACY_STEERING_BEV_INTERVAL_EDGES_HPP
#define LS2K_LEGACY_STEERING_BEV_INTERVAL_EDGES_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "legacy/steering_bev_simple_perception.hpp"

namespace ls2k::legacy {

struct BEVIntervalEdgeVisibility {
    bool low_visible = false;
    bool high_visible = false;
};

struct BEVIntervalEdgeVisibilityOptions {
    bool treat_unknown_sampleable_edge_as_boundary = false;
};

inline bool HasSampleableLateralSpan(const BEVSimpleRowScan& row) {
    return row.sampleable_count > 1U &&
           row.sampleable_width_m > 0.0F &&
           std::isfinite(row.sampleable_left_m) &&
           std::isfinite(row.sampleable_right_m) &&
           row.sampleable_right_m >= row.sampleable_left_m;
}

inline float EstimateSampleableLateralStep(const BEVSimpleRowScan& row) {
    if (!HasSampleableLateralSpan(row)) {
        return 0.0F;
    }
    const std::size_t intervals =
        std::max<std::size_t>(1U, row.sampleable_count - 1U);
    return row.sampleable_width_m / static_cast<float>(intervals);
}

inline float IntervalEdgeTouchTolerance(const BEVSimpleRowScan& row) {
    return std::max(1.0e-4F, EstimateSampleableLateralStep(row) * 1.5F);
}

inline bool IntervalLowEdgeTouchesUnknownBoundary(
    const BEVSimpleRowScan& row,
    const BEVSimpleWhiteInterval& interval,
    float tolerance_m) {
    return row.sampleable_left_unknown_run &&
           std::isfinite(row.sampleable_left_unknown_run_right_m) &&
           std::fabs(interval.left_m - row.sampleable_left_unknown_run_right_m) <=
               tolerance_m;
}

inline bool IntervalHighEdgeTouchesUnknownBoundary(
    const BEVSimpleRowScan& row,
    const BEVSimpleWhiteInterval& interval,
    float tolerance_m) {
    return row.sampleable_right_unknown_run &&
           std::isfinite(row.sampleable_right_unknown_run_left_m) &&
           std::fabs(interval.right_m - row.sampleable_right_unknown_run_left_m) <=
               tolerance_m;
}

inline BEVIntervalEdgeVisibility EvaluateIntervalEdgeVisibility(
    const BEVSimpleRowScan& row,
    const BEVSimpleWhiteInterval& interval,
    const BEVIntervalEdgeVisibilityOptions& options = {}) {
    if (!std::isfinite(interval.left_m) || !std::isfinite(interval.right_m)) {
        return {};
    }
    if (!HasSampleableLateralSpan(row)) {
        return {true, true};
    }
    const float tolerance_m = IntervalEdgeTouchTolerance(row);
    const bool low_touches_boundary =
        std::fabs(interval.left_m - row.sampleable_left_m) <= tolerance_m ||
        (options.treat_unknown_sampleable_edge_as_boundary &&
         IntervalLowEdgeTouchesUnknownBoundary(row, interval, tolerance_m));
    const bool high_touches_boundary =
        std::fabs(interval.right_m - row.sampleable_right_m) <= tolerance_m ||
        (options.treat_unknown_sampleable_edge_as_boundary &&
         IntervalHighEdgeTouchesUnknownBoundary(row, interval, tolerance_m));
    return {
        !low_touches_boundary,
        !high_touches_boundary,
    };
}

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_INTERVAL_EDGES_HPP
