#ifndef LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP

#include <vector>

#include "legacy/steering_bev_simple_perception.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

port::CrossExitElementEvidence DetectCrossExitEvidence(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params);

port::VisualReferenceCandidate BuildCrossExitVisualReferenceCandidate(
    const port::CrossExitElementEvidence& evidence,
    const port::VisualReferenceCandidate& line_candidate,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP
