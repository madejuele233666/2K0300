#ifndef LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP
#define LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP

#include <cstddef>
#include <string>

#include "port/bev_reference_types.hpp"

namespace ls2k::port {

enum class VisualReferenceCandidateKind {
    kLine,
    kCrossExit,
    kCircleLeft,
    kCircleRight,
    kRoadblockBypass,
    kMlGrounded,
};

struct VisualReferenceCandidate {
    bool present = false;
    VisualReferenceCandidateKind kind = VisualReferenceCandidateKind::kLine;
    BEVReferencePath reference_path{};
    float confidence = 0.0F;
    std::string source = "none";
    std::string reason = "none";
};

struct VisualReferenceSelection {
    bool present = false;
    BEVReferencePath reference_path{};
    std::string source = "none";
    std::string reason = "no_visual_reference_candidate";
    std::size_t candidate_count = 0;
    std::string rejected_candidate_reason = "none";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP
