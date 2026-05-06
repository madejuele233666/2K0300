#ifndef LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP

#include "legacy/steering_bev_element_raster.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"

namespace ls2k::legacy {

struct CircleElementEvidenceResult {
    port::VisualElementEvidenceRecord left_raw{};
    port::VisualElementEvidenceRecord right_raw{};
};

CircleElementEvidenceResult DetectCircleElementEvidence(
    const BEVElementRasterFrame* raster,
    const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
