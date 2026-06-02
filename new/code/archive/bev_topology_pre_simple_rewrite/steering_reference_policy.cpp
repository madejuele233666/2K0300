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

struct ReferenceForwardRange {
    bool valid = false;
    float min_forward_m = 0.0F;
    float max_forward_m = 0.0F;
};

ReferenceForwardRange ComputeReferenceForwardRange(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    ReferenceForwardRange range{};
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        if (!range.valid) {
            range.valid = true;
            range.min_forward_m = sample.point.forward_m;
            range.max_forward_m = sample.point.forward_m;
        } else {
            range.min_forward_m = std::min(range.min_forward_m, sample.point.forward_m);
            range.max_forward_m = std::max(range.max_forward_m, sample.point.forward_m);
        }
    }
    return range;
}

bool InterpolatePathAt(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                       float forward_m,
                       port::BEVPathSample& output) {
    constexpr float kForwardEpsilonM = 1.0e-4F;
    const ReferenceForwardRange range = ComputeReferenceForwardRange(path);
    if (!range.valid || forward_m < range.min_forward_m - kForwardEpsilonM ||
        forward_m > range.max_forward_m + kForwardEpsilonM) {
        return false;
    }

    const port::BEVPathSample* previous = nullptr;
    const port::BEVPathSample* next = nullptr;
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        if (sample.point.forward_m <= forward_m + kForwardEpsilonM) {
            previous = &sample;
        }
        if (sample.point.forward_m >= forward_m - kForwardEpsilonM) {
            next = &sample;
            break;
        }
    }
    if (previous == nullptr && next == nullptr) {
        return false;
    }
    if (previous == nullptr) {
        output = *next;
        output.point.forward_m = forward_m;
        return true;
    }
    if (next == nullptr) {
        output = *previous;
        output.point.forward_m = forward_m;
        return true;
    }
    const float span = next->point.forward_m - previous->point.forward_m;
    if (std::abs(span) < kForwardEpsilonM) {
        output = *next;
        output.point.forward_m = forward_m;
        return true;
    }
    const float t = std::clamp((forward_m - previous->point.forward_m) / span, 0.0F, 1.0F);
    output.valid = true;
    output.point.forward_m = forward_m;
    output.point.lateral_m =
        previous->point.lateral_m + t * (next->point.lateral_m - previous->point.lateral_m);
    output.confidence = previous->confidence + t * (next->confidence - previous->confidence);
    return true;
}

float PathHeadingAt(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                    float forward_m) {
    const ReferenceForwardRange range = ComputeReferenceForwardRange(path);
    if (!range.valid) {
        return 0.0F;
    }
    const float window = std::max(0.04F, (range.max_forward_m - range.min_forward_m) * 0.18F);
    const float before_m = std::clamp(forward_m - window, range.min_forward_m, range.max_forward_m);
    const float after_m = std::clamp(forward_m + window, range.min_forward_m, range.max_forward_m);
    port::BEVPathSample before{};
    port::BEVPathSample after{};
    if (!InterpolatePathAt(path, before_m, before) ||
        !InterpolatePathAt(path, after_m, after)) {
        return 0.0F;
    }
    const float ds = after.point.forward_m - before.point.forward_m;
    if (std::abs(ds) < 1.0e-4F) {
        return 0.0F;
    }
    return std::atan2(after.point.lateral_m - before.point.lateral_m, ds);
}

float AngleDiffAbs(float left, float right) {
    constexpr float kPi = 3.1415926535F;
    float diff = std::fmod(left - right + kPi, 2.0F * kPi);
    if (diff < 0.0F) {
        diff += 2.0F * kPi;
    }
    return std::abs(diff - kPi);
}

