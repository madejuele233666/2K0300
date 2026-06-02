#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "vision/bev/bev_projector.hpp"
#include "vision/bev/reference_connectivity.hpp"
#include "reference/visual_reference_orchestration.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::vision::BEVPixelClassificationModel TestClassificationModel(int threshold = 100) {
    ls2k::vision::BEVPixelClassificationModel model{};
    model.valid = ls2k::vision::ValidGrayThreshold(threshold);
    model.threshold = threshold;
    return model;
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

ls2k::port::LegacyCameraFrame MakeConnectivityFrame(std::uint8_t fill) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = 10;
    frame.height = 10;
    frame.gray.fill(fill);
    return frame;
}

void SetPixel(ls2k::port::LegacyCameraFrame& frame, int row, int col, std::uint8_t value) {
    frame.gray[static_cast<std::size_t>(row) *
                   static_cast<std::size_t>(frame.width) +
               static_cast<std::size_t>(col)] = value;
}

ls2k::vision::BEVProjector MakeIdentityConnectivityProjector() {
    ls2k::port::BEVProjectorCalibration calibration{};
    calibration.source_points = {
        {ls2k::port::ImagePoint{0.0F, 0.0F},
         ls2k::port::ImagePoint{0.0F, 9.0F},
         ls2k::port::ImagePoint{9.0F, 0.0F},
         ls2k::port::ImagePoint{9.0F, 9.0F}}};
    calibration.target_points = {
        {ls2k::port::BEVPoint{0.0F, 0.0F},
         ls2k::port::BEVPoint{0.0F, 9.0F},
         ls2k::port::BEVPoint{9.0F, 0.0F},
         ls2k::port::BEVPoint{9.0F, 9.0F}}};
    ls2k::vision::BEVProjector projector{};
    Expect(projector.Configure(calibration),
           "identity connectivity projector must configure");
    return projector;
}

ls2k::port::BEVReferencePath MakeConnectivityPath() {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    path.sampled_path[0].present = true;
    path.sampled_path[0].point = {1.0F, 1.0F};
    path.sampled_path[0].confidence = 1.0F;
    path.sampled_path[0].source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    path.sampled_path[1].present = true;
    path.sampled_path[1].point = {8.0F, 8.0F};
    path.sampled_path[1].confidence = 1.0F;
    path.sampled_path[1].source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    return path;
}

ls2k::port::VisualReferenceCandidate CandidateWithPath(
    ls2k::port::VisualReferenceCandidateKind kind,
    const ls2k::port::BEVReferencePath& path,
    const std::string& source) {
    ls2k::port::VisualReferenceCandidate candidate{};
    candidate.present = true;
    candidate.kind = kind;
    candidate.reference_path = path;
    candidate.confidence = 1.0F;
    candidate.source = source;
    candidate.reason = "connectivity_gate_test";
    return candidate;
}

void AppendAllVisualKindsThroughConnectivityGate(
    const ls2k::vision::ReferenceConnectivityFrameView& frame,
    const ls2k::port::BEVReferencePath& path,
    std::vector<ls2k::port::VisualReferenceCandidate>& accepted) {
    ls2k::vision::AppendConnectedVisualReferenceCandidate(
        frame,
        CandidateWithPath(ls2k::port::VisualReferenceCandidateKind::kLine,
                          path,
                          "line"),
        accepted);
    ls2k::vision::AppendConnectedVisualReferenceCandidate(
        frame,
        CandidateWithPath(ls2k::port::VisualReferenceCandidateKind::kCrossExit,
                          path,
                          "cross_exit"),
        accepted);
    ls2k::vision::AppendConnectedVisualReferenceCandidate(
        frame,
        CandidateWithPath(ls2k::port::VisualReferenceCandidateKind::kCircleLeft,
                          path,
                          "circle_v2_inner"),
        accepted);
}

void TestNoCandidatesSelectsNone() {
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference({});
    Expect(!selection.present, "empty candidate set must not select a visual reference");
    Expect(selection.reason == "no_visual_reference_candidate",
           "empty candidate set must explain missing visual reference");
    Expect(selection.candidate_count == 0, "empty candidate set must report zero accepted candidates");
}

void TestValidLineCandidateIsSelected() {
    const ls2k::port::BEVReferencePath line_path = MakePath(3);
    const ls2k::port::VisualReferenceCandidate line =
        ls2k::reference::MakeLineVisualReferenceCandidate(line_path, "simple_interval_center");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference({line});
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
        ls2k::reference::SelectVisualReference({line});
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
        ls2k::reference::SelectVisualReference({line});
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
        ls2k::reference::SelectVisualReference({line});
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
        ls2k::reference::SelectVisualReference({line, cross});
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
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleLeft, 3, 0.80F, "circle_v2_inner");
    const ls2k::port::VisualReferenceCandidate roadblock =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kRoadblockBypass,
                  3,
                  0.70F,
                  "roadblock_bypass");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference({line, cross, circle, roadblock});
    Expect(selection.present, "different-priority special candidates must be arbitrated");
    Expect(selection.source == "roadblock_bypass",
           "highest-priority special candidate must be selected");
    Expect(selection.reason == "special_visual_candidate_selected",
           "special priority selection must expose deterministic reason");
    Expect(selection.candidate_count == 4, "all structurally valid candidates must be counted");
}

