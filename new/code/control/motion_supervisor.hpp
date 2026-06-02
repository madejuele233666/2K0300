#ifndef LS2K_CONTROL_MOTION_SUPERVISOR_HPP
#define LS2K_CONTROL_MOTION_SUPERVISOR_HPP

#include "control/motion_types.hpp"

namespace ls2k::control {

/// 运动监督器 —— 管理运动阶段状态机（DISARMED / START_REQUESTED / SPINUP / RUNNING / STOPPING / FAIL_SAFE_LATCHED）。
class MotionSupervisor {
public:
    /// 评估当前运动输入，确定下一运动阶段和决策
    /// @param inputs  运动监督器输入（含当前状态、意图、门控、参数等）
    /// @return        运动决策（包含状态转换、速度目标、转向限制等）
    MotionDecision Evaluate(const MotionSupervisorInputs& inputs) const;
};

}  // namespace ls2k::control

#endif  // LS2K_CONTROL_MOTION_SUPERVISOR_HPP
