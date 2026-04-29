#include "legacy/steering_reference_policy.hpp"

// 参考路径策略实现。
// 根据场景状态和道路假设，从多个候选路径中选择/合成最终参考路径。
// 模式包括：中心线、内侧偏移、外侧偏移、弧线跟随、保持上次、丢失预测等。
// 新版接口（基于 RoadHypotheses）比旧版（基于 BEVTrackEstimate）更丰富。

#include <algorithm>
#include <cmath>

#include "legacy/steering_bev_geometry.hpp"

namespace ls2k::legacy {
namespace {

// 场景退出阶段的混合因子（progress_cycles / release_cycles）
// 用于环岛退出时从偏移路径平滑过渡到中心线
float BlendFactor(const port::SpecialSceneFsmState& scene_state, const port::RuntimeParameters& params) {
    if (scene_state.phase != port::SpecialScenePhase::kExit) {
        return 0.0F;
    }
    return std::clamp(static_cast<float>(scene_state.progress_cycles) /
                          static_cast<float>(std::max(1, params.bev_scene_fsm.circle_release_cycles)),
                      0.0F,
                      1.0F);
}

// 将 int 索引限幅到合法的采样点索引范围
std::size_t ClampedSampleIndex(int index) {
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, index)),
        port::kBevTrackSampleCount - 1U);
}

// 统计路径中有效采样点的数量
int CountValidSamples(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    int count = 0;
    for (const port::BEVPathSample& sample : path) {
        if (sample.valid) {
            ++count;
        }
    }
    return count;
}

// 检查路径是否有可用参考（至少 2 个有效采样点）
bool HasUsableReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    return CountValidSamples(path) >= 2;
}

// 找到路径中的第一个有效采样点（可选输出索引）
const port::BEVPathSample* FirstValidSample(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t* out_index = nullptr) {
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index].valid) {
            if (out_index != nullptr) {
                *out_index = index;
            }
            return &path[index];
        }
    }
    return nullptr;
}

// 从路径候选生成参考路径（直接引用 sampled_path）
port::BEVReferencePath ReferenceFromCandidate(const port::PathCandidate& candidate,
                                              port::ReferenceMode mode) {
    port::BEVReferencePath reference{};
    reference.valid = candidate.valid;
    reference.mode = mode;
    reference.sampled_path = candidate.sampled_path;
    return reference;
}

// 从路径候选生成参考路径（额外检查采样点可用性）
port::BEVReferencePath DirectReferenceFromCandidate(const port::PathCandidate& candidate,
                                                    port::ReferenceMode mode) {
    port::BEVReferencePath reference = ReferenceFromCandidate(candidate, mode);
    reference.valid = candidate.valid && HasUsableReference(candidate.sampled_path);
    return reference;
}

// 从历史状态生成参考路径（保持上次有效参考）
port::BEVReferencePath ReferenceFromPrior(const port::ReferencePolicyState& prior_state,
                                          port::ReferenceMode mode) {
    port::BEVReferencePath reference{};
    if (!prior_state.valid || !HasUsableReference(prior_state.last_reference)) {
        return reference;
    }
    reference.valid = true;
    reference.mode = mode;
    reference.sampled_path = prior_state.last_reference;
    return reference;
}

