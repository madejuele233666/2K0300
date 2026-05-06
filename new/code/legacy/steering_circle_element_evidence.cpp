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

void FinishPresent(port::VisualElementEvidenceRecord& record, float confidence) {
    record.present = true;
    record.confidence = confidence;
    record.reason = "present";
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
    }
    result.left_raw.reason = "no_opening";
    return result;
}

}  // namespace ls2k::legacy
