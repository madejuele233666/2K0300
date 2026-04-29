#include "legacy/steering_scene_fsm.hpp"

// 场景状态机实现。
// 管理四种特殊场景（十字路口/环岛左/环岛右/斑马线）的生命周期。
// 状态迁移路径：Idle → Candidate → Entry → Interior → Exit → Idle
// 每条路径的触发阈值（confirm/hold/release）由 RuntimeParameters 控制。

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

// 环岛转角存在的最低评分阈值
constexpr float kCircleCornerPresentScoreMin = 0.65F;

// 将环岛场景枚举解析为方向字符串
std::string ResolveCircleDirection(port::SpecialSceneKind scene) {
    return scene == port::SpecialSceneKind::kCircleLeft
               ? "left"
               : (scene == port::SpecialSceneKind::kCircleRight ? "right" : "none");
}

// 判断场景是否为环岛（左或右）
bool IsCircleScene(port::SpecialSceneKind scene) {
    return scene == port::SpecialSceneKind::kCircleLeft ||
           scene == port::SpecialSceneKind::kCircleRight;
}

// 重置环岛进度（航向累积、时间戳、阶段）
void ResetCircleProgress(port::SpecialSceneFsmState& state) {
    state.circle_yaw_accum_deg = 0.0F;
    state.circle_yaw_last_time_ms = 0;
    state.circle_path_phase = "none";
}

// 开始环岛进度追踪（重置累积 + 记录起始时间 + 设置阶段）
void StartCircleProgress(const port::VehicleContext& vehicle,
                         port::SpecialSceneFsmState& state,
                         const char* phase) {
    state.circle_yaw_accum_deg = 0.0F;
    state.circle_yaw_last_time_ms = vehicle.capture_time_ms;
    state.circle_path_phase = phase;
}

// 通过陀螺仪积分累积环岛航向角
// 用于判断车辆在环岛中的旋转进度，达到 exit 阈值后切换到退出阶段
void IntegrateCircleYaw(const port::VehicleContext& vehicle,
                        port::SpecialSceneFsmState& state) {
    if (!vehicle.imu_valid || vehicle.capture_time_ms == 0) {
        return;
    }
    if (state.circle_yaw_last_time_ms == 0 ||
        vehicle.capture_time_ms <= state.circle_yaw_last_time_ms) {
        state.circle_yaw_last_time_ms = vehicle.capture_time_ms;
        return;
    }
    const float dt_s =
        std::min(0.25F,
                 static_cast<float>(vehicle.capture_time_ms - state.circle_yaw_last_time_ms) *
                     0.001F);
    state.circle_yaw_accum_deg += std::abs(vehicle.yaw_rate_deg_s) * dt_s;
    state.circle_yaw_last_time_ms = vehicle.capture_time_ms;
}

// 根据场景类型获取对应的环岛转角证据（左圈或右圈）
const port::BEVCircleCornerEvidence* CircleCornerForScene(port::SpecialSceneKind scene,
                                                          const port::TopologyEvidence& evidence) {
    if (!evidence.element_evidence.valid) {
        return nullptr;
    }
    if (scene == port::SpecialSceneKind::kCircleLeft) {
        return evidence.element_evidence.left_circle_corner.present
                   ? &evidence.element_evidence.left_circle_corner
                   : nullptr;
    }
    if (scene == port::SpecialSceneKind::kCircleRight) {
        return evidence.element_evidence.right_circle_corner.present
                   ? &evidence.element_evidence.right_circle_corner
                   : nullptr;
    }
    return nullptr;
}

