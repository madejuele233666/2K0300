#include "legacy/motor_logic.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

port::ActuatorCommand LegacyMotorLogic::Compose(int left_pwm,
                                                int right_pwm,
                                                bool emergency_stop,
                                                int pwm_limit) const {
    if (emergency_stop) {
        return {};
    }

    port::ActuatorCommand command{};
    command.left_pwm = std::clamp(left_pwm, -pwm_limit, pwm_limit);
    command.right_pwm = std::clamp(right_pwm, -pwm_limit, pwm_limit);
    command.emergency_stop = false;
    return command;
}

}  // namespace ls2k::legacy
