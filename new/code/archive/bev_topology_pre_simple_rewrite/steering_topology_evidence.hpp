#ifndef LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP

// 拓扑证据评分层 —— 对道路假设进行多维度评分，输出场景级拓扑证据。
// 评分维度：ordinary（普通）、cross（十字路口）、circle（环岛）、
// zebra（斑马线）、lost（丢失）。同时支持时序累积（exponential moving average）。

#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 对道路假设进行拓扑证据评分
// 从假设、间隔、车辆上下文、历史累积器、元素证据中计算出各场景得分
port::TopologyEvidence ScoreTopologyEvidence(const port::RoadHypotheses& hypotheses,
                                             const CorridorIntervalSet& intervals,
                                             const port::VehicleContext& vehicle,
                                             const port::RuntimeParameters& params,
                                             const port::TopologyEvidenceAccumulator& prior_accumulator,
                                             const port::BEVElementEvidence* element_evidence = nullptr);

// 更新拓扑证据累积器（EMA 滤波）
// 使用 evidence_decay 参数对当前评分和历史评分做加权平均
port::TopologyEvidenceAccumulator UpdateTopologyEvidenceAccumulator(
    const port::TopologyEvidence& evidence,
    const port::RuntimeParameters& params,
    const port::TopologyEvidenceAccumulator& prior_accumulator);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP
