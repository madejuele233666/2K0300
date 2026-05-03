#include "legacy/steering_topology_evidence.hpp"

// 拓扑证据评分实现。
// 对道路假设进行 5 个维度的评分（ordinary/cross/circle/zebra/lost），
// 并维护时序累积器（EMA）用于平滑评分波动。
// 评分考虑了：走廊间隔统计、边界观测质量、开口/膨胀对称性、曲率否决、
// 斑马线纹理模式、元素证据融合等。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

// 值限幅到 [0, 1] 区间
float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

// 间隔证据汇总结构 —— 从所有层的最佳间隔中提取关键统计数据
struct IntervalEvidenceSummary {
    bool valid = false;
    float near_left = 0.0F;
    float near_right = 0.0F;
    float near_width = 0.0F;
    float max_width = 0.0F;
    float left_opening = 0.0F;
    float right_opening = 0.0F;
    float left_observed_ratio = 0.0F;
    float right_observed_ratio = 0.0F;
    float invalid_edge_penalty = 0.0F;
    float confidence = 0.0F;
    int valid_layers = 0;
};

// 在指定层中找置信度最高的间隔（不做其他过滤）
const port::CorridorInterval* BestInterval(const CorridorIntervalLayer& layer) {
    const port::CorridorInterval* best = nullptr;
    for (const port::CorridorInterval& interval : layer.intervals) {
        if (best == nullptr || interval.confidence > best->confidence) {
            best = &interval;
        }
    }
    return best;
}

// 判断间隔是否可作为可靠的开口锚点（宽度在有效范围内 + 置信度 + 采样比例达标）
bool ReliableOpeningAnchor(const port::CorridorInterval& interval,
                           const port::BEVCorridorGraphParameters& graph_params) {
    const float nominal_width = std::max(1e-4F, graph_params.nominal_lane_width_m);
    const float minimum_width = std::max(graph_params.min_interval_width_m, nominal_width * 0.70F);
    const float maximum_width = std::max(minimum_width, graph_params.max_interval_width_m);
    return interval.width_m >= minimum_width &&
           interval.width_m <= maximum_width &&
           interval.confidence >= 0.45F &&
           interval.valid_sample_ratio >= 0.35F;
}

// 汇总所有层的间隔统计信息
// 选取开口锚点层，计算近端宽度、最大宽度、左右开口量、
// 边界观测比例、无效边惩罚和平均置信度
IntervalEvidenceSummary SummarizeIntervals(const CorridorIntervalSet& intervals,
                                           const port::BEVCorridorGraphParameters& graph_params) {
    IntervalEvidenceSummary summary{};
    std::array<const port::CorridorInterval*, port::kBevTrackSampleCount> best_by_layer{};
    const port::CorridorInterval* fallback_anchor = nullptr;
    std::size_t fallback_layer = 0U;
    const port::CorridorInterval* opening_anchor = nullptr;
    std::size_t opening_anchor_layer = 0U;

    for (std::size_t layer_index = 0; layer_index < intervals.layers.size(); ++layer_index) {
        const port::CorridorInterval* best = BestInterval(intervals.layers[layer_index]);
        best_by_layer[layer_index] = best;
        if (best == nullptr) {
            continue;
        }
        if (fallback_anchor == nullptr) {
            fallback_anchor = best;
            fallback_layer = layer_index;
        }
        if (opening_anchor == nullptr && ReliableOpeningAnchor(*best, graph_params)) {
            opening_anchor = best;
            opening_anchor_layer = layer_index;
        }
    }

    if (opening_anchor == nullptr) {
        opening_anchor = fallback_anchor;
        opening_anchor_layer = fallback_layer;
    }
    if (opening_anchor == nullptr) {
        return summary;
    }

    summary.valid = true;
    summary.near_left = opening_anchor->lateral_min_m;
    summary.near_right = opening_anchor->lateral_max_m;
    summary.near_width = opening_anchor->width_m;

    float confidence_sum = 0.0F;
    int edge_count = 0;
    int invalid_edges = 0;
    int left_observed_edges = 0;
    int right_observed_edges = 0;
    for (std::size_t layer_index = opening_anchor_layer; layer_index < best_by_layer.size(); ++layer_index) {
        const port::CorridorInterval* best = best_by_layer[layer_index];
        if (best == nullptr) {
            continue;
        }
        summary.max_width = std::max(summary.max_width, best->width_m);
        summary.left_opening = std::max(summary.left_opening, summary.near_left - best->lateral_min_m);
        summary.right_opening = std::max(summary.right_opening, best->lateral_max_m - summary.near_right);
        if (best->left_edge_valid) {
            ++left_observed_edges;
        } else {
            ++invalid_edges;
        }
        if (best->right_edge_valid) {
            ++right_observed_edges;
        } else {
            ++invalid_edges;
        }
        edge_count += 2;
        confidence_sum += best->confidence;
        ++summary.valid_layers;
    }
    if (summary.valid_layers > 0) {
        summary.confidence = Clamp01(confidence_sum / static_cast<float>(summary.valid_layers));
    }
    if (edge_count > 0) {
        summary.invalid_edge_penalty = Clamp01(static_cast<float>(invalid_edges) / static_cast<float>(edge_count));
    }
    if (summary.valid_layers > 0) {
        summary.left_observed_ratio =
            Clamp01(static_cast<float>(left_observed_edges) / static_cast<float>(summary.valid_layers));
        summary.right_observed_ratio =
            Clamp01(static_cast<float>(right_observed_edges) / static_cast<float>(summary.valid_layers));
    }
    return summary;
}

// 宽度膨胀评分 —— 评估道路从近端到最宽处的膨胀程度
// 用于十字路口检测（路口处道路显著变宽）
float WidthBulgeScore(const IntervalEvidenceSummary& summary,
                      const port::BEVCorridorGraphParameters& params) {
    const float reference = summary.near_width > 1e-4F ? summary.near_width : params.nominal_lane_width_m;
    const float bulge = summary.max_width - reference;
    return Clamp01(bulge / std::max(1e-4F, params.nominal_lane_width_m));
}

// 可见范围覆盖率评分 —— 候选路径的有效前向覆盖程度
float VisibleCoverageScore(const port::PathCandidate& candidate,
                           const port::RuntimeParameters& params) {
    if (!candidate.valid) {
        return 0.0F;
    }
    const float full_range_m = std::max(params.bev_topology_sampler.forward_samples_m.back(),
                                        params.bev_geometry.forward_samples_m.back());
    const float reliable_start_m = std::max(0.0F, params.bev_geometry.min_visible_range_m);
    return Clamp01((candidate.end_forward_m - reliable_start_m) /
                   std::max(1e-4F, full_range_m - reliable_start_m));
}

// 已进入场景的评分 —— 只有超过进入阈值的得分才保留（否则归零）
float EnteredSceneScore(float score, float enter_threshold) {
    return score >= enter_threshold ? score : 0.0F;
}

// 模糊场景压力计算 —— 在 release_threshold 和 enter_threshold 之间的评分产生"压力"
// 压力使 lost_score 升高（系统不确定当前场景但又不能完全忽略）
float AmbiguousScenePressure(float score, float release_threshold, float enter_threshold) {
    if (score >= enter_threshold) {
        return 0.0F;
    }
    if (score >= release_threshold) {
        return score;
    }
    const float weak_floor = release_threshold * 0.70F;
    if (score < weak_floor) {
        return 0.0F;
    }
    const float pressure =
        Clamp01(score / std::max(1e-4F, release_threshold)) * std::nextafter(enter_threshold, 0.0F);
    return std::max(release_threshold, pressure);
}

// 曲率否决的环岛压力 —— 弯曲否决强时环岛分数被放大作为压力
float CurvatureVetoedCirclePressure(float circle_score, float bend_veto_score) {
    const float curvature_scale = std::max(0.20F, 1.0F - bend_veto_score);
    return Clamp01(circle_score / curvature_scale);
}

