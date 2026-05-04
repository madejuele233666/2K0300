#include "legacy/steering_visual_reference_orchestration.hpp"

#include <cmath>

namespace ls2k::legacy {
namespace {

constexpr float kSpecialCandidateConfidenceMin = 0.65F;

struct CandidateValidation {
    bool accepted = false;
    std::string rejected_reason = "none";
};

bool IsSpecialKind(port::VisualReferenceCandidateKind kind) {
    return kind != port::VisualReferenceCandidateKind::kLine;
}

bool IsConfidentSpecialCandidate(const port::VisualReferenceCandidate& candidate) {
    return IsSpecialKind(candidate.kind) && candidate.confidence >= kSpecialCandidateConfidenceMin;
}

bool SameConfidence(float lhs, float rhs) {
    return std::fabs(lhs - rhs) <= 1e-6F;
}

int Priority(port::VisualReferenceCandidateKind kind) {
    switch (kind) {
        case port::VisualReferenceCandidateKind::kRoadblockBypass:
            return 50;
        case port::VisualReferenceCandidateKind::kCircleLeft:
        case port::VisualReferenceCandidateKind::kCircleRight:
            return 40;
        case port::VisualReferenceCandidateKind::kCrossExit:
            return 30;
        case port::VisualReferenceCandidateKind::kMlGrounded:
            return 20;
        case port::VisualReferenceCandidateKind::kLine:
            return 10;
    }
    return 0;
}

CandidateValidation ValidateCandidate(const port::VisualReferenceCandidate& candidate) {
    if (!candidate.present) {
        return {};
    }
    if (!std::isfinite(candidate.confidence) || candidate.confidence < 0.0F) {
        return {false, "candidate_confidence_invalid"};
    }
    switch (candidate.reference_path.mode) {
        case port::ReferenceMode::kIntervalCenter:
            break;
        case port::ReferenceMode::kHoldLast:
            return {false, "hold_candidate_not_visual"};
        case port::ReferenceMode::kNone:
            return {false, "none_candidate_not_visual"};
    }

    const auto& samples = candidate.reference_path.sampled_path;
    if (!samples[0].present) {
        return {false, "missing_leading_reference_sample"};
    }

    bool gap_seen = false;
    for (const port::BEVPathSample& sample : samples) {
        if (!sample.present) {
            gap_seen = true;
            continue;
        }
        if (gap_seen) {
            return {false, "non_contiguous_reference_candidate"};
        }
        if (!std::isfinite(sample.point.forward_m) || !std::isfinite(sample.point.lateral_m)) {
            return {false, "non_finite_reference_candidate"};
        }
    }
    return {true, "none"};
}

void SelectCandidate(port::VisualReferenceSelection& selection,
                     const port::VisualReferenceCandidate& candidate,
                     const char* reason) {
    selection.present = true;
    selection.reference_path = candidate.reference_path;
    selection.source = candidate.source.empty() ? ToString(candidate.kind) : candidate.source;
    selection.reason = reason;
}

}  // namespace

const char* ToString(port::VisualReferenceCandidateKind kind) {
    switch (kind) {
        case port::VisualReferenceCandidateKind::kLine:
            return "line";
        case port::VisualReferenceCandidateKind::kCrossExit:
            return "cross_exit";
        case port::VisualReferenceCandidateKind::kCircleLeft:
            return "circle_left";
        case port::VisualReferenceCandidateKind::kCircleRight:
            return "circle_right";
        case port::VisualReferenceCandidateKind::kRoadblockBypass:
            return "roadblock_bypass";
        case port::VisualReferenceCandidateKind::kMlGrounded:
            return "ml_grounded";
    }
    return "line";
}

port::VisualReferenceCandidate MakeLineVisualReferenceCandidate(
    const port::BEVReferencePath& reference_path,
    const std::string& source) {
    port::VisualReferenceCandidate candidate{};
    candidate.present = reference_path.sampled_path[0].present;
    candidate.kind = port::VisualReferenceCandidateKind::kLine;
    candidate.reference_path = reference_path;
    candidate.confidence = candidate.present ? reference_path.sampled_path[0].confidence : 0.0F;
    candidate.source = source.empty() ? "line" : source;
    candidate.reason = candidate.present ? "line_reference_candidate" : "line_reference_absent";
    return candidate;
}

port::VisualReferenceSelection SelectVisualReference(
    const std::vector<port::VisualReferenceCandidate>& candidates) {
    port::VisualReferenceSelection selection{};
    const port::VisualReferenceCandidate* best_line = nullptr;
    const port::VisualReferenceCandidate* best_special = nullptr;
    int best_special_priority = -1;
    bool best_special_tied = false;

    for (const port::VisualReferenceCandidate& candidate : candidates) {
        if (!candidate.present) {
            continue;
        }
        const CandidateValidation validation = ValidateCandidate(candidate);
        if (!validation.accepted) {
            if (selection.rejected_candidate_reason == "none") {
                selection.rejected_candidate_reason = validation.rejected_reason;
            }
            continue;
        }

        ++selection.candidate_count;
        if (candidate.kind == port::VisualReferenceCandidateKind::kLine) {
            if (best_line == nullptr || candidate.confidence > best_line->confidence) {
                best_line = &candidate;
            }
            continue;
        }

        if (!IsConfidentSpecialCandidate(candidate)) {
            continue;
        }
        const int priority = Priority(candidate.kind);
        if (best_special == nullptr || priority > best_special_priority) {
            best_special = &candidate;
            best_special_priority = priority;
            best_special_tied = false;
            continue;
        }
        if (priority < best_special_priority) {
            continue;
        }
        if (candidate.confidence > best_special->confidence &&
            !SameConfidence(candidate.confidence, best_special->confidence)) {
            best_special = &candidate;
            best_special_tied = false;
            continue;
        }
        if (SameConfidence(candidate.confidence, best_special->confidence)) {
            best_special_tied = true;
        }
    }

    if (best_special_tied) {
        selection.present = false;
        selection.reason = "ambiguous_visual_reference_candidates";
        selection.source = "none";
        return selection;
    }
    if (best_special != nullptr) {
        SelectCandidate(selection, *best_special, "special_visual_candidate_selected");
        return selection;
    }
    if (best_line != nullptr) {
        SelectCandidate(selection, *best_line, "line_candidate_selected");
        return selection;
    }
    if (selection.rejected_candidate_reason != "none") {
        selection.reason = "no_valid_visual_reference_candidate";
    }
    return selection;
}

}  // namespace ls2k::legacy
