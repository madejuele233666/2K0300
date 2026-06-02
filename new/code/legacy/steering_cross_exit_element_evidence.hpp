#ifndef LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP

#include <vector>

#include "legacy/steering_bev_simple_perception.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

/// 检测十字出口元素证据（从稀疏行扫描中检测十字路口出口特征）
/// @param rows 稀疏行扫描结果
/// @param params 运行时参数
/// @return 十字出口元素证据检测结果
port::CrossExitElementEvidence DetectCrossExitEvidence(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params);

/// 构建十字出口视觉参考候选
/// @param evidence 十字出口元素证据
/// @param line_candidate 车道线参考候选（用于继承参考路径）
/// @param params 运行时参数
/// @param summary [输出] 候选摘要信息
/// @return 构建的视觉参考候选
port::VisualReferenceCandidate BuildCrossExitVisualReferenceCandidate(
    const port::CrossExitElementEvidence& evidence,
    const port::VisualReferenceCandidate& line_candidate,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CROSS_EXIT_ELEMENT_EVIDENCE_HPP
