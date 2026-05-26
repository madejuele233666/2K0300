/**
 * @file visual_reference_orchestration_types.hpp
 * @brief 视觉参考路径编排类型定义
 *
 * 定义视觉参考路径候选的选择和编排类型。
 * 系统从多个候选（直线、十字出口、左转/右转圆、绕障、ML）中选择最合适的参考路径。
 */

#ifndef LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP
#define LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP

#include <array>
#include <cstddef>
#include <string>

#include "port/bev_reference_types.hpp"

namespace ls2k::port {

/**
 * @enum VisualReferenceCandidateKind
 * @brief 视觉参考路径候选的种类
 *
 * 标识参考路径的语义类型，用于候选选择和仲裁。
 */
enum class VisualReferenceCandidateKind {
    kLine,             ///< 直线路径
    kCrossExit,        ///< 十字路口出口路径
    kCircleLeft,       ///< 左转圆形路径
    kCircleRight,      ///< 右转圆形路径
    kRoadblockBypass,  ///< 路障绕行路径
    kMlGrounded,       ///< 机器学习模型输出的路径
};

/**
 * @struct VisualReferenceCandidate
 * @brief 单个视觉参考路径候选
 *
 * 包含候选路径的存在性、种类、BEV参考路径、置信度、来源和原因。
 */
struct VisualReferenceCandidate {
    bool present = false;                              ///< 候选是否存在
    VisualReferenceCandidateKind kind = VisualReferenceCandidateKind::kLine;  ///< 候选种类
    BEVReferencePath reference_path{};                 ///< BEV参考路径
    float confidence = 0.0F;                           ///< 候选置信度
    std::string source = "none";                       ///< 候选来源描述
    std::string reason = "none";                       ///< 原因描述
};

constexpr std::size_t kVisualReferenceCandidatePathCapacity = 6U;

struct VisualReferenceCandidatePathSet {
    std::array<VisualReferenceCandidate, kVisualReferenceCandidatePathCapacity> entries{};
    std::size_t count = 0;
    std::size_t omitted_count = 0;
};

inline void AppendVisualReferenceCandidatePath(VisualReferenceCandidatePathSet& set,
                                               const VisualReferenceCandidate& candidate) {
    if (set.count < set.entries.size()) {
        set.entries[set.count] = candidate;
        ++set.count;
        return;
    }
    ++set.omitted_count;
}

/**
 * @struct VisualReferenceSelection
 * @brief 视觉参考路径的最终选择结果
 *
 * 经过仲裁后从多个候选中选出的最终参考路径。
 * 包含是否选中、选中的路径、来源、原因、候选总数和被拒绝的候选原因。
 */
struct VisualReferenceSelection {
    bool present = false;                              ///< 是否选中了参考路径
    BEVReferencePath reference_path{};                 ///< 选中的BEV参考路径
    std::string source = "none";                       ///< 选中来源描述
    std::string reason = "no_visual_reference_candidate";  ///< 选择/未选择的原因
    std::size_t candidate_count = 0;                   ///< 候选总数
    std::string rejected_candidate_reason = "none";    ///< 被拒绝的候选的原因
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_VISUAL_REFERENCE_ORCHESTRATION_TYPES_HPP
