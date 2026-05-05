#include "legacy/steering_visual_element_pipeline.hpp"

#include "legacy/steering_cross_exit_element_evidence.hpp"

namespace ls2k::legacy {

VisualElementPipelineResult RunVisualElementPipeline(const VisualElementPipelineInput& input,
                                                     const port::RuntimeParameters& params) {
    VisualElementPipelineResult result{};
    const std::vector<BEVSimpleRowScan> empty_rows{};
    const std::vector<BEVSimpleRowScan>& rows =
        input.sparse_rows == nullptr ? empty_rows : *input.sparse_rows;

    result.evidence.cross_exit = DetectCrossExitEvidence(rows, params);
    port::VisualElementCandidateSummary cross_candidate_summary{};
    const port::VisualReferenceCandidate cross_candidate =
        BuildCrossExitVisualReferenceCandidate(result.evidence.cross_exit,
                                              input.line_candidate,
                                              params,
                                              cross_candidate_summary);
    result.evidence.cross_exit.candidate = cross_candidate_summary;
    if (cross_candidate_summary.included_in_arbitration) {
        result.candidates.push_back(cross_candidate);
    }

    (void)input.element_raster;
    return result;
}

}  // namespace ls2k::legacy
