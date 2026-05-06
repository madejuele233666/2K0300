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
    return {MakeRow(0.24F, 40U, 39U, 0U, 1.00F, -0.36F, 0.36F),
            MakeRow(0.30F, 40U, 39U, 0U, 1.00F, -0.43F, 0.43F),
            MakeRow(0.36F, 40U, 39U, 0U, 1.00F, -0.50F, 0.50F)};
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
    return MakeRasterFromBounds(-0.12F, 0.20F, -0.42F, 0.20F);
}

ls2k::legacy::BEVElementRasterFrame RightCircleRaster() {
    return MakeRasterFromBounds(-0.20F, 0.12F, -0.20F, 0.42F);
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

void TestCandidateTakeoverDisabledByDefault() {
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
    Expect(!summary.takeover_enabled, "takeover must default to disabled");
    Expect(!summary.included_in_arbitration, "disabled takeover must not enter arbitration");
    Expect(summary.reason == "takeover_disabled", "disabled takeover must be explicit");
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
    Expect(evidence.left_raw.confidence >= params.bev_element.circle_present_confidence_min,
           "left circle confidence must pass threshold");
    Expect(evidence.left_raw.support.sampleable_count > 0U,
           "left circle must expose sampleable support");
    Expect(evidence.left_raw.support.supporting_white_count > 0U,
           "left circle must expose white support");
    Expect(!evidence.right_raw.present, "left circle must not produce right circle");
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
    const ls2k::legacy::BEVElementRasterFrame both_open =
        MakeRasterFromBounds(-0.12F, 0.12F, -0.42F, 0.42F);
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

    ls2k::port::RuntimeParameters disabled_params{};
    disabled_params.bev_element.circle_evidence_enabled = false;
    const ls2k::legacy::CircleElementEvidenceResult disabled =
        ls2k::legacy::DetectCircleElementEvidence(&straight, disabled_params);
    Expect(disabled.left_raw.reason == "circle_evidence_disabled",
           "disabled circle evidence must explain absence");

    const ls2k::legacy::CircleElementEvidenceResult missing =
        ls2k::legacy::DetectCircleElementEvidence(nullptr, params);
    Expect(missing.left_raw.reason == "raster_unavailable",
           "missing raster must fail closed");
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
           "saturated wide white rows must stay out of raw circle evidence");
    Expect(evidence.left_raw.reason == "saturated_wide_white_rows",
           "saturated wide white rows need an explicit circle fail-closed reason");
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
    Expect(evidence.left_raw.reason == "bend",
           "non-straight double opening should be reported as bend, reason=" +
               evidence.left_raw.reason);
    Expect(evidence.right_raw.reason == "bend",
           "both raw circle sides should agree on bend reason");
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
           "opposite shrink must report the bend fail-closed reason");
    Expect(!evidence.right_raw.present,
           "opposite shrink must not produce right circle");
}

void TestCircleRejectsWeakSupport() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element.circle_min_sampleable_per_row = 1000;
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    const ls2k::legacy::CircleElementEvidenceResult unsupported =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(!unsupported.left_raw.present, "insufficient support must fail closed");
    Expect(unsupported.left_raw.reason == "insufficient_sampleable_support",
           "insufficient support reason must be stable");

    params = {};
    const ls2k::legacy::BEVElementRasterFrame drift =
        MakeRasterFromBounds(-0.12F, 0.24F, -0.42F, -0.08F);
    const ls2k::legacy::CircleElementEvidenceResult drift_result =
        ls2k::legacy::DetectCircleElementEvidence(&drift, params);
    Expect(!drift_result.left_raw.present, "opposite side shrink must fail closed");
    Expect(drift_result.left_raw.reason == "bend",
           "opposite side shrink reason must be bend");

    params = {};
    params.bev_element.circle_present_confidence_min = 1.01F;
    const ls2k::legacy::CircleElementEvidenceResult low_confidence =
        ls2k::legacy::DetectCircleElementEvidence(&raster, params);
    Expect(!low_confidence.left_raw.present, "low confidence must fail closed");
    Expect(low_confidence.left_raw.reason == "low_confidence",
           "low confidence reason must be stable");
}