// 检查环岛候选是否与之前的锚定位置兼容（位置变化是否在容差范围内）
// 用于 Candidate 阶段多次检测到环岛转角时做空间一致性验证
bool IsCompatibleCircleConfirmation(port::SpecialSceneKind scene,
                                    const port::TopologyEvidence& evidence,
                                    const port::RuntimeParameters& params,
                                    const port::SpecialSceneFsmState& state) {
    const port::BEVCircleCornerEvidence* corner = CircleCornerForScene(scene, evidence);
    if (corner == nullptr) {
        return !evidence.element_evidence.valid;
    }
    if (!state.circle_candidate_anchor_valid) {
        return false;
    }
    const float sample_spacing =
        std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                 params.bev_topology_sampler.forward_samples_m[0]);
    const float forward_delta_min = std::max(0.08F, sample_spacing * 2.0F);
    const float lateral_delta_min =
        std::max(0.08F, std::abs(params.bev_topology_sampler.lateral_step_m) * 4.0F);
    const float forward_delta = std::abs(corner->forward_m - state.circle_candidate_forward_m);
    const float lateral_delta = std::abs(corner->lateral_m - state.circle_candidate_lateral_m);
    if (forward_delta >= forward_delta_min || lateral_delta >= lateral_delta_min) {
        return true;
    }

    const bool candidate_survived_gap = state.progress_cycles > 0;
    const float compatible_forward_window = std::max(0.22F, forward_delta_min * 2.0F);
    const float compatible_lateral_window = std::max(0.14F, lateral_delta_min * 1.5F);
    return candidate_survived_gap &&
           corner->score >= kCircleCornerPresentScoreMin &&
           forward_delta <= compatible_forward_window &&
           lateral_delta <= compatible_lateral_window;
}

// 环岛候选保持周期数（release_cycles + confirm_cycles）
int CircleCandidateHoldCycles(const port::RuntimeParameters& params) {
    return std::max(params.bev_scene_fsm.circle_release_cycles,
                    params.bev_scene_fsm.circle_release_cycles +
                        params.bev_scene_fsm.circle_confirm_cycles);
}

// 存储环岛候选锚点位置（用于后续的兼容性验证）
void StoreCircleCandidateAnchor(port::SpecialSceneKind scene,
                                const port::TopologyEvidence& evidence,
                                port::SpecialSceneFsmState& state) {
    const port::BEVCircleCornerEvidence* corner = CircleCornerForScene(scene, evidence);
    if (corner == nullptr) {
        state.circle_candidate_anchor_valid = false;
        state.circle_candidate_forward_m = 0.0F;
        state.circle_candidate_lateral_m = 0.0F;
        return;
    }
    state.circle_candidate_anchor_valid = true;
    state.circle_candidate_forward_m = corner->forward_m;
    state.circle_candidate_lateral_m = corner->lateral_m;
}

// 清除环岛候选锚点（非环岛场景确认时调用）
void ClearCircleCandidateAnchor(port::SpecialSceneFsmState& state) {
    state.circle_candidate_anchor_valid = false;
    state.circle_candidate_forward_m = 0.0F;
    state.circle_candidate_lateral_m = 0.0F;
}

// 从拓扑证据中提取指定场景类型的得分
float TopologyCandidateScore(port::SpecialSceneKind scene, const port::TopologyEvidence& evidence) {
    switch (scene) {
        case port::SpecialSceneKind::kCross:
            return evidence.cross_score;
        case port::SpecialSceneKind::kZebra:
            return evidence.zebra_score;
        case port::SpecialSceneKind::kCircleLeft:
            return evidence.left_circle_score;
        case port::SpecialSceneKind::kCircleRight:
            return evidence.right_circle_score;
        case port::SpecialSceneKind::kOrdinary:
        default:
            return evidence.ordinary_score;
    }
}

// 从拓扑证据中选择最佳场景候选（超过进入阈值且得分最高的场景）
port::SpecialSceneKind PickTopologyCandidate(const port::TopologyEvidence& evidence,
                                             const port::RuntimeParameters& params) {
    port::SpecialSceneKind best = port::SpecialSceneKind::kOrdinary;
    float best_score = params.bev_topology_evidence.ordinary_release_score;
    if (evidence.cross_score >= params.bev_topology_evidence.cross_enter_score &&
        evidence.cross_score >= best_score) {
        best = port::SpecialSceneKind::kCross;
        best_score = evidence.cross_score;
    }
    if (evidence.zebra_score >= params.bev_topology_evidence.zebra_enter_score &&
        evidence.zebra_score >= best_score) {
        best = port::SpecialSceneKind::kZebra;
        best_score = evidence.zebra_score;
    }
    if (evidence.left_circle_score >= params.bev_topology_evidence.circle_enter_score &&
        evidence.left_circle_score >= best_score) {
        best = port::SpecialSceneKind::kCircleLeft;
        best_score = evidence.left_circle_score;
    }
    if (evidence.right_circle_score >= params.bev_topology_evidence.circle_enter_score &&
        evidence.right_circle_score >= best_score) {
        best = port::SpecialSceneKind::kCircleRight;
    }
    return best;
}

}  // namespace