// 构建十字路口出口参考路径 —— 从 cross_band 位置到出口候选的线性过渡
// 近端从 0 横向偏移开始，在 blend_start_forward 处与出口候选汇合
port::BEVReferencePath BuildCrossExitReference(const port::PathCandidate& exit_candidate,
                                               const port::BEVCrossBandEvidence& cross_band,
                                               const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    reference.mode = port::ReferenceMode::kBlendToExit;
    if (!exit_candidate.valid || !HasUsableReference(exit_candidate.sampled_path)) {
        return reference;
    }

    std::size_t first_exit_index = 0U;
    const port::BEVPathSample* first_exit =
        FirstValidSample(exit_candidate.sampled_path, &first_exit_index);
    if (first_exit == nullptr) {
        return reference;
    }

    const float first_forward = first_exit->point.forward_m;
    const float first_lateral = first_exit->point.lateral_m;
    const float start_forward = params.bev_topology_sampler.forward_samples_m.front();
    const float blend_start_forward =
        cross_band.present ? std::clamp(cross_band.forward_m, start_forward, first_forward)
                           : start_forward;
    constexpr float kEntryLateralM = 0.0F;
    int valid_count = 0;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        if (exit_candidate.sampled_path[index].valid) {
            reference.sampled_path[index] = exit_candidate.sampled_path[index];
            ++valid_count;
            continue;
        }
        if (index >= first_exit_index) {
            continue;
        }
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        const float alpha =
            first_forward > blend_start_forward + 1.0e-4F
                ? std::clamp((forward - blend_start_forward) / (first_forward - blend_start_forward),
                             0.0F,
                             1.0F)
                : 1.0F;
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m = kEntryLateralM * (1.0F - alpha) + first_lateral * alpha;
        sample.confidence = std::clamp(first_exit->confidence * (0.55F + 0.35F * alpha),
                                       0.0F,
                                       1.0F);
        reference.sampled_path[index] = sample;
        ++valid_count;
    }
    reference.valid = valid_count >= 2;
    return reference;
}

// 复制基础参考路径（优先用历史状态，fallback 到候选路径）
bool CopyBaseReference(const port::ReferencePolicyState& prior_state,
                       const port::PathCandidate& fallback,
                       std::array<port::BEVPathSample, port::kBevTrackSampleCount>& base) {
    if (prior_state.valid && HasUsableReference(prior_state.last_reference)) {
        base = prior_state.last_reference;
        return true;
    }
    if (fallback.valid && HasUsableReference(fallback.sampled_path)) {
        base = fallback.sampled_path;
        return true;
    }
    return false;
}

// 构建入口航向外推参考路径 —— 从历史参考的近端航向角外推到所有层
// 用于十字路口接近阶段，当 exit 候选不可用时的 fallback
port::BEVReferencePath BuildEntryHeadingExtension(const port::ReferencePolicyState& prior_state,
                                                  const port::PathCandidate& fallback,
                                                  const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    reference.mode = port::ReferenceMode::kEntryHeadingExtension;

    std::array<port::BEVPathSample, port::kBevTrackSampleCount> base{};
    if (!CopyBaseReference(prior_state, fallback, base)) {
        return reference;
    }

    std::size_t anchor = ClampedSampleIndex(params.bev_control_model.near_sample_index);
    if (!base[anchor].valid) {
        for (std::size_t index = 0; index < base.size(); ++index) {
            if (base[index].valid) {
                anchor = index;
                break;
            }
        }
    }

    std::size_t heading_sample = ClampedSampleIndex(params.bev_control_model.far_sample_index);
    if (heading_sample <= anchor || !base[heading_sample].valid) {
        heading_sample = anchor;
        for (std::size_t index = anchor + 1U; index < base.size(); ++index) {
            if (base[index].valid) {
                heading_sample = index;
            }
        }
    }
    if (heading_sample <= anchor || !base[anchor].valid || !base[heading_sample].valid) {
        return reference;
    }

    const float ds = base[heading_sample].point.forward_m - base[anchor].point.forward_m;
    if (std::abs(ds) < 1e-4F) {
        return reference;
    }
    const float slope = (base[heading_sample].point.lateral_m - base[anchor].point.lateral_m) / ds;
    const float lateral_limit =
        static_cast<float>(std::max(0.0, params.bev_control_model.max_reference_bias_m));
    const float confidence = std::clamp(base[anchor].confidence, 0.0F, 1.0F);
    int valid_count = 0;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m =
            std::clamp(base[anchor].point.lateral_m + slope * (forward - base[anchor].point.forward_m),
                       -lateral_limit,
                       lateral_limit);
        sample.confidence = confidence;
        reference.sampled_path[index] = sample;
        ++valid_count;
    }
    reference.valid = valid_count >= 2;
    return reference;
}

