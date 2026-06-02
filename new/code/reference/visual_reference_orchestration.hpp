#ifndef LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP
#define LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP

#include <string>
#include <vector>

#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::reference {

/// 将视觉参考候选类型枚举转换为可读字符串
const char* ToString(port::VisualReferenceCandidateKind kind);

/// 从BEV参考路径创建车道线视觉参考候选
/// @param reference_path BEV参考路径
/// @param source 候选来源描述
/// @return 车道线视觉参考候选
port::VisualReferenceCandidate MakeLineVisualReferenceCandidate(
    const port::BEVReferencePath& reference_path,
    const std::string& source);

/// 从候选列表中选择最佳的视觉参考候选
/// 优先选择特殊候选（如环形入口、十字出口等），其次选择车道线候选
/// 如果存在候选但均无效，记录拒绝原因
/// @param candidates 视觉参考候选列表
/// @return 选择结果（选中候选或失败原因）
port::VisualReferenceSelection SelectVisualReference(
    const std::vector<port::VisualReferenceCandidate>& candidates);

}  // namespace ls2k::reference

#endif  // LS2K_LEGACY_STEERING_VISUAL_REFERENCE_ORCHESTRATION_HPP
