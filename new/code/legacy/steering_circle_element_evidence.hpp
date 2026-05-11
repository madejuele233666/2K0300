#ifndef LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP

#include <string>
#include <vector>

#include "legacy/steering_bev_element_raster.hpp"
#include "port/bev_geometry_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

struct CircleEntryPathFacts {
    bool present = false;
    std::string reason = "not_evaluated";
    float road_half_width_m = 0.0F;
    float direction_delta_lateral_m = 0.0F;
    float direction_delta_forward_m = 0.0F;
    std::vector<port::BEVPoint> near_centerline_points{};
    std::vector<port::BEVPoint> frontier_points{};
    std::vector<port::BEVPoint> centerline_points{};
};

struct CircleEntryPipelineDiagnostics {
    CircleEntryPathFacts left{};
    CircleEntryPathFacts right{};
};

struct CircleElementEvidenceResult {
    port::VisualElementEvidenceRecord left_raw{};
    port::VisualElementEvidenceRecord right_raw{};
    CircleEntryPathFacts left_entry{};
    CircleEntryPathFacts right_entry{};
};

CircleElementEvidenceResult DetectCircleElementEvidence(
    const BEVElementRasterFrame* raster,
    const port::RuntimeParameters& params);

port::VisualReferenceCandidate BuildCircleEntryVisualReferenceCandidate(
    const port::VisualElementEvidenceRecord& evidence,
    const CircleEntryPathFacts& entry,
    port::VisualReferenceCandidateKind kind,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
