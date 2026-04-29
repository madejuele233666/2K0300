#ifndef LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP

// 参考路径策略层 —— 根据场景状态（FSM）和拓扑证据从候选路径中选取最终参考路径。
// 支持多种参考模式（中心线/内侧偏移/外侧偏移/弧线跟随/保持上次/丢失预测等）。
// 新旧两版接口分别兼容 BEVSceneObservation 和 TopologyEvidence 两种管线。

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

// 参考策略输出结果
struct ReferencePolicyResult {
    port::ReferencePolicyState state{};            //!< 策略状态（含历史参考、置信度等）
    port::BEVReferencePath reference_path{};       //!< 最终参考路径
    std::string reference_mode = "centerline";     //!< 参考模式字符串
};

// 旧版参考策略解析（基于 BEVTrackEstimate 和 BEVSceneObservation）
ReferencePolicyResult ResolveReferencePolicy(const port::BEVTrackEstimate& track,
                                             const port::BEVSceneObservation& observation,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params);

// 新版参考策略解析（基于 RoadHypotheses 和 TopologyEvidence）
// 支持更丰富的候选路径选择（ordinary/cross/circle/zebra/lost）
ReferencePolicyResult ResolveReferencePolicy(const port::RoadHypotheses& hypotheses,
                                             const port::TopologyEvidence& evidence,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params);

// 参考模式枚举转字符串
const char* ToString(port::ReferenceMode mode);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP
