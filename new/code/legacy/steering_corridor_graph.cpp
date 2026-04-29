#include "legacy/steering_corridor_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_path_math.hpp"

// 走廊图构建实现。
// 将每层的走廊间隔组织为图结构，用动态规划搜索最优跨层路径链。
// 核心流程：
// 1. LaneObservation：将 CorridorInterval 转化为"车道观测"（含中心、宽度、置信度）
// 2. BuildChainObservations：对每层选中的间隔，利用法线锚定推理确定车道中心
// 3. BuildCorridorGraph：Viterbi 式 DP 搜索，最大化累积得分（节点分+跃迁分）
// 4. FillCandidateSummary：将最优路径链填充为标准化 PathCandidate
// 5. BuildBEVTrackEstimateFromCorridorGraph：从图输出最终轨迹估计

namespace ls2k::legacy {
namespace {

constexpr float kNegativeInfinity = -1.0e9F;

// 车道观测结构 —— 将走廊间隔转化为车道级观测结果
// 包含中心位置、两侧边界是否被观测到、车道宽度、置信度等信息
// 是边界过滤和法线锚定推理的核心数据类型
struct LaneObservation {
    bool usable = false;
    bool left_boundary_observed = false;
    bool right_boundary_observed = false;
    bool normal_anchor_allowed = true;
    float center_forward_m = 0.0F;
    float center_m = 0.0F;
    float lane_width_m = 0.0F;
    float confidence = 0.0F;
};

// 中心候选点 —— 法线偏移后得到的车道中心位置
struct CenterCandidate {
    bool valid = false;      //!< 候选是否有效
    float forward_m = 0.0F;  //!< 前向距离（米）
    float lateral_m = 0.0F;  //!< 横向位置（米）
};

// 锚定候选点 —— 包含从哪一侧边界进行法线推理的信息
struct AnchorCandidate {
    bool valid = false;                 //!< 候选是否有效
    BoundaryAnchorSide side = BoundaryAnchorSide::kNone;  //!< 锚定侧（左/右）
    CenterCandidate center{};           //!< 推得的中心点
    int support_rank = 0;              //!< 支撑等级（前后层是否也有有效边界）
};

using PathSampleArray = std::array<port::BEVPathSample, port::kBevTrackSampleCount>;

// 边界轨迹集合 —— 左右两侧边界的跨层轨迹
struct BoundaryTraces {
    PathSampleArray left{};   //!< 左侧边界跨层轨迹
    PathSampleArray right{};  //!< 右侧边界跨层轨迹
};

// 创建一个路径采样点（辅助函数，带置信度限幅）
port::BEVPathSample MakePathSample(float forward_m, float lateral_m, float confidence) {
    port::BEVPathSample sample{};
    sample.valid = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = std::clamp(confidence, 0.0F, 1.0F);
    return sample;
}

// 计算两个走廊间隔的横向重叠得分（重叠宽度 / 较窄间隔宽度）
// 用于衡量跨层连接时两个间隔是否对齐
float IntervalOverlapScore(const port::CorridorInterval& a, const port::CorridorInterval& b) {
    const float overlap_min = std::max(a.lateral_min_m, b.lateral_min_m);
    const float overlap_max = std::min(a.lateral_max_m, b.lateral_max_m);
    const float overlap = std::max(0.0F, overlap_max - overlap_min);
    const float denominator = std::max(1e-4F, std::min(a.width_m, b.width_m));
    return std::clamp(overlap / denominator, 0.0F, 1.0F);
}

// 计算历史先验承载得分 —— 评估当前观测与上一帧轨迹的横向一致性
// 用于在节点评分中奖励与历史轨迹连续的候选
float PriorCarryScore(const port::LegacySteeringState& prior_state,
                      std::size_t layer_index,
                      const LaneObservation& observation,
                      const port::BEVCorridorGraphParameters& graph_params) {
    const port::BEVTrackEstimate& prior =
        prior_state.last_bev_track.valid ? prior_state.last_bev_track
                                         : prior_state.bev_track_memory.previous_track;
    if (!prior.valid || layer_index >= prior.sampled_centerline.size() ||
        !prior.sampled_centerline[layer_index].valid) {
        return 0.0F;
    }
    const float distance =
        std::abs(prior.sampled_centerline[layer_index].point.lateral_m - observation.center_m);
    const float normalized = 1.0F - distance / std::max(1e-4F, graph_params.max_center_jump_m);
    return std::clamp(normalized, 0.0F, 1.0F) * graph_params.prior_carry_confidence_scale;
}

// 从走廊间隔和边界观测状态创建车道观测
// 根据左右边界是否被观测到，分四种情况推演车道中心、宽度和置信度：
// - 双侧观测：直接使用间隔中心
// - 单侧观测：用标称车道宽度从观测侧推算中心
// - 宽支撑截断：当截断+单侧观测+宽度超标时，仍使用间隔中心但降低置信度
LaneObservation ObserveLaneWithEdges(const port::CorridorInterval& interval,
                                     const port::BEVCorridorGraphParameters& graph_params,
                                     bool left_boundary_observed,
                                     bool right_boundary_observed) {
    LaneObservation observation{};
    observation.left_boundary_observed = left_boundary_observed;
    observation.right_boundary_observed = right_boundary_observed;
    observation.center_forward_m = interval.forward_m;

    const float nominal_width = std::max(1e-4F, graph_params.nominal_lane_width_m);
    const auto is_clipped_evidence = [](port::BEVBoundaryEvidenceKind evidence) {
        return evidence == port::BEVBoundaryEvidenceKind::kSearchWindowEdge ||
               evidence == port::BEVBoundaryEvidenceKind::kInvalidOutsideImage ||
               evidence == port::BEVBoundaryEvidenceKind::kImageBorder;
    };
    const bool support_is_clipped =
        is_clipped_evidence(interval.left_boundary_evidence) ||
        is_clipped_evidence(interval.right_boundary_evidence);
    const bool single_observed_boundary =
        observation.left_boundary_observed != observation.right_boundary_observed;
    const bool clipped_single_edge_wide_support =
        support_is_clipped && single_observed_boundary &&
        interval.width_m >= nominal_width * 1.25F;

    if (observation.left_boundary_observed && observation.right_boundary_observed) {
        observation.lane_width_m = interval.width_m;
        observation.center_m = interval.lateral_center_m;
        observation.confidence = interval.confidence;
    } else if (observation.left_boundary_observed) {
        observation.lane_width_m = clipped_single_edge_wide_support ? interval.width_m : nominal_width;
        observation.center_m = clipped_single_edge_wide_support
                                   ? interval.lateral_center_m
                                   : interval.lateral_min_m + nominal_width * 0.5F;
        observation.confidence = interval.confidence * (clipped_single_edge_wide_support ? 0.50F : 0.65F);
        observation.normal_anchor_allowed = !clipped_single_edge_wide_support;
    } else if (observation.right_boundary_observed) {
        observation.lane_width_m = clipped_single_edge_wide_support ? interval.width_m : nominal_width;
        observation.center_m = clipped_single_edge_wide_support
                                   ? interval.lateral_center_m
                                   : interval.lateral_max_m - nominal_width * 0.5F;
        observation.confidence = interval.confidence * (clipped_single_edge_wide_support ? 0.50F : 0.65F);
        observation.normal_anchor_allowed = !clipped_single_edge_wide_support;
    } else {
        observation.lane_width_m = nominal_width;
        observation.center_m = interval.lateral_center_m;
        observation.confidence = support_is_clipped ? 0.0F : interval.confidence * 0.45F;
    }

    observation.confidence = std::clamp(observation.confidence, 0.0F, 1.0F);
    observation.usable = observation.confidence > 0.0F;
    return observation;
}

// 简化版车道观测 —— 使用间隔自身记录的边界有效性标记
LaneObservation ObserveLane(const port::CorridorInterval& interval,
                            const port::BEVCorridorGraphParameters& graph_params) {
    return ObserveLaneWithEdges(interval,
                                graph_params,
                                interval.left_edge_valid,
                                interval.right_edge_valid);
}

// 根据索引数组获取指定层的选中间隔指针（越界返回 nullptr）
const port::CorridorInterval* SelectedIntervalAt(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    std::size_t layer) {
    if (layer >= indices.size()) {
        return nullptr;
    }
    const int interval_index = indices[layer];
    if (interval_index < 0 ||
        interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
        return nullptr;
    }
    return &intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
}

// 布尔值转边界锚定侧枚举
BoundaryAnchorSide AnchorSide(bool left_edge) {
    return left_edge ? BoundaryAnchorSide::kLeft : BoundaryAnchorSide::kRight;
}

// 计算边界连续性跳变限幅 —— 约为最大中心跳变的 55%
// 用于过滤边界轨迹中的异常跳变点
float BoundaryContinuityJumpLimit(const port::BEVCorridorGraphParameters& graph_params) {
    return std::max(0.04F, graph_params.max_center_jump_m * 0.55F);
}

// 检查边界轨迹的某层是否有邻近层支撑（前后 2 层内存在有效点）
// 用于过滤孤立无效的边界点，确保边界轨迹的连续性
bool HasNearbyBoundarySupport(const PathSampleArray& path,
                              std::size_t layer,
                              const port::BEVCorridorGraphParameters& graph_params) {
    if (layer >= path.size() || !path[layer].valid) {
        return false;
    }
    constexpr std::size_t kMaxLayerGap = 2U;
    const float jump_limit = BoundaryContinuityJumpLimit(graph_params);
    for (std::size_t gap = 1U; gap <= kMaxLayerGap; ++gap) {
        if (layer >= gap && path[layer - gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer - gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
        if (layer + gap < path.size() && path[layer + gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer + gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
    }
    return false;
}

// 过滤边界轨迹 —— 移除没有邻近层支撑的孤立边界点
PathSampleArray FilterBoundaryTrace(const PathSampleArray& raw,
                                    const port::BEVCorridorGraphParameters& graph_params) {
    PathSampleArray filtered{};
    for (std::size_t layer = 0; layer < raw.size(); ++layer) {
        if (HasNearbyBoundarySupport(raw, layer, graph_params)) {
            filtered[layer] = raw[layer];
        }
    }
    return filtered;
}

// 从选中的间隔集合构建左右边界轨迹
// 先提取原始边界点，再过滤掉缺乏邻近支撑的孤立点
BoundaryTraces BuildBoundaryTraces(const CorridorIntervalSet& intervals,
                                   const std::array<int, port::kBevTrackSampleCount>& indices,
                                   const port::BEVCorridorGraphParameters& graph_params) {
    BoundaryTraces raw{};
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = SelectedIntervalAt(intervals, indices, layer);
        if (interval == nullptr) {
            continue;
        }
        if (interval->left_edge_valid) {
            raw.left[layer] =
                MakePathSample(interval->forward_m, interval->lateral_min_m, interval->confidence);
        }
        if (interval->right_edge_valid) {
            raw.right[layer] =
                MakePathSample(interval->forward_m, interval->lateral_max_m, interval->confidence);
        }
    }
    BoundaryTraces traces{};
    traces.left = FilterBoundaryTrace(raw.left, graph_params);
    traces.right = FilterBoundaryTrace(raw.right, graph_params);
    return traces;
}

// 计算路径某层的支撑等级（前后层是否有有效点）
// 等级 0=孤立点, 1=至少一侧有相邻有效点, 2=两侧都有
int PathSupportRank(const PathSampleArray& path, std::size_t layer) {
    int support_rank = 0;
    for (std::size_t index = layer; index > 0U; --index) {
        if (path[index - 1U].valid) {
            ++support_rank;
            break;
        }
    }
    for (std::size_t index = layer + 1U; index < path.size(); ++index) {
        if (path[index].valid) {
            ++support_rank;
            break;
        }
    }
    return support_rank;
}

// 从边界轨迹沿法线偏移生成车道中心候选点
// 利用边界点 + 法线向量 + 半车道宽，推算出观测到的车道中心
bool MakeNormalCenterCandidate(
    const PathSampleArray& boundary_path,
    std::size_t layer,
    const port::BEVCorridorGraphParameters& graph_params,
    bool left_edge,
    AnchorCandidate& candidate) {
    if (layer >= boundary_path.size() || !boundary_path[layer].valid) {
        return false;
    }
    const int support_rank = PathSupportRank(boundary_path, layer);
    if (support_rank <= 0) {
        return false;
    }

    const float half_width = std::max(1e-4F, graph_params.nominal_lane_width_m) * 0.5F;
    const float offset_m = left_edge ? half_width : -half_width;
    const port::BEVPathSample center =
        OffsetPathSampleAlongNormal(boundary_path, layer, offset_m, half_width);
    if (!center.valid) {
        return false;
    }
    candidate.valid = true;
    candidate.side = AnchorSide(left_edge);
    candidate.center.valid = true;
    candidate.center.forward_m = center.point.forward_m;
    candidate.center.lateral_m = center.point.lateral_m;
    candidate.support_rank = support_rank;
    return true;
}

// 计算锚定候选与上一帧观测的连续性得分（横向跳变越小越高）
float CandidateContinuityScore(const AnchorCandidate& candidate,
                               const LaneObservation* previous_observation,
                               const port::BEVCorridorGraphParameters& graph_params) {
    if (!candidate.valid || previous_observation == nullptr || !previous_observation->usable) {
        return 0.0F;
    }
    const float jump =
        std::abs(candidate.center.lateral_m - previous_observation->center_m);
    const float normalized =
        1.0F - jump / std::max(1e-4F, graph_params.max_center_jump_m);
    return std::clamp(normalized, 0.0F, 1.0F);
}

// 计算锚定候选与降级观测中心的中点贴近得分
// 用于在没有先验时的默认选择依据
float CandidateMidpointScore(const AnchorCandidate& candidate,
                             const LaneObservation& fallback,
                             const port::BEVCorridorGraphParameters& graph_params) {
    if (!candidate.valid) {
        return 0.0F;
    }
    const float half_width = std::max(0.1F, graph_params.nominal_lane_width_m * 0.5F);
    const float delta = std::abs(candidate.center.lateral_m - fallback.center_m);
    return std::clamp(1.0F - delta / half_width, 0.0F, 1.0F);
}

// 从左右两个锚定候选中选择更优的一个
// 选择策略（按优先级）：
// 1. 只有一个有效 → 强制选择
// 2. 有 committed 侧 + 对侧连续性显著更好 → 切换
// 3. 综合评分（支撑等级 + 连续性 + 中点贴近度）
// 4. 平局时沿用上一帧的锚定侧
const AnchorCandidate* SelectAnchorCandidate(
    const AnchorCandidate& left_candidate,
    const AnchorCandidate& right_candidate,
    const LaneObservation* previous_observation,
    BoundaryAnchorSide committed_anchor_side,
    BoundaryAnchorSide previous_anchor_side,
    const LaneObservation& fallback,
    const port::BEVCorridorGraphParameters& graph_params,
    bool& forced_choice) {
    forced_choice = false;
    if (left_candidate.valid && !right_candidate.valid) {
        forced_choice = true;
        return &left_candidate;
    }
    if (!left_candidate.valid && right_candidate.valid) {
        forced_choice = true;
        return &right_candidate;
    }
    if (!left_candidate.valid && !right_candidate.valid) {
        return nullptr;
    }

    const AnchorCandidate* committed_candidate = nullptr;
    const AnchorCandidate* opposite_candidate = nullptr;
    if (committed_anchor_side == BoundaryAnchorSide::kLeft) {
        committed_candidate = left_candidate.valid ? &left_candidate : nullptr;
        opposite_candidate = right_candidate.valid ? &right_candidate : nullptr;
    } else if (committed_anchor_side == BoundaryAnchorSide::kRight) {
        committed_candidate = right_candidate.valid ? &right_candidate : nullptr;
        opposite_candidate = left_candidate.valid ? &left_candidate : nullptr;
    }
    if (committed_candidate != nullptr && opposite_candidate != nullptr) {
        const float committed_continuity =
            CandidateContinuityScore(*committed_candidate, previous_observation, graph_params);
        const float opposite_continuity =
            CandidateContinuityScore(*opposite_candidate, previous_observation, graph_params);
        if (opposite_continuity > committed_continuity + 0.35F) {
            return opposite_candidate;
        }
        return committed_candidate;
    }

    const float left_score =
        static_cast<float>(left_candidate.support_rank) * 2.0F +
        CandidateContinuityScore(left_candidate, previous_observation, graph_params) +
        0.25F * CandidateMidpointScore(left_candidate, fallback, graph_params);
    const float right_score =
        static_cast<float>(right_candidate.support_rank) * 2.0F +
        CandidateContinuityScore(right_candidate, previous_observation, graph_params) +
        0.25F * CandidateMidpointScore(right_candidate, fallback, graph_params);
    if (std::abs(left_score - right_score) > 1e-4F) {
        return left_score > right_score ? &left_candidate : &right_candidate;
    }
    if (previous_anchor_side == BoundaryAnchorSide::kLeft) {
        return &left_candidate;
    }
    if (previous_anchor_side == BoundaryAnchorSide::kRight) {
        return &right_candidate;
    }
    return &left_candidate;
}

// 应用法线锚定中心推理 —— 利用观测到的边界轨迹沿法线推算车道中心
// 如果降级观测不允许法线锚定（normal_anchor_allowed=false），直接返回降级观测
// 否则在左右候选中选择最优者，用其法线偏移位置作为最终中心
LaneObservation ApplyNormalCenterInference(
    const BoundaryTraces& boundary_traces,
    std::size_t layer,
    const LaneObservation& fallback,
    const port::BEVCorridorGraphParameters& graph_params,
    const LaneObservation* previous_observation,
    BoundaryAnchorSide committed_anchor_side,
    BoundaryAnchorSide previous_anchor_side,
    BoundaryAnchorSide& anchor_side,
    bool& forced_choice) {
    AnchorCandidate left_candidate{};
    AnchorCandidate right_candidate{};
    anchor_side = BoundaryAnchorSide::kNone;
    if (!fallback.normal_anchor_allowed) {
        forced_choice = false;
        return fallback;
    }
    (void)MakeNormalCenterCandidate(
        boundary_traces.left, layer, graph_params, true, left_candidate);
    (void)MakeNormalCenterCandidate(
        boundary_traces.right, layer, graph_params, false, right_candidate);
    const AnchorCandidate* candidate = SelectAnchorCandidate(left_candidate,
                                                             right_candidate,
                                                             previous_observation,
                                                             committed_anchor_side,
                                                             previous_anchor_side,
                                                             fallback,
                                                             graph_params,
                                                             forced_choice);
    if (candidate == nullptr) {
        return fallback;
    }

    LaneObservation observation = fallback;
    observation.center_forward_m = candidate->center.forward_m;
    observation.center_m = candidate->center.lateral_m;
    anchor_side = candidate->side;
    return observation;
}

// 构建全链车道观测 —— 遍历所有层，为每个选中间隔生成带法线锚定的观测
// 同时维护 committed_anchor_side（非强制选择时提交的锚定侧）和
// previous_anchor_side（上一层的锚定侧）用于候选选择的连续性
std::array<LaneObservation, port::kBevTrackSampleCount> BuildChainObservations(
    const CorridorIntervalSet& intervals,
    const std::array<int, port::kBevTrackSampleCount>& indices,
    const port::BEVCorridorGraphParameters& graph_params,
    std::array<BoundaryAnchorSide, port::kBevTrackSampleCount>* anchor_sides) {
    std::array<LaneObservation, port::kBevTrackSampleCount> observations{};
    if (anchor_sides != nullptr) {
        anchor_sides->fill(BoundaryAnchorSide::kNone);
    }
    const BoundaryTraces boundary_traces = BuildBoundaryTraces(intervals, indices, graph_params);
    LaneObservation previous_observation{};
    const LaneObservation* previous_observation_ptr = nullptr;
    BoundaryAnchorSide committed_anchor_side = BoundaryAnchorSide::kNone;
    BoundaryAnchorSide previous_anchor_side = BoundaryAnchorSide::kNone;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const port::CorridorInterval* interval = SelectedIntervalAt(intervals, indices, layer);
        if (interval == nullptr) {
            continue;
        }
        const LaneObservation fallback =
            ObserveLaneWithEdges(*interval,
                                 graph_params,
                                 boundary_traces.left[layer].valid,
                                 boundary_traces.right[layer].valid);
        BoundaryAnchorSide anchor_side = BoundaryAnchorSide::kNone;
        bool forced_choice = false;
        observations[layer] = ApplyNormalCenterInference(boundary_traces,
                                                         layer,
                                                         fallback,
                                                         graph_params,
                                                         previous_observation_ptr,
                                                         committed_anchor_side,
                                                         previous_anchor_side,
                                                         anchor_side,
                                                         forced_choice);
        if (anchor_sides != nullptr) {
            (*anchor_sides)[layer] = anchor_side;
        }
        if (observations[layer].usable) {
            previous_observation = observations[layer];
            previous_observation_ptr = &previous_observation;
            previous_anchor_side = anchor_side;
            if (!forced_choice && anchor_side != BoundaryAnchorSide::kNone) {
                committed_anchor_side = anchor_side;
            }
        }
    }
    return observations;
}

// 判断走廊间隔是否可用作图节点（必须 usable + 宽度在有效范围内）
bool IntervalAllowed(const port::CorridorInterval& interval,
                     const port::BEVCorridorGraphParameters& graph_params) {
    const LaneObservation observation = ObserveLane(interval, graph_params);
    return observation.usable &&
           interval.width_m >= graph_params.min_interval_width_m &&
           interval.width_m <= graph_params.max_interval_width_m;
}

// 生成普通路段的走廊图参数 —— 从 RuntimeParameters 提取并合并 bev_geometry 约束
port::BEVCorridorGraphParameters OrdinaryGraphParams(const port::RuntimeParameters& params) {
    port::BEVCorridorGraphParameters graph_params = params.bev_corridor_graph;
    if (graph_params.nominal_lane_width_m <= 0.0F) {
        graph_params.nominal_lane_width_m = params.bev_geometry.nominal_lane_width_m;
    }
    if (params.bev_geometry.min_lane_width_m > 0.0F) {
        graph_params.min_interval_width_m =
            std::max(graph_params.min_interval_width_m, params.bev_geometry.min_lane_width_m);
    }
    if (params.bev_geometry.max_lane_width_m > 0.0F) {
        graph_params.max_interval_width_m =
            std::min(graph_params.max_interval_width_m, params.bev_geometry.max_lane_width_m);
    }
    return graph_params;
}

// 计算图节点的得分 —— 综合置信度、中心偏离度、宽度与标称宽度匹配度
// 节点分用于动态规划搜索中的候选排序
float NodeScore(const LaneObservation& observation,
                const port::BEVCorridorGraphParameters& graph_params) {
    const float center_score =
        std::clamp(1.0F - std::abs(observation.center_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m * 2.0F),
                   0.0F,
                   1.0F);
    const float width_score =
        std::clamp(1.0F - std::abs(observation.lane_width_m - graph_params.nominal_lane_width_m) /
                             std::max(0.1F, graph_params.nominal_lane_width_m),
                   0.0F,
                   1.0F);
    return observation.confidence + 0.35F * center_score + 0.25F * width_score;
}

// 构建跨层连接边 —— 计算两个间隔之间的 overlap / center_jump / width_change / confidence
// 边用于 DP 搜索中的跃迁成本/收益计算
port::CorridorGraphEdge BuildEdge(int from_layer,
                                  int from_interval,
                                  int to_layer,
                                  int to_interval,
                                  const port::CorridorInterval& from,
                                  const port::CorridorInterval& to,
                                  const port::BEVCorridorGraphParameters& graph_params) {
    const LaneObservation from_observation = ObserveLane(from, graph_params);
    const LaneObservation to_observation = ObserveLane(to, graph_params);
    port::CorridorGraphEdge edge{};
    edge.from_layer = from_layer;
    edge.from_interval = from_interval;
    edge.to_layer = to_layer;
    edge.to_interval = to_interval;
    edge.overlap_score = IntervalOverlapScore(from, to);
    edge.center_jump_cost = std::abs(to_observation.center_m - from_observation.center_m);
    edge.width_change_cost = std::abs(to_observation.lane_width_m - from_observation.lane_width_m);
    edge.curvature_cost = 0.0F;
    edge.confidence =
        std::clamp((from_observation.confidence + to_observation.confidence) * 0.5F, 0.0F, 1.0F);
    return edge;
}

// 计算两个路径点之间的航向角（atan2），用于远侧航向误差
float ComputeHeading(const port::BEVPathSample& near_sample, const port::BEVPathSample& far_sample) {
    const float delta_forward = far_sample.point.forward_m - near_sample.point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 0.0F;
    }
    return std::atan2(far_sample.point.lateral_m - near_sample.point.lateral_m, delta_forward);
}

// 将 int 索引限幅到合法的 size_t 范围 [0, kBevTrackSampleCount)
std::size_t ClampSampleIndex(int index) {
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, index)),
        port::kBevTrackSampleCount - 1U);
}

// 检查指定层是否有被选中的间隔（索引 >= 0）
bool HasSelectedIntervalAt(const std::array<int, port::kBevTrackSampleCount>& indices,
                           std::size_t layer_index) {
    return layer_index < indices.size() && indices[layer_index] >= 0;
}

// 裁剪路径到选中间隔所覆盖的前向范围之外的点（法线外推可能超出支撑区间）
// 超过选中层范围的点是推理伪影，予以移除
int PrunePathToSelectedSupportHorizon(
    std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    const std::array<int, port::kBevTrackSampleCount>& indices) {
    std::size_t first_selected_layer = port::kBevTrackSampleCount;
    std::size_t last_selected_layer = port::kBevTrackSampleCount;
    for (std::size_t layer = 0; layer < indices.size(); ++layer) {
        if (HasSelectedIntervalAt(indices, layer)) {
            first_selected_layer = std::min(first_selected_layer, layer);
            last_selected_layer = layer;
        }
    }
    int valid_count = 0;
    for (std::size_t layer = 0; layer < path.size(); ++layer) {
        // Normal-offset fitting can project samples outside the corridor
        // support span. Those samples remain inference artifacts, not
        // observed path support.
        if (last_selected_layer == port::kBevTrackSampleCount ||
            layer < first_selected_layer ||
            layer > last_selected_layer) {
            path[layer] = {};
            continue;
        }
        if (path[layer].valid) {
            ++valid_count;
        }
    }
    return valid_count;
}

// 检查从指定层开始往后是否有任何选中间隔
bool HasSelectedIntervalAtOrBeyond(const std::array<int, port::kBevTrackSampleCount>& indices,
                                   std::size_t layer_index) {
    for (std::size_t index = layer_index; index < indices.size(); ++index) {
        if (HasSelectedIntervalAt(indices, index)) {
            return true;
        }
    }
    return false;
}

// 填充路径候选摘要：
// 1. 构建全链观测（BuildChainObservations）
// 2. 提取中心线采样点
// 3. 归一化到标准前向网格（NormalizePathToForwardSamples）
// 4. 裁剪支撑范围外的外推点
// 5. 计算统计量（置信度、平均宽度、宽度稳定性、曲率）
void FillCandidateSummary(port::PathCandidate& candidate,
                          const CorridorIntervalSet& intervals,
                          const std::array<int, port::kBevTrackSampleCount>& indices,
                          const port::RuntimeParameters& params,
                          const port::BEVCorridorGraphParameters& graph_params,
                          std::array<port::BEVPathSample, port::kBevTrackSampleCount>* raw_centerline,
                          std::array<BoundaryAnchorSide, port::kBevTrackSampleCount>* anchor_sides) {
    const std::array<LaneObservation, port::kBevTrackSampleCount> observations =
        BuildChainObservations(intervals, indices, graph_params, anchor_sides);
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float width_sum = 0.0F;
    float width_sq_sum = 0.0F;
    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* mid = nullptr;
    const port::BEVPathSample* last = nullptr;

    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int interval_index = indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const LaneObservation& observation = observations[layer];
        if (!observation.usable) {
            continue;
        }
        candidate.sampled_path[layer] =
            MakePathSample(observation.center_forward_m, observation.center_m, observation.confidence);
        if (raw_centerline != nullptr) {
            (*raw_centerline)[layer] = candidate.sampled_path[layer];
        }
        confidence_sum += observation.confidence;
        width_sum += observation.lane_width_m;
        width_sq_sum += observation.lane_width_m * observation.lane_width_m;
        if (first == nullptr) {
            first = &candidate.sampled_path[layer];
            candidate.start_forward_m = observation.center_forward_m;
        }
        mid = last;
        last = &candidate.sampled_path[layer];
        candidate.end_forward_m = observation.center_forward_m;
        ++valid_count;
    }

    const float sample_spacing =
        std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                 params.bev_topology_sampler.forward_samples_m[0]);
    const auto direct_observation_path = candidate.sampled_path;
    const float max_forward_extrapolate =
        std::max(sample_spacing * 0.75F,
                 std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                          std::abs(params.bev_geometry.lateral_step_m)) *
                     2.0F);
    (void)NormalizePathToForwardSamples(candidate.sampled_path,
                                        params.bev_topology_sampler.forward_samples_m,
                                        max_forward_extrapolate,
                                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                 std::abs(params.bev_geometry.lateral_step_m)) *
                                            2.0F);
    int normalized_count = PrunePathToSelectedSupportHorizon(candidate.sampled_path, indices);
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        if (candidate.sampled_path[layer].valid ||
            !direct_observation_path[layer].valid ||
            !HasSelectedIntervalAt(indices, layer)) {
            continue;
        }
        const int interval_index = indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const port::CorridorInterval& interval =
            intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
        if (!interval.left_edge_valid || !interval.right_edge_valid) {
            continue;
        }
        candidate.sampled_path[layer] = direct_observation_path[layer];
        candidate.sampled_path[layer].point.forward_m =
            params.bev_topology_sampler.forward_samples_m[layer];
        ++normalized_count;
    }
    candidate.valid = normalized_count >= 3;
    if (!candidate.valid) {
        return;
    }
    candidate.confidence = std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F);
    candidate.mean_width_m = width_sum / static_cast<float>(valid_count);
    const float width_variance =
        std::max(0.0F, width_sq_sum / static_cast<float>(valid_count) -
                           candidate.mean_width_m * candidate.mean_width_m);
    const float width_stddev = std::sqrt(width_variance);
    candidate.width_stability =
        std::clamp(1.0F - width_stddev / std::max(1e-4F, candidate.mean_width_m), 0.0F, 1.0F);
    first = nullptr;
    mid = nullptr;
    last = nullptr;
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
            candidate.start_forward_m = sample.point.forward_m;
        }
        mid = last;
        last = &sample;
        candidate.end_forward_m = sample.point.forward_m;
    }
    if (first != nullptr && mid != nullptr && last != nullptr && first != last && mid != last) {
        candidate.curvature = PathCurvatureFromThreeSamples(*first, *mid, *last);
        candidate.curvature_consistency =
            std::clamp(1.0F - std::abs(candidate.curvature) /
                                 std::max(1e-4F, graph_params.max_curvature_abs),
                       0.0F,
                       1.0F);
    }
}

// 清空轨迹估计的几何信息（将所有数组置零/无效）
// 用于降级或无效路径时的安全清理
void ClearPublishedTrackGeometry(port::BEVTrackEstimate& track) {
    track.sampled_left_boundary.fill({});
    track.sampled_centerline.fill({});
    track.sampled_right_boundary.fill({});
    track.sampled_drivable_left_boundary.fill({});
    track.sampled_drivable_right_boundary.fill({});
    track.lane_width_profile_m.fill(0.0F);
    track.drivable_width_profile_m.fill(0.0F);
    track.visible_range_m = 0.0F;
    track.track_confidence = 0.0F;
    track.near_lateral_error = 0.0F;
    track.far_heading_error = 0.0F;
    track.preview_curvature = 0.0F;
}

}  // namespace

// ========== 主入口：构建走廊图 ==========
// 使用 Viterbi 式动态规划搜索最优跨层间隔链：
// 阶段 1：初始化 — 为每层每个合法间隔计算节点得分（含先验承载分）
// 阶段 2：向前传播 — 跨层连接，计算跃迁得分，记录最优前驱
// 阶段 3：回溯 — 找到全局最优路径，填充 graph.ordinary_interval_indices
// 阶段 4：填充候选摘要（中心线归一化 + 统计量）
// 阶段 5：有效性验证（最少 3 个有效采样点 + 控制水平线必须存在）
CorridorGraph BuildCorridorGraph(const CorridorIntervalSet& intervals,
                                 const port::RuntimeParameters& params,
                                 const port::LegacySteeringState& prior_state) {
    CorridorGraph graph{};
    graph.ordinary_interval_indices.fill(-1);
    graph.ordinary_center_anchor_side.fill(BoundaryAnchorSide::kNone);
    graph.ordinary_raw_centerline.fill({});
    if (!intervals.valid) {
        return graph;
    }

    const port::BEVCorridorGraphParameters graph_params = OrdinaryGraphParams(params);

    // scores[layer][i] = 到达第 layer 层第 i 个间隔的最大累积得分
    // predecessors[layer][i] = 到达该最优路径的前一层间隔索引
    std::array<std::vector<float>, port::kBevTrackSampleCount> scores{};
    std::array<std::vector<int>, port::kBevTrackSampleCount> predecessors{};
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const std::size_t count = intervals.layers[layer].intervals.size();
        scores[layer].assign(count, kNegativeInfinity);
        predecessors[layer].assign(count, -1);
        for (std::size_t interval_index = 0; interval_index < count; ++interval_index) {
            const port::CorridorInterval& interval = intervals.layers[layer].intervals[interval_index];
            if (!IntervalAllowed(interval, graph_params)) {
                continue;
            }
            const LaneObservation observation = ObserveLane(interval, graph_params);
            const float base_score = NodeScore(observation, graph_params) +
                                     PriorCarryScore(prior_state, layer, observation, graph_params);
            scores[layer][interval_index] = base_score;
        }
    }

    // 阶段 2：向前传播 —— 跨层连接，计算跃迁得分，记录最优前驱
    // 对每层的每个合法间隔，尝试与上一层所有合法间隔连接
    // 跃迁分 = edge.confidence + edge.overlap - center_jump_cost - 0.5*width_change_cost
    for (std::size_t layer = 1; layer < port::kBevTrackSampleCount; ++layer) {
        const CorridorIntervalLayer& previous_layer = intervals.layers[layer - 1U];
        const CorridorIntervalLayer& current_layer = intervals.layers[layer];
        for (std::size_t to_index = 0; to_index < current_layer.intervals.size(); ++to_index) {
            if (scores[layer][to_index] <= kNegativeInfinity * 0.5F) {
                continue;
            }
            const port::CorridorInterval& to = current_layer.intervals[to_index];
            float best_score = scores[layer][to_index];
            int best_predecessor = -1;
            for (std::size_t from_index = 0; from_index < previous_layer.intervals.size(); ++from_index) {
                if (scores[layer - 1U][from_index] <= kNegativeInfinity * 0.5F) {
                    continue;
                }
                const port::CorridorInterval& from = previous_layer.intervals[from_index];
                port::CorridorGraphEdge edge = BuildEdge(static_cast<int>(layer - 1U),
                                                         static_cast<int>(from_index),
                                                         static_cast<int>(layer),
                                                         static_cast<int>(to_index),
                                                         from,
                                                         to,
                                                         graph_params);
                if (edge.center_jump_cost > graph_params.max_center_jump_m ||
                    edge.width_change_cost > graph_params.max_width_change_m) {
                    continue;
                }
                const float transition_score = edge.confidence + edge.overlap_score -
                                               edge.center_jump_cost / graph_params.max_center_jump_m -
                                               0.5F * edge.width_change_cost /
                                                   std::max(1e-4F, graph_params.max_width_change_m);
                const float candidate_score = scores[layer - 1U][from_index] +
                                              scores[layer][to_index] + transition_score;
                graph.edges.push_back(edge);
                if (candidate_score > best_score) {
                    best_score = candidate_score;
                    best_predecessor = static_cast<int>(from_index);
                }
            }
            scores[layer][to_index] = best_score;
            predecessors[layer][to_index] = best_predecessor;
        }
    }

    // 阶段 3：回溯 —— 在最终层找到全局最优（累积得分最高的节点），反向追溯整条路径
    float best_score = kNegativeInfinity;
    int best_layer = -1;
    int best_interval = -1;
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        for (std::size_t interval_index = 0; interval_index < scores[layer].size(); ++interval_index) {
            if (scores[layer][interval_index] > best_score) {
                best_score = scores[layer][interval_index];
                best_layer = static_cast<int>(layer);
                best_interval = static_cast<int>(interval_index);
            }
        }
    }

    // 反向回溯：从最优终点沿 predecessor 链回推到起点，填充各层选中的间隔索引
    while (best_layer >= 0 && best_interval >= 0) {
        graph.ordinary_interval_indices[static_cast<std::size_t>(best_layer)] = best_interval;
        const int previous = predecessors[static_cast<std::size_t>(best_layer)]
                                          [static_cast<std::size_t>(best_interval)];
        --best_layer;
        best_interval = previous;
    }

    // 阶段 4：填充候选摘要（中心线归一化到标准前向网格 + 统计量）
    graph.ordinary.mode = port::ReferenceMode::kCenterline;
    FillCandidateSummary(graph.ordinary,
                         intervals,
                         graph.ordinary_interval_indices,
                         params,
                         graph_params,
                         &graph.ordinary_raw_centerline,
                         &graph.ordinary_center_anchor_side);
    // 阶段 5：有效性验证 —— 候选必须有效且控制水平线（曲率采样点）存在
    const std::size_t curvature_index = ClampSampleIndex(params.bev_control_model.curvature_sample_index);
    if (!graph.ordinary.valid) {
        graph.fallback_mode = "insufficient_corridor_samples";
    } else if (!HasSelectedIntervalAtOrBeyond(graph.ordinary_interval_indices, curvature_index)) {
        graph.ordinary.valid = false;
        graph.fallback_mode = "control_horizon_missing";
    }
    graph.valid = graph.ordinary.valid;
    return graph;
}

// ========== 从走廊图生成最终轨迹估计 ==========
// 将 BuildCorridorGraph 选出的最优路径链转换为 BEVTrackEstimate：
// 1. 复制 centerline 采样点，提取左右边界和 drivable 边界
// 2. 计算近侧横向误差、远侧航向误差、预览曲率
// 3. 检查置信度和可见范围，决定是否需要降级
port::BEVTrackEstimate BuildBEVTrackEstimateFromCorridorGraph(const CorridorIntervalSet& intervals,
                                                              const CorridorGraph& graph,
                                                              const port::RuntimeParameters& params) {
    port::BEVTrackEstimate track{};
    track.calibration_valid = true;
    track.continuity_valid = graph.valid;
    track.source = "bev_corridor_topology";
    if (!graph.valid) {
        track.fallback_mode =
            graph.fallback_mode.empty() || graph.fallback_mode == "none" ? "no_corridor_chain"
                                                                         : graph.fallback_mode;
        return track;
    }

    // 步骤 1：复制中心线采样点，计算可见范围
    int valid_count = 0;
    float confidence_sum = 0.0F;
    const port::BEVCorridorGraphParameters graph_params = OrdinaryGraphParams(params);
    const std::array<LaneObservation, port::kBevTrackSampleCount> observations =
        BuildChainObservations(intervals, graph.ordinary_interval_indices, graph_params, nullptr);
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        if (!graph.ordinary.sampled_path[layer].valid) {
            continue;
        }
        track.sampled_centerline[layer] = graph.ordinary.sampled_path[layer];
        track.visible_range_m =
            std::max(track.visible_range_m, graph.ordinary.sampled_path[layer].point.forward_m);
    }
    // 步骤 2：提取左右边界和 drivable 边界，累加车道宽度和置信度
    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const int interval_index = graph.ordinary_interval_indices[layer];
        if (interval_index < 0 ||
            interval_index >= static_cast<int>(intervals.layers[layer].intervals.size())) {
            continue;
        }
        const port::CorridorInterval& interval =
            intervals.layers[layer].intervals[static_cast<std::size_t>(interval_index)];
        const LaneObservation& observation = observations[layer];
        if (!observation.usable) {
            continue;
        }
        if (observation.left_boundary_observed) {
            track.sampled_left_boundary[layer] =
                MakePathSample(interval.forward_m, interval.lateral_min_m, observation.confidence);
        }
        if (observation.right_boundary_observed) {
            track.sampled_right_boundary[layer] =
                MakePathSample(interval.forward_m, interval.lateral_max_m, observation.confidence);
        }
        track.sampled_drivable_left_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_min_m, interval.confidence);
        track.sampled_drivable_right_boundary[layer] =
            MakePathSample(interval.forward_m, interval.lateral_max_m, interval.confidence);
        track.lane_width_profile_m[layer] = observation.lane_width_m;
        track.drivable_width_profile_m[layer] = interval.width_m;
        confidence_sum += observation.confidence;
        ++valid_count;
    }

    // 步骤 3：计算控制误差（近侧横向误差、远侧航向误差、预览曲率）
    const std::size_t near_index = ClampSampleIndex(params.bev_control_model.near_sample_index);
    track.valid = valid_count >= 3;
    track.track_confidence =
        valid_count > 0 ? std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F) : 0.0F;
    if (!track.valid) {
        track.fallback_mode = "insufficient_corridor_samples";
        ClearPublishedTrackGeometry(track);
        return track;
    }

    std::size_t first_index = port::kBevTrackSampleCount;
    std::size_t far_index = near_index;
    for (std::size_t index = 0; index < port::kBevTrackSampleCount; ++index) {
        if (track.sampled_centerline[index].valid) {
            first_index = std::min(first_index, index);
            far_index = index;
        }
    }
    if (near_index < port::kBevTrackSampleCount && track.sampled_centerline[near_index].valid) {
        track.near_lateral_error = track.sampled_centerline[near_index].point.lateral_m;
    }
    if (near_index < far_index && track.sampled_centerline[near_index].valid &&
        track.sampled_centerline[far_index].valid) {
        track.far_heading_error =
            ComputeHeading(track.sampled_centerline[near_index], track.sampled_centerline[far_index]);
    }
    const std::size_t curvature_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    if (first_index < port::kBevTrackSampleCount && near_index < curvature_index &&
        track.sampled_centerline[first_index].valid && track.sampled_centerline[near_index].valid &&
        track.sampled_centerline[curvature_index].valid) {
        track.preview_curvature =
            PathCurvatureFromThreeSamples(track.sampled_centerline[first_index],
                                          track.sampled_centerline[near_index],
                                          track.sampled_centerline[curvature_index]);
    }
    if (track.track_confidence < params.bev_geometry.min_track_confidence) {
        track.fallback_mode = "low_confidence";
    } else if (track.visible_range_m < params.bev_geometry.min_visible_range_m) {
        track.fallback_mode = "short_visible_range";
    }
    return track;
}

}  // namespace ls2k::legacy
