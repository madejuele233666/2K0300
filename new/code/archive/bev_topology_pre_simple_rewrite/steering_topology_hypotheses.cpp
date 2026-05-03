#include "legacy/steering_topology_hypotheses.hpp"

// 道路假设生成实现。
// 在走廊图的最优路径（ordinary）基础上，派生出多种场景候选路径：
// - forward_exit: 基于最佳观测间隔的前向退出路径
// - cross_exit: 十字路口退出路径（在 cross_band 之后）
// - left_arc/right_arc: 沿法线偏移的弧线路径
// - circle_inner_left/right: 环岛内侧路径（沿边界跟随）
// - circle_outer_guard: 环岛外侧守卫路径
// - left_branch/right_branch: 分支候选路径

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_path_math.hpp"

namespace ls2k::legacy {
namespace {

// 刷新候选路径的前向范围（start_forward_m / end_forward_m）
void RefreshCandidateForwardRange(port::PathCandidate& candidate) {
    bool have_sample = false;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (!have_sample) {
            candidate.start_forward_m = sample.point.forward_m;
            have_sample = true;
        }
        candidate.end_forward_m = sample.point.forward_m;
    }
}

// 终态化候选路径：设置有效性、置信度、平均宽度、宽度稳定性、曲率
void FinalizeCandidate(port::PathCandidate& candidate,
                       int valid_count,
                       float confidence_sum,
                       float width_sum,
                       const port::RuntimeParameters& params,
                       int min_valid_count = 3) {
    candidate.valid = valid_count >= std::max(1, min_valid_count);
    if (!candidate.valid) {
        return;
    }
    candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
    candidate.mean_width_m =
        valid_count > 0 ? width_sum / static_cast<float>(valid_count) : params.bev_corridor_graph.nominal_lane_width_m;
    candidate.width_stability =
        std::clamp(1.0F - std::abs(candidate.mean_width_m - params.bev_corridor_graph.nominal_lane_width_m) /
                             std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m),
                   0.0F,
                   1.0F);
    RefreshCandidateForwardRange(candidate);

    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* mid = nullptr;
    const port::BEVPathSample* last = nullptr;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
        }
        mid = last;
        last = &sample;
    }
    if (first != nullptr && mid != nullptr && last != nullptr && first != mid && mid != last) {
        candidate.curvature = PathCurvatureFromThreeSamples(*first, *mid, *last);
        candidate.curvature_consistency =
            std::clamp(1.0F - std::abs(candidate.curvature) /
                                 std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs),
                       0.0F,
                       1.0F);
    }
}

// 在指定层中找到最佳的双侧观测间隔（左右边界均有效且宽度在有效范围内）
// 置信度最高的间隔胜出
const port::CorridorInterval* BestObservedInterval(const CorridorIntervalLayer& layer,
                                                   const port::RuntimeParameters& params) {
    const port::CorridorInterval* best = nullptr;
    const float min_width = std::max(params.bev_corridor_graph.min_interval_width_m,
                                     params.bev_geometry.min_lane_width_m);
    const float max_width = std::min(params.bev_corridor_graph.max_interval_width_m,
                                     params.bev_geometry.max_lane_width_m);
    for (const port::CorridorInterval& interval : layer.intervals) {
        if (!interval.left_edge_valid || !interval.right_edge_valid ||
            interval.width_m < min_width || interval.width_m > max_width) {
            continue;
        }
        if (best == nullptr || interval.confidence > best->confidence) {
            best = &interval;
        }
    }
    return best;
}

