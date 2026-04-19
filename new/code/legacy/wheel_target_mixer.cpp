#include "legacy/wheel_target_mixer.hpp"

#include <algorithm>

namespace ls2k::legacy {

void WheelTargetMixer::Configure(const port::RuntimeParameters& params) {
    turn_target_scale_ = params.wheel_turn_target_scale;
}

WheelSpeedTargets WheelTargetMixer::Compute(double effective_speed_target,
                                            int turn_pwm_command,
                                            int pwm_limit) const {
    const double safe_pwm_limit = pwm_limit <= 0 ? 1.0 : static_cast<double>(pwm_limit);
    const double turn_ratio =
        std::clamp(static_cast<double>(turn_pwm_command) / safe_pwm_limit, -1.0, 1.0);
    const double turn_delta = turn_ratio * turn_target_scale_;

    WheelSpeedTargets targets{};
    targets.left = std::max(0.0, effective_speed_target - turn_delta);
    targets.right = std::max(0.0, effective_speed_target + turn_delta);
    return targets;
}

}  // namespace ls2k::legacy
