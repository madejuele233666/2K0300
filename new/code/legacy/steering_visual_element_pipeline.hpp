#ifndef LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
#define LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP

#include <vector>

#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

struct VisualElementPipelineInput {
    const std::vector<BEVSimpleRowScan>* sparse_rows = nullptr;
    const BEVElementRasterFrame* element_raster = nullptr;
    port::VisualReferenceCandidate line_candidate{};
};

struct VisualElementPipelineResult {
    port::VisualElementEvidenceFrame evidence{};
    std::vector<port::VisualReferenceCandidate> candidates{};
};

VisualElementPipelineResult RunVisualElementPipeline(const VisualElementPipelineInput& input,
                                                     const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