// 构建前向退出候选路径（完整版，可指定起始层和最小有效点数）
// 从 start_layer 开始遍历后续各层，每层选 BestObservedInterval
port::PathCandidate BuildForwardExitCandidate(const CorridorIntervalSet& intervals,
                                              const port::RuntimeParameters& params,
                                              std::size_t start_layer,
                                              int min_valid_count) {
    port::PathCandidate candidate{};
    candidate.mode = port::ReferenceMode::kBlendToExit;
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    for (std::size_t layer = start_layer; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = BestObservedInterval(intervals.layers[layer], params);
        if (interval == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = interval->forward_m;
        sample.point.lateral_m = interval->lateral_center_m;
        sample.confidence = interval->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += interval->confidence;
        width_sum += interval->width_m;
        ++valid_count;
    }
    FinalizeCandidate(candidate, valid_count, confidence_sum, width_sum, params, min_valid_count);
    return candidate;
}

// 构建前向退出候选路径（简化版）—— 从 curvature_sample_index 层开始搜索
port::PathCandidate BuildForwardExitCandidate(const CorridorIntervalSet& intervals,
                                              const port::RuntimeParameters& params) {
    const std::size_t start_layer =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    return BuildForwardExitCandidate(intervals, params, start_layer, 3);
}

// 找到前向采样网格中第一个 >= 指定距离的层索引
std::size_t FirstForwardLayerAtOrAfter(const port::RuntimeParameters& params, float forward_m) {
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        if (params.bev_topology_sampler.forward_samples_m[layer] >= forward_m) {
            return layer;
        }
    }
    return port::kBevTrackSampleCount - 1U;
}

// 计算候选路径的绝对航向角（首尾有效点的方向）
float CandidateHeadingAbs(const port::PathCandidate& candidate) {
    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* last = nullptr;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
        }
        last = &sample;
    }
    if (first == nullptr || last == nullptr || first == last) {
        return 0.0F;
    }
    const float df = last->point.forward_m - first->point.forward_m;
    if (std::abs(df) < 1e-4F) {
        return 0.0F;
    }
    return std::abs(std::atan2(last->point.lateral_m - first->point.lateral_m, df));
}

// 构建十字路口退出候选路径
// 在 cross_band 位置之后 start_layer 层开始搜索出口
// 如果路径航向角过大（转弯太急），视为无效
port::PathCandidate BuildCrossExitCandidate(const CorridorIntervalSet& intervals,
                                            const port::RuntimeParameters& params,
                                            const port::BEVElementEvidence* element_evidence) {
    if (element_evidence == nullptr || !element_evidence->valid ||
        !element_evidence->cross_band.present) {
        return {};
    }
    const float start_forward = element_evidence->cross_band.forward_m +
                                params.bev_path_policy.cross_exit_after_band_min_m;
    const std::size_t start_layer = FirstForwardLayerAtOrAfter(params, start_forward);
    port::PathCandidate candidate =
        BuildForwardExitCandidate(intervals,
                                  params,
                                  start_layer,
                                  std::max(1, params.bev_path_policy.cross_exit_min_layers));
    candidate.mode = port::ReferenceMode::kBlendToExit;
    if (candidate.valid &&
        CandidateHeadingAbs(candidate) > params.bev_path_policy.cross_exit_heading_abs_max_rad) {
        candidate.valid = false;
    }
    return candidate;
}

// 构建边界偏移候选路径 —— 沿某一侧边界 + 法线偏移半车道宽
// 用于左右弧线路径，在没有中心线弧线候选时作为 fallback
port::PathCandidate BuildBoundaryOffsetCandidate(const CorridorIntervalSet& intervals,
                                                 bool left_boundary,
                                                 const port::RuntimeParameters& params,
                                                 int min_valid_count = 3) {
    port::PathCandidate candidate{};
    candidate.mode = port::ReferenceMode::kStableBoundaryOffset;
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* best = nullptr;
        for (const port::CorridorInterval& interval : intervals.layers[layer].intervals) {
            const bool edge_valid = left_boundary ? interval.left_edge_valid : interval.right_edge_valid;
            if (!edge_valid) {
                continue;
            }
            if (best == nullptr || interval.confidence > best->confidence) {
                best = &interval;
            }
        }
        if (best == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = best->forward_m;
        sample.point.lateral_m = left_boundary ? best->lateral_min_m : best->lateral_max_m;
        sample.confidence = best->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += best->confidence;
        width_sum += std::max(1e-4F, best->width_m);
        ++valid_count;
    }
    if (valid_count < std::max(1, min_valid_count)) {
        return candidate;
    }

    const float half_width = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    OffsetPathAlongNormals(candidate.sampled_path,
                           left_boundary ? half_width : -half_width,
                           half_width);
    (void)NormalizePathToForwardSamples(candidate.sampled_path,
                                        params.bev_topology_sampler.forward_samples_m,
                                        half_width,
                                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                 std::abs(params.bev_geometry.lateral_step_m)) *
                                            2.0F);
    FinalizeCandidate(candidate, valid_count, confidence_sum, width_sum, params, min_valid_count);
    if (candidate.valid) {
        candidate.confidence = std::clamp(candidate.confidence * 0.75F, 0.0F, 1.0F);
        for (port::BEVPathSample& sample : candidate.sampled_path) {
            if (sample.valid) {
                sample.confidence = std::clamp(sample.confidence * 0.75F, 0.0F, 1.0F);
            }
        }
    }
    return candidate;
}

