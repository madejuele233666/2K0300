#include "legacy/steering_circle_element_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace ls2k::legacy {
namespace {

constexpr float kRatioDenominatorFloor = 1.0e-4F;
constexpr std::size_t kOpeningSustainRows = 2U;

struct RowObservation {
    int y = 0;
    int first_x = -1;
    int last_x = -1;
    float forward_m = 0.0F;
    float left_m = 0.0F;
    float right_m = 0.0F;
    std::size_t sampleable_count = 0;
    std::size_t white_count = 0;
    std::size_t black_count = 0;
    std::size_t unknown_count = 0;
};

struct SideAssessment {
    bool left_open = false;
    bool right_open = false;
    bool left_straight = false;
    bool right_straight = false;
    bool left_shrink = false;
    bool right_shrink = false;
    float left_confidence = 0.0F;
    float right_confidence = 0.0F;
};

struct BoundaryLineFit {
    bool straight = false;
    bool shrink = false;
    float confidence = 0.0F;
};

struct BoundaryChangeEvidence {
    float ratio = 0.0F;
    float delta_m = 0.0F;
};

struct WhiteRun {
    int first_x = -1;
    int last_x = -1;
    std::size_t count = 0U;
};

struct FrontierSupportPoint {
    port::BEVPoint frontier{};
    port::BEVPoint centerline{};
};

struct FrontierChain {
    std::vector<FrontierSupportPoint> points{};
};

float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

port::VisualElementCandidateSummary EvidenceOnlyCandidate() {
    port::VisualElementCandidateSummary candidate{};
    candidate.reason = "evidence_only";
    return candidate;
}

port::VisualElementEvidenceRecord MakeRecord(const char* id, const char* reason) {
    port::VisualElementEvidenceRecord record{};
    record.id = id;
    record.reason = reason;
    record.candidate = EvidenceOnlyCandidate();
    return record;
}

void AddSupport(port::VisualElementEvidenceRecord& record, const RowObservation& row) {
    record.support.sampleable_count += row.sampleable_count;
    record.support.supporting_white_count += row.white_count;
    record.support.supporting_black_count += row.black_count;
    record.support.unknown_count += row.unknown_count;
}

void AddBounds(port::VisualElementEvidenceRecord& record,
               const RowObservation& row,
               bool first) {
    if (first) {
        record.bounds.forward_min_m = row.forward_m;
        record.bounds.forward_max_m = row.forward_m;
        record.bounds.lateral_min_m = row.left_m;
        record.bounds.lateral_max_m = row.right_m;
        return;
    }
    record.bounds.forward_min_m = std::min(record.bounds.forward_min_m, row.forward_m);
    record.bounds.forward_max_m = std::max(record.bounds.forward_max_m, row.forward_m);
    record.bounds.lateral_min_m = std::min(record.bounds.lateral_min_m, row.left_m);
    record.bounds.lateral_max_m = std::max(record.bounds.lateral_max_m, row.right_m);
}

bool BuildRowObservation(const BEVElementRasterFrame& raster,
                         int y,
                         int min_sampleable_per_row,
                         RowObservation& row) {
    WhiteRun current_run{};
    WhiteRun best_run{};
    const auto finish_run = [&current_run, &best_run]() {
        if (current_run.count > best_run.count) {
            best_run = current_run;
        }
        current_run = {};
    };
    for (int x = 0; x < raster.width; ++x) {
        const std::size_t index = raster.Index(x, y);
        const bool sampleable =
            index < raster.projection_states.size() &&
            index < raster.classes.size() &&
            raster.projection_states[index] == port::BEVElementRasterProjectionState::kSampleable;
        if (!sampleable) {
            finish_run();
            continue;
        }
        ++row.sampleable_count;
        switch (raster.classes[index]) {
            case port::BEVElementRasterCellClass::kWhite:
                ++row.white_count;
                if (current_run.count == 0U) {
                    current_run.first_x = x;
                }
                current_run.last_x = x;
                ++current_run.count;
                break;
            case port::BEVElementRasterCellClass::kBlack:
                finish_run();
                ++row.black_count;
                break;
            case port::BEVElementRasterCellClass::kUnknown:
                finish_run();
                ++row.unknown_count;
                break;
            case port::BEVElementRasterCellClass::kInvalid:
                finish_run();
                break;
        }
    }
    finish_run();
    if (row.sampleable_count < static_cast<std::size_t>(std::max(1, min_sampleable_per_row)) ||
        best_run.count == 0U ||
        best_run.first_x < 0 ||
        best_run.last_x < best_run.first_x) {
        return false;
    }
    row.forward_m = raster.CellToMetric(raster.width / 2, y).forward_m;
    row.y = y;
    row.first_x = best_run.first_x;
    row.last_x = best_run.last_x;
    row.left_m = raster.CellToMetric(best_run.first_x, y).lateral_m;
    row.right_m = raster.CellToMetric(best_run.last_x, y).lateral_m;
    return true;
}

std::vector<RowObservation> CollectRows(const BEVElementRasterFrame& raster,
                                        const port::BEVElementParameters& params,
                                        port::VisualElementEvidenceRecord& left,
                                        port::VisualElementEvidenceRecord& right) {
    std::vector<RowObservation> rows;
    for (int y = 0; y < raster.height; ++y) {
        RowObservation row{};
        if (!BuildRowObservation(raster, y, params.circle_min_sampleable_per_row, row)) {
            continue;
        }
        AddSupport(left, row);
        AddSupport(right, row);
        AddBounds(left, row, rows.empty());
        AddBounds(right, row, rows.empty());
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const RowObservation& lhs, const RowObservation& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    return rows;
}

bool HasSaturatedWideWhiteRows(const std::vector<RowObservation>& rows,
                               const port::BEVElementParameters& params) {
    std::size_t consecutive = 0U;
    const float ratio_min = std::clamp(params.cross_wide_row_white_ratio_min, 0.0F, 1.0F);
    for (const RowObservation& row : rows) {
        const float ratio =
            row.sampleable_count > 0U
                ? static_cast<float>(row.white_count) / static_cast<float>(row.sampleable_count)
                : 0.0F;
        if (ratio >= ratio_min) {
            ++consecutive;
            if (consecutive >= kOpeningSustainRows) {
                return true;
            }
        } else {
            consecutive = 0U;
        }
    }
    return false;
}

float Reach(const RowObservation& row, bool use_left) {
    return use_left ? std::max(0.0F, -row.left_m) : std::max(0.0F, row.right_m);
}

float GrowthRatio(float near_reach, float far_reach) {
    return (far_reach - near_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

float ShrinkRatio(float near_reach, float far_reach) {
    return (near_reach - far_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

BoundaryChangeEvidence SustainedGrowthEvidence(const std::vector<RowObservation>& rows,
                                               bool use_left) {
    BoundaryChangeEvidence best{};
    if (rows.size() <= kOpeningSustainRows) {
        return best;
    }
    float anchor_reach = Reach(rows.front(), use_left);
    for (std::size_t split = 1U; split + kOpeningSustainRows <= rows.size(); ++split) {
        anchor_reach = std::min(anchor_reach, Reach(rows[split - 1U], use_left));
        float sustained_reach = Reach(rows[split], use_left);
        for (std::size_t offset = 1U; offset < kOpeningSustainRows; ++offset) {
            sustained_reach =
                std::min(sustained_reach, Reach(rows[split + offset], use_left));
        }
        const BoundaryChangeEvidence candidate{
            GrowthRatio(anchor_reach, sustained_reach),
            sustained_reach - anchor_reach};
        if (candidate.ratio > best.ratio) {
            best = candidate;
        }
    }
    return best;
}

float BoundaryValue(const RowObservation& row, bool use_left) {
    return use_left ? row.left_m : row.right_m;
}

float ReachFromBoundaryValue(float boundary_m, bool use_left) {
    return use_left ? std::max(0.0F, -boundary_m) : std::max(0.0F, boundary_m);
}

BoundaryLineFit FitBoundaryLine(const std::vector<RowObservation>& rows,
                                bool use_left,
                                const port::BEVElementParameters& params) {
    BoundaryLineFit fit{};
    if (rows.size() < static_cast<std::size_t>(std::max(2, params.circle_min_support_rows))) {
        return fit;
    }

    float sum_x = 0.0F;
    float sum_y = 0.0F;
    for (const RowObservation& row : rows) {
        sum_x += row.forward_m;
        sum_y += BoundaryValue(row, use_left);
    }
    const float count = static_cast<float>(rows.size());
    const float mean_x = sum_x / count;
    const float mean_y = sum_y / count;

    float var_x = 0.0F;
    float cov_xy = 0.0F;
    for (const RowObservation& row : rows) {
        const float dx = row.forward_m - mean_x;
        var_x += dx * dx;
        cov_xy += dx * (BoundaryValue(row, use_left) - mean_y);
    }
    if (var_x <= kRatioDenominatorFloor) {
        return fit;
    }

    const float slope = cov_xy / var_x;
    const float intercept = mean_y - slope * mean_x;
    float min_forward = rows.front().forward_m;
    float max_forward = rows.front().forward_m;
    std::vector<float> squared_errors;
    squared_errors.reserve(rows.size());
    for (const RowObservation& row : rows) {
        min_forward = std::min(min_forward, row.forward_m);
        max_forward = std::max(max_forward, row.forward_m);
        const float expected = slope * row.forward_m + intercept;
        const float error = BoundaryValue(row, use_left) - expected;
        squared_errors.push_back(error * error);
    }
    std::sort(squared_errors.begin(), squared_errors.end());
    const std::size_t retained_count = std::max<std::size_t>(
        1U,
        (squared_errors.size() * 9U + 9U) / 10U);
    const float retained_squared_error_sum =
        std::accumulate(squared_errors.begin(),
                        squared_errors.begin() + static_cast<std::ptrdiff_t>(retained_count),
                        0.0F);

    const float rmse = std::sqrt(retained_squared_error_sum /
                                 static_cast<float>(retained_count));
    const float drift_max = std::max(1.0e-4F, params.circle_opposite_straight_drift_max_m);
    const float near_reach = ReachFromBoundaryValue(slope * min_forward + intercept, use_left);
    const float far_reach = ReachFromBoundaryValue(slope * max_forward + intercept, use_left);
    const float fitted_reach_drift_m = std::abs(far_reach - near_reach);
    const bool fitted_shrink =
        near_reach > far_reach &&
        ShrinkRatio(near_reach, far_reach) >=
            std::max(kRatioDenominatorFloor, params.circle_opposite_shrink_ratio_min);
    const float shrink_delta_min_m =
        std::max(kRatioDenominatorFloor, 2.0F * params.circle_open_expansion_min_m);
    const bool fitted_shrink_exceeded =
        fitted_shrink && fitted_reach_drift_m > shrink_delta_min_m;
    fit.shrink = fitted_shrink_exceeded;
    fit.straight = rmse <= drift_max && !fit.shrink;
    const float residual_score = Clamp01(1.0F - rmse / drift_max);
    fit.confidence = residual_score;
    return fit;
}

SideAssessment AssessSides(const std::vector<RowObservation>& rows,
                           const port::BEVElementParameters& params) {
    SideAssessment assessment{};
    const BoundaryChangeEvidence left_growth = SustainedGrowthEvidence(rows, true);
    const BoundaryChangeEvidence right_growth = SustainedGrowthEvidence(rows, false);
    const float opening_ratio_min =
        std::max(kRatioDenominatorFloor, params.circle_opening_expansion_ratio_min);
    const float opening_delta_min =
        std::max(kRatioDenominatorFloor, params.circle_open_expansion_min_m);
    const BoundaryLineFit left_fit = FitBoundaryLine(rows, true, params);
    const BoundaryLineFit right_fit = FitBoundaryLine(rows, false, params);

    assessment.left_open =
        left_growth.ratio >= opening_ratio_min && left_growth.delta_m >= opening_delta_min;
    assessment.right_open =
        right_growth.ratio >= opening_ratio_min && right_growth.delta_m >= opening_delta_min;
    assessment.left_straight = left_fit.straight;
    assessment.right_straight = right_fit.straight;
    assessment.left_shrink = left_fit.shrink;
    assessment.right_shrink = right_fit.shrink;

    const float left_open_score = Clamp01(left_growth.ratio / opening_ratio_min);
    const float right_open_score = Clamp01(right_growth.ratio / opening_ratio_min);
    const float support_score =
        Clamp01(static_cast<float>(rows.size()) /
                static_cast<float>(std::max(1, params.circle_min_support_rows)));
    assessment.left_confidence =
        Clamp01(0.80F * left_open_score +
                0.10F * right_fit.confidence +
                0.10F * support_score);
    assessment.right_confidence =
        Clamp01(0.80F * right_open_score +
                0.10F * left_fit.confidence +
                0.10F * support_score);
    return assessment;
}

bool IntervalsOverlap(const RowObservation& lhs, const RowObservation& rhs) {
    return lhs.first_x <= rhs.last_x + 1 && rhs.first_x <= lhs.last_x + 1;
}

std::vector<RowObservation> NearConnectedRows(const std::vector<RowObservation>& rows) {
    std::vector<RowObservation> connected;
    if (rows.empty()) {
        return connected;
    }
    connected.push_back(rows.front());
    RowObservation previous = rows.front();
    for (std::size_t index = 1U; index < rows.size(); ++index) {
        const RowObservation& row = rows[index];
        if (!IntervalsOverlap(previous, row)) {
            break;
        }
        connected.push_back(row);
        previous = row;
    }
    return connected;
}

float Median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0F;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    if ((values.size() % 2U) == 1U) {
        return values[middle];
    }
    return 0.5F * (values[middle - 1U] + values[middle]);
}

bool IsSampleableBlack(const BEVElementRasterFrame& raster, int x, int y) {
    if (!raster.InBounds(x, y)) {
        return false;
    }
    const std::size_t index = raster.Index(x, y);
    return index < raster.projection_states.size() &&
           index < raster.classes.size() &&
           raster.projection_states[index] ==
               port::BEVElementRasterProjectionState::kSampleable &&
           raster.classes[index] == port::BEVElementRasterCellClass::kBlack;
}

bool IsInteriorSampleableBlack(const BEVElementRasterFrame& raster, int x, int y) {
    if (x <= 0 || y <= 0 || x >= raster.width - 1 || y >= raster.height - 1) {
        return false;
    }
    return IsSampleableBlack(raster, x, y);
}

bool IsSampleableWhite(const BEVElementRasterFrame& raster, int x, int y) {
    if (!raster.InBounds(x, y)) {
        return false;
    }
    const std::size_t index = raster.Index(x, y);
    return index < raster.projection_states.size() &&
           index < raster.classes.size() &&
           raster.projection_states[index] ==
               port::BEVElementRasterProjectionState::kSampleable &&
           raster.classes[index] == port::BEVElementRasterCellClass::kWhite;
}

bool HasRearSideBlack(const BEVElementRasterFrame& raster, int x, int y, bool left_side) {
    const int rear_y = y + 1;
    if (rear_y >= raster.height) {
        return false;
    }
    if (IsInteriorSampleableBlack(raster, x, rear_y)) {
        return true;
    }
    const int side_x = left_side ? x - 1 : x + 1;
    return IsInteriorSampleableBlack(raster, side_x, rear_y);
}

bool FindRearFrontierPoint(const BEVElementRasterFrame& raster,
                           const RowObservation& row,
                           bool left_side,
                           port::BEVPoint& point) {
    if (row.y <= 0 || row.y >= raster.height - 1) {
        return false;
    }
    const int step = left_side ? 1 : -1;
    const int begin = left_side ? row.first_x : row.last_x;
    const int end = left_side ? row.last_x : row.first_x;
    for (int x = begin; left_side ? x <= end : x >= end; x += step) {
        if (x <= 0 || x >= raster.width - 1) {
            continue;
        }
        if (!IsSampleableWhite(raster, x, row.y) ||
            !HasRearSideBlack(raster, x, row.y, left_side)) {
            continue;
        }
        point = raster.CellToMetric(x, row.y);
        return true;
    }
    return false;
}

void AddPoint(std::vector<port::BEVPoint>& points, const port::BEVPoint& point) {
    if (!std::isfinite(point.forward_m) || !std::isfinite(point.lateral_m)) {
        return;
    }
    if (!points.empty() && std::fabs(points.back().forward_m - point.forward_m) <= 1.0e-5F) {
        points.back() = point;
        return;
    }
    points.push_back(point);
}

CircleEntryPathFacts BuildEntryFacts(const BEVElementRasterFrame& raster,
                                     const std::vector<RowObservation>& rows,
                                     const port::BEVElementParameters& params,
                                     bool left_side) {
    CircleEntryPathFacts facts{};
    const int support_rows_min = std::max(1, params.circle_min_support_rows);
    const std::vector<RowObservation> component = NearConnectedRows(rows);
    if (component.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }

    std::vector<float> near_widths;
    near_widths.reserve(static_cast<std::size_t>(support_rows_min));
    for (std::size_t index = 0U;
         index < component.size() && near_widths.size() < static_cast<std::size_t>(support_rows_min);
         ++index) {
        const float width = component[index].right_m - component[index].left_m;
        if (std::isfinite(width) && width > 0.0F) {
            near_widths.push_back(width);
        }
    }
    if (near_widths.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }

    facts.road_half_width_m = 0.5F * Median(near_widths);
    if (!std::isfinite(facts.road_half_width_m) || facts.road_half_width_m <= 0.0F) {
        facts.reason = "road_half_width_unavailable";
        return facts;
    }

    std::vector<FrontierSupportPoint> frontier_candidates;
    for (const RowObservation& row : component) {
        port::BEVPoint near_center{};
        near_center.forward_m = row.forward_m;
        near_center.lateral_m = 0.5F * (row.left_m + row.right_m);
        AddPoint(facts.near_centerline_points, near_center);

        port::BEVPoint frontier{};
        if (!FindRearFrontierPoint(raster, row, left_side, frontier)) {
            continue;
        }

        port::BEVPoint center = frontier;
        center.lateral_m += left_side ? facts.road_half_width_m : -facts.road_half_width_m;
        frontier_candidates.push_back({frontier, center});
    }

    const int frontier_min = std::max(1, params.circle_entry_min_frontier_points);
    if (frontier_candidates.size() < static_cast<std::size_t>(frontier_min)) {
        facts.reason = "frontier_points_insufficient";
        return facts;
    }

    const float max_gap = std::max(1.0e-4F, params.circle_entry_max_interpolation_gap_m);
    std::vector<FrontierChain> chains;
    FrontierChain current_chain{};
    for (const FrontierSupportPoint& candidate : frontier_candidates) {
        if (!current_chain.points.empty()) {
            const float gap =
                candidate.frontier.forward_m - current_chain.points.back().frontier.forward_m;
            if (gap > max_gap) {
                chains.push_back(current_chain);
                current_chain = {};
            }
        }
        current_chain.points.push_back(candidate);
    }
    if (!current_chain.points.empty()) {
        chains.push_back(current_chain);
    }

    const float lateral_min = std::max(0.0F, params.circle_entry_direction_min_lateral_m);
    const FrontierChain* selected_chain = nullptr;
    std::size_t selected_count = 0U;
    float selected_abs_lateral_delta = 0.0F;
    bool has_supported_chain = false;
    bool has_forward_chain = false;
    float best_failed_forward_delta = 0.0F;
    float best_failed_lateral_delta = 0.0F;
    float best_failed_abs_lateral_delta = -1.0F;
    for (const FrontierChain& chain : chains) {
        if (chain.points.size() < static_cast<std::size_t>(frontier_min)) {
            continue;
        }
        has_supported_chain = true;
        const port::BEVPoint& first = chain.points.front().frontier;
        const port::BEVPoint& last = chain.points.back().frontier;
        const float forward_delta = last.forward_m - first.forward_m;
        const float lateral_delta = last.lateral_m - first.lateral_m;
        if (forward_delta <= 0.0F) {
            continue;
        }
        has_forward_chain = true;
        const float abs_lateral_delta = std::fabs(lateral_delta);
        if (abs_lateral_delta > best_failed_abs_lateral_delta) {
            best_failed_abs_lateral_delta = abs_lateral_delta;
            best_failed_forward_delta = forward_delta;
            best_failed_lateral_delta = lateral_delta;
        }
        const bool direction_ok =
            left_side ? lateral_delta <= -lateral_min : lateral_delta >= lateral_min;
        if (!direction_ok) {
            continue;
        }
        if (selected_chain == nullptr ||
            chain.points.size() > selected_count ||
            (chain.points.size() == selected_count &&
             abs_lateral_delta > selected_abs_lateral_delta)) {
            selected_chain = &chain;
            selected_count = chain.points.size();
            selected_abs_lateral_delta = abs_lateral_delta;
            facts.direction_delta_forward_m = forward_delta;
            facts.direction_delta_lateral_m = lateral_delta;
        }
    }
    if (!has_supported_chain) {
        facts.reason = "interpolation_gap_exceeded";
        return facts;
    }
    if (!has_forward_chain) {
        facts.reason = "frontier_direction_not_forward";
        return facts;
    }
    if (selected_chain == nullptr) {
        facts.direction_delta_forward_m = best_failed_forward_delta;
        facts.direction_delta_lateral_m = best_failed_lateral_delta;
        facts.reason = "frontier_direction_insufficient";
        return facts;
    }

    for (const FrontierSupportPoint& point : selected_chain->points) {
        AddPoint(facts.frontier_points, point.frontier);
        AddPoint(facts.centerline_points, point.centerline);
    }
    facts.present = true;
    facts.reason = "present";
    return facts;
}

void FinishPresent(port::VisualElementEvidenceRecord& record, float confidence) {
    record.present = true;
    record.confidence = confidence;
    record.reason = "present";
}

bool BuildCircleReferencePath(const CircleEntryPathFacts& entry,
                              const port::RuntimeParameters& params,
                              port::BEVReferencePath& path,
                              std::string& reason) {
    std::vector<port::BEVPoint> support;
    const float max_gap = std::max(1.0e-4F, params.bev_element.circle_entry_max_interpolation_gap_m);
    const float max_join = std::max(0.0F, params.bev_element.circle_entry_max_join_jump_m);
    const float first_frontier_forward =
        entry.centerline_points.empty() ? 0.0F : entry.centerline_points.front().forward_m;

    for (const port::BEVPoint& point : entry.near_centerline_points) {
        if (point.forward_m <= first_frontier_forward + 1.0e-5F) {
            AddPoint(support, point);
        }
    }
    if (!support.empty() && !entry.centerline_points.empty() &&
        std::fabs(support.back().lateral_m - entry.centerline_points.front().lateral_m) >
            max_join) {
        reason = "join_jump_exceeded";
        return false;
    }
    for (const port::BEVPoint& point : entry.centerline_points) {
        AddPoint(support, point);
    }
    if (support.size() < 2U) {
        reason = "path_support_insufficient";
        return false;
    }
    std::sort(support.begin(), support.end(), [](const port::BEVPoint& lhs,
                                                 const port::BEVPoint& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    for (std::size_t index = 1U; index < support.size(); ++index) {
        const float gap = support[index].forward_m - support[index - 1U].forward_m;
        if (gap <= 1.0e-5F) {
            reason = "path_support_not_ordered";
            return false;
        }
        if (gap > max_gap) {
            reason = "interpolation_gap_exceeded";
            return false;
        }
    }

    path = {};
    path.mode = port::ReferenceMode::kIntervalCenter;
    std::size_t support_index = 0U;
    for (std::size_t sample_index = 0U;
         sample_index < port::kBevReferenceSampleCount;
         ++sample_index) {
        const float forward = params.bev_geometry.forward_samples_m[sample_index];
        if (forward < support.front().forward_m - max_gap) {
            break;
        }
        while (support_index + 1U < support.size() &&
               support[support_index + 1U].forward_m < forward) {
            ++support_index;
        }

        float lateral = 0.0F;
        if (forward <= support.front().forward_m) {
            lateral = support.front().lateral_m;
        } else if (support_index + 1U < support.size()) {
            const port::BEVPoint& before = support[support_index];
            const port::BEVPoint& after = support[support_index + 1U];
            const float gap = after.forward_m - before.forward_m;
            if (forward < before.forward_m || forward > after.forward_m || gap > max_gap) {
                break;
            }
            const float ratio = (forward - before.forward_m) / gap;
            lateral = before.lateral_m + ratio * (after.lateral_m - before.lateral_m);
        } else {
            break;
        }

        port::BEVPathSample& sample = path.sampled_path[sample_index];
        sample.present = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m = lateral;
        sample.confidence = 1.0F;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
    }

    if (!path.sampled_path[0].present) {
        reason = "missing_leading_reference_sample";
        path = {};
        return false;
    }
    reason = "present";
    return true;
}

}  // namespace

CircleElementEvidenceResult DetectCircleElementEvidence(
    const BEVElementRasterFrame* raster,
    const port::RuntimeParameters& params) {
    CircleElementEvidenceResult result{};
    result.left_raw = MakeRecord("circle_left_raw", "not_evaluated");
    result.right_raw = MakeRecord("circle_right_raw", "not_evaluated");

    if (!params.bev_element.circle_evidence_enabled) {
        result.left_raw.reason = "circle_evidence_disabled";
        result.right_raw.reason = "circle_evidence_disabled";
        return result;
    }
    if (raster == nullptr || !raster->enabled || !raster->valid ||
        raster->width <= 1 || raster->height <= 1) {
        result.left_raw.reason = "raster_unavailable";
        result.right_raw.reason = "raster_unavailable";
        return result;
    }

    const std::vector<RowObservation> rows =
        CollectRows(*raster, params.bev_element, result.left_raw, result.right_raw);
    if (rows.size() < static_cast<std::size_t>(std::max(1, params.bev_element.circle_min_support_rows))) {
        result.left_raw.reason = "insufficient_sampleable_support";
        result.right_raw.reason = "insufficient_sampleable_support";
        return result;
    }
    if (HasSaturatedWideWhiteRows(rows, params.bev_element)) {
        result.left_raw.reason = "saturated_wide_white_rows";
        result.right_raw.reason = "saturated_wide_white_rows";
        return result;
    }

    const SideAssessment assessment = AssessSides(rows, params.bev_element);
    const float present_min = params.bev_element.circle_present_confidence_min;
    if (assessment.left_open && assessment.right_open) {
        if (assessment.left_straight && !assessment.right_straight) {
            if (assessment.right_confidence < present_min) {
                result.right_raw.confidence = assessment.right_confidence;
                result.right_raw.reason = "low_confidence";
            } else {
                FinishPresent(result.right_raw, assessment.right_confidence);
                result.right_entry = BuildEntryFacts(*raster, rows, params.bev_element, false);
            }
            result.left_raw.reason = "no_opening";
            return result;
        }
        if (assessment.right_straight && !assessment.left_straight) {
            if (assessment.left_confidence < present_min) {
                result.left_raw.confidence = assessment.left_confidence;
                result.left_raw.reason = "low_confidence";
            } else {
                FinishPresent(result.left_raw, assessment.left_confidence);
                result.left_entry = BuildEntryFacts(*raster, rows, params.bev_element, true);
            }
            result.right_raw.reason = "no_opening";
            return result;
        }
        if (!assessment.left_straight && !assessment.right_straight) {
            result.left_raw.reason = "bend";
            result.right_raw.reason = "bend";
            return result;
        }
        result.left_raw.reason = "both_sides_open";
        result.right_raw.reason = "both_sides_open";
        return result;
    }
    if (!assessment.left_open && !assessment.right_open) {
        result.left_raw.reason = "no_opening";
        result.right_raw.reason = "no_opening";
        return result;
    }

    if (assessment.left_open) {
        if (!assessment.right_straight) {
            result.left_raw.reason =
                assessment.right_shrink ? "bend" : "opposite_straight_drift_exceeded";
        } else if (assessment.left_confidence < present_min) {
            result.left_raw.confidence = assessment.left_confidence;
            result.left_raw.reason = "low_confidence";
        } else {
            FinishPresent(result.left_raw, assessment.left_confidence);
            result.left_entry = BuildEntryFacts(*raster, rows, params.bev_element, true);
        }
        result.right_raw.reason = "no_opening";
        return result;
    }

    if (!assessment.left_straight) {
        result.right_raw.reason =
            assessment.left_shrink ? "bend" : "opposite_straight_drift_exceeded";
    } else if (assessment.right_confidence < present_min) {
        result.right_raw.confidence = assessment.right_confidence;
        result.right_raw.reason = "low_confidence";
    } else {
        FinishPresent(result.right_raw, assessment.right_confidence);
        result.right_entry = BuildEntryFacts(*raster, rows, params.bev_element, false);
    }
    result.left_raw.reason = "no_opening";
    return result;
}

port::VisualReferenceCandidate BuildCircleEntryVisualReferenceCandidate(
    const port::VisualElementEvidenceRecord& evidence,
    const CircleEntryPathFacts& entry,
    port::VisualReferenceCandidateKind kind,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary) {
    port::VisualReferenceCandidate candidate{};
    summary = {};
    summary.takeover_enabled = params.bev_element.circle_entry_takeover_enabled;
    if (!evidence.present) {
        summary.reason = evidence.reason.empty() ? "evidence_absent" : evidence.reason;
        return candidate;
    }
    if (!entry.present) {
        summary.reason = entry.reason.empty() ? "entry_facts_absent" : entry.reason;
        return candidate;
    }

    port::BEVReferencePath path{};
    std::string reason;
    if (!BuildCircleReferencePath(entry, params, path, reason)) {
        summary.reason = reason.empty() ? "reference_path_invalid" : reason;
        return candidate;
    }

    candidate.present = true;
    candidate.kind = kind;
    candidate.reference_path = path;
    candidate.confidence = evidence.confidence;
    if (kind == port::VisualReferenceCandidateKind::kCircleLeft) {
        candidate.source = "circle_left";
        candidate.reason = "circle_left_entry_frontier_candidate";
    } else {
        candidate.source = "circle_right";
        candidate.reason = "circle_right_entry_frontier_candidate";
    }

    summary.built = true;
    summary.included_in_arbitration = summary.takeover_enabled;
    summary.reason = summary.included_in_arbitration ? "included_in_arbitration"
                                                     : "takeover_disabled";
    return candidate;
}

}  // namespace ls2k::legacy
