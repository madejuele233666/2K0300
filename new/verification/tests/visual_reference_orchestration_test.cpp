#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "legacy/steering_visual_reference_orchestration.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::port::BEVReferencePath MakePath(int present_count) {
    ls2k::port::BEVReferencePath path{};
    path.mode = present_count > 0 ? ls2k::port::ReferenceMode::kIntervalCenter
                                  : ls2k::port::ReferenceMode::kNone;
    for (std::size_t index = 0; index < path.sampled_path.size(); ++index) {
        ls2k::port::BEVPathSample& sample = path.sampled_path[index];
        sample.point.forward_m = 0.05F + 0.05F * static_cast<float>(index);
        sample.point.lateral_m = 0.01F * static_cast<float>(index);
        if (static_cast<int>(index) < present_count) {
            sample.present = true;
            sample.confidence = 1.0F;
            sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
        }
    }
    return path;
}

ls2k::port::VisualReferenceCandidate Candidate(ls2k::port::VisualReferenceCandidateKind kind,
                                               int present_count,
                                               float confidence,
                                               const std::string& source) {
    ls2k::port::VisualReferenceCandidate candidate{};
    candidate.present = present_count > 0;
    candidate.kind = kind;
    candidate.reference_path = MakePath(present_count);
    candidate.confidence = confidence;
    candidate.source = source;
    candidate.reason = "unit_test_candidate";
    return candidate;
}

void TestNoCandidatesSelectsNone() {
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({});
    Expect(!selection.present, "empty candidate set must not select a visual reference");
    Expect(selection.reason == "no_visual_reference_candidate",
           "empty candidate set must explain missing visual reference");
    Expect(selection.candidate_count == 0, "empty candidate set must report zero accepted candidates");
}

void TestValidLineCandidateIsSelected() {
    const ls2k::port::BEVReferencePath line_path = MakePath(3);
    const ls2k::port::VisualReferenceCandidate line =
        ls2k::legacy::MakeLineVisualReferenceCandidate(line_path, "simple_interval_center");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line});
    Expect(selection.present, "valid line candidate must be selected");
    Expect(selection.source == "simple_interval_center",
           "line selection must preserve factual reference source");
    Expect(selection.reason == "line_candidate_selected",
           "line selection must expose deterministic reason");
    Expect(selection.reference_path.sampled_path[0].present,
           "selected line path must preserve leading sample");
    Expect(selection.candidate_count == 1, "one valid candidate must be counted");
}

void TestMissingIndexZeroRejectsCandidate() {
    ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    line.reference_path.sampled_path[0].present = false;
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line});
    Expect(!selection.present, "candidate without index zero must be rejected");
    Expect(selection.reason == "no_valid_visual_reference_candidate",
           "rejected-only set must expose no-valid reason");
    Expect(selection.rejected_candidate_reason == "missing_leading_reference_sample",
           "missing index zero must be the rejection reason");
}

void TestNoneModeRejectsCandidate() {
    ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    line.reference_path.mode = ls2k::port::ReferenceMode::kNone;
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line});
    Expect(!selection.present, "kNone mode candidate must be rejected");
    Expect(selection.reason == "no_valid_visual_reference_candidate",
           "rejected-only kNone candidate must expose no-valid reason");
    Expect(selection.rejected_candidate_reason == "none_candidate_not_visual",
           "kNone mode rejection must be explicit");
}

void TestHoldModeRejectsCandidate() {
    ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    line.reference_path.mode = ls2k::port::ReferenceMode::kHoldLast;
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line});
    Expect(!selection.present, "kHoldLast mode candidate must be rejected");
    Expect(selection.reason == "no_valid_visual_reference_candidate",
           "rejected-only kHoldLast candidate must expose no-valid reason");
    Expect(selection.rejected_candidate_reason == "hold_candidate_not_visual",
           "kHoldLast mode rejection must be explicit");
}

void TestLineWinsWhenSpecialIsAbsentOrLowConfidence() {
    const ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    ls2k::port::VisualReferenceCandidate cross =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCrossExit, 3, 0.30F, "cross_exit");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line, cross});
    Expect(selection.present, "line must remain available when special candidate is low-confidence");
    Expect(selection.source == "line", "low-confidence special candidate must not displace line");
    Expect(selection.reason == "line_candidate_selected",
           "line fallback must keep deterministic selection reason");
}

void TestPriorityExplainsMultipleSpecialCandidates() {
    const ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    const ls2k::port::VisualReferenceCandidate cross =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCrossExit, 3, 0.90F, "cross_exit");
    const ls2k::port::VisualReferenceCandidate circle =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleLeft, 3, 0.80F, "circle_left");
    const ls2k::port::VisualReferenceCandidate roadblock =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kRoadblockBypass,
                  3,
                  0.70F,
                  "roadblock_bypass");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line, cross, circle, roadblock});
    Expect(selection.present, "different-priority special candidates must be arbitrated");
    Expect(selection.source == "roadblock_bypass",
           "highest-priority special candidate must be selected");
    Expect(selection.reason == "special_visual_candidate_selected",
           "special priority selection must expose deterministic reason");
    Expect(selection.candidate_count == 4, "all structurally valid candidates must be counted");
}

void TestEqualSpecialTieSelectsNone() {
    const ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    const ls2k::port::VisualReferenceCandidate circle_left =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleLeft, 3, 0.80F, "circle_left");
    const ls2k::port::VisualReferenceCandidate circle_right =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleRight, 3, 0.80F, "circle_right");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::legacy::SelectVisualReference({line, circle_left, circle_right});
    Expect(!selection.present, "equal-priority equal-confidence special tie must fail closed");
    Expect(selection.reason == "ambiguous_visual_reference_candidates",
           "special tie must be explainable");
    Expect(selection.candidate_count == 3, "all structurally valid candidates must be counted");
}

}  // namespace

int main() {
    try {
        TestNoCandidatesSelectsNone();
        TestValidLineCandidateIsSelected();
        TestMissingIndexZeroRejectsCandidate();
        TestNoneModeRejectsCandidate();
        TestHoldModeRejectsCandidate();
        TestLineWinsWhenSpecialIsAbsentOrLowConfidence();
        TestPriorityExplainsMultipleSpecialCandidates();
        TestEqualSpecialTieSelectsNone();
    } catch (const TestFailure& failure) {
        std::cerr << "visual_reference_orchestration_test failed: "
                  << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "visual_reference_orchestration_test passed\n";
    return EXIT_SUCCESS;
}
