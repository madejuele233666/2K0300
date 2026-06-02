#include "vision/elements/circle_v2/circle_v2_reference_adapter.hpp"

namespace ls2k::vision {
namespace {

const char* SourceForRole(CircleV2ReferenceRole role) {
    if (role == CircleV2ReferenceRole::kInnerTrace) {
        return "circle_v2_inner";
    }
    if (role == CircleV2ReferenceRole::kExitTrace) {
        return "circle_v2_exit";
    }
    return "none";
}

}  // namespace

std::optional<port::VisualReferenceCandidate> AdaptCircleV2ReferencePlan(
    const std::optional<CircleV2ReferencePlan>& plan) {
    if (!plan.has_value() || plan->dir == CircleDir::kNone ||
        (plan->role != CircleV2ReferenceRole::kInnerTrace &&
         plan->role != CircleV2ReferenceRole::kExitTrace) ||
        !plan->reference_path.sampled_path[0].present) {
        return std::nullopt;
    }
    port::VisualReferenceCandidate candidate{};
    candidate.present = true;
    candidate.kind = plan->dir == CircleDir::kLeft
                         ? port::VisualReferenceCandidateKind::kCircleLeft
                         : port::VisualReferenceCandidateKind::kCircleRight;
    candidate.reference_path = plan->reference_path;
    candidate.confidence = 1.0F;
    candidate.source = SourceForRole(plan->role);
    candidate.reason = "circle_v2_scene";
    return candidate;
}

}  // namespace ls2k::vision
