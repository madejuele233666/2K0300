#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "legacy/steering_cross_exit_element_evidence.hpp"

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
    return {MakeRow(0.24F, 40U, 34U, 1U, 1.00F, -0.36F, 0.36F),
            MakeRow(0.30F, 40U, 35U, 1U, 1.00F, -0.37F, 0.37F),
            MakeRow(0.36F, 40U, 34U, 1U, 1.00F, -0.35F, 0.35F)};
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
}

void TestLowConfidenceReason() {
    ls2k::port::RuntimeParameters params{};
    params.bev_geometry.search_lateral_limit_m = 2.0F;
    const std::vector<ls2k::legacy::BEVSimpleRowScan> weak{
        MakeRow(0.2F, 40U, 30U, 9U, 0.52F, -0.26F, 0.26F),
        MakeRow(0.3F, 40U, 30U, 9U, 0.52F, -0.26F, 0.26F),
        MakeRow(0.4F, 40U, 30U, 9U, 0.52F, -0.26F, 0.26F)};
    const ls2k::port::CrossExitElementEvidence evidence =
        ls2k::legacy::DetectCrossExitEvidence(weak, params);
    Expect(!evidence.present, "weak wide rows must not pass present confidence");
    Expect(evidence.reason == "low_confidence", "weak wide rows must explain low confidence");
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

}  // namespace

int main() {
    try {
        TestCrossPresentFromWideRows();
        TestCrossAbsentReasons();
        TestLowConfidenceReason();
        TestCandidateTakeoverDisabledByDefault();
        TestCandidateCanBeExplicitlyIncluded();
        TestCandidateRejectsGappedLineFacts();
    } catch (const TestFailure& failure) {
        std::cerr << "visual_element_evidence_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "visual_element_evidence_test passed\n";
    return EXIT_SUCCESS;
}
