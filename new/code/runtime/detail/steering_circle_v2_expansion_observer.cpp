#include "runtime/detail/steering_circle_v2_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

namespace ls2k::runtime::detail {
namespace {

constexpr float kRatioDenominatorFloor = 1.0e-4F;
constexpr std::size_t kOpeningSustainRows = 2U;

struct ExpansionParams {
    int min_support_rows = 4;
    int min_sampleable_per_row = 16;
    float bottom_start_forward_max_m = 0.20F;
    float bottom_row_gap_max_m = 0.10F;
    float open_expansion_min_m = 0.05F;
    float opening_expansion_ratio_min = 0.10F;
    float opposite_straight_drift_max_m = 0.06F;
    float opposite_shrink_ratio_min = 0.10F;
    float present_confidence_min = 0.65F;
};

constexpr ExpansionParams kParams{};

struct RowObservation {
    float forward_m = 0.0F;
    float left_m = 0.0F;
    float right_m = 0.0F;
};

struct BoundaryTracePoint {
    float forward_m = 0.0F;
    float boundary_m = 0.0F;
};

using BoundaryTrace = std::vector<BoundaryTracePoint>;

struct GrowthEvidence {
    bool found = false;
    std::size_t anchor_begin = 0;
    std::size_t split = 0;
    float anchor_reach = 0.0F;
    float ratio = 0.0F;
    float delta_m = 0.0F;
};

struct BoundaryLineFit {
    bool straight = false;
    bool shrink = false;
    float confidence = 0.0F;
};

float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

std::optional<float> CenterLateralForRow(const SceneFrameView& frame,
                                         std::size_t row_index,
                                         float forward_m) {
    if (!frame.ordinary_road.has_value()) {
        return std::nullopt;
    }
    const port::BEVReferencePath& center_path = frame.ordinary_road->center_path;
    if (row_index < center_path.sampled_path.size()) {
        const port::BEVPathSample& sample = center_path.sampled_path[row_index];
        if (sample.present && std::isfinite(sample.point.forward_m) &&
            std::isfinite(sample.point.lateral_m) &&
            std::fabs(sample.point.forward_m - forward_m) <= kParams.bottom_row_gap_max_m) {
            return sample.point.lateral_m;
        }
    }

    const port::BEVPathSample* best_sample = nullptr;
    float best_forward_error = std::numeric_limits<float>::max();
    for (const port::BEVPathSample& sample : center_path.sampled_path) {
        if (!sample.present || !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            continue;
        }
        const float forward_error = std::fabs(sample.point.forward_m - forward_m);
        if (forward_error < best_forward_error) {
            best_forward_error = forward_error;
            best_sample = &sample;
        }
    }
    if (best_sample != nullptr && best_forward_error <= kParams.bottom_row_gap_max_m) {
        return best_sample->point.lateral_m;
    }
    return std::nullopt;
}

float IntervalDistanceToLateral(const legacy::BEVSimpleWhiteInterval& interval,
                                float lateral_m) {
    if (lateral_m < interval.left_m) {
        return interval.left_m - lateral_m;
    }
    if (lateral_m > interval.right_m) {
        return lateral_m - interval.right_m;
    }
    return 0.0F;
}

const legacy::BEVSimpleWhiteInterval* SelectRoadConnectedInterval(
    const legacy::BEVSimpleRowScan& scan,
    const std::optional<float>& center_lateral) {
    const legacy::BEVSimpleWhiteInterval* selected = nullptr;
    float best_distance = std::numeric_limits<float>::max();
    for (const legacy::BEVSimpleWhiteInterval& interval : scan.intervals) {
        if (!std::isfinite(interval.left_m) ||
            !std::isfinite(interval.right_m) ||
            interval.right_m < interval.left_m) {
            continue;
        }
        if (!center_lateral.has_value()) {
            return nullptr;
        }
        const float distance = IntervalDistanceToLateral(interval, *center_lateral);
        if (selected == nullptr || distance < best_distance) {
            selected = &interval;
            best_distance = distance;
        }
    }
    return selected;
}

std::vector<RowObservation> CollectRows(const SceneFrameView& frame) {
    std::vector<RowObservation> rows;
    rows.reserve(frame.rows.rows.size());
    for (std::size_t index = 0; index < frame.rows.rows.size(); ++index) {
        const legacy::BEVSimpleRowScan& scan = frame.rows.rows[index];
        if (!scan.valid ||
            scan.sampleable_count <
                static_cast<std::size_t>(std::max(1, kParams.min_sampleable_per_row))) {
            continue;
        }

        RowObservation observation{};
        observation.forward_m = scan.forward_m;
        const std::optional<float> center_lateral =
            CenterLateralForRow(frame, index, scan.forward_m);
        const legacy::BEVSimpleWhiteInterval* connected_interval =
            SelectRoadConnectedInterval(scan, center_lateral);
        if (connected_interval != nullptr) {
            observation.left_m = connected_interval->left_m;
            observation.right_m = connected_interval->right_m;
            rows.push_back(observation);
            continue;
        }

        bool found_interval = false;
        for (const legacy::BEVSimpleWhiteInterval& interval : scan.intervals) {
            if (!std::isfinite(interval.left_m) || !std::isfinite(interval.right_m) ||
                interval.right_m < interval.left_m) {
                continue;
            }
            observation.left_m =
                found_interval ? std::min(observation.left_m, interval.left_m)
                               : interval.left_m;
            observation.right_m =
                found_interval ? std::max(observation.right_m, interval.right_m)
                               : interval.right_m;
            found_interval = true;
        }
        if (!found_interval) {
            continue;
        }
        rows.push_back(observation);
    }
    std::sort(rows.begin(), rows.end(), [](const RowObservation& lhs,
                                           const RowObservation& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    return rows;
}

float BoundaryValue(const RowObservation& row, bool use_left) {
    return use_left ? row.left_m : row.right_m;
}

BoundaryTrace BuildBoundaryTrace(const std::vector<RowObservation>& rows, bool use_left) {
    BoundaryTrace trace;
    trace.reserve(rows.size());
    for (const RowObservation& row : rows) {
        trace.push_back(BoundaryTracePoint{row.forward_m, BoundaryValue(row, use_left)});
    }
    return trace;
}

float Reach(const BoundaryTracePoint& point, bool use_left) {
    return use_left ? std::max(0.0F, -point.boundary_m)
                    : std::max(0.0F, point.boundary_m);
}

float ReachFromBoundaryValue(float boundary_m, bool use_left) {
    return use_left ? std::max(0.0F, -boundary_m) : std::max(0.0F, boundary_m);
}

float GrowthRatio(float near_reach, float far_reach) {
    return (far_reach - near_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

float ShrinkRatio(float near_reach, float far_reach) {
    return (near_reach - far_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

GrowthEvidence SustainedGrowthEvidence(const BoundaryTrace& trace,
                                       bool use_left) {
    GrowthEvidence best{};
    if (trace.size() <= kOpeningSustainRows) {
        return best;
    }
    for (std::size_t split = 1U; split + kOpeningSustainRows <= trace.size(); ++split) {
        const std::size_t anchor_begin =
            split > kOpeningSustainRows ? split - kOpeningSustainRows : 0U;
        float anchor_reach = Reach(trace[anchor_begin], use_left);
        for (std::size_t index = anchor_begin + 1U; index < split; ++index) {
            anchor_reach = std::max(anchor_reach, Reach(trace[index], use_left));
        }
        float sustained_reach = Reach(trace[split], use_left);
        for (std::size_t offset = 1U; offset < kOpeningSustainRows; ++offset) {
            sustained_reach =
                std::min(sustained_reach, Reach(trace[split + offset], use_left));
        }
        const float ratio = GrowthRatio(anchor_reach, sustained_reach);
        const float delta_m = sustained_reach - anchor_reach;
        if (!best.found || ratio > best.ratio) {
            best.found = true;
            best.anchor_begin = anchor_begin;
            best.split = split;
            best.anchor_reach = anchor_reach;
            best.ratio = ratio;
            best.delta_m = delta_m;
        }
    }
    return best;
}

BoundaryLineFit FitBoundaryLine(const BoundaryTrace& trace, bool use_left) {
    BoundaryLineFit fit{};
    if (trace.size() < static_cast<std::size_t>(std::max(2, kParams.min_support_rows))) {
        return fit;
    }

    float sum_x = 0.0F;
    float sum_y = 0.0F;
    for (const BoundaryTracePoint& point : trace) {
        sum_x += point.forward_m;
        sum_y += point.boundary_m;
    }
    const float count = static_cast<float>(trace.size());
    const float mean_x = sum_x / count;
    const float mean_y = sum_y / count;

    float var_x = 0.0F;
    float cov_xy = 0.0F;
    for (const BoundaryTracePoint& point : trace) {
        const float dx = point.forward_m - mean_x;
        var_x += dx * dx;
        cov_xy += dx * (point.boundary_m - mean_y);
    }
    if (var_x <= kRatioDenominatorFloor) {
        return fit;
    }

    const float slope = cov_xy / var_x;
    const float intercept = mean_y - slope * mean_x;
    float min_forward = trace.front().forward_m;
    float max_forward = trace.front().forward_m;
    std::vector<float> squared_errors;
    squared_errors.reserve(trace.size());
    for (const BoundaryTracePoint& point : trace) {
        min_forward = std::min(min_forward, point.forward_m);
        max_forward = std::max(max_forward, point.forward_m);
        const float expected = slope * point.forward_m + intercept;
        const float error = point.boundary_m - expected;
        squared_errors.push_back(error * error);
    }
    std::sort(squared_errors.begin(), squared_errors.end());
    const std::size_t retained_count =
        std::max<std::size_t>(1U, (squared_errors.size() * 9U + 9U) / 10U);
    const float retained_squared_error_sum =
        std::accumulate(squared_errors.begin(),
                        squared_errors.begin() + static_cast<std::ptrdiff_t>(retained_count),
                        0.0F);
    const float rmse =
        std::sqrt(retained_squared_error_sum / static_cast<float>(retained_count));

    const float drift_max = std::max(1.0e-4F, kParams.opposite_straight_drift_max_m);
    const float near_reach = ReachFromBoundaryValue(slope * min_forward + intercept, use_left);
    const float far_reach = ReachFromBoundaryValue(slope * max_forward + intercept, use_left);
    const float fitted_reach_drift_m = std::fabs(far_reach - near_reach);
    const bool fitted_shrink =
        near_reach > far_reach &&
        ShrinkRatio(near_reach, far_reach) >=
            std::max(kRatioDenominatorFloor, kParams.opposite_shrink_ratio_min);
    const bool fitted_shrink_exceeded =
        fitted_shrink &&
        fitted_reach_drift_m > std::max(kRatioDenominatorFloor, kParams.open_expansion_min_m);
    fit.shrink = fitted_shrink_exceeded;
    fit.straight = rmse <= drift_max && !fit.shrink;
    fit.confidence = Clamp01(1.0F - rmse / drift_max);
    return fit;
}

bool IsOpen(const GrowthEvidence& growth) {
    return growth.found &&
           growth.ratio >= std::max(kRatioDenominatorFloor, kParams.opening_expansion_ratio_min) &&
           growth.delta_m >= std::max(kRatioDenominatorFloor, kParams.open_expansion_min_m);
}

bool ReliableStraight(const BoundaryLineFit& fit, const CircleV2Params& params) {
    return fit.straight && fit.confidence >= params.opposite_straight_confidence_min;
}

std::vector<RowObservation> BottomRows(const std::vector<RowObservation>& rows) {
    std::vector<RowObservation> bottom_rows;
    if (rows.empty() || rows.front().forward_m > kParams.bottom_start_forward_max_m) {
        return bottom_rows;
    }
    const std::size_t target_count = std::min(
        rows.size(), static_cast<std::size_t>(std::max(1, kParams.min_support_rows)));
    bottom_rows.reserve(target_count);
    for (std::size_t index = 0; index < rows.size() && bottom_rows.size() < target_count;
         ++index) {
        if (!bottom_rows.empty() &&
            rows[index].forward_m - bottom_rows.back().forward_m > kParams.bottom_row_gap_max_m) {
            break;
        }
        bottom_rows.push_back(rows[index]);
    }
    return bottom_rows;
}

bool BottomSideOpeningReached(const std::vector<RowObservation>& bottom_rows, bool use_left) {
    const BoundaryTrace side_trace = BuildBoundaryTrace(bottom_rows, use_left);
    return IsOpen(SustainedGrowthEvidence(side_trace, use_left));
}

bool BottomEntryGateReached(const std::vector<RowObservation>& rows,
                            bool use_left,
                            const CircleV2Params& params) {
    const std::vector<RowObservation> bottom_rows = BottomRows(rows);
    if (bottom_rows.size() <
        static_cast<std::size_t>(std::max(1, kParams.min_support_rows))) {
        return false;
    }

    const bool opposite_use_left = !use_left;
    const BoundaryTrace opposite_trace = BuildBoundaryTrace(bottom_rows, opposite_use_left);
    const BoundaryLineFit opposite_fit = FitBoundaryLine(opposite_trace, opposite_use_left);
    return BottomSideOpeningReached(bottom_rows, use_left) &&
           ReliableStraight(opposite_fit, params);
}

float BaselineBoundary(const BoundaryTrace& trace,
                       const GrowthEvidence& growth,
                       bool /*use_left*/) {
    float sum = 0.0F;
    std::size_t count = 0;
    for (std::size_t index = growth.anchor_begin; index < growth.split; ++index) {
        sum += trace[index].boundary_m;
        ++count;
    }
    if (count == 0U) {
        return trace[growth.anchor_begin].boundary_m;
    }
    return sum / static_cast<float>(count);
}

bool EstimateP(const BoundaryTrace& trace,
               const GrowthEvidence& growth,
               bool use_left,
               port::BEVPoint& p) {
    if (!IsOpen(growth) || growth.split >= trace.size()) {
        return false;
    }
    std::size_t far_index = growth.split;
    for (std::size_t index = growth.split + 1U; index < trace.size(); ++index) {
        const float reach = Reach(trace[index], use_left);
        if (reach - growth.anchor_reach < kParams.open_expansion_min_m ||
            GrowthRatio(growth.anchor_reach, reach) < kParams.opening_expansion_ratio_min) {
            break;
        }
        far_index = index;
    }
    p.forward_m = trace[far_index].forward_m;
    p.lateral_m = BaselineBoundary(trace, growth, use_left);
    return std::isfinite(p.forward_m) && std::isfinite(p.lateral_m);
}

CircleDir DetectDir(bool left_open,
                    bool right_open,
                    const BoundaryLineFit& left_fit,
                    const BoundaryLineFit& right_fit,
                    const GrowthEvidence& left_growth,
                    const GrowthEvidence& right_growth,
                    std::size_t row_count,
                    const CircleV2Params& params) {
    if (left_open == right_open) {
        return CircleDir::kNone;
    }
    const float support_score =
        Clamp01(static_cast<float>(row_count) /
                static_cast<float>(std::max(1, kParams.min_support_rows)));
    if (left_open) {
        const float open_score =
            Clamp01(left_growth.ratio / std::max(kRatioDenominatorFloor,
                                                 kParams.opening_expansion_ratio_min));
        const float confidence =
            Clamp01(0.80F * open_score + 0.10F * right_fit.confidence + 0.10F * support_score);
        return !left_fit.straight && ReliableStraight(right_fit, params) &&
                       confidence >= kParams.present_confidence_min
                   ? CircleDir::kLeft
                   : CircleDir::kNone;
    }

    const float open_score =
        Clamp01(right_growth.ratio / std::max(kRatioDenominatorFloor,
                                              kParams.opening_expansion_ratio_min));
    const float confidence =
        Clamp01(0.80F * open_score + 0.10F * left_fit.confidence + 0.10F * support_score);
    return !right_fit.straight && ReliableStraight(left_fit, params) &&
                   confidence >= kParams.present_confidence_min
               ? CircleDir::kRight
               : CircleDir::kNone;
}

}  // namespace

CircleSideExpansionObservation ObserveCircleSideExpansion(const SceneFrameView& frame,
                                                          const CircleV2Params& params) {
    CircleSideExpansionObservation observation{};
    const std::vector<RowObservation> rows = CollectRows(frame);
    if (rows.size() < static_cast<std::size_t>(std::max(1, kParams.min_support_rows))) {
        return observation;
    }

    const BoundaryTrace left_trace = BuildBoundaryTrace(rows, true);
    const BoundaryTrace right_trace = BuildBoundaryTrace(rows, false);
    const GrowthEvidence left_growth = SustainedGrowthEvidence(left_trace, true);
    const GrowthEvidence right_growth = SustainedGrowthEvidence(right_trace, false);
    observation.left_phase1_open = IsOpen(left_growth);
    observation.right_phase1_open = IsOpen(right_growth);
    observation.left_entry_gate_reached = BottomEntryGateReached(rows, true, params);
    observation.right_entry_gate_reached = BottomEntryGateReached(rows, false, params);

    const BoundaryLineFit left_fit = FitBoundaryLine(left_trace, true);
    const BoundaryLineFit right_fit = FitBoundaryLine(right_trace, false);
    observation.detected_dir =
        DetectDir(observation.left_phase1_open,
                  observation.right_phase1_open,
                  left_fit,
                  right_fit,
                  left_growth,
                  right_growth,
                  rows.size(),
                  params);

    observation.left_p_available = EstimateP(left_trace, left_growth, true, observation.left_p);
    observation.right_p_available =
        EstimateP(right_trace, right_growth, false, observation.right_p);
    return observation;
}

}  // namespace ls2k::runtime::detail
