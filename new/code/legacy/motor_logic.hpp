#ifndef LS2K_LEGACY_MOTOR_LOGIC_HPP
#define LS2K_LEGACY_MOTOR_LOGIC_HPP

#include "port/control_types.hpp"

namespace ls2k::legacy {

class LegacyMotorLogic {
public:
    port::ActuatorCommand Compose(int left_pwm, int right_pwm, bool emergency_veto, int pwm_limit) const;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_MOTOR_LOGIC_HPP