// ========== 拓扑版场景 FSM（基于 TopologyEvidence，带车辆上下文） ==========
// 完整的状态机生命周期管理，比旧版增加：
// - 环岛陀螺仪航向积分（检测环岛退出）
// - 环岛候选锚定追踪（位置一致性验证后才确认）
// - 十字路口 Entry → Hold → Exit 三阶段
// - 更精细的 lost_score 处理（> 0.65 时进入 lost_prediction 状态）
SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::VehicleContext& vehicle,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state) {
    SceneFsmResult result{};
    result.state = prior_state;

    const port::SpecialSceneKind candidate = PickTopologyCandidate(evidence, params);
    const float candidate_score = TopologyCandidateScore(candidate, evidence);
    // 检查是否正在保持环岛候选（ordinary 但 phase=candidate 且 candidate 是环岛）
    const bool holding_circle_candidate =
        result.state.active_scene == port::SpecialSceneKind::kOrdinary &&
        result.state.phase == port::SpecialScenePhase::kCandidate &&
        IsCircleScene(result.state.candidate_scene);

    // 候选连续性：同场景候选累加 streak，新候选重置 streak
    if (candidate != port::SpecialSceneKind::kOrdinary &&
        candidate == result.state.candidate_scene) {
        ++result.state.candidate_streak;
    } else if (candidate != port::SpecialSceneKind::kOrdinary) {
        result.state.candidate_scene = candidate;
        result.state.candidate_streak = 1;
        result.state.progress_cycles = 0;
    } else if (!holding_circle_candidate) {
        result.state.candidate_scene = port::SpecialSceneKind::kOrdinary;
        result.state.candidate_streak = 0;
    }

    result.state.debug_candidate =
        evidence.lost_score > 0.65F ? "lost"
                                    : (result.state.candidate_scene == port::SpecialSceneKind::kOrdinary
                                           ? "none"
                                           : ToString(result.state.candidate_scene));
    result.state.debug_candidate_score =
        evidence.lost_score > 0.65F ? evidence.lost_score : candidate_score;

    // === 已激活环岛场景：Entry → Interior → Exit ===
    if (result.state.active_scene == port::SpecialSceneKind::kCircleLeft ||
        result.state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.state.latched = true;
        result.state.circle_direction = ResolveCircleDirection(result.state.active_scene);
        IntegrateCircleYaw(vehicle, result.state);
        const float direction_score = result.state.active_scene == port::SpecialSceneKind::kCircleLeft
                                          ? evidence.left_circle_score
                                          : evidence.right_circle_score;
        if (result.state.phase == port::SpecialScenePhase::kEntry) {
            result.state.circle_path_phase = "entry";
            ++result.state.progress_cycles;
            if (result.state.progress_cycles >= 2) {
                result.state.phase = port::SpecialScenePhase::kInterior;
                result.state.progress_cycles = 0;
                result.state.circle_path_phase = "interior";
            }
        } else if (result.state.phase == port::SpecialScenePhase::kInterior) {
            result.state.circle_path_phase = "interior";
            const bool gyro_exit =
                vehicle.imu_valid &&
                result.state.circle_yaw_accum_deg >= params.bev_path_policy.circle_exit_yaw_deg;
            if (gyro_exit) {
                result.state.phase = port::SpecialScenePhase::kExit;
                result.state.progress_cycles = 0;
                result.state.release_cycles = 0;
                result.state.circle_path_phase = "exit";
            } else if (direction_score < params.bev_topology_evidence.circle_release_score) {
                ++result.state.release_cycles;
                if (result.state.release_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                    result.state.phase = port::SpecialScenePhase::kExit;
                    result.state.progress_cycles = 0;
                    result.state.circle_path_phase = "exit";
                }
            } else {
                result.state.release_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kExit) {
            result.state.circle_path_phase = "exit";
            ++result.state.progress_cycles;
            if (evidence.ordinary_score >= params.bev_topology_evidence.ordinary_release_score ||
                result.state.progress_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                result.state = {};
            }
        }
    // === 已激活十字路口场景：Entry → Hold → Exit ===
    } else if (result.state.active_scene == port::SpecialSceneKind::kCross) {
        if (result.state.phase == port::SpecialScenePhase::kEntry) {
            ++result.state.progress_cycles;
            result.state.phase = port::SpecialScenePhase::kHold;
        } else if (result.state.phase == port::SpecialScenePhase::kHold) {
            ++result.state.progress_cycles;
            if (evidence.cross_score < params.bev_topology_evidence.cross_release_score ||
                result.state.progress_cycles >= params.bev_scene_fsm.cross_hold_cycles) {
                result.state.phase = port::SpecialScenePhase::kExit;
                result.state.progress_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kExit) {
            ++result.state.progress_cycles;
            if (evidence.ordinary_score >= params.bev_topology_evidence.ordinary_release_score ||
                result.state.progress_cycles >= params.bev_reference_policy.blend_min_cycles) {
                result.state = {};
            }
        }
    // === 已激活斑马线场景（拓扑版）：基于 zebra_score 释放 ===
    } else if (result.state.active_scene == port::SpecialSceneKind::kZebra) {
        ++result.state.progress_cycles;
        if (evidence.zebra_score < params.bev_topology_evidence.zebra_release_score ||
            result.state.progress_cycles >= params.bev_scene_fsm.zebra_hold_cycles) {
            result.state = {};
        }
    // === 未激活场景（Idle / Candidate 阶段） ===
    } else {
        // 保持环岛候选：如果 candidate 消失但之前有环岛候选，在 HoldCycles 内保持
        if (result.state.phase == port::SpecialScenePhase::kCandidate &&
            IsCircleScene(result.state.candidate_scene) &&
            candidate == port::SpecialSceneKind::kOrdinary) {
            ++result.state.progress_cycles;
            if (result.state.progress_cycles > CircleCandidateHoldCycles(params)) {
                result.state = {};
            }
        // 环岛候选确认：检查空间兼容性，满足条件则激活环岛场景
        } else if (IsCircleScene(candidate)) {
            const bool confirms_circle =
                result.state.phase == port::SpecialScenePhase::kCandidate &&
                IsCircleScene(result.state.candidate_scene) &&
                IsCompatibleCircleConfirmation(candidate, evidence, params, result.state);
            if (confirms_circle) {
                result.state.active_scene = candidate;
                result.state.progress_cycles = 0;
                result.state.release_cycles = 0;
                result.state.latched = true;
                result.state.circle_direction = ResolveCircleDirection(candidate);
                result.state.circle_entry_signal_active = true;
                result.state.phase = port::SpecialScenePhase::kEntry;
                StartCircleProgress(vehicle, result.state, "entry");
            } else {
                result.state.active_scene = port::SpecialSceneKind::kOrdinary;
                result.state.phase = port::SpecialScenePhase::kCandidate;
                result.state.circle_direction = ResolveCircleDirection(candidate);
                result.state.circle_entry_signal_active = false;
                result.state.circle_path_phase = "candidate";
                result.state.progress_cycles = 0;
                if (!result.state.circle_candidate_anchor_valid ||
                    result.state.candidate_scene != candidate) {
                    StoreCircleCandidateAnchor(candidate, evidence, result.state);
                }
            }
        // 非环岛候选确认（cross/zebra）：累积足够 streak 后激活
        } else {
            const int required_cycles =
                candidate == port::SpecialSceneKind::kCross
                    ? params.bev_scene_fsm.cross_confirm_cycles
                    : (IsCircleScene(candidate) ? params.bev_scene_fsm.circle_confirm_cycles : 1);
            if (candidate != port::SpecialSceneKind::kOrdinary &&
                result.state.candidate_streak >= required_cycles) {
                result.state.active_scene = candidate;
                result.state.progress_cycles = 0;
                result.state.release_cycles = 0;
                result.state.latched = IsCircleScene(candidate);
                result.state.circle_direction = ResolveCircleDirection(candidate);
                result.state.circle_entry_signal_active = result.state.latched;
                result.state.phase =
                    candidate == port::SpecialSceneKind::kCross ? port::SpecialScenePhase::kEntry
                                                                : port::SpecialScenePhase::kEntry;
                if (IsCircleScene(candidate)) {
                    StartCircleProgress(vehicle, result.state, "entry");
                }
                if (candidate == port::SpecialSceneKind::kZebra) {
                    result.state.phase = port::SpecialScenePhase::kHold;
                }
                if (!IsCircleScene(candidate)) {
                    ClearCircleCandidateAnchor(result.state);
                    ResetCircleProgress(result.state);
                }
            } else {
                result.state.active_scene = port::SpecialSceneKind::kOrdinary;
                result.state.phase =
                    evidence.lost_score > 0.65F ? port::SpecialScenePhase::kHold : port::SpecialScenePhase::kIdle;
                ClearCircleCandidateAnchor(result.state);
                ResetCircleProgress(result.state);
            }
        }
    }

    if (result.state.phase == port::SpecialScenePhase::kCandidate &&
        IsCircleScene(result.state.candidate_scene)) {
        result.state.debug_candidate = ToString(result.state.candidate_scene);
        result.state.debug_candidate_score =
            result.state.candidate_scene == port::SpecialSceneKind::kCircleLeft
                ? evidence.left_circle_score
                : evidence.right_circle_score;
    } else {
        result.state.debug_candidate =
            evidence.lost_score > 0.65F ? "lost"
                                        : (result.state.candidate_scene == port::SpecialSceneKind::kOrdinary
                                               ? "none"
                                               : ToString(result.state.candidate_scene));
        result.state.debug_candidate_score =
            evidence.lost_score > 0.65F ? evidence.lost_score : candidate_score;
    }

    // 设置输出模块名和场景阶段字符串
    switch (result.state.active_scene) {
        case port::SpecialSceneKind::kCross:
            result.active_module = "cross";
            result.scene_override_source = "topology_evidence";
            result.scene_phase = result.state.phase == port::SpecialScenePhase::kEntry
                                     ? "cross_approach"
                                     : (result.state.phase == port::SpecialScenePhase::kExit
                                            ? "cross_reacquire"
                                            : "cross_hold");
            break;
        case port::SpecialSceneKind::kZebra:
            result.active_module = "zebra";
            result.scene_phase = "zebra_hold";
            result.scene_override_source = "topology_evidence";
            break;
        case port::SpecialSceneKind::kCircleLeft:
        case port::SpecialSceneKind::kCircleRight:
            result.active_module = "circle";
            result.scene_override_source = "topology_evidence";
            result.scene_phase = result.state.phase == port::SpecialScenePhase::kEntry
                                     ? "circle_entry"
                                     : (result.state.phase == port::SpecialScenePhase::kInterior
                                            ? "circle_interior"
                                            : "circle_exit");
            break;
        case port::SpecialSceneKind::kOrdinary:
        case port::SpecialSceneKind::kBend:
        default:
            result.active_module = "straight";
            if (result.state.phase == port::SpecialScenePhase::kCandidate &&
                IsCircleScene(result.state.candidate_scene)) {
                result.scene_phase = "circle_candidate";
                result.scene_override_source = "topology_evidence";
            } else {
                result.scene_phase =
                    evidence.lost_score > 0.65F ? "lost_prediction" : "idle";
                result.scene_override_source =
                    evidence.lost_score > 0.65F ? "topology_evidence" : "none";
            }
            break;
    }

    return result;
}

SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state) {
    return UpdateTopologySceneFsm(evidence, port::VehicleContext{}, params, prior_state);
}

const char* ToString(port::SpecialSceneKind kind) {
    switch (kind) {
        case port::SpecialSceneKind::kBend:
            return "bend";
        case port::SpecialSceneKind::kCross:
            return "cross";
        case port::SpecialSceneKind::kZebra:
            return "zebra";
        case port::SpecialSceneKind::kCircleLeft:
            return "circle_left";
        case port::SpecialSceneKind::kCircleRight:
            return "circle_right";
        case port::SpecialSceneKind::kOrdinary:
        default:
            return "ordinary";
    }
}

const char* ToString(port::SpecialScenePhase phase) {
    switch (phase) {
        case port::SpecialScenePhase::kCandidate:
            return "candidate";
        case port::SpecialScenePhase::kConfirm:
            return "confirm";
        case port::SpecialScenePhase::kEntry:
            return "entry";
        case port::SpecialScenePhase::kInterior:
            return "interior";
        case port::SpecialScenePhase::kExit:
            return "exit";
        case port::SpecialScenePhase::kHold:
            return "hold";
        case port::SpecialScenePhase::kRelease:
            return "release";
        case port::SpecialScenePhase::kIdle:
        default:
            return "idle";
    }
}

}  // namespace ls2k::legacy
