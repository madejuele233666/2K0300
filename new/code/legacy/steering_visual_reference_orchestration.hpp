#ifndef LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP
#define LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP

#include <string>
#include <vector>

#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

const char* ToString(port::VisualReferenceCandidateKind kind);

port::VisualReferenceCandidate MakeLineVisualReferenceCandidate(
    const port::BEVReferencePath& reference_path,
    const std::string& source);

port::VisualReferenceSelection SelectVisualReference(
    const std::vector<port::VisualReferenceCandidate>& candidates);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP
