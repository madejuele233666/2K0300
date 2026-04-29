#ifndef LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP
#define LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP

// 走廊图构建层 —— 在走廊间隔的基础上建立跨层连接（图结构），
// 从连续间隔链中选取最优路径作为候选轨迹。
// 核心机制：
// 1. 从每层可行驶间隔中选取候选，构建跨层边（overlap / center jump / width change）
// 2. 动态规划搜索最优路径链（最大化累积得分）
// 3. 法线锚定推理：利用观测到的左右边界，沿法线推算出车道中心
// 4. 输出标准化的 24 层轨迹估计（中心线/边界/曲率）

#include <array>
#include <string>
#include <vector>

#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 边界锚定侧枚举 —— 指示法线锚定推理中使用的是哪一侧边界
enum class BoundaryAnchorSide : int {
    kNone = 0,   //!< 未锚定（无有效边界支撑）
    kLeft = -1,  //!< 左侧边界锚定
    kRight = 1   //!< 右侧边界锚定
};

// 走廊图 —— 动态规划选出的最优路径候选及其边界轨迹
struct CorridorGraph {
    bool valid = false;                                                                         //!< 图是否有效（存在可行路径）
    std::string fallback_mode = "none";                                                         //!< 降级原因（若无效）
    std::array<int, port::kBevTrackSampleCount> ordinary_interval_indices{};                     //!< 每层选中的走廊间隔索引（-1 表示未选中）
    std::array<BoundaryAnchorSide, port::kBevTrackSampleCount> ordinary_center_anchor_side{};    //!< 每层中心线的锚定侧（法线推理来源）
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> ordinary_raw_centerline{};       //!< 归一化前的原始中心线采样点
    std::vector<port::CorridorGraphEdge> edges{};                                                //!< 跨层连接边集合（用于调试/分析）
    port::PathCandidate ordinary{};                                                              //!< 普通路径候选（含标准化中心线、曲率、置信度）
};

// 从走廊间隔集合构建最优走廊图
// 使用动态规划（Viterbi 式）逐层搜索，找到得分最高的跨层间隔链
CorridorGraph BuildCorridorGraph(const CorridorIntervalSet& intervals,
                                 const port::RuntimeParameters& params,
                                 const port::LegacySteeringState& prior_state);

// 从走廊图生成最终轨迹估计（中心线、左右边界、曲率、控制误差）
// 输出标准化到 24 层前向网格的轨迹，包含车道宽度、可见范围、近侧/远侧误差
port::BEVTrackEstimate BuildBEVTrackEstimateFromCorridorGraph(const CorridorIntervalSet& intervals,
                                                              const CorridorGraph& graph,
                                                              const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CORRIDOR_GRAPH_HPP
