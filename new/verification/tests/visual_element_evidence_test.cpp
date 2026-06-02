#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "legacy/steering_circle_element_evidence.hpp"
#include "legacy/steering_cross_exit_element_evidence.hpp"
#include "legacy/steering_visual_element_pipeline.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::legacy::BEVSimpleRowScan MakeRow(float forward_m,
                                       std::size_t sampleable_count,
                                       std::size_t white_count,
                                       std::size_t unknown_count,
                                       float sampleable_width_m,
                                       float interval_left_m,
                                       float interval_right_m) {
    ls2k::legacy::BEVSimpleRowScan row{};
    row.valid = true;
    row.forward_m = forward_m;
    row.sampleable_count = sampleable_count;
    row.white_count = white_count;
    row.unknown_count = unknown_count;
    row.black_count = sampleable_count > white_count + unknown_count
                          ? sampleable_count - white_count - unknown_count
                          : 0U;
    row.sampleable_left_m = -0.5F * sampleable_width_m;
    row.sampleable_right_m = 0.5F * sampleable_width_m;
    row.sampleable_width_m = sampleable_width_m;
    if (interval_right_m > interval_left_m) {
        ls2k::legacy::BEVSimpleWhiteInterval interval{};
        interval.forward_m = forward_m;
        interval.left_m = interval_left_m;
        interval.right_m = interval_right_m;
        interval.center_m = 0.5F * (interval_left_m + interval_right_m);
        interval.width_m = interval_right_m - interval_left_m;
        row.intervals.push_back(interval);
    }
    return row;
}

std::vector<ls2k::legacy::BEVSimpleRowScan> WideCrossRows() {
    return {MakeRow(0.24F, 80U, 78U, 0U, 1.40F, -0.50F, 0.50F),
            MakeRow(0.30F, 80U, 78U, 0U, 1.40F, -0.58F, 0.58F),
            MakeRow(0.36F, 80U, 78U, 0U, 1.40F, -0.66F, 0.66F)};
}

std::vector<ls2k::legacy::BEVSimpleRowScan> MakeRowsFromReachRows(
    const std::vector<float>& left_reach_near_to_far,
    const std::vector<float>& right_reach_near_to_far) {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows;
    const std::size_t count = std::min(left_reach_near_to_far.size(),
                                       right_reach_near_to_far.size());
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float left_reach = left_reach_near_to_far[index];
        const float right_reach = right_reach_near_to_far[index];
        const std::size_t white_count =
            static_cast<std::size_t>(std::clamp((left_reach + right_reach) / 1.30F,
                                                0.0F,
                                                1.0F) *
                                     65.0F);
        ls2k::legacy::BEVSimpleRowScan row =
            MakeRow(0.06F + static_cast<float>(index) * 0.06F,
                    65U,
                    std::max<std::size_t>(1U, white_count),
                    0U,
                    1.30F,
                    -left_reach,
                    right_reach);
        row.row_px = static_cast<int>(index);
        rows.push_back(row);
    }
    return rows;
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftCircleRows() {
    std::vector<float> left(24U, 0.42F);
    left[0] = 0.12F;
    left[1] = 0.18F;
    left[2] = 0.34F;
    std::vector<float> right(24U, 0.20F);
    return MakeRowsFromReachRows(left, right);
}

ls2k::legacy::BEVElementRasterFrame MakeRasterFromBounds(float near_left_m,
                                                         float near_right_m,
                                                         float far_left_m,
                                                         float far_right_m) {
    ls2k::legacy::BEVElementRasterFrame raster{};
    raster.valid = true;
    raster.enabled = true;
    raster.width = 65;
    raster.height = 24;
    raster.lateral_limit_m = 0.65F;
    raster.forward_max_m = 1.50F;
    const std::size_t cell_count = static_cast<std::size_t>(raster.width * raster.height);
    raster.classes.assign(cell_count, ls2k::port::BEVElementRasterCellClass::kBlack);
    raster.projection_states.assign(cell_count,
                                    ls2k::port::BEVElementRasterProjectionState::kSampleable);
    for (int y = 0; y < raster.height; ++y) {
        const float far_ratio =
            1.0F - static_cast<float>(y) / static_cast<float>(raster.height - 1);
        const float left_m = near_left_m + far_ratio * (far_left_m - near_left_m);
        const float right_m = near_right_m + far_ratio * (far_right_m - near_right_m);
        for (int x = 0; x < raster.width; ++x) {
            const float lateral_m = raster.CellToMetric(x, y).lateral_m;
            if (lateral_m >= left_m && lateral_m <= right_m) {
                raster.classes[raster.Index(x, y)] =
                    ls2k::port::BEVElementRasterCellClass::kWhite;
            }
        }
    }
    return raster;
}

