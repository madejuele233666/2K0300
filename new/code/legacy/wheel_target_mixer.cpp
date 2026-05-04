#include "legacy/wheel_target_mixer.hpp"

#include <algorithm>

namespace ls2k::legacy {

WheelSpeedTargets WheelTargetMixer::Compute(double effective_speed_target,
                                            int applied_turn_output) const {
    const double base_target = std::max(0.0, effective_speed_target);
    const double turn_delta = static_cast<double>(applied_turn_output);

    WheelSpeedTargets targets{};
    targets.left = std::max(0.0, base_target + turn_delta);
    targets.right = std::max(0.0, base_target - turn_delta);
    return targets;
}

}  // namespace ls2k::legacy