void TestCrossExitPriorityExceedsCircle() {
    const ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    const ls2k::port::VisualReferenceCandidate cross =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCrossExit, 3, 0.70F, "cross_exit");
    const ls2k::port::VisualReferenceCandidate circle =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleLeft, 3, 0.95F, "circle_v2_inner");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference({line, circle, cross});
    Expect(selection.present, "cross and circle candidates must be arbitrated");
    Expect(selection.source == "cross_exit",
           "cross exit must outrank circle even when circle confidence is higher");
    Expect(selection.reason == "special_visual_candidate_selected",
           "cross-over-circle selection must expose deterministic reason");
}

void TestEqualSpecialTieSelectsNone() {
    const ls2k::port::VisualReferenceCandidate line =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kLine, 3, 1.0F, "line");
    const ls2k::port::VisualReferenceCandidate circle_left =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleLeft, 3, 0.80F, "circle_v2_inner");
    const ls2k::port::VisualReferenceCandidate circle_right =
        Candidate(ls2k::port::VisualReferenceCandidateKind::kCircleRight, 3, 0.80F, "circle_v2_exit");
    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference({line, circle_left, circle_right});
    Expect(!selection.present, "equal-priority equal-confidence special tie must fail closed");
    Expect(selection.reason == "ambiguous_visual_reference_candidates",
           "special tie must be explainable");
    Expect(selection.candidate_count == 3, "all structurally valid candidates must be counted");
}

void TestCentralConnectivityGateClipsDisconnectedVisualKinds() {
    ls2k::port::RuntimeParameters params{};
    const ls2k::vision::BEVProjector projector =
        MakeIdentityConnectivityProjector();
    const ls2k::port::BEVReferencePath path = MakeConnectivityPath();

    {
        ls2k::port::LegacyCameraFrame frame = MakeConnectivityFrame(255U);
        const ls2k::vision::ReferenceConnectivityFrameView connectivity_frame{
            frame.View(10, 10),
            projector,
            TestClassificationModel(),
            params.bev_classification,
        };
        std::vector<ls2k::port::VisualReferenceCandidate> accepted;
        AppendAllVisualKindsThroughConnectivityGate(connectivity_frame, path, accepted);
        Expect(accepted.size() == 3U,
               "connected line, cross, and circle candidates must pass the central gate");
    }

    {
        ls2k::port::LegacyCameraFrame blocked = MakeConnectivityFrame(255U);
        SetPixel(blocked, 0, 0, 0U);
        const ls2k::vision::ReferenceConnectivityFrameView blocked_frame{
            blocked.View(12, 12),
            projector,
            TestClassificationModel(),
            params.bev_classification,
        };
        std::vector<ls2k::port::VisualReferenceCandidate> accepted;
        AppendAllVisualKindsThroughConnectivityGate(blocked_frame, path, accepted);
        Expect(accepted.size() == 3U,
               "origin-disconnected candidates must be clipped before selection");
        for (const ls2k::port::VisualReferenceCandidate& candidate : accepted) {
            Expect(candidate.present,
                   "clipped candidate must remain a visual candidate for usability to judge");
            Expect(!candidate.reference_path.sampled_path[0].present,
                   "origin block must remove the first visual reference sample");
            Expect(candidate.reason.find("connectivity_prefix_clipped") != std::string::npos,
                   "clipped candidate must expose connectivity clipping in debug reason");
        }
    }

    ls2k::port::LegacyCameraFrame blocked = MakeConnectivityFrame(255U);
    SetPixel(blocked, 4, 4, 0U);
    const ls2k::vision::ReferenceConnectivityFrameView blocked_frame{
        blocked.View(11, 11),
        projector,
        TestClassificationModel(),
        params.bev_classification,
    };
    std::vector<ls2k::port::VisualReferenceCandidate> accepted;
    AppendAllVisualKindsThroughConnectivityGate(blocked_frame, path, accepted);
    Expect(accepted.size() == 3U,
           "disconnected line, cross, and circle candidates must be clipped before selection");
    for (const ls2k::port::VisualReferenceCandidate& candidate : accepted) {
        Expect(candidate.present,
               "clipped candidate must remain a visual candidate for usability to judge");
        Expect(candidate.reference_path.sampled_path[0].present,
               "connected prefix must keep the leading sample");
        Expect(!candidate.reference_path.sampled_path[1].present,
               "blocked segment must stop the connected prefix before the blocked sample");
        Expect(candidate.reason.find("connectivity_prefix_clipped") != std::string::npos,
               "clipped candidate must expose connectivity clipping in debug reason");
    }

    ls2k::port::VisualReferenceCandidatePathSet debug_paths{};
    for (const ls2k::port::VisualReferenceCandidate& candidate : accepted) {
        ls2k::port::AppendVisualReferenceCandidatePath(debug_paths, candidate);
    }
    Expect(debug_paths.count == 3U,
           "clipped candidates must remain visible in public candidate-path debug output");

    const ls2k::port::VisualReferenceSelection selection =
        ls2k::reference::SelectVisualReference(accepted);
    Expect(selection.present,
           "selector may select a clipped visual candidate; usability owns sample-count rejection");
    Expect(selection.candidate_count == 3U,
           "selector candidate count must include structurally valid clipped candidates");
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
        TestCrossExitPriorityExceedsCircle();
        TestEqualSpecialTieSelectsNone();
        TestCentralConnectivityGateClipsDisconnectedVisualKinds();
    } catch (const TestFailure& failure) {
        std::cerr << "visual_reference_orchestration_test failed: "
                  << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "visual_reference_orchestration_test passed\n";
    return EXIT_SUCCESS;
}