bool EdgePathUsable(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& edge,
                    int min_valid_count) {
    int valid_count = 0;
    for (const port::BEVPathSample& sample : edge) {
        if (sample.valid) {
            ++valid_count;
        }
    }
    return valid_count >= std::max(1, min_valid_count);
}

bool SegmentHasNearSupport(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& segment,
                           const port::RuntimeParameters& params) {
    const std::size_t curvature_index =
        std::min<std::size_t>(static_cast<std::size_t>(
                                  std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    const float near_limit =
        std::max(static_cast<float>(params.bev_control_model.lookahead_max_m),
                 params.bev_topology_sampler.forward_samples_m[curvature_index]) +
        std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                 params.bev_topology_sampler.forward_samples_m[0]);
    for (const port::BEVPathSample& sample : segment) {
        if (sample.valid) {
            return sample.point.forward_m <= near_limit + 1.0e-4F;
        }
    }
    return false;
}

port::PathCandidate BuildCircleInnerFromIslandMemoryCandidate(
    const port::ReferencePolicyState& memory,
    const port::BEVCircleInnerIslandEvidence& current,
    bool left_side,
    const port::RuntimeParameters& params) {
    port::PathCandidate candidate{};
    candidate.mode = port::ReferenceMode::kCircleInnerIsland;
    const int min_layers = std::max(1, params.bev_path_policy.circle_inner_min_layers);
    const bool memory_available =
        memory.circle_inner_island_memory_active &&
        memory.circle_inner_island_memory_left == left_side &&
        EdgePathUsable(memory.circle_inner_island_edge, min_layers);
    const bool current_available =
        current.edge_present && current.trace_present &&
        EdgePathUsable(current.road_facing_edge, min_layers);
    if (!memory_available && !current_available) {
        return candidate;
    }
    if (!memory_available && !current.present) {
        return candidate;
    }

    const bool memory_edge_available =
        memory_available && SegmentHasNearSupport(memory.circle_inner_island_edge, params);

    std::array<port::BEVPathSample, port::kBevTrackSampleCount> edge{};
    float confidence_scale = 1.0F;
    if (current_available && (memory_available || current.present)) {
        edge = current.road_facing_edge;
        confidence_scale =
            memory_available
                ? std::max(std::clamp(memory.circle_inner_island_memory_confidence, 0.35F, 1.0F),
                           std::clamp(current.score, 0.35F, 1.0F))
                : std::clamp(current.score, 0.35F, 1.0F);
    } else if (memory_edge_available) {
        edge = memory.circle_inner_island_edge;
        confidence_scale =
            std::clamp(memory.circle_inner_island_memory_confidence, 0.35F, 1.0F);
    } else {
        return candidate;
    }

    const float signed_offset =
        left_side
            ? std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.5F,
                       std::abs(params.bev_topology_sampler.lateral_step_m) * 2.0F)
            : -std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.5F,
                        std::abs(params.bev_topology_sampler.lateral_step_m) * 2.0F);
    int valid_count = 0;
    float confidence_sum = 0.0F;
    for (std::size_t index = 0; index < edge.size(); ++index) {
        if (!edge[index].valid) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = edge[index].point.forward_m;
        sample.point.lateral_m = edge[index].point.lateral_m + signed_offset;
        sample.confidence = std::clamp(edge[index].confidence * confidence_scale, 0.0F, 1.0F);
        candidate.sampled_path[index] = sample;
        confidence_sum += sample.confidence;
        ++valid_count;
    }
    if (valid_count < min_layers) {
        return candidate;
    }
    candidate.valid = true;
    candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
    candidate.mean_width_m = params.bev_corridor_graph.nominal_lane_width_m;
    candidate.width_stability = 1.0F;
    RefreshCandidateForwardRange(candidate);
    FinalizeCandidate(candidate,
                      valid_count,
                      confidence_sum,
                      params.bev_corridor_graph.nominal_lane_width_m * static_cast<float>(valid_count),
                      params,
                      min_layers);
    return candidate;
}

