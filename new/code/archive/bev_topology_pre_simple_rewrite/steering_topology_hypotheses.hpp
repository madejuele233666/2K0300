#ifndef LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP
#define LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP

// 道路假设生成层 —— 从走廊图和元素证据生成多种候选路径。
// 包括：普通中心线、前向退出、十字路口退出、左右弧线、
// 边界偏移跟随、环岛内外侧、分支候选等。
// 为上层场景 FSM 提供路径选择基础。

#include "legacy/steering_corridor_graph.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 生成完整的道路假设集合（RoadHypotheses）
// 基于走廊图的最优路径（ordinary）派生出各种场景候选路径
// element_evidence 为可选输入，提供十字路口/环岛转角语义
port::RoadHypotheses GenerateRoadHypotheses(const CorridorGraph& graph,
                                            const CorridorIntervalSet& intervals,
                                            const port::LegacySteeringState& prior_state,
                                            const port::RuntimeParameters& params,
                                            const port::BEVElementEvidence* element_evidence = nullptr);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_TOPOLOGY_HYPOTHESES_HPP
