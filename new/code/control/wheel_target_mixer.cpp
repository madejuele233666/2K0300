#include "control/wheel_target_mixer.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::control {

/// WheelTargetMixer::Compute 实现
/// 根据差速转向原理计算左右轮目标速度
/// 正转向输出：左轮加速、右轮减速
/// 负转向输出：左轮减速、右轮加速
WheelSpeedTargets WheelTargetMixer::Compute(double effective_speed_target,
                                            int applied_turn_output,
                                            const WheelTargetMixerParameters& params) const {
    const double base_target = std::max(0.0, effective_speed_target);
    const double turn_delta = std::abs(static_cast<double>(applied_turn_output));
    const double accel_delta = turn_delta * params.accel_delta_scale;
    const double decel_delta = turn_delta * params.decel_delta_scale;

    WheelSpeedTargets targets{};
    if (applied_turn_output > 0) {
        targets.left = std::max(0.0, base_target + accel_delta);
        targets.right = std::max(0.0, base_target - decel_delta);
    } else if (applied_turn_output < 0) {
        targets.left = std::max(0.0, base_target - decel_delta);
        targets.right = std::max(0.0, base_target + accel_delta);
    } else {
        targets.left = base_target;
        targets.right = base_target;
    }
    return targets;
}

}  // namespace ls2k::control