// 未解决的曲率否决环岛评分 —— 环岛分数被否决但压力仍存时的残留评分
float UnresolvedCurvatureVetoedCircleScore(float circle_score,
                                           float bend_veto_score,
                                           float release_threshold,
                                           float enter_threshold) {
    if (circle_score >= enter_threshold) {
        return 0.0F;
    }
    const float pressure = CurvatureVetoedCirclePressure(circle_score, bend_veto_score);
    if (pressure < release_threshold) {
        return 0.0F;
    }
    return std::min(pressure, std::nextafter(enter_threshold, 0.0F));
}

// 开口平衡评分 —— 评估左右两侧开口的对称性（较小值/较大值）
float OpeningBalanceScore(float left_opening, float right_opening) {
    const float larger = std::max(left_opening, right_opening);
    if (larger <= 1e-4F) {
        return 0.0F;
    }
    return Clamp01(std::min(left_opening, right_opening) / larger);
}

// 重新获取评分 —— 候选路径有效 + 置信度达标 + 可见范围达标时返回 1.0
float ReacquireScore(const port::PathCandidate& candidate,
                     const port::RuntimeParameters& params) {
    if (!candidate.valid ||
        candidate.confidence < params.bev_reference_policy.arc_follow_confidence_min) {
        return 0.0F;
    }
    const float coverage = VisibleCoverageScore(candidate, params);
    if (coverage < static_cast<float>(params.bev_control_model.lookahead_visible_range_ratio)) {
        return 0.0F;
    }
    return 1.0F;
}

// 弧线支撑评分 —— 检查弧线候选或分支候选是否达到可信阈值
float ArcSupportScore(const port::PathCandidate& arc_candidate,
                      const port::PathCandidate& branch_candidate,
                      const port::RuntimeParameters& params) {
    const float arc_threshold =
        arc_candidate.mode == port::ReferenceMode::kStableBoundaryOffset
            ? params.bev_reference_policy.stable_boundary_confidence_min
            : params.bev_reference_policy.arc_follow_confidence_min;
    if (arc_candidate.valid &&
        arc_candidate.confidence >= arc_threshold) {
        return 1.0F;
    }
    if (branch_candidate.valid &&
        branch_candidate.confidence >= params.bev_reference_policy.stable_boundary_confidence_min) {
        return 0.75F;
    }
    return 0.0F;
}

// 近端锚点信任缩放 —— 当近端只有一个边界被观测到时降低 trust
// 如果是直道场景且缺少一侧边界，trust 受到更大折损
float NearAnchorOrdinaryTrustScale(const port::PathCandidate& ordinary,
                                   const CorridorIntervalSet& intervals,
                                   const port::RuntimeParameters& params) {
    if (!ordinary.valid) {
        return 1.0F;
    }
    const std::size_t near_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.near_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    const port::CorridorInterval* near_interval = BestInterval(intervals.layers[near_index]);
    if (near_interval == nullptr || (near_interval->left_edge_valid && near_interval->right_edge_valid)) {
        return 1.0F;
    }
    const float straight_curvature_window =
        std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs * 0.15F);
    const float straightness =
        Clamp01(1.0F - std::abs(ordinary.curvature) / straight_curvature_window);
    return 1.0F - 0.5F * straightness;
}

// 斑马线评分 —— 检测前向层中宽度交替变化（宽-窄-宽模式）
// 斑马线的典型特征是间隔宽度在前向层中反复切换（条纹状）
float ZebraStripeScore(const CorridorIntervalSet& intervals,
                       const port::RuntimeParameters& params,
                       float invalid_edge_penalty) {
    constexpr std::size_t kMaxZebraLayers = 8U;
    bool previous_wide_layer = false;
    bool have_previous = false;
    int transitions = 0;
    int wide_layers = 0;
    int inspected_layers = 0;
    const float wide_threshold = params.bev_corridor_graph.nominal_lane_width_m * 0.75F;
    for (std::size_t layer = 0; layer < intervals.layers.size() && layer < kMaxZebraLayers; ++layer) {
        bool wide_layer = false;
        for (const port::CorridorInterval& interval : intervals.layers[layer].intervals) {
            if (interval.width_m >= wide_threshold && interval.confidence >= 0.45F &&
                interval.valid_sample_ratio >= 0.45F) {
                wide_layer = true;
                break;
            }
        }
        if (wide_layer) {
            ++wide_layers;
        }
        if (have_previous && wide_layer != previous_wide_layer) {
            ++transitions;
        }
        previous_wide_layer = wide_layer;
        have_previous = true;
        ++inspected_layers;
    }
    if (inspected_layers < 4) {
        return 0.0F;
    }
    const float transition_score = Clamp01(static_cast<float>(transitions) / 3.0F);
    const float density_score = Clamp01(static_cast<float>(wide_layers) / 4.0F);
    return Clamp01(transition_score * density_score * (1.0F - 0.25F * invalid_edge_penalty));
}

}  // namespace