// 平移候选路径 —— 整体沿法线偏移指定距离（用于生成弧线路径）
// 同时缩放置信度，保留原始路径形状
port::PathCandidate ShiftCandidate(const port::PathCandidate& base,
                                   port::ReferenceMode mode,
                                   float lateral_offset,
                                   float confidence_scale,
                                   const port::RuntimeParameters& params) {
    port::PathCandidate shifted = base;
    shifted.mode = mode;
    shifted.confidence = std::clamp(base.confidence * confidence_scale, 0.0F, 1.0F);
    OffsetPathAlongNormals(shifted.sampled_path, lateral_offset, std::abs(lateral_offset));
    (void)NormalizePathToForwardSamples(shifted.sampled_path,
                                        params.bev_topology_sampler.forward_samples_m,
                                        std::abs(lateral_offset),
                                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                 std::abs(params.bev_geometry.lateral_step_m)) *
                                            2.0F);
    for (port::BEVPathSample& sample : shifted.sampled_path) {
        if (sample.valid) {
            sample.confidence = std::clamp(sample.confidence * confidence_scale, 0.0F, 1.0F);
        }
    }
    RefreshCandidateForwardRange(shifted);
    return shifted;
}

// 选取分支候选路径 —— 在 ordinary 路径同一层中，寻找位于左侧或右侧的其他间隔
// 用于检测道路分支（分叉路或出口）
port::PathCandidate PickBranchCandidate(const CorridorGraph& graph,
                                        const CorridorIntervalSet& intervals,
                                        bool left_branch,
                                        const port::RuntimeParameters& params) {
    port::PathCandidate candidate{};
    candidate.mode = left_branch ? port::ReferenceMode::kStableBoundaryOffset
                                 : port::ReferenceMode::kStableBoundaryOffset;

    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int ordinary_index = graph.ordinary_interval_indices[layer];
        const auto& layer_intervals = intervals.layers[layer].intervals;
        if (ordinary_index < 0 || ordinary_index >= static_cast<int>(layer_intervals.size())) {
            continue;
        }
        const port::CorridorInterval& ordinary =
            layer_intervals[static_cast<std::size_t>(ordinary_index)];
        const float ordinary_center =
            graph.ordinary.sampled_path[layer].valid ? graph.ordinary.sampled_path[layer].point.lateral_m
                                                     : ordinary.lateral_center_m;
        const port::CorridorInterval* best = nullptr;
        for (std::size_t interval_index = 0; interval_index < layer_intervals.size(); ++interval_index) {
            if (static_cast<int>(interval_index) == ordinary_index) {
                continue;
            }
            const port::CorridorInterval& interval = layer_intervals[interval_index];
            if (!interval.left_edge_valid && !interval.right_edge_valid) {
                continue;
            }
            const bool side_ok = left_branch ? interval.lateral_center_m < ordinary_center
                                             : interval.lateral_center_m > ordinary_center;
            if (!side_ok) {
                continue;
            }
            if (best == nullptr ||
                (left_branch ? interval.lateral_center_m < best->lateral_center_m
                             : interval.lateral_center_m > best->lateral_center_m)) {
                best = &interval;
            }
        }
        if (best == nullptr) {
            continue;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = best->forward_m;
        sample.point.lateral_m = best->lateral_center_m;
        sample.confidence = best->confidence;
        candidate.sampled_path[layer] = sample;
        confidence_sum += best->confidence;
        width_sum += best->width_m;
        candidate.start_forward_m = valid_count == 0 ? best->forward_m : candidate.start_forward_m;
        candidate.end_forward_m = best->forward_m;
        ++valid_count;
    }
    candidate.valid = valid_count >= 2;
    if (candidate.valid) {
        candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
        candidate.mean_width_m = width_sum / static_cast<float>(valid_count);
        candidate.width_stability =
            std::clamp(1.0F - std::abs(candidate.mean_width_m - params.bev_corridor_graph.nominal_lane_width_m) /
                                 std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m),
                       0.0F,
                       1.0F);
    }
    return candidate;
}

}  // namespace

