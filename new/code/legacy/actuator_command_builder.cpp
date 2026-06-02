#include "legacy/actuator_command_builder.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

port::ActuatorCommand ActuatorCommandBuilder::Compose(int left_drive_pwm,
                                                      int right_drive_pwm,
                                                      int left_brushless_pwm,
                                                      int right_brushless_pwm,
                                                      bool emergency_stop,
                                                      int drive_pwm_limit,
                                                      int brushless_pwm_limit) const {
    if (emergency_stop) {
        return {};
    }

    port::ActuatorCommand command{};
    command.left_drive_pwm = std::clamp(left_drive_pwm, -drive_pwm_limit, drive_pwm_limit);
    command.right_drive_pwm = std::clamp(right_drive_pwm, -drive_pwm_limit, drive_pwm_limit);
    command.left_brushless_pwm = std::clamp(left_brushless_pwm, 0, std::max(0, brushless_pwm_limit));
    command.right_brushless_pwm = std::clamp(right_brushless_pwm, 0, std::max(0, brushless_pwm_limit));
    command.emergency_stop = false;
    return command;
}

}  // namespace ls2k::legacy
