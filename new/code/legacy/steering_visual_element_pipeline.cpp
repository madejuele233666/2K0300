#include "legacy/steering_visual_element_pipeline.hpp"

#include "legacy/steering_circle_element_evidence.hpp"
#include "legacy/steering_cross_exit_element_evidence.hpp"

namespace ls2k::legacy {
namespace {

port::VisualElementEvidenceRecord MakeEffectiveCircleRecord(
    port::VisualElementEvidenceRecord raw,
    const char* id,
    bool suppress_by_cross) {
    raw.id = id;
    raw.candidate.built = false;
    raw.candidate.takeover_enabled = false;
    raw.candidate.included_in_arbitration = false;
    raw.candidate.reason = "evidence_only";
    if (suppress_by_cross && raw.present) {
        raw.present = false;
        raw.reason = "suppressed_by_cross_exit";
    }
    return raw;
}

void AppendCircleEvidence(port::VisualElementEvidenceFrame& evidence,
                          const CircleElementEvidenceResult& circle) {
    evidence.records.push_back(circle.left_raw);
    evidence.records.push_back(circle.right_raw);
    evidence.records.push_back(MakeEffectiveCircleRecord(circle.left_raw,
                                                         "circle_left",
                                                         evidence.cross_exit.present));
    evidence.records.push_back(MakeEffectiveCircleRecord(circle.right_raw,
                                                         "circle_right",
                                                         evidence.cross_exit.present));
}

}  // namespace

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

    const CircleElementEvidenceResult circle =
        DetectCircleElementEvidence(input.element_raster, params);
    AppendCircleEvidence(result.evidence, circle);
    return result;
}

}  // namespace ls2k::legacy