// 检查 FSM 是否处于环岛候选状态（active=ordinary, phase=candidate, candidate=circle）
bool IsCircleCandidateState(const port::SpecialSceneFsmState& scene_state) {
    return scene_state.active_scene == port::SpecialSceneKind::kOrdinary &&
           scene_state.phase == port::SpecialScenePhase::kCandidate &&
           (scene_state.candidate_scene == port::SpecialSceneKind::kCircleLeft ||
            scene_state.candidate_scene == port::SpecialSceneKind::kCircleRight);
}

// 检查候选路径在近端（前视范围内）是否有自支撑采样点
bool HasNearSelfSupport(const port::PathCandidate& candidate,
                        const port::RuntimeParameters& params) {
    if (!candidate.valid) {
        return false;
    }
    const float near_support_limit_m =
        std::max(static_cast<float>(params.bev_control_model.lookahead_max_m),
                 params.bev_topology_sampler.forward_samples_m[ClampedSampleIndex(params.bev_control_model.far_sample_index)]);
    for (const port::BEVPathSample& sample : candidate.sampled_path) {
        if (sample.valid && sample.point.forward_m <= near_support_limit_m + 1.0e-4F) {
            return true;
        }
    }
    return false;
}

// 检查是否有可信的历史参考（history 有效 + lost_prediction 未超限）
bool TrustedReferenceAvailable(const port::ReferencePolicyState& prior_state,
                               const port::RuntimeParameters& params) {
    return prior_state.valid && HasUsableReference(prior_state.last_reference) &&
           prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles;
}

// 计算候选路径与历史参考的近端加权兼容性误差
// 权重随前向距离指数衰减（近端差异权重更大）
float NearWeightedCompatibilityError(const port::PathCandidate& candidate,
                                     const port::ReferencePolicyState& prior_state,
                                     const port::RuntimeParameters& params) {
    if (!candidate.valid || !TrustedReferenceAvailable(prior_state, params)) {
        return static_cast<float>(params.bev_path_policy.reference_compatibility_max_error_m) + 1.0F;
    }
    const float tau = std::max(0.05F, params.bev_path_policy.reference_compatibility_tau_m);
    float weighted_error = 0.0F;
    float weight_sum = 0.0F;
    for (std::size_t index = 0; index < candidate.sampled_path.size(); ++index) {
        const port::BEVPathSample& candidate_sample = candidate.sampled_path[index];
        const port::BEVPathSample& trusted_sample = prior_state.last_reference[index];
        if (!candidate_sample.valid || !trusted_sample.valid) {
            continue;
        }
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        const float weight = std::exp(-std::max(0.0F, forward) / tau);
        weighted_error +=
            weight * std::abs(candidate_sample.point.lateral_m - trusted_sample.point.lateral_m);
        weight_sum += weight;
    }
    if (weight_sum <= 1.0e-4F) {
        return static_cast<float>(params.bev_path_policy.reference_compatibility_max_error_m) + 1.0F;
    }
    return weighted_error / weight_sum;
}

// 计算路径的平均置信度
float AveragePathConfidence(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    float sum = 0.0F;
    int count = 0;
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        sum += sample.confidence;
        ++count;
    }
    return count > 0 ? std::clamp(sum / static_cast<float>(count), 0.0F, 1.0F) : 0.0F;
}