ls2k::legacy::BEVElementRasterFrame MakeRasterFromReachRows(
    const std::vector<float>& left_reach_near_to_far,
    const std::vector<float>& right_reach_near_to_far) {
    ls2k::legacy::BEVElementRasterFrame raster{};
    raster.valid = true;
    raster.enabled = true;
    raster.width = 65;
    raster.height = static_cast<int>(std::min(left_reach_near_to_far.size(),
                                              right_reach_near_to_far.size()));
    raster.lateral_limit_m = 0.65F;
    raster.forward_max_m = 1.50F;
    const std::size_t cell_count = static_cast<std::size_t>(raster.width * raster.height);
    raster.classes.assign(cell_count, ls2k::port::BEVElementRasterCellClass::kBlack);
    raster.projection_states.assign(cell_count,
                                    ls2k::port::BEVElementRasterProjectionState::kSampleable);
    for (int y = 0; y < raster.height; ++y) {
        const std::size_t reach_index = static_cast<std::size_t>(raster.height - 1 - y);
        const float left_m = -left_reach_near_to_far[reach_index];
        const float right_m = right_reach_near_to_far[reach_index];
        for (int x = 0; x < raster.width; ++x) {
            const float lateral_m = raster.CellToMetric(x, y).lateral_m;
            if (lateral_m >= left_m && lateral_m <= right_m) {
                raster.classes[raster.Index(x, y)] =
                    ls2k::port::BEVElementRasterCellClass::kWhite;
            }
        }
    }
    return raster;
}

ls2k::legacy::BEVElementRasterFrame MakeRasterWithUnknownEdgeReachRows(
    const std::vector<float>& left_reach_near_to_far,
    const std::vector<float>& right_reach_near_to_far,
    bool left_unknown_prefix,
    bool right_unknown_suffix) {
    ls2k::legacy::BEVElementRasterFrame raster{};
    raster.valid = true;
    raster.enabled = true;
    raster.width = 65;
    raster.height = static_cast<int>(std::min(left_reach_near_to_far.size(),
                                              right_reach_near_to_far.size()));
    raster.lateral_limit_m = 0.65F;
    raster.forward_max_m = 1.50F;
    const std::size_t cell_count = static_cast<std::size_t>(raster.width * raster.height);
    raster.classes.assign(cell_count, ls2k::port::BEVElementRasterCellClass::kBlack);
    raster.projection_states.assign(cell_count,
                                    ls2k::port::BEVElementRasterProjectionState::kSampleable);
    for (int y = 0; y < raster.height; ++y) {
        const std::size_t reach_index = static_cast<std::size_t>(raster.height - 1 - y);
        const float left_m = -left_reach_near_to_far[reach_index];
        const float right_m = right_reach_near_to_far[reach_index];
        for (int x = 0; x < raster.width; ++x) {
            const float lateral_m = raster.CellToMetric(x, y).lateral_m;
            ls2k::port::BEVElementRasterCellClass cell_class =
                ls2k::port::BEVElementRasterCellClass::kBlack;
            if (lateral_m >= left_m && lateral_m <= right_m) {
                cell_class = ls2k::port::BEVElementRasterCellClass::kWhite;
            } else if ((left_unknown_prefix && lateral_m < left_m) ||
                       (right_unknown_suffix && lateral_m > right_m)) {
                cell_class = ls2k::port::BEVElementRasterCellClass::kUnknown;
            }
            raster.classes[raster.Index(x, y)] = cell_class;
        }
    }
    return raster;
}

void AddDetachedLeftIsland(ls2k::legacy::BEVElementRasterFrame& raster,
                           std::size_t near_to_far_index,
                           float left_m,
                           float right_m) {
    if (near_to_far_index >= static_cast<std::size_t>(std::max(0, raster.height))) {
        return;
    }
    const int y = raster.height - 1 - static_cast<int>(near_to_far_index);
    for (int x = 0; x < raster.width; ++x) {
        const float lateral_m = raster.CellToMetric(x, y).lateral_m;
        if (lateral_m >= left_m && lateral_m <= right_m) {
            raster.classes[raster.Index(x, y)] =
                ls2k::port::BEVElementRasterCellClass::kWhite;
        }
    }
}

