#ifndef LS2K_LEGACY_STEERING_SCENE_FSM_HPP
#define LS2K_LEGACY_STEERING_SCENE_FSM_HPP

// 场景状态机层 —— 根据拓扑证据驱动场景状态迁移。
// 管理四种特殊场景（十字路口、环岛左/右、斑马线）的生命周期：
// Candidate → Confirm → Entry → Interior → Exit → Release。

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

// 场景 FSM 输出结果
struct SceneFsmResult {
    port::SpecialSceneFsmState state{};        //!< FSM 内部状态
    std::string active_module = "straight";     //!< 当前激活的模块名（straight/cross/circle/zebra/bend）
    std::string scene_phase = "idle";           //!< 当前场景阶段
    std::string scene_override_source = "none"; //!< 场景覆盖来源（scene_fsm/topology_evidence）
};

// 拓扑版场景 FSM（带车辆上下文）：基于 TopologyEvidence 更新场景状态
SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::VehicleContext& vehicle,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state);

// 拓扑版场景 FSM（无车辆上下文）：简化接口，适用于无 IMU 场景
SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state);

// 场景枚举转字符串
const char* ToString(port::SpecialSceneKind kind);
// 场景阶段枚举转字符串
const char* ToString(port::SpecialScenePhase phase);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_FSM_HPP