// 从候选路径生成参考路径，与可信历史参考混合
// 流程：
// 1. 无历史参考 → 检查候选自支撑，直接使用
// 2. 有历史参考但兼容性超限 → 拒绝
// 3. 有历史参考且兼容 → 近端偏向候选，远端偏向历史的加权混合
port::BEVReferencePath ReferenceFromCandidateWithTrusted(
    const port::PathCandidate& candidate,
    port::ReferenceMode mode,
    const port::ReferencePolicyState& prior_state,
    const port::RuntimeParameters& params,
    float& compatibility_error_m,
    std::string& source) {
    port::BEVReferencePath reference{};
    reference.mode = mode;
    compatibility_error_m = 0.0F;
    source = "none";

    if (!candidate.valid) {
        return reference;
    }

    const bool trusted_available = TrustedReferenceAvailable(prior_state, params);
    if (!trusted_available) {
        if (!HasNearSelfSupport(candidate, params)) {
            compatibility_error_m =
                static_cast<float>(params.bev_path_policy.reference_compatibility_max_error_m) + 1.0F;
            source = "candidate_far_untrusted";
            return reference;
        }
        reference = ReferenceFromCandidate(candidate, mode);
        source = "candidate_self_supported";
        return reference;
    }

    compatibility_error_m = NearWeightedCompatibilityError(candidate, prior_state, params);
    if (compatibility_error_m >
        static_cast<float>(params.bev_path_policy.reference_compatibility_max_error_m)) {
        source = "candidate_incompatible_with_trusted";
        return reference;
    }

    const float tau = std::max(0.05F, params.bev_path_policy.reference_compatibility_tau_m);
    const float trusted_decay = std::clamp(params.bev_path_policy.trusted_reference_decay, 0.0F, 1.0F);
    int valid_count = 0;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        const port::BEVPathSample& candidate_sample = candidate.sampled_path[index];
        const port::BEVPathSample& trusted_sample = prior_state.last_reference[index];
        if (!candidate_sample.valid && !trusted_sample.valid) {
            continue;
        }
        if (candidate_sample.valid && trusted_sample.valid) {
            const float forward = params.bev_topology_sampler.forward_samples_m[index];
            const float near_weight = std::exp(-std::max(0.0F, forward) / tau);
            const float candidate_alpha = std::clamp(0.25F + 0.55F * (1.0F - near_weight), 0.25F, 0.80F);
            port::BEVPathSample sample{};
            sample.valid = true;
            sample.point.forward_m = candidate_sample.point.forward_m;
            sample.point.lateral_m =
                trusted_sample.point.lateral_m * (1.0F - candidate_alpha) +
                candidate_sample.point.lateral_m * candidate_alpha;
            sample.confidence = std::clamp(
                trusted_sample.confidence * trusted_decay * (1.0F - candidate_alpha) +
                    candidate_sample.confidence * candidate_alpha,
                0.0F,
                1.0F);
            reference.sampled_path[index] = sample;
        } else if (candidate_sample.valid) {
            reference.sampled_path[index] = candidate_sample;
        } else {
            port::BEVPathSample sample = trusted_sample;
            sample.confidence = std::clamp(sample.confidence * trusted_decay, 0.0F, 1.0F);
            reference.sampled_path[index] = sample;
        }
        ++valid_count;
    }
    reference.valid = valid_count >= 2;
    source = reference.valid ? "candidate_trusted_blend" : "candidate_trusted_blend_short";
    return reference;
}

// 终态化参考策略结果：设置状态、更新置信度/年龄/保持/丢失计数
void FinalizeReferencePolicyResult(ReferencePolicyResult& result,
                                   const port::ReferencePolicyState& prior_state,
                                   const port::RuntimeParameters& params,
                                   float compatibility_error_m,
                                   const std::string& source) {
    result.reference_mode = ToString(result.reference_path.mode);
    result.state.valid = result.reference_path.valid;
    result.state.mode = result.reference_path.mode;
    result.state.last_reference = result.reference_path.sampled_path;
    result.state.compatibility_error_m = compatibility_error_m;
    result.state.reference_source = source;
    if (result.reference_path.valid) {
        const float confidence = AveragePathConfidence(result.reference_path.sampled_path);
        result.state.trusted_confidence =
            std::max(confidence, prior_state.trusted_confidence * params.bev_path_policy.trusted_reference_decay);
        result.state.trusted_age_cycles =
            result.reference_path.mode == port::ReferenceMode::kHoldLast ||
                    result.reference_path.mode == port::ReferenceMode::kLostPrediction
                ? prior_state.trusted_age_cycles + 1
                : 0;
    } else {
        result.state.trusted_confidence =
            prior_state.trusted_confidence * params.bev_path_policy.trusted_reference_decay;
        result.state.trusted_age_cycles = prior_state.trusted_age_cycles + 1;
    }
    if (result.reference_path.mode == port::ReferenceMode::kHoldLast) {
        result.state.hold_cycles = prior_state.hold_cycles + 1;
    } else {
        result.state.hold_cycles = 0;
    }
    if (result.reference_path.mode == port::ReferenceMode::kLostPrediction) {
        result.state.lost_prediction_cycles = prior_state.lost_prediction_cycles + 1;
    } else {
        result.state.lost_prediction_cycles = 0;
    }
}

}  // namespace

