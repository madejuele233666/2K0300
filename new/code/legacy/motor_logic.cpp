#include "legacy/motor_logic.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

port::ActuatorCommand LegacyMotorLogic::Mix(int mean_pwm,
                                            float turn_output,
                                            bool emergency_veto,
                                            int pwm_limit) const {
    if (emergency_veto) {
        return {};
    }

    const int left = static_cast<int>(std::round(static_cast<float>(mean_pwm) - turn_output));
    const int right = static_cast<int>(std::round(static_cast<float>(mean_pwm) + turn_output));

    port::ActuatorCommand command{};
    command.left_pwm = std::clamp(left, -pwm_limit, pwm_limit);
    command.right_pwm = std::clamp(right, -pwm_limit, pwm_limit);
    command.emergency_stop = false;
    return command;
}

}  // namespace ls2k::legacy