// ========== 主入口：拓扑证据评分 ==========
// 综合多个维度评分输入，输出当前帧的场景拓扑证据。
// 评分维度包括：
// - invalid_edge_penalty: 无效边惩罚
// - bilateral_opening_sync: 双侧开口同步度
// - ordinary_score: 普通路段置信度
// - cross_score: 十字路口得分
// - left_circle_score / right_circle_score: 环岛得分
// - zebra_score: 斑马线得分
// - lost_score: 丢失得分
port::TopologyEvidence ScoreTopologyEvidence(const port::RoadHypotheses& hypotheses,
                                             const CorridorIntervalSet& intervals,
                                             const port::VehicleContext& vehicle,
                                             const port::RuntimeParameters& params,
                                             const port::TopologyEvidenceAccumulator& prior_accumulator,
                                             const port::BEVElementEvidence* element_evidence) {
    (void)vehicle;
    (void)prior_accumulator;
    port::TopologyEvidence evidence{};
    // 汇总走廊间隔统计，作为评分基础
    const IntervalEvidenceSummary summary = SummarizeIntervals(intervals, params.bev_corridor_graph);
    // 基础证据：元素证据 + 无效边惩罚 + 开口评分 + 弯曲否决
    const bool have_element_evidence = element_evidence != nullptr && element_evidence->valid;
    if (have_element_evidence) {
        evidence.element_evidence = *element_evidence;
    }

    evidence.invalid_edge_penalty = summary.invalid_edge_penalty;
    if (have_element_evidence) {
        evidence.invalid_edge_penalty =
            std::max(evidence.invalid_edge_penalty, element_evidence->invalid_edge_penalty);
    }
    evidence.left_opening_score = summary.left_opening;
    evidence.right_opening_score = summary.right_opening;
    // 双侧开口同步度 = 较小开口 / 半车道宽
    evidence.bilateral_opening_sync =
        Clamp01(std::min(summary.left_opening, summary.right_opening) /
                std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    evidence.bend_curvature_abs = std::abs(hypotheses.ordinary.curvature);
    evidence.bend_veto_score =
        Clamp01(evidence.bend_curvature_abs / std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs));
    evidence.zebra_score = intervals.valid ? ZebraStripeScore(intervals, params, evidence.invalid_edge_penalty) : 0.0F;

    // ordinary_score：普通路段置信度
    // 综合：路径置信度 × 宽度稳定性 × 边界有效性 × 可见覆盖 × 近端信任
    if (hypotheses.ordinary.valid) {
        const float visible_coverage = VisibleCoverageScore(hypotheses.ordinary, params);
        const float near_anchor_trust =
            NearAnchorOrdinaryTrustScale(hypotheses.ordinary, intervals, params);
        evidence.ordinary_score =
            Clamp01(hypotheses.ordinary.confidence * (0.5F + 0.5F * hypotheses.ordinary.width_stability) *
                    (1.0F - 0.5F * evidence.invalid_edge_penalty) * visible_coverage *
                    near_anchor_trust);
    }
    // forward_reacquire_score：前向重新获取能力评分
    evidence.forward_reacquire_score = ReacquireScore(hypotheses.forward_exit, params);

    // cross_score：十字路口评分
    // 入口1：有元素证据（cross_band present）→ 直接用元素证据评分
    // 入口2：无元素证据 → 基于双侧开口 + 宽度膨胀 + 开口平衡 + 弯曲否决
    const float width_bulge = WidthBulgeScore(summary, params.bev_corridor_graph);
    const float bilateral_open_m = std::min(summary.left_opening, summary.right_opening);
    if (have_element_evidence) {
        if (element_evidence->cross_band.present) {
            evidence.cross_score =
                Clamp01(std::max(element_evidence->cross_band.score,
                                 params.bev_topology_evidence.cross_enter_score));
        }
    } else if (bilateral_open_m >= params.bev_scene_fsm.cross_bilateral_open_min_m) {
        const float opening_balance = OpeningBalanceScore(summary.left_opening, summary.right_opening);
        const float core_cross = std::min(width_bulge, evidence.bilateral_opening_sync);
        if (evidence.zebra_score < params.bev_topology_evidence.zebra_enter_score &&
            core_cross >= params.bev_topology_evidence.cross_release_score &&
            opening_balance >= std::max(0.5F, params.bev_topology_evidence.cross_release_score) &&
            evidence.bend_veto_score <= params.bev_topology_evidence.cross_release_score) {
            evidence.cross_score =
                Clamp01(0.60F * core_cross + 0.40F * opening_balance);
        }
    }

    // circle_score（左/右）：环岛评分
    // 入口1（有元素证据）：用 element_evidence 的 circle_corner 评分
    // 入口2（无元素证据）：基于开口 + 弧线支撑 + 对侧观测 + 弯曲否决
    const float left_open_score =
        Clamp01(summary.left_opening / std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    const float right_open_score =
        Clamp01(summary.right_opening / std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    const float left_arc_support = ArcSupportScore(hypotheses.left_arc, hypotheses.left_branch, params);
    const float right_arc_support = ArcSupportScore(hypotheses.right_arc, hypotheses.right_branch, params);
    if (have_element_evidence) {
        if (!element_evidence->cross_band.present) {
            const bool left_corner_present = element_evidence->left_circle_corner.present;
            const bool right_corner_present = element_evidence->right_circle_corner.present;
            const float corner_dominance_margin = 0.25F;
            const bool unilateral_left =
                left_corner_present &&
                (!right_corner_present ||
                 element_evidence->left_circle_corner.score >=
                     element_evidence->right_circle_corner.score + corner_dominance_margin);
            const bool unilateral_right =
                right_corner_present &&
                (!left_corner_present ||
                 element_evidence->right_circle_corner.score >=
                     element_evidence->left_circle_corner.score + corner_dominance_margin);
            if (unilateral_left) {
                evidence.left_circle_score =
                    Clamp01(std::max(params.bev_topology_evidence.circle_enter_score,
                                     element_evidence->left_circle_corner.score *
                                         (1.0F - 0.5F * element_evidence->left_circle_corner.invalid_penalty)));
            }
            if (unilateral_right) {
                evidence.right_circle_score =
                    Clamp01(std::max(params.bev_topology_evidence.circle_enter_score,
                                     element_evidence->right_circle_corner.score *
                                         (1.0F - 0.5F * element_evidence->right_circle_corner.invalid_penalty)));
            }
        }
    } else {
        evidence.left_circle_score =
            Clamp01(std::max(left_open_score, hypotheses.left_branch.confidence) *
                    left_arc_support *
                    summary.right_observed_ratio *
                    (1.0F - evidence.bilateral_opening_sync) *
                    (1.0F - evidence.bend_veto_score) *
                    (1.0F - 0.5F * evidence.invalid_edge_penalty));
        evidence.right_circle_score =
            Clamp01(std::max(right_open_score, hypotheses.right_branch.confidence) *
                    right_arc_support *
                    summary.left_observed_ratio *
                    (1.0F - evidence.bilateral_opening_sync) *
                    (1.0F - evidence.bend_veto_score) *
                    (1.0F - 0.5F * evidence.invalid_edge_penalty));
    }

    // lost_score：丢失评分 = 1 - 所有已知场景中的最高得分
    // 当 ordinary 分数低于 release 阈值时，未解决的特殊场景压力会提高 lost_score
    const float best_known =
        std::max({evidence.ordinary_score,
                  EnteredSceneScore(evidence.cross_score, params.bev_topology_evidence.cross_enter_score),
                  EnteredSceneScore(evidence.left_circle_score, params.bev_topology_evidence.circle_enter_score),
                  EnteredSceneScore(evidence.right_circle_score, params.bev_topology_evidence.circle_enter_score),
                  EnteredSceneScore(evidence.zebra_score, params.bev_topology_evidence.zebra_enter_score)});
    evidence.lost_score = Clamp01(1.0F - best_known);
    if (evidence.ordinary_score < params.bev_topology_evidence.ordinary_release_score) {
        const float unresolved_special =
            std::max({AmbiguousScenePressure(evidence.cross_score,
                                             params.bev_topology_evidence.cross_release_score,
                                             params.bev_topology_evidence.cross_enter_score),
                      UnresolvedCurvatureVetoedCircleScore(evidence.left_circle_score,
                                                           evidence.bend_veto_score,
                                                           params.bev_topology_evidence.circle_release_score,
                                                           params.bev_topology_evidence.circle_enter_score),
                      UnresolvedCurvatureVetoedCircleScore(evidence.right_circle_score,
                                                           evidence.bend_veto_score,
                                                           params.bev_topology_evidence.circle_release_score,
                                                           params.bev_topology_evidence.circle_enter_score),
                      AmbiguousScenePressure(evidence.zebra_score,
                                             params.bev_topology_evidence.zebra_release_score,
                                             params.bev_topology_evidence.zebra_enter_score)});
        evidence.lost_score = std::max(evidence.lost_score, unresolved_special);
    }
    return evidence;
}

// ========== 更新拓扑证据累积器（EMA 滤波） ==========
// 对每个评分维度做指数移动平均：next = prior * keep + current * add
// 平滑评分波动，防止场景状态在帧间频繁抖动
port::TopologyEvidenceAccumulator UpdateTopologyEvidenceAccumulator(
    const port::TopologyEvidence& evidence,
    const port::RuntimeParameters& params,
    const port::TopologyEvidenceAccumulator& prior_accumulator) {
    const float keep = Clamp01(params.bev_topology_evidence.evidence_decay);
    const float add = 1.0F - keep;
    port::TopologyEvidenceAccumulator next{};
    next.value.ordinary_score = prior_accumulator.value.ordinary_score * keep + evidence.ordinary_score * add;
    next.value.bend_curvature_abs =
        prior_accumulator.value.bend_curvature_abs * keep + evidence.bend_curvature_abs * add;
    next.value.bend_veto_score =
        prior_accumulator.value.bend_veto_score * keep + evidence.bend_veto_score * add;
    next.value.cross_score = prior_accumulator.value.cross_score * keep + evidence.cross_score * add;
    next.value.left_circle_score =
        prior_accumulator.value.left_circle_score * keep + evidence.left_circle_score * add;
    next.value.right_circle_score =
        prior_accumulator.value.right_circle_score * keep + evidence.right_circle_score * add;
    next.value.zebra_score = prior_accumulator.value.zebra_score * keep + evidence.zebra_score * add;
    next.value.lost_score = prior_accumulator.value.lost_score * keep + evidence.lost_score * add;
    next.value.bilateral_opening_sync =
        prior_accumulator.value.bilateral_opening_sync * keep + evidence.bilateral_opening_sync * add;
    next.value.forward_reacquire_score =
        prior_accumulator.value.forward_reacquire_score * keep + evidence.forward_reacquire_score * add;
    next.value.left_opening_score =
        prior_accumulator.value.left_opening_score * keep + evidence.left_opening_score * add;
    next.value.right_opening_score =
        prior_accumulator.value.right_opening_score * keep + evidence.right_opening_score * add;
    next.value.invalid_edge_penalty =
        prior_accumulator.value.invalid_edge_penalty * keep + evidence.invalid_edge_penalty * add;
    next.value.element_evidence = evidence.element_evidence;
    next.update_cycles = prior_accumulator.update_cycles + 1;
    return next;
}

}  // namespace ls2k::legacy