// ========== 旧版参考策略解析（基于 BEVTrackEstimate） ==========
// 根据场景状态选择参考模式：
// - kCircleLeft/kCircleRight: 沿左/右边界法线内偏半车道宽
// - kCross/kZebra: 保持上次参考（HoldLast）
// - 其他: 使用中心线
// 环岛退出阶段使用 Blend 模式从偏移平滑过渡到中心线
ReferencePolicyResult ResolveReferencePolicy(const port::BEVTrackEstimate& track,
                                             const port::BEVSceneObservation& observation,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params) {
    ReferencePolicyResult result{};
    result.state = prior_state;
    result.reference_path.valid = track.valid;
    result.reference_path.mode = port::ReferenceMode::kCenterline;

    const float half_nominal_width = params.bev_geometry.nominal_lane_width_m * 0.5F;
    const float exit_blend = BlendFactor(scene_state, params);
    bool normal_offset_reference = false;
    for (std::size_t i = 0; i < track.sampled_centerline.size(); ++i) {
        result.reference_path.sampled_path[i] = track.sampled_centerline[i];
    }

    if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        result.reference_path.mode =
            exit_blend > 0.0F ? port::ReferenceMode::kBlend : port::ReferenceMode::kInnerOffset;
        for (std::size_t i = 0; i < result.reference_path.sampled_path.size(); ++i) {
            if (!track.sampled_left_boundary[i].valid) {
                continue;
            }
            port::BEVPathSample desired =
                OffsetPathSampleAlongNormal(
                    track.sampled_left_boundary, i, half_nominal_width, half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.forward_m =
                    desired.point.forward_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.forward_m * exit_blend;
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
        normal_offset_reference = true;
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.reference_path.mode =
            exit_blend > 0.0F ? port::ReferenceMode::kBlend : port::ReferenceMode::kInnerOffset;
        for (std::size_t i = 0; i < result.reference_path.sampled_path.size(); ++i) {
            if (!track.sampled_right_boundary[i].valid) {
                continue;
            }
            port::BEVPathSample desired =
                OffsetPathSampleAlongNormal(
                    track.sampled_right_boundary, i, -half_nominal_width, half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.forward_m =
                    desired.point.forward_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.forward_m * exit_blend;
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
        normal_offset_reference = true;
    } else if ((scene_state.active_scene == port::SpecialSceneKind::kCross ||
                scene_state.active_scene == port::SpecialSceneKind::kZebra) &&
               prior_state.valid) {
        result.reference_path.mode = port::ReferenceMode::kHoldLast;
        result.reference_path.sampled_path = prior_state.last_reference;
    } else {
        (void)observation;
    }

    if (normal_offset_reference) {
        (void)NormalizePathToForwardSamples(result.reference_path.sampled_path,
                                            params.bev_topology_sampler.forward_samples_m,
                                            half_nominal_width,
                                            std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                     std::abs(params.bev_geometry.lateral_step_m)) *
                                                2.0F);
    }

    result.reference_mode = ToString(result.reference_path.mode);
    result.reference_path.bias_m = 0.0F;
    result.state.valid = result.reference_path.valid;
    result.state.mode = result.reference_path.mode;
    result.state.last_reference = result.reference_path.sampled_path;
    return result;
}

// ========== 新版参考策略解析（基于 RoadHypotheses） ==========
// 根据场景状态和拓扑证据，从多个候选路径中选择最佳参考：
// - Ordinary: 使用 ReferenceFromCandidateWithTrusted（与历史参考混合）
// - Cross Entry/Hold: 十字路口出口路径（BuildCrossExitReference）
// - Cross Exit: 航向外推或保持上次
// - Zebra: 保持上次（HoldLast），fallback 到 zebra_hold 候选
// - Circle Candidate: 外侧守卫路径（outer guard）
// - Circle Entry/Interior: 内侧跟随路径（inner follow）
// - Circle Exit: 环岛出口（blend to exit）
// - Lost: 丢失预测（hold last reference）
ReferencePolicyResult ResolveReferencePolicy(const port::RoadHypotheses& hypotheses,
                                             const port::TopologyEvidence& evidence,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params) {
    ReferencePolicyResult result{};
    result.state = prior_state;
    float compatibility_error_m = 0.0F;
    std::string reference_source = "ordinary_candidate";
    // 默认使用 ordinary 候选，与历史参考混合
    result.reference_path =
        ReferenceFromCandidateWithTrusted(hypotheses.ordinary,
                                          port::ReferenceMode::kCenterline,
                                          prior_state,
                                          params,
                                          compatibility_error_m,
                                          reference_source);

    // === 十字路口场景 ===
    if (scene_state.active_scene == port::SpecialSceneKind::kCross) {
        result.reference_path = {};
        reference_source = "cross_none";
        const port::PathCandidate& exit_candidate =
            hypotheses.cross_exit.valid ? hypotheses.cross_exit : hypotheses.forward_exit;
        if (scene_state.phase == port::SpecialScenePhase::kExit) {
            result.reference_path =
                BuildCrossExitReference(exit_candidate, evidence.element_evidence.cross_band, params);
            reference_source = result.reference_path.valid ? "cross_exit_entry_center_stitch" : reference_source;
            if (!result.reference_path.valid) {
                result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
                reference_source =
                    result.reference_path.valid ? "cross_entry_heading_extension" : reference_source;
            }
        } else if (scene_state.phase == port::SpecialScenePhase::kEntry ||
                   scene_state.phase == port::SpecialScenePhase::kHold) {
            if (hypotheses.cross_exit.valid) {
                result.reference_path =
                    BuildCrossExitReference(hypotheses.cross_exit, evidence.element_evidence.cross_band, params);
                reference_source =
                    result.reference_path.valid ? "cross_exit_entry_center_stitch" : reference_source;
            }
            if (!result.reference_path.valid && scene_state.phase == port::SpecialScenePhase::kEntry) {
                result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
                reference_source =
                    result.reference_path.valid ? "cross_entry_heading_extension" : reference_source;
            }
            if (!result.reference_path.valid) {
                result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
                reference_source = result.reference_path.valid ? "cross_hold_trusted" : reference_source;
            }
        } else {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
            reference_source = result.reference_path.valid ? "cross_hold_trusted" : reference_source;
            if (!result.reference_path.valid) {
                result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
                reference_source =
                    result.reference_path.valid ? "cross_entry_heading_extension" : reference_source;
            }
        }
    // === 斑马线场景 ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kZebra) {
        result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
        reference_source = result.reference_path.valid ? "zebra_hold_trusted" : "zebra_none";
        if (!result.reference_path.valid && hypotheses.zebra_hold.valid) {
            result.reference_path =
                ReferenceFromCandidateWithTrusted(hypotheses.zebra_hold,
                                                  port::ReferenceMode::kHoldLast,
                                                  prior_state,
                                                  params,
                                                  compatibility_error_m,
                                                  reference_source);
        }
    // === 环岛候选状态（未确认环岛，用外侧守卫） ===
    } else if (IsCircleCandidateState(scene_state)) {
        const port::PathCandidate& candidate =
            scene_state.candidate_scene == port::SpecialSceneKind::kCircleLeft
                ? hypotheses.circle_outer_guard_left
                : hypotheses.circle_outer_guard_right;
        result.reference_path = DirectReferenceFromCandidate(candidate, port::ReferenceMode::kOuterOffset);
        reference_source = result.reference_path.valid ? "circle_candidate_outer_guard" : "circle_candidate_none";
        if (!result.reference_path.valid) {
            result.reference_path =
                ReferenceFromCandidateWithTrusted(hypotheses.ordinary,
                                                  port::ReferenceMode::kCenterline,
                                                  prior_state,
                                                  params,
                                                  compatibility_error_m,
                                                  reference_source);
        }
    // === 左环岛场景（Entry → Interior → Exit） ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        const port::PathCandidate* candidate = &hypotheses.circle_inner_left;
        port::ReferenceMode mode = candidate->mode == port::ReferenceMode::kStableBoundaryOffset
                                       ? port::ReferenceMode::kStableBoundaryOffset
                                       : port::ReferenceMode::kArcFollow;
        // Entry 阶段内侧无效时用外侧守卫，Exit 阶段用环岛出口
        if (scene_state.phase == port::SpecialScenePhase::kEntry &&
            !hypotheses.circle_inner_left.valid) {
            candidate = &hypotheses.circle_outer_guard_left;
            mode = port::ReferenceMode::kOuterOffset;
        } else if (scene_state.phase == port::SpecialScenePhase::kExit) {
            candidate = hypotheses.circle_exit_left.valid ? &hypotheses.circle_exit_left
                                                          : &hypotheses.forward_exit;
            mode = port::ReferenceMode::kBlendToExit;
        }
        result.reference_path = DirectReferenceFromCandidate(*candidate, mode);
        reference_source = result.reference_path.valid ? "circle_left_direct" : reference_source;
        if (!result.reference_path.valid && prior_state.valid &&
            prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
            reference_source = result.reference_path.valid ? "circle_left_lost_prediction" : reference_source;
        }
    // === 右环岛场景（Entry → Interior → Exit） ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        const port::PathCandidate* candidate = &hypotheses.circle_inner_right;
        port::ReferenceMode mode = candidate->mode == port::ReferenceMode::kStableBoundaryOffset
                                       ? port::ReferenceMode::kStableBoundaryOffset
                                       : port::ReferenceMode::kArcFollow;
        if (scene_state.phase == port::SpecialScenePhase::kEntry &&
            !hypotheses.circle_inner_right.valid) {
            candidate = &hypotheses.circle_outer_guard_right;
            mode = port::ReferenceMode::kOuterOffset;
        } else if (scene_state.phase == port::SpecialScenePhase::kExit) {
            candidate = hypotheses.circle_exit_right.valid ? &hypotheses.circle_exit_right
                                                           : &hypotheses.forward_exit;
            mode = port::ReferenceMode::kBlendToExit;
        }
        result.reference_path = DirectReferenceFromCandidate(*candidate, mode);
        reference_source = result.reference_path.valid ? "circle_right_direct" : reference_source;
        if (!result.reference_path.valid && prior_state.valid &&
            prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
            reference_source = result.reference_path.valid ? "circle_right_lost_prediction" : reference_source;
        }
    // === 丢失预测模式（ordinary 无效且 lost_score 高） ===
    } else if (!hypotheses.ordinary.valid && evidence.lost_score > 0.65F && prior_state.valid &&
               prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
        result.reference_path.mode = port::ReferenceMode::kLostPrediction;
        result.reference_path.valid = true;
        result.reference_path.sampled_path = prior_state.last_reference;
        reference_source = "lost_prediction_trusted";
    } else if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params) &&
               evidence.lost_score > 0.65F) {
        result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
        reference_source = result.reference_path.valid ? "lost_prediction_trusted" : reference_source;
    }

    FinalizeReferencePolicyResult(result,
                                  prior_state,
                                  params,
                                  compatibility_error_m,
                                  reference_source);
    return result;
}

const char* ToString(port::ReferenceMode mode) {
    switch (mode) {
        case port::ReferenceMode::kInnerOffset:
            return "inner_offset";
        case port::ReferenceMode::kOuterOffset:
            return "outer_offset";
        case port::ReferenceMode::kBlend:
            return "blend";
        case port::ReferenceMode::kHoldLast:
            return "hold_last";
        case port::ReferenceMode::kEntryHeadingExtension:
            return "entry_heading_extension";
        case port::ReferenceMode::kStableBoundaryOffset:
            return "stable_boundary_offset";
        case port::ReferenceMode::kArcFollow:
            return "arc_follow";
        case port::ReferenceMode::kBlendToExit:
            return "blend_to_exit";
        case port::ReferenceMode::kLostPrediction:
            return "lost_prediction";
        case port::ReferenceMode::kCenterline:
        default:
            return "centerline";
    }
}

}  // namespace ls2k::legacy