void TestPipelineAppendsCircleRecordsWithoutCandidate() {
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    ls2k::legacy::VisualElementPipelineInput input{};
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows{};
    input.sparse_rows = &rows;
    input.element_raster = &raster;
    input.line_candidate = MakeLineCandidate(3);
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementPipeline(input, ls2k::port::RuntimeParameters{});
    Expect(result.evidence.records.size() == 4U, "pipeline must always append four circle records");
    Expect(result.evidence.records[0].id == "circle_left_raw", "record 0 must be left raw");
    Expect(result.evidence.records[1].id == "circle_right_raw", "record 1 must be right raw");
    Expect(result.evidence.records[2].id == "circle_left", "record 2 must be effective left");
    Expect(result.evidence.records[3].id == "circle_right", "record 3 must be effective right");
    Expect(result.evidence.records[0].present, "raw left circle must be preserved");
    Expect(result.evidence.records[2].present, "cross-absent effective left must mirror raw");
    Expect(result.evidence.records[2].candidate.reason == "evidence_only",
           "circle candidate summary must be evidence-only");
    Expect(result.candidates.empty(), "circle evidence must not push candidates in phase 1");
}

void TestPipelineCrossSuppressesEffectiveCircleOnly() {
    const ls2k::legacy::BEVElementRasterFrame raster = LeftCircleRaster();
    const std::vector<ls2k::legacy::BEVSimpleRowScan> rows = WideCrossRows();
    ls2k::legacy::VisualElementPipelineInput input{};
    input.sparse_rows = &rows;
    input.element_raster = &raster;
    input.line_candidate = MakeLineCandidate(3);
    const ls2k::legacy::VisualElementPipelineResult result =
        ls2k::legacy::RunVisualElementPipeline(input, ls2k::port::RuntimeParameters{});
    const ls2k::port::VisualElementEvidenceRecord* left_raw =
        FindRecord(result.evidence, "circle_left_raw");
    const ls2k::port::VisualElementEvidenceRecord* left =
        FindRecord(result.evidence, "circle_left");
    Expect(result.evidence.cross_exit.present, "wide rows must produce cross evidence");
    Expect(left_raw != nullptr && left_raw->present, "raw circle fact must survive cross evidence");
    Expect(left != nullptr && !left->present, "effective circle must be suppressed by cross");
    Expect(left->reason == "suppressed_by_cross_exit", "suppressed circle reason must be stable");
    Expect(result.candidates.empty(), "disabled cross and circle must not push candidates");
}

}  // namespace

int main() {
    try {
        TestCrossPresentFromWideRows();
        TestCrossAbsentReasons();
        TestCrossWhiteRatioCanBeParameterized();
        TestCandidateTakeoverDisabledByDefault();
        TestCandidateCanBeExplicitlyIncluded();
        TestCandidateRejectsGappedLineFacts();
        TestCircleLeftPresentFromRaster();
        TestCircleRightPresentFromRaster();
        TestCircleAbsentCases();
        TestCircleOpeningUsesNetExpansionNotStrictMonotonic();
        TestCircleRejectsTransientOpeningSpike();
        TestCircleIgnoresDetachedWhiteIslandOutsideMainBand();
        TestCircleAllowsSmallOppositeFittedDrift();
        TestCircleRejectsSaturatedWideWhiteRows();
        TestCircleReportsBendForFragmentedDoubleOpening();
        TestCircleRejectsOppositeShrinkAsBend();
        TestCircleRejectsWeakSupport();
        TestPipelineAppendsCircleRecordsWithoutCandidate();
        TestPipelineCrossSuppressesEffectiveCircleOnly();
    } catch (const TestFailure& failure) {
        std::cerr << "visual_element_evidence_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "visual_element_evidence_test passed\n";
    return EXIT_SUCCESS;
}