// ========== 主入口：生成道路假设 ==========
// 从走廊图的最优路径（ordinary）派生出所有场景候选：
// 1. ordinary: 走廊图选出的最优中心线
// 2. forward_exit: 前向退出（最佳观测间隔链）
// 3. cross_exit: 十字路口退出路径
// 4. left_arc/right_arc: 弧线偏移路径
// 5. circle_inner_left/right: 环岛内侧路径
// 6. circle_outer_guard_left/right: 环岛外侧守卫
// 7. left_branch/right_branch: 分支候选
// 8. zebra_hold: 斑马线保持路径
port::RoadHypotheses GenerateRoadHypotheses(const CorridorGraph& graph,
                                            const CorridorIntervalSet& intervals,
                                            const port::LegacySteeringState& prior_state,
                                            const port::RuntimeParameters& params,
                                            const port::BEVElementEvidence* element_evidence) {
    (void)prior_state;
    port::RoadHypotheses hypotheses{};
    // 基础候选：ordinary + forward_exit
    hypotheses.ordinary = graph.ordinary;
    hypotheses.ordinary.mode = port::ReferenceMode::kCenterline;
    hypotheses.forward_exit = graph.ordinary;
    hypotheses.forward_exit.mode = port::ReferenceMode::kCenterline;
    // 如果 ordinary 无效，用 forward_exit 替代
    if (!hypotheses.forward_exit.valid) {
        hypotheses.forward_exit = BuildForwardExitCandidate(intervals, params);
    }
    // 十字路口退出候选
    hypotheses.cross_exit = BuildCrossExitCandidate(intervals, params, element_evidence);

    // 弧线路径：从 ordinary 沿法线偏移半车道宽
    const float arc_offset = params.bev_corridor_graph.nominal_lane_width_m * 0.5F;
    if (graph.ordinary.valid) {
        hypotheses.left_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, -arc_offset, 0.80F, params);
        hypotheses.right_arc =
            ShiftCandidate(graph.ordinary, port::ReferenceMode::kArcFollow, arc_offset, 0.80F, params);
        hypotheses.zebra_hold = graph.ordinary;
        hypotheses.zebra_hold.mode = port::ReferenceMode::kHoldLast;
    }
    // 如果法线偏移无效，fallback 到基于边界的偏移候选
    if (!hypotheses.left_arc.valid) {
        hypotheses.left_arc = BuildBoundaryOffsetCandidate(intervals, true, params);
    }
    if (!hypotheses.right_arc.valid) {
        hypotheses.right_arc = BuildBoundaryOffsetCandidate(intervals, false, params);
    }
    // 环岛路径：外侧守卫（对侧边界偏移）和内侧黑区记忆跟随
    const int circle_min_layers = std::max(1, params.bev_path_policy.circle_inner_min_layers);
    hypotheses.circle_outer_guard_left =
        BuildBoundaryOffsetCandidate(intervals, false, params, circle_min_layers);
    hypotheses.circle_outer_guard_left.mode = port::ReferenceMode::kOuterOffset;
    hypotheses.circle_outer_guard_right =
        BuildBoundaryOffsetCandidate(intervals, true, params, circle_min_layers);
    hypotheses.circle_outer_guard_right.mode = port::ReferenceMode::kOuterOffset;
    hypotheses.circle_inner_left =
        BuildCircleInnerFromIslandMemoryCandidate(prior_state.reference_policy,
                                                  element_evidence != nullptr
                                                      ? element_evidence->left_inner_island
                                                      : port::BEVCircleInnerIslandEvidence{},
                                                  true,
                                                  params);
    hypotheses.circle_inner_right =
        BuildCircleInnerFromIslandMemoryCandidate(prior_state.reference_policy,
                                                  element_evidence != nullptr
                                                      ? element_evidence->right_inner_island
                                                      : port::BEVCircleInnerIslandEvidence{},
                                                  false,
                                                  params);
    // 环岛出口路径：优先用 cross_exit，否则用 forward_exit
    hypotheses.circle_exit_left = hypotheses.cross_exit.valid ? hypotheses.cross_exit : hypotheses.forward_exit;
    hypotheses.circle_exit_left.mode = port::ReferenceMode::kBlendToExit;
    hypotheses.circle_exit_right = hypotheses.circle_exit_left;

    // 分支候选路径
    hypotheses.left_branch = PickBranchCandidate(graph, intervals, true, params);
    hypotheses.right_branch = PickBranchCandidate(graph, intervals, false, params);
    return hypotheses;
}

}  // namespace ls2k::legacy
