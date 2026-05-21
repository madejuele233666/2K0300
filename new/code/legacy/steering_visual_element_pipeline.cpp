#include "legacy/steering_visual_element_pipeline.hpp"

#include "legacy/steering_circle_element_evidence.hpp"
#include "legacy/steering_cross_exit_element_evidence.hpp"

namespace ls2k::legacy {
namespace {

/// 创建有效的环形证据记录
/// 设置ID并清除候选状态，如果十字出口已存在则抑制环形检测结果
/// @param raw 原始环形证据记录
/// @param id 环形证据ID（"circle_left"或"circle_right"）
/// @param suppress_by_cross 是否因十字出口存在而抑制
/// @return 处理后的环形证据记录
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

/// 尝试构建环形入口视觉参考候选
/// 如果候选被纳入仲裁，则将其加入候选列表
void MaybeBuildCircleCandidate(port::VisualElementEvidenceRecord& record,
                               const CircleEntryPathFacts& entry,
                               port::VisualReferenceCandidateKind kind,
    const port::RuntimeParameters& params,
    std::vector<port::VisualReferenceCandidate>& candidates) {
    if (!record.present) {
        record.candidate = {};
        record.candidate.reason = record.reason.empty() ? "evidence_absent" : record.reason;
        return;
    }
    if (!params.bev_element.circle_entry_takeover_enabled) {
        record.candidate = {};
        record.candidate.takeover_enabled = false;
        record.candidate.reason = "takeover_disabled";
        return;
    }
    port::VisualElementCandidateSummary summary{};
    const port::VisualReferenceCandidate candidate =
        BuildCircleEntryVisualReferenceCandidate(record, entry, kind, params, summary);
    record.candidate = summary;
    if (summary.included_in_arbitration) {
        candidates.push_back(candidate);
    }
}

/// 将环形检测结果追加到证据帧和候选列表中
/// 先添加原始记录，再添加处理后的有效记录，并根据十字出口状态决定是否抑制
void AppendCircleEvidence(port::VisualElementEvidenceFrame& evidence,
                          std::vector<port::VisualReferenceCandidate>& candidates,
                          const CircleElementEvidenceResult& circle,
                          const VisualElementPipelineInput& input,
                          const port::RuntimeParameters& params,
                          CircleEntryPipelineDiagnostics& diagnostics) {
    evidence.records.push_back(circle.left_raw);
    evidence.records.push_back(circle.right_raw);
    port::VisualElementEvidenceRecord left =
        MakeEffectiveCircleRecord(circle.left_raw, "circle_left", evidence.cross_exit.present);
    port::VisualElementEvidenceRecord right =
        MakeEffectiveCircleRecord(circle.right_raw, "circle_right", evidence.cross_exit.present);
    CircleEntryPathFacts left_entry = circle.left_entry;
    CircleEntryPathFacts right_entry = circle.right_entry;
    if (params.bev_element.circle_entry_takeover_enabled &&
        !evidence.cross_exit.present &&
        (left.present || right.present)) {
        const std::vector<BEVSimpleRowScan> empty_rows{};
        const std::vector<BEVSimpleRowScan>& rows =
            input.sparse_rows == nullptr ? empty_rows : *input.sparse_rows;
        if (input.frame != nullptr && input.projector != nullptr && input.frame->Valid()) {
            BEVMetricClassSampler sampler(*input.frame, input.threshold, params, *input.projector);
            if (left.present) {
                left_entry = BuildCircleEntryPathFacts(sampler, rows, params, true);
            }
            if (right.present) {
                right_entry = BuildCircleEntryPathFacts(sampler, rows, params, false);
            }
        } else {
            if (left.present) {
                left_entry.reason = "roi_context_unavailable";
            }
            if (right.present) {
                right_entry.reason = "roi_context_unavailable";
            }
        }
    }
    MaybeBuildCircleCandidate(left,
                              left_entry,
                              port::VisualReferenceCandidateKind::kCircleLeft,
                              params,
                              candidates);
    MaybeBuildCircleCandidate(right,
                              right_entry,
                              port::VisualReferenceCandidateKind::kCircleRight,
                              params,
                              candidates);
    diagnostics.left = left_entry;
    diagnostics.right = right_entry;
    evidence.records.push_back(left);
    evidence.records.push_back(right);
}

}  // namespace

/// RunVisualElementPipeline 实现
/// 运行完整的视觉元素管线，按顺序执行：
/// 1. 十字出口证据检测 -> 构建候选
/// 2. 环形入口证据检测 -> 构建候选（受十字出口抑制）
/// 将所有候选加入结果列表供后续仲裁使用
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
        input.element_raster == nullptr
            ? DetectCircleElementEvidence(rows, params)
            : DetectCircleElementEvidence(input.element_raster, params);
    AppendCircleEvidence(result.evidence,
                         result.candidates,
                         circle,
                         input,
                         params,
                         result.circle_entry_diagnostics);
    return result;
}

}  // namespace ls2k::legacy
