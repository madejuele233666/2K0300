#include "runtime/steering_circle_v2_reference_adapter.hpp"

namespace ls2k::runtime {

std::optional<port::VisualReferenceCandidate> AdaptCircleV2ReferencePlan(
    const std::optional<CircleV2ReferencePlan>& plan) {
    if (!plan.has_value() || plan->dir == CircleDir::kNone ||
        plan->role != CircleV2ReferenceRole::kExitTrace) {
        return std::nullopt;
    }
    port::VisualReferenceCandidate candidate{};
    candidate.present = true;
    candidate.kind = plan->dir == CircleDir::kLeft
                         ? port::VisualReferenceCandidateKind::kCircleLeft
                         : port::VisualReferenceCandidateKind::kCircleRight;
    candidate.reference_path = plan->reference_path;
    candidate.confidence = 1.0F;
    candidate.source = "circle_v2_exit";
    candidate.reason = "circle_v2_scene";
    return candidate;
}

std::optional<BoundaryOverrideRequest> BuildCircleV2BoundaryOverrideRequest(
    const std::optional<CircleV2BoundaryOverridePlan>& plan) {
    if (!plan.has_value() || plan->override_side == CircleDir::kNone) {
        return std::nullopt;
    }
    BoundaryOverrideRequest request{};
    request.side = plan->override_side == CircleDir::kLeft
                       ? BoundaryOverrideSide::kLeft
                       : BoundaryOverrideSide::kRight;
    request.boundary_path = plan->boundary_path;
    return request;
}

std::optional<port::VisualReferenceCandidate> AdaptCircleV2BoundaryOverrideReference(
    const std::optional<CircleV2BoundaryOverridePlan>& plan,
    const std::optional<port::BEVReferencePath>& reference_path) {
    if (!plan.has_value() || !reference_path.has_value() ||
        plan->dir == CircleDir::kNone ||
        plan->role != CircleV2ReferenceRole::kInnerTrace ||
        plan->override_side == CircleDir::kNone ||
        !reference_path->sampled_path[0].present) {
        return std::nullopt;
    }
    port::VisualReferenceCandidate candidate{};
    candidate.present = true;
    candidate.kind = plan->dir == CircleDir::kLeft
                         ? port::VisualReferenceCandidateKind::kCircleLeft
                         : port::VisualReferenceCandidateKind::kCircleRight;
    candidate.reference_path = *reference_path;
    candidate.confidence = 1.0F;
    candidate.source = "circle_v2_inner";
    candidate.reason = "circle_v2_boundary_override";
    return candidate;
}

}  // namespace ls2k::runtime