// 构建十字路口出口参考路径 —— 从当前预期路径近端平滑接到出口候选
port::BEVReferencePath BuildCrossExitReference(const port::PathCandidate& exit_candidate,
                                               const port::BEVReferencePath& entry_reference,
                                               const port::BEVCrossBandEvidence& cross_band,
                                               const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    reference.mode = port::ReferenceMode::kBlendToExit;
    if (!exit_candidate.valid || !HasUsableReference(exit_candidate.sampled_path) ||
        !entry_reference.valid || !HasUsableReference(entry_reference.sampled_path)) {
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
        port::BEVPathSample entry_sample{};
        if (!InterpolatePathAt(entry_reference.sampled_path, forward, entry_sample)) {
            continue;
        }
        const float alpha =
            first_forward > blend_start_forward + 1.0e-4F
                ? std::clamp((forward - blend_start_forward) / (first_forward - blend_start_forward),
                             0.0F,
                             1.0F)
                : 1.0F;
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m = entry_sample.point.lateral_m * (1.0F - alpha) + first_lateral * alpha;
        sample.confidence = std::clamp(entry_sample.confidence * (1.0F - alpha) +
                                           first_exit->confidence * alpha,
                                       0.0F,
                                       1.0F);
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

port::BEVReferencePath TrustedErrorReferenceFromPrior(const port::ReferencePolicyState& prior_state,
                                                      const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    if (!TrustedReferenceAvailable(prior_state, params)) {
        return reference;
    }
    reference.valid = true;
    reference.mode = port::ReferenceMode::kHoldLast;
    reference.sampled_path = prior_state.last_reference;
    return reference;
}

port::BEVReferencePath ExpectedReferenceFromCandidate(const port::PathCandidate& candidate,
                                                      port::ReferenceMode mode) {
    return DirectReferenceFromCandidate(candidate, mode);
}

bool CandidateHasNearSupport(const port::PathCandidate& candidate,
                             const port::RuntimeParameters& params) {
    return HasNearSelfSupport(candidate, params);
}

bool IsCircleInnerIslandCandidate(const port::PathCandidate& candidate) {
    return candidate.mode == port::ReferenceMode::kCircleInnerIsland;
}

bool PathsTangentCompatible(const port::PathCandidate& first,
                            const port::PathCandidate& second,
                            const port::RuntimeParameters& params) {
    if (!first.valid || !second.valid) {
        return false;
    }
    const ReferenceForwardRange first_range = ComputeReferenceForwardRange(first.sampled_path);
    const ReferenceForwardRange second_range = ComputeReferenceForwardRange(second.sampled_path);
    if (!first_range.valid || !second_range.valid) {
        return false;
    }
    const float overlap_min = std::max(first_range.min_forward_m, second_range.min_forward_m);
    const float overlap_max = std::min(first_range.max_forward_m, second_range.max_forward_m);
    if (overlap_min > overlap_max + 1.0e-4F) {
        return false;
    }
    const float probe_forward = overlap_min;
    return AngleDiffAbs(PathHeadingAt(first.sampled_path, probe_forward),
                        PathHeadingAt(second.sampled_path, probe_forward)) <=
           params.bev_path_policy.circle_tangent_parallel_abs_max_rad;
}

bool PathTangentCompatibleWithPrior(const port::PathCandidate& candidate,
                                    const port::ReferencePolicyState& prior_state,
                                    const port::RuntimeParameters& params) {
    if (!candidate.valid || !TrustedReferenceAvailable(prior_state, params)) {
        return false;
    }
    const ReferenceForwardRange candidate_range = ComputeReferenceForwardRange(candidate.sampled_path);
    const ReferenceForwardRange prior_range = ComputeReferenceForwardRange(prior_state.last_reference);
    if (!candidate_range.valid || !prior_range.valid) {
        return false;
    }
    const float overlap_min = std::max(candidate_range.min_forward_m, prior_range.min_forward_m);
    const float overlap_max = std::min(candidate_range.max_forward_m, prior_range.max_forward_m);
    if (overlap_min > overlap_max + 1.0e-4F) {
        return false;
    }
    const float probe_forward = overlap_min;
    return AngleDiffAbs(PathHeadingAt(candidate.sampled_path, probe_forward),
                        PathHeadingAt(prior_state.last_reference, probe_forward)) <=
           params.bev_path_policy.circle_tangent_parallel_abs_max_rad;
}

bool InnerCircleSwitchAllowed(const port::PathCandidate& inner,
                              const port::PathCandidate& outer_guard,
                              const port::ReferencePolicyState& prior_state,
                              const port::RuntimeParameters& params) {
    if (!inner.valid || !IsCircleInnerIslandCandidate(inner)) {
        return false;
    }
    if (CandidateHasNearSupport(inner, params)) {
        return true;
    }
    if (prior_state.circle_inner_island_memory_active &&
        prior_state.circle_inner_island_memory_age_cycles <=
            params.bev_reference_policy.hold_last_max_cycles) {
        return true;
    }
    if (PathsTangentCompatible(inner, outer_guard, params) ||
        PathTangentCompatibleWithPrior(inner, prior_state, params)) {
        return true;
    }
    const ReferenceForwardRange range = ComputeReferenceForwardRange(inner.sampled_path);
    if (!range.valid) {
        return false;
    }
    const float late_switch_heading_abs =
        std::max(0.50F, params.bev_path_policy.circle_tangent_parallel_abs_max_rad * 1.5F);
    return std::abs(PathHeadingAt(inner.sampled_path, range.min_forward_m)) >= late_switch_heading_abs;
}

bool EdgePathsCompatibleForMemory(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& memory_edge,
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& current_edge,
    const port::RuntimeParameters& params) {
    const float max_delta =
        std::max(0.12F, params.bev_corridor_graph.max_center_jump_m * 1.25F);
    int overlap_count = 0;
    float delta_sum = 0.0F;
    float delta_max = 0.0F;
    for (std::size_t index = 0; index < port::kBevTrackSampleCount; ++index) {
        if (!memory_edge[index].valid || !current_edge[index].valid) {
            continue;
        }
        const float delta =
            std::abs(memory_edge[index].point.lateral_m - current_edge[index].point.lateral_m);
        delta_sum += delta;
        delta_max = std::max(delta_max, delta);
        ++overlap_count;
    }
    if (overlap_count == 0) {
        return false;
    }
    return delta_sum / static_cast<float>(overlap_count) <= max_delta &&
           delta_max <= max_delta * 1.75F;
}

bool InnerIslandCalibrationCompatibleForMemory(
    const port::ReferencePolicyState& state,
    const port::BEVCircleInnerIslandEvidence& evidence,
    bool left_side,
    const port::RuntimeParameters& params) {
    if (!state.circle_inner_island_memory_active) {
        return true;
    }
    if (state.circle_inner_island_memory_left != left_side) {
        return false;
    }
    if (state.circle_inner_island_black_end_forward_m <=
            state.circle_inner_island_black_start_forward_m + 1.0e-4F ||
        evidence.black_end_forward_m <= evidence.black_start_forward_m + 1.0e-4F) {
        return EdgePathsCompatibleForMemory(state.circle_inner_island_edge,
                                            evidence.road_facing_edge,
                                            params);
    }
    const float overlap_start =
        std::max(state.circle_inner_island_black_start_forward_m, evidence.black_start_forward_m);
    const float overlap_end =
        std::min(state.circle_inner_island_black_end_forward_m, evidence.black_end_forward_m);
    const float overlap = std::max(0.0F, overlap_end - overlap_start);
    const float remembered_len =
        state.circle_inner_island_black_end_forward_m -
        state.circle_inner_island_black_start_forward_m;
    const float current_len = evidence.black_end_forward_m - evidence.black_start_forward_m;
    const float min_overlap =
        std::max(0.035F, std::min(remembered_len, current_len) * 0.25F);
    const float max_scan_delta =
        std::max(0.10F, params.bev_corridor_graph.nominal_lane_width_m * 0.35F);
    return overlap >= min_overlap &&
           std::abs(state.circle_inner_island_scan_lateral_m - evidence.scan_lateral_m) <=
               max_scan_delta;
}

bool EdgeUsableForMemory(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& edge,
    const port::RuntimeParameters& params) {
    int count = 0;
    for (const port::BEVPathSample& sample : edge) {
        if (sample.valid) {
            ++count;
        }
    }
    return count >= std::max(1, params.bev_path_policy.circle_inner_min_layers);
}

void ClearInnerIslandMemory(port::ReferencePolicyState& state) {
    state.circle_inner_island_memory_active = false;
    state.circle_inner_island_memory_age_cycles = 0;
    state.circle_inner_island_missing_edge_cycles = 0;
    state.circle_inner_island_memory_confidence = 0.0F;
    state.circle_inner_island_scan_lateral_m = 0.0F;
    state.circle_inner_island_black_start_forward_m = 0.0F;
    state.circle_inner_island_black_end_forward_m = 0.0F;
    state.circle_inner_island_edge = {};
}

void AdoptInnerIslandMemory(port::ReferencePolicyState& state,
                            const port::BEVCircleInnerIslandEvidence& evidence,
                            bool left_side) {
    state.circle_inner_island_memory_active = true;
    state.circle_inner_island_memory_left = left_side;
    state.circle_inner_island_memory_age_cycles = 0;
    state.circle_inner_island_missing_edge_cycles = 0;
    state.circle_inner_island_memory_confidence = std::clamp(evidence.score, 0.25F, 1.0F);
    state.circle_inner_island_scan_lateral_m = evidence.scan_lateral_m;
    state.circle_inner_island_black_start_forward_m = evidence.black_start_forward_m;
    state.circle_inner_island_black_end_forward_m = evidence.black_end_forward_m;
    state.circle_inner_island_edge = evidence.road_facing_edge;
}

void MergeInnerIslandMemoryEdge(port::ReferencePolicyState& state,
                                const port::BEVCircleInnerIslandEvidence& evidence,
                                const port::RuntimeParameters& params) {
    for (std::size_t index = 0; index < port::kBevTrackSampleCount; ++index) {
        if (evidence.road_facing_edge[index].valid) {
            state.circle_inner_island_edge[index] = evidence.road_facing_edge[index];
        }
    }
    state.circle_inner_island_memory_age_cycles = 0;
    state.circle_inner_island_missing_edge_cycles = 0;
    state.circle_inner_island_memory_confidence =
        std::min(1.0F,
                 std::max(state.circle_inner_island_memory_confidence,
                          std::clamp(evidence.score, 0.25F, 1.0F)) /
                     std::max(0.50F, params.bev_path_policy.trusted_reference_decay));
}

void UpdateInnerIslandMemory(port::ReferencePolicyState& state,
                             const port::TopologyEvidence& evidence,
                             const port::SpecialSceneFsmState& scene_state,
                             const port::RuntimeParameters& params) {
    if ((evidence.element_evidence.valid && evidence.element_evidence.cross_band.present) ||
        scene_state.active_scene == port::SpecialSceneKind::kCross) {
        ClearInnerIslandMemory(state);
        return;
    }

    bool updated = false;
    const auto try_update = [&](const port::BEVCircleInnerIslandEvidence& island, bool left_side) {
        if (!island.edge_present || !island.trace_present ||
            !EdgeUsableForMemory(island.road_facing_edge, params)) {
            return;
        }
        if (island.present) {
            if (InnerIslandCalibrationCompatibleForMemory(state, island, left_side, params)) {
                AdoptInnerIslandMemory(state, island, left_side);
                updated = true;
                return;
            }
            if (state.circle_inner_island_memory_active &&
                state.circle_inner_island_memory_left == left_side &&
                EdgePathsCompatibleForMemory(state.circle_inner_island_edge,
                                             island.road_facing_edge,
                                             params)) {
                MergeInnerIslandMemoryEdge(state, island, params);
                updated = true;
            } else if (state.circle_inner_island_memory_active &&
                       state.circle_inner_island_memory_left == left_side) {
                ++state.circle_inner_island_missing_edge_cycles;
            }
            return;
        }
        if (state.circle_inner_island_memory_active &&
            state.circle_inner_island_memory_left == left_side &&
            EdgePathsCompatibleForMemory(state.circle_inner_island_edge,
                                         island.road_facing_edge,
                                         params)) {
            MergeInnerIslandMemoryEdge(state, island, params);
            updated = true;
        } else if (state.circle_inner_island_memory_active &&
                   state.circle_inner_island_memory_left == left_side) {
            ++state.circle_inner_island_missing_edge_cycles;
        }
    };

    if (evidence.element_evidence.valid) {
        const bool prefer_left =
            evidence.element_evidence.left_inner_island.score >=
            evidence.element_evidence.right_inner_island.score;
        if (prefer_left) {
            try_update(evidence.element_evidence.left_inner_island, true);
            if (!updated) {
                try_update(evidence.element_evidence.right_inner_island, false);
            }
        } else {
            try_update(evidence.element_evidence.right_inner_island, false);
            if (!updated) {
                try_update(evidence.element_evidence.left_inner_island, true);
            }
        }
    }

    if (!updated && state.circle_inner_island_memory_active) {
        ++state.circle_inner_island_memory_age_cycles;
        if (scene_state.phase != port::SpecialScenePhase::kCandidate) {
            ++state.circle_inner_island_missing_edge_cycles;
        }
        state.circle_inner_island_memory_confidence *= params.bev_path_policy.trusted_reference_decay;
    }

    const int max_age =
        params.bev_reference_policy.hold_last_max_cycles +
        params.bev_scene_fsm.circle_release_cycles +
        params.bev_scene_fsm.circle_confirm_cycles;
    const int max_missing =
        params.bev_reference_policy.hold_last_max_cycles +
        params.bev_scene_fsm.circle_release_cycles;
    if (state.circle_inner_island_memory_active &&
        (state.circle_inner_island_memory_confidence < 0.15F ||
         state.circle_inner_island_memory_age_cycles > max_age ||
         state.circle_inner_island_missing_edge_cycles > max_missing)) {
        ClearInnerIslandMemory(state);
    }
}

bool CircleExitSwitchAllowed(const port::SpecialSceneFsmState& scene_state,
                             const port::PathCandidate& inner,
                             const port::PathCandidate& exit_candidate,
                             const port::RuntimeParameters& params) {
    if (scene_state.circle_yaw_accum_deg < params.bev_path_policy.circle_exit_yaw_deg ||
        !exit_candidate.valid) {
        return false;
    }
    return !inner.valid || PathsTangentCompatible(inner, exit_candidate, params);
}

void SmoothFinalReferencePath(port::BEVReferencePath& reference,
                              const port::RuntimeParameters& params) {
    if (!reference.valid) {
        return;
    }
    const auto original = reference.sampled_path;
    const float max_adjust =
        std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                 std::abs(params.bev_geometry.lateral_step_m)) *
        1.5F;
    int valid_count = 0;
    for (std::size_t index = 0; index < original.size(); ++index) {
        if (!original[index].valid) {
            continue;
        }
        ++valid_count;
        if (index == 0U || index + 1U >= original.size() ||
            !original[index - 1U].valid || !original[index + 1U].valid) {
            continue;
        }
        const float smoothed =
            original[index - 1U].point.lateral_m * 0.25F +
            original[index].point.lateral_m * 0.50F +
            original[index + 1U].point.lateral_m * 0.25F;
        reference.sampled_path[index].point.lateral_m =
            std::clamp(smoothed,
                       original[index].point.lateral_m - max_adjust,
                       original[index].point.lateral_m + max_adjust);
    }
    reference.valid = valid_count >= 2;
}

// 终态化参考策略结果：设置状态、更新置信度/年龄/保持/丢失计数
void FinalizeReferencePolicyResult(ReferencePolicyResult& result,
                                   const port::ReferencePolicyState& prior_state,
                                   const port::RuntimeParameters& params,
                                   float compatibility_error_m,
                                   const std::string& source) {
    result.reference_mode = ToString(result.reference_path.mode);
    const bool has_current_reference = result.reference_path.valid;
    result.state.last_reference =
        has_current_reference ? result.reference_path.sampled_path : prior_state.last_reference;
    result.state.valid = has_current_reference || HasUsableReference(result.state.last_reference);
    result.state.mode = has_current_reference ? result.reference_path.mode : prior_state.mode;
    result.state.compatibility_error_m = compatibility_error_m;
    result.state.reference_source = source;
    if (has_current_reference) {
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
// 根据场景状态和拓扑证据，从多个候选路径中选择最终预期路径：
// - Ordinary: 使用普通中心趋势，最后仅做局部限幅平滑
// - Cross: 有出口时从当前预期近端拼接到出口；无出口时只保持 trusted
// - Zebra: 保持上次（HoldLast），fallback 到 zebra_hold 候选
// - Circle Candidate: 外侧守卫路径（outer guard）
// - Circle Entry/Interior: 满足切换门控后锁存内圆跟随
// - Circle Exit: yaw 和切线兼容后锁存出环路径
// - Lost: 丢失预测（hold last reference）
// trusted path 不参与候选几何混合，也不拒绝候选；它只导出给 control error。
ReferencePolicyResult ResolveReferencePolicy(const port::RoadHypotheses& hypotheses,
                                             const port::TopologyEvidence& evidence,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params) {
    ReferencePolicyResult result{};
    result.state = prior_state;
    UpdateInnerIslandMemory(result.state, evidence, scene_state, params);
    result.trusted_error_reference = TrustedErrorReferenceFromPrior(prior_state, params);
    result.trusted_error_confidence =
        result.trusted_error_reference.valid ? std::clamp(prior_state.trusted_confidence, 0.0F, 1.0F) : 0.0F;
    float compatibility_error_m = 0.0F;
    std::string reference_source = "ordinary_none";
    std::string circle_reference_phase = "none";
    bool circle_inner_latched = false;
    bool circle_exit_latched = false;

    // 默认使用 ordinary 候选。trusted path 不参与几何混合，也不拒绝候选。
    result.reference_path = ExpectedReferenceFromCandidate(hypotheses.ordinary, port::ReferenceMode::kCenterline);
    if (result.reference_path.valid) {
        reference_source = "ordinary_final_smooth";
    }

    // === 十字路口场景 ===
    if (scene_state.active_scene == port::SpecialSceneKind::kCross) {
        result.reference_path = {};
        reference_source = "cross_none";
        port::BEVReferencePath entry_reference =
            ExpectedReferenceFromCandidate(hypotheses.ordinary, port::ReferenceMode::kCenterline);
        if (!entry_reference.valid && TrustedReferenceAvailable(prior_state, params)) {
            entry_reference = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
        }
        if (hypotheses.cross_exit.valid) {
            result.reference_path =
                BuildCrossExitReference(hypotheses.cross_exit,
                                        entry_reference,
                                        evidence.element_evidence.cross_band,
                                        params);
            reference_source = result.reference_path.valid ? "cross_exit_stitch" : reference_source;
        }
        if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params)) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
            reference_source = result.reference_path.valid ? "cross_hold_trusted" : reference_source;
        }
    // === 斑马线场景 ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kZebra) {
        result.reference_path =
            TrustedReferenceAvailable(prior_state, params)
                ? ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast)
                : port::BEVReferencePath{};
        reference_source = result.reference_path.valid ? "zebra_hold_trusted" : "zebra_none";
        if (!result.reference_path.valid && hypotheses.zebra_hold.valid) {
            result.reference_path = ExpectedReferenceFromCandidate(hypotheses.zebra_hold,
                                                                   port::ReferenceMode::kHoldLast);
            reference_source = result.reference_path.valid ? "zebra_hold_candidate" : reference_source;
        }
    // === 环岛候选状态（未确认环岛，用外侧守卫） ===
    } else if (IsCircleCandidateState(scene_state)) {
        const port::PathCandidate& candidate =
            scene_state.candidate_scene == port::SpecialSceneKind::kCircleLeft
                ? hypotheses.circle_outer_guard_left
                : hypotheses.circle_outer_guard_right;
        result.reference_path = ExpectedReferenceFromCandidate(candidate, port::ReferenceMode::kOuterOffset);
        reference_source = result.reference_path.valid ? "circle_candidate_outer_guard" : "circle_candidate_none";
        circle_reference_phase = "outer_guard_entry";
        if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params)) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
            reference_source = result.reference_path.valid ? "circle_candidate_hold_trusted" : reference_source;
        }
    // === 左环岛场景（Entry → Interior → Exit） ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        result.reference_path = {};
        reference_source = "circle_left_none";
        circle_inner_latched = prior_state.circle_inner_latched;
        circle_exit_latched = prior_state.circle_exit_latched;
        const bool exit_allowed =
            circle_exit_latched ||
            CircleExitSwitchAllowed(scene_state, hypotheses.circle_inner_left, hypotheses.circle_exit_left, params);
        if (scene_state.phase == port::SpecialScenePhase::kExit && exit_allowed) {
            result.reference_path =
                ExpectedReferenceFromCandidate(hypotheses.circle_exit_left, port::ReferenceMode::kBlendToExit);
            circle_exit_latched = circle_exit_latched || result.reference_path.valid;
            circle_inner_latched = true;
            circle_reference_phase = "exit_guard";
            reference_source = result.reference_path.valid ? "circle_exit_guard" : "circle_left_exit_none";
        } else {
            const bool inner_allowed =
                circle_inner_latched ||
                InnerCircleSwitchAllowed(hypotheses.circle_inner_left,
                                         hypotheses.circle_outer_guard_left,
                                         prior_state,
                                         params);
            if (inner_allowed) {
                const port::ReferenceMode mode =
                    hypotheses.circle_inner_left.mode == port::ReferenceMode::kCircleInnerIsland
                        ? port::ReferenceMode::kCircleInnerIsland
                        : port::ReferenceMode::kArcFollow;
                result.reference_path = ExpectedReferenceFromCandidate(hypotheses.circle_inner_left, mode);
                circle_inner_latched = circle_inner_latched || result.reference_path.valid;
                circle_reference_phase = "inner_follow";
                reference_source =
                    result.reference_path.valid ? "circle_inner_island_memory_follow" : "circle_left_inner_none";
            }
            if (!result.reference_path.valid && !circle_inner_latched) {
                result.reference_path =
                    ExpectedReferenceFromCandidate(hypotheses.circle_outer_guard_left,
                                                   port::ReferenceMode::kOuterOffset);
                circle_reference_phase = "outer_guard_entry";
                reference_source = result.reference_path.valid ? "circle_outer_guard_entry" : reference_source;
            }
        }
        if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params)) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
            reference_source = result.reference_path.valid ? "circle_left_lost_prediction" : reference_source;
        }
    // === 右环岛场景（Entry → Interior → Exit） ===
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.reference_path = {};
        reference_source = "circle_right_none";
        circle_inner_latched = prior_state.circle_inner_latched;
        circle_exit_latched = prior_state.circle_exit_latched;
        const bool exit_allowed =
            circle_exit_latched ||
            CircleExitSwitchAllowed(scene_state, hypotheses.circle_inner_right, hypotheses.circle_exit_right, params);
        if (scene_state.phase == port::SpecialScenePhase::kExit && exit_allowed) {
            result.reference_path =
                ExpectedReferenceFromCandidate(hypotheses.circle_exit_right, port::ReferenceMode::kBlendToExit);
            circle_exit_latched = circle_exit_latched || result.reference_path.valid;
            circle_inner_latched = true;
            circle_reference_phase = "exit_guard";
            reference_source = result.reference_path.valid ? "circle_exit_guard" : "circle_right_exit_none";
        } else {
            const bool inner_allowed =
                circle_inner_latched ||
                InnerCircleSwitchAllowed(hypotheses.circle_inner_right,
                                         hypotheses.circle_outer_guard_right,
                                         prior_state,
                                         params);
            if (inner_allowed) {
                const port::ReferenceMode mode =
                    hypotheses.circle_inner_right.mode == port::ReferenceMode::kCircleInnerIsland
                        ? port::ReferenceMode::kCircleInnerIsland
                        : port::ReferenceMode::kArcFollow;
                result.reference_path = ExpectedReferenceFromCandidate(hypotheses.circle_inner_right, mode);
                circle_inner_latched = circle_inner_latched || result.reference_path.valid;
                circle_reference_phase = "inner_follow";
                reference_source =
                    result.reference_path.valid ? "circle_inner_island_memory_follow" : "circle_right_inner_none";
            }
            if (!result.reference_path.valid && !circle_inner_latched) {
                result.reference_path =
                    ExpectedReferenceFromCandidate(hypotheses.circle_outer_guard_right,
                                                   port::ReferenceMode::kOuterOffset);
                circle_reference_phase = "outer_guard_entry";
                reference_source = result.reference_path.valid ? "circle_outer_guard_entry" : reference_source;
            }
        }
        if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params)) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
            reference_source = result.reference_path.valid ? "circle_right_lost_prediction" : reference_source;
        }
    // === 丢失预测模式（ordinary 无效且 lost_score 高） ===
    } else if (!hypotheses.ordinary.valid && evidence.lost_score > 0.65F &&
               TrustedReferenceAvailable(prior_state, params)) {
        result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
        reference_source = "lost_prediction_trusted";
    } else if (!result.reference_path.valid && TrustedReferenceAvailable(prior_state, params) &&
               evidence.lost_score > 0.65F) {
        result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
        reference_source = result.reference_path.valid ? "lost_prediction_trusted" : reference_source;
    }

    SmoothFinalReferencePath(result.reference_path, params);
    if (result.reference_path.valid && reference_source == "ordinary_none") {
        reference_source = "ordinary_final_smooth";
    }
    result.state.circle_reference_phase = circle_reference_phase;
    result.state.circle_inner_latched = circle_inner_latched;
    result.state.circle_exit_latched = circle_exit_latched;

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
        case port::ReferenceMode::kCircleInnerIsland:
            return "circle_inner_island";
        case port::ReferenceMode::kCenterline:
        default:
            return "centerline";
    }
}

}  // namespace ls2k::legacy