ls2k::legacy::BEVElementRasterFrame LeftCircleRaster() {
    std::vector<float> left(24U, 0.42F);
    left[0] = 0.12F;
    left[1] = 0.18F;
    left[2] = 0.34F;
    std::vector<float> right(24U, 0.20F);
    return MakeRasterFromReachRows(left, right);
}

ls2k::legacy::BEVElementRasterFrame RightCircleRaster() {
    std::vector<float> left(24U, 0.20F);
    std::vector<float> right(24U, 0.42F);
    right[0] = 0.12F;
    right[1] = 0.18F;
    right[2] = 0.34F;
    return MakeRasterFromReachRows(left, right);
}

const ls2k::port::VisualElementEvidenceRecord* FindRecord(
    const ls2k::port::VisualElementEvidenceFrame& evidence,
    const std::string& id) {
    for (const ls2k::port::VisualElementEvidenceRecord& record : evidence.records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

ls2k::port::VisualReferenceCandidate MakeLineCandidate(int present_count) {
    ls2k::port::VisualReferenceCandidate candidate{};
    candidate.present = present_count > 0;
    candidate.kind = ls2k::port::VisualReferenceCandidateKind::kLine;
    candidate.reference_path.mode = present_count > 0 ? ls2k::port::ReferenceMode::kIntervalCenter
                                                       : ls2k::port::ReferenceMode::kNone;
    candidate.confidence = 1.0F;
    candidate.source = "simple_interval_center";
    for (std::size_t index = 0; index < candidate.reference_path.sampled_path.size(); ++index) {
        ls2k::port::BEVPathSample& sample = candidate.reference_path.sampled_path[index];
        sample.point.forward_m = 0.05F + static_cast<float>(index) * 0.05F;
        sample.point.lateral_m = 0.0F;
        if (static_cast<int>(index) < present_count) {
            sample.present = true;
            sample.confidence = 1.0F;
            sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
        }
    }
    return candidate;
}

void TestCrossPresentFromWideRows() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(WideCrossRows(), params);
    Expect(evidence.present, "wide contiguous rows must produce cross evidence");
    Expect(evidence.reason == "present", "present evidence must expose present reason");
    Expect(evidence.confidence >= 0.70F, "present evidence must have configured confidence");
    Expect(evidence.sampleable_count > 0U, "present evidence must expose sampleable support");
    Expect(evidence.supporting_white_count > 0U, "present evidence must expose white support");
    Expect(evidence.forward_max_m >= evidence.forward_min_m, "present evidence must expose forward bounds");
    Expect(evidence.lateral_max_m > evidence.lateral_min_m, "present evidence must expose lateral bounds");
}

void TestCrossAbsentReasons() {
    const ls2k::port::RuntimeParameters params{};
    Expect(ls2k::legacy::DetectCrossExitEvidence({}, params).reason == "no_sparse_rows",
           "empty rows must explain absence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> unsupported{
        MakeRow(0.2F, 4U, 4U, 0U, 1.0F, -0.4F, 0.4F),
        MakeRow(0.3F, 4U, 4U, 0U, 1.0F, -0.4F, 0.4F),
        MakeRow(0.4F, 4U, 4U, 0U, 1.0F, -0.4F, 0.4F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(unsupported, params).reason ==
               "insufficient_sampleable_support",
           "low sampleable support must fail closed");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> narrow{
        MakeRow(0.2F, 40U, 12U, 0U, 1.0F, -0.10F, 0.10F),
        MakeRow(0.3F, 40U, 12U, 0U, 1.0F, -0.10F, 0.10F),
        MakeRow(0.4F, 40U, 12U, 0U, 1.0F, -0.10F, 0.10F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(narrow, params).reason == "wide_white_rows_absent",
           "narrow white rows must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> loose_white_ratio{
        MakeRow(0.24F, 100U, 94U, 0U, 1.00F, -0.36F, 0.36F),
        MakeRow(0.30F, 100U, 94U, 0U, 1.00F, -0.43F, 0.43F),
        MakeRow(0.36F, 100U, 94U, 0U, 1.00F, -0.50F, 0.50F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(loose_white_ratio, params).reason ==
               "wide_white_rows_absent",
           "wide rows below the configured white ratio must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> one_side_open{
        MakeRow(0.24F, 170U, 165U, 1U, 1.00F, -0.49F, 0.17F),
        MakeRow(0.30F, 170U, 165U, 1U, 1.00F, -0.49F, 0.17F),
        MakeRow(0.36F, 170U, 165U, 1U, 1.00F, -0.49F, 0.17F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(one_side_open, params).reason ==
               "wide_white_rows_absent",
           "one-side circle-like opening must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> one_side_open_with_straight_edge{
        MakeRow(0.24F, 170U, 165U, 0U, 1.00F, -0.49F, 0.25F),
        MakeRow(0.30F, 170U, 165U, 0U, 1.00F, -0.49F, 0.25F),
        MakeRow(0.36F, 170U, 165U, 0U, 1.00F, -0.49F, 0.25F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(one_side_open_with_straight_edge, params).reason ==
               "wide_white_rows_absent",
           "one-side opening with straight opposite edge must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> far_wide_bend{
        MakeRow(0.80F, 80U, 70U, 2U, 1.30F, -0.57F, 0.63F),
        MakeRow(0.86F, 80U, 70U, 2U, 1.30F, -0.61F, 0.61F),
        MakeRow(0.92F, 80U, 70U, 2U, 1.30F, -0.65F, 0.57F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(far_wide_bend, params).reason ==
               "wide_white_rows_absent",
           "far-only wide bend rows must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> transient_expansion{
        MakeRow(0.24F, 100U, 98U, 0U, 1.00F, -0.36F, 0.36F),
        MakeRow(0.30F, 100U, 98U, 0U, 1.00F, -0.50F, 0.50F),
        MakeRow(0.36F, 100U, 98U, 0U, 1.00F, -0.36F, 0.36F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(transient_expansion, params).reason ==
               "wide_white_rows_absent",
           "a one-row expansion spike must not become cross evidence");

    const std::vector<ls2k::legacy::BEVSimpleRowScan> below_cross_width_symmetric_expansion{
        MakeRow(0.18F, 100U, 98U, 0U, 1.00F, -0.36F, 0.36F),
        MakeRow(0.24F, 100U, 98U, 0U, 1.00F, -0.42F, 0.42F),
        MakeRow(0.30F, 100U, 98U, 0U, 1.00F, -0.44F, 0.44F)};
    Expect(ls2k::legacy::DetectCrossExitEvidence(below_cross_width_symmetric_expansion,
                                                 params).reason ==
               "wide_white_rows_absent",
           "symmetric expansion below the cross width threshold must not become cross evidence");
}

void TestCrossWhiteRatioCanBeParameterized() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element.cross_wide_row_white_ratio_min = 0.98F;
    const std::vector<ls2k::legacy::BEVSimpleRowScan> strict{
        MakeRow(0.24F, 100U, 97U, 0U, 1.00F, -0.36F, 0.36F),
        MakeRow(0.30F, 100U, 97U, 0U, 1.00F, -0.43F, 0.43F),
        MakeRow(0.36F, 100U, 97U, 0U, 1.00F, -0.50F, 0.50F)};
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(strict, params);
    Expect(!evidence.present, "white ratio below an explicitly stricter threshold must fail");
    Expect(evidence.reason == "wide_white_rows_absent",
           "strict white-ratio rejection must remain fail-closed");
}

void TestCandidateTakeoverEnabledByDefault() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(WideCrossRows(), params);
    ls2k::port::VisualElementCandidateSummary summary{};
    const ls2k::port::VisualReferenceCandidate candidate =
        ls2k::legacy::BuildCrossExitVisualReferenceCandidate(evidence,
                                                            MakeLineCandidate(3),
                                                            params,
                                                            summary);
    Expect(candidate.present, "present evidence with line facts must build a candidate");
    Expect(summary.built, "candidate summary must report built candidate");
    Expect(summary.takeover_enabled, "takeover must default to enabled");
    Expect(summary.included_in_arbitration, "default takeover must enter arbitration");
    Expect(summary.reason == "included_in_arbitration",
           "default takeover inclusion must be explicit");
}

void TestCandidateCanBeExplicitlyIncluded() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element.cross_exit_takeover_enabled = true;
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(WideCrossRows(), params);
    ls2k::port::VisualElementCandidateSummary summary{};
    const ls2k::port::VisualReferenceCandidate candidate =
        ls2k::legacy::BuildCrossExitVisualReferenceCandidate(evidence,
                                                            MakeLineCandidate(3),
                                                            params,
                                                            summary);
    Expect(candidate.present, "enabled takeover still requires a built candidate");
    Expect(summary.takeover_enabled, "explicit parameter must enable takeover");
    Expect(summary.included_in_arbitration, "enabled built candidate must be includable");
    Expect(summary.reason == "included_in_arbitration", "included candidate must be explicit");
}

void TestCandidateRejectsGappedLineFacts() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element.cross_exit_takeover_enabled = true;
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(WideCrossRows(), params);
    ls2k::port::VisualReferenceCandidate line = MakeLineCandidate(3);
    line.reference_path.sampled_path[1].present = false;
    ls2k::port::VisualElementCandidateSummary summary{};
    const ls2k::port::VisualReferenceCandidate candidate =
        ls2k::legacy::BuildCrossExitVisualReferenceCandidate(evidence, line, params, summary);
    Expect(!candidate.present, "gapped line facts must not build a cross candidate");
    Expect(!summary.built, "gapped line facts must fail before candidate build");
    Expect(!summary.included_in_arbitration, "gapped candidate must never enter arbitration");
    Expect(summary.reason == "line_candidate_absent", "gapped line facts must be explained");
}

void TestCircleLeftPresentFromRaster() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(evidence.left_raw.id == "circle_left_raw", "left raw id must be stable");
    Expect(evidence.left_raw.present,
           "left opening plus right straight must produce left circle, reason=" +
               evidence.left_raw.reason);
    Expect(evidence.left_raw.reason == "present", "left circle present reason must be present");
    Expect(evidence.left_raw.confidence >= 0.65F,
           "left circle confidence must pass threshold");
    Expect(evidence.left_raw.support.sampleable_count > 0U,
           "left circle must expose sampleable support");
    Expect(evidence.left_raw.support.supporting_white_count > 0U,
           "left circle must expose white support");
    Expect(!evidence.right_raw.present, "left circle must not produce right circle");
}

void TestCircleLeftPresentFromSparseRows() {
    const ls2k::port::RuntimeParameters params{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftCircleRows();
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(rows, params);
    Expect(evidence.left_raw.id == "circle_left_raw", "sparse left raw id must be stable");
    Expect(evidence.left_raw.present,
           "sparse left opening plus right straight must produce left circle, reason=" +
               evidence.left_raw.reason);
    Expect(evidence.left_raw.reason == "present",
           "sparse left circle present reason must be present");
    Expect(!evidence.right_raw.present, "sparse left circle must not produce right circle");
}

void TestCircleRightPresentFromRaster() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame raster = RightCircleRaster();
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(evidence.right_raw.id == "circle_right_raw", "right raw id must be stable");
    Expect(evidence.right_raw.present, "right opening plus left straight must produce right circle");
    Expect(evidence.right_raw.reason == "present", "right circle present reason must be present");
    Expect(!evidence.left_raw.present, "right circle must not produce left circle");
}

void TestCircleAbsentCases() {
    const ls2k::port::RuntimeParameters params{};
    std::vector<float> both_left(24U, 0.42F);
    std::vector<float> both_right(24U, 0.42F);
    both_left[0] = 0.12F;
    both_left[1] = 0.18F;
    both_left[2] = 0.34F;
    both_right[0] = 0.12F;
    both_right[1] = 0.18F;
    both_right[2] = 0.34F;
    const ls2k::legacy::BEVElementRasterFrame both_open =
        MakeRasterFromReachRows(both_left, both_right);
    const ls2k::legacy::CircleElementEvidenceResult both =
        ls2k::legacy::DetectCircleElementEvidence(&both_open, params);
    Expect(!both.left_raw.present && !both.right_raw.present,
           "both-side opening must not produce circle evidence");
    Expect(both.left_raw.reason == "both_sides_open",
           "both-side opening must be distinguishable");

    const ls2k::legacy::BEVElementRasterFrame straight =
        MakeRasterFromBounds(-0.12F, 0.12F, -0.12F, 0.12F);
    const ls2k::legacy::CircleElementEvidenceResult no_open =
        ls2k::legacy::DetectCircleElementEvidence(&straight, params);
    Expect(!no_open.left_raw.present && !no_open.right_raw.present,
           "no opening must not produce circle evidence");
    Expect(no_open.left_raw.reason == "no_opening", "no opening reason must be stable");

    const ls2k::legacy::CircleElementEvidenceResult missing =
        ls2k::legacy::DetectCircleElementEvidence(nullptr, params);
    Expect(missing.left_raw.reason == "raster_unavailable",
           "missing raster must fail closed");
}

void TestCircleRejectsSampleableBoundaryClippedOpening() {
    const ls2k::port::RuntimeParameters params{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows =
        MakeRowsFromReachRows({0.12F, 0.18F, 0.65F, 0.65F},
                              {0.20F, 0.20F, 0.20F, 0.20F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(rows, params);
    Expect(!evidence.left_raw.present && !evidence.right_raw.present,
           "sampleable-boundary clipped rows must not become circle evidence");
    Expect(evidence.left_raw.reason == "insufficient_sampleable_support",
           "clipped opening rows must fail before boundary growth is trusted");
}

void TestCircleRejectsUnknownScreenEdgeClippedRasterOpening() {
    const ls2k::port::RuntimeParameters params{};
    const std::vector<float> left_open{0.12F, 0.18F, 0.34F, 0.42F, 0.42F};
    const std::vector<float> right_straight(5U, 0.20F);
    const ls2k::legacy::BEVElementRasterFrame left_unknown =
        MakeRasterWithUnknownEdgeReachRows(left_open,
                                           right_straight,
                                           true,
                                           false);
    const ls2k::legacy::CircleElementEvidenceResult left_evidence =
        ls2k::legacy::DetectCircleElementEvidence(&left_unknown, params);
    Expect(!left_evidence.left_raw.present && !left_evidence.right_raw.present,
           "left unknown screen-edge prefix must not become raster circle evidence");
    Expect(left_evidence.left_raw.reason == "insufficient_sampleable_support",
           "left unknown screen-edge rows must be removed before opening classification");

    const std::vector<float> left_straight(5U, 0.20F);
    const std::vector<float> right_open{0.12F, 0.18F, 0.34F, 0.42F, 0.42F};
    const ls2k::legacy::BEVElementRasterFrame right_unknown =
        MakeRasterWithUnknownEdgeReachRows(left_straight,
                                           right_open,
                                           false,
                                           true);
    const ls2k::legacy::CircleElementEvidenceResult right_evidence =
        ls2k::legacy::DetectCircleElementEvidence(&right_unknown, params);
    Expect(!right_evidence.left_raw.present && !right_evidence.right_raw.present,
           "right unknown screen-edge suffix must not become raster circle evidence");
    Expect(right_evidence.right_raw.reason == "insufficient_sampleable_support",
           "right unknown screen-edge rows must be removed before opening classification");
}

void TestCircleOpeningUsesNetExpansionNotStrictMonotonic() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame net_open =
        MakeRasterFromReachRows({0.10F, 0.30F, 0.29F, 0.29F},
                                {0.24F, 0.24F, 0.24F, 0.24F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&net_open, params);
    Expect(evidence.left_raw.present,
           "net left expansion with a small later contraction must remain left circle");
    Expect(evidence.left_raw.reason == "present",
           "net expansion circle reason must be present");
    Expect(!evidence.right_raw.present,
           "stable opposite side must not become right circle");
    Expect(evidence.right_raw.reason == "no_opening",
           "stable opposite side must be reported as no opening");
}

void TestCircleRejectsTransientOpeningSpike() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame spike =
        MakeRasterFromReachRows({0.20F, 0.32F, 0.20F, 0.20F},
                                {0.24F, 0.24F, 0.24F, 0.24F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&spike, params);
    Expect(!evidence.left_raw.present,
           "a transient left expansion spike must not become circle evidence");
    Expect(evidence.left_raw.reason == "no_opening",
           "transient expansion spike must remain no_opening");
}

void TestCircleIgnoresDetachedWhiteIslandOutsideMainBand() {
    const ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVElementRasterFrame raster =
        MakeRasterFromReachRows({0.17F, 0.17F, 0.17F, 0.17F, 0.17F, 0.17F},
                                {0.21F, 0.29F, 0.35F, 0.41F, 0.49F, 0.55F});
    AddDetachedLeftIsland(raster, 2U, -0.65F, -0.57F);
    AddDetachedLeftIsland(raster, 3U, -0.65F, -0.55F);
    AddDetachedLeftIsland(raster, 4U, -0.65F, -0.55F);

    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(!evidence.left_raw.present,
           "detached left white island must not become a left opening");
    Expect(evidence.left_raw.reason == "no_opening",
           "stable main left boundary must remain no_opening, reason=" +
               evidence.left_raw.reason);
    Expect(evidence.right_raw.present,
           "right opening with stable main left boundary must produce right circle, reason=" +
               evidence.right_raw.reason);
}

void TestCircleAllowsSmallOppositeFittedDrift() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame raster =
        MakeRasterFromReachRows({0.19F, 0.47F, 0.35F, 0.20F, 0.65F, 0.65F, 0.15F, 0.15F},
                                {0.21F, 0.22F, 0.23F, 0.24F, 0.25F, 0.27F, 0.28F, 0.29F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(evidence.left_raw.present,
           "left opening plus fitted-straight right boundary must remain left circle, reason=" +
               evidence.left_raw.reason);
    Expect(!evidence.right_raw.present,
           "small fitted drift on the opposite boundary must not become right circle");
}

void TestCircleRejectsSaturatedWideWhiteRows() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame raster =
        MakeRasterFromReachRows({0.12F, 0.42F, 0.65F, 0.65F},
                                {0.20F, 0.20F, 0.65F, 0.65F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(!evidence.left_raw.present && !evidence.right_raw.present,
           "two-sided wide opening must stay out of raw circle evidence");
    Expect(evidence.left_raw.reason == "insufficient_sampleable_support",
           "sampleable-boundary saturated rows must fail before opening classification");
}

void TestCircleReportsBendForFragmentedDoubleOpening() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame raster =
        MakeRasterFromReachRows({0.18F, 0.34F, 0.50F, 0.65F, 0.65F, 0.00F, 0.00F, 0.00F},
                                {0.22F, 0.10F, 0.00F, 0.00F, 0.00F, 0.22F, 0.48F, 0.65F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(!evidence.left_raw.present && !evidence.right_raw.present,
           "fragmented bend-like double opening must not produce circle evidence");
    Expect(evidence.left_raw.reason == "both_sides_open",
           "two-sided opening should be reported as both_sides_open, reason=" +
               evidence.left_raw.reason);
    Expect(evidence.right_raw.reason == "both_sides_open",
           "both raw circle sides should agree on both_sides_open reason");
}

void TestCircleRejectsOppositeShrinkAsBend() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVElementRasterFrame bend =
        MakeRasterFromReachRows({0.10F, 0.30F, 0.29F, 0.29F},
                                {0.30F, 0.20F, 0.18F, 0.18F});
    const ls2k::legacy::CircleElementEvidenceResult evidence =
        ls2k::legacy::DetectCircleElementEvidence(&bend, params);
    Expect(!evidence.left_raw.present,
           "one-side expansion with opposite shrink must be bend, not circle");
    Expect(evidence.left_raw.reason == "bend",
           "opposite shrink must report the bend fail-closed reason, reason=" +
               evidence.left_raw.reason);
    Expect(!evidence.right_raw.present,
           "opposite shrink must not produce right circle");
}

void TestPipelineDoesNotAppendCircleRecordsFromRasterPath() {
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    ls2k::legacy::VisualElementRasterCompatibilityInput input{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows{};
    input.sparse_rows = &rows;
    input.element_raster = &raster;
    input.line_candidate = MakeLineCandidate(3);
    ls2k::port::RuntimeParameters params{};
    params.bev_element.cross_exit_takeover_enabled = false;
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementRasterCompatibilityPipeline(
            input,
            params);
    Expect(FindRecord(result.evidence, "circle_left_raw") == nullptr,
           "runtime visual element pipeline must not emit left raw circle records");
    Expect(FindRecord(result.evidence, "circle_right_raw") == nullptr,
           "runtime visual element pipeline must not emit right raw circle records");
    Expect(FindRecord(result.evidence, "circle_left") == nullptr,
           "runtime visual element pipeline must not emit effective left circle records");
    Expect(FindRecord(result.evidence, "circle_right") == nullptr,
           "runtime visual element pipeline must not emit effective right circle records");
    Expect(result.candidates.empty(), "visual element pipeline must not push circle candidates");
}

void TestSparsePipelineDoesNotAppendCircleRecords() {
    ls2k::legacy::VisualElementPipelineInput input{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftCircleRows();
    input.sparse_rows = &rows;
    input.line_candidate = MakeLineCandidate(3);
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementPipeline(input, ls2k::port::RuntimeParameters{});
    const ls2k::port::VisualElementEvidenceRecord* left_raw =
        FindRecord(result.evidence, "circle_left_raw");
    const ls2k::port::VisualElementEvidenceRecord* left =
        FindRecord(result.evidence, "circle_left");
    Expect(left_raw == nullptr, "sparse runtime pipeline must not publish raw circle facts");
    Expect(left == nullptr, "sparse runtime pipeline must not publish effective circle facts");
    Expect(result.candidates.empty(),
           "sparse runtime pipeline must not push circle candidates");
}

void TestPipelineNeverPushesLegacyCircleCandidate() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element.circle_v2_enabled = true;
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    ls2k::legacy::VisualElementRasterCompatibilityInput input{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows{};
    input.sparse_rows = &rows;
    input.element_raster = &raster;
    input.line_candidate = MakeLineCandidate(3);
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementRasterCompatibilityPipeline(input, params);
    Expect(result.candidates.empty(),
           "CircleV2 enablement must not make legacy visual element pipeline push circles");
    Expect(FindRecord(result.evidence, "circle_left") == nullptr,
           "legacy effective circle records must remain absent");
}

void TestPipelineCrossDoesNotCreateSuppressedCircleRecords() {
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows = WideCrossRows();
    ls2k::legacy::VisualElementRasterCompatibilityInput input{};
    input.sparse_rows = &rows;
    input.element_raster = &raster;
    input.line_candidate = MakeLineCandidate(3);
    ls2k::port::RuntimeParameters params{};
    params.bev_element.cross_exit_takeover_enabled = false;
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementRasterCompatibilityPipeline(
            input,
            params);
    const ls2k::port::VisualElementEvidenceRecord* left_raw =
        FindRecord(result.evidence, "circle_left_raw");
    const ls2k::port::VisualElementEvidenceRecord* left =
        FindRecord(result.evidence, "circle_left");
    Expect(result.evidence.cross_exit.present, "wide rows must produce cross evidence");
    Expect(left_raw == nullptr, "cross evidence must not preserve raw circle records");
    Expect(left == nullptr, "cross evidence must not create suppressed circle records");
    Expect(result.candidates.empty(), "disabled cross and circle must not push candidates");
}

}  // namespace

int main() {
    try {
        TestCrossPresentFromWideRows();
        TestCrossAbsentReasons();
        TestCrossWhiteRatioCanBeParameterized();
    TestCandidateTakeoverEnabledByDefault();
        TestCandidateCanBeExplicitlyIncluded();
        TestCandidateRejectsGappedLineFacts();
        TestCircleLeftPresentFromRaster();
        TestCircleLeftPresentFromSparseRows();
        TestCircleRightPresentFromRaster();
        TestCircleAbsentCases();
        TestCircleRejectsSampleableBoundaryClippedOpening();
        TestCircleRejectsUnknownScreenEdgeClippedRasterOpening();
        TestCircleOpeningUsesNetExpansionNotStrictMonotonic();
        TestCircleRejectsTransientOpeningSpike();
        TestCircleIgnoresDetachedWhiteIslandOutsideMainBand();
        TestCircleAllowsSmallOppositeFittedDrift();
        TestCircleRejectsSaturatedWideWhiteRows();
        TestCircleReportsBendForFragmentedDoubleOpening();
        TestCircleRejectsOppositeShrinkAsBend();
        TestPipelineDoesNotAppendCircleRecordsFromRasterPath();
        TestSparsePipelineDoesNotAppendCircleRecords();
        TestPipelineNeverPushesLegacyCircleCandidate();
        TestPipelineCrossDoesNotCreateSuppressedCircleRecords();
    } catch (const TestFailure& failure) {
        std::cerr << "visual_element_evidence_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "visual_element_evidence_test passed\n";
    return EXIT_SUCCESS;
}
