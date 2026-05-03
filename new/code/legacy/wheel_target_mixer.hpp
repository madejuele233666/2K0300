#ifndef LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
#define LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP

#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

struct WheelSpeedTargets {
    double left = 0.0;
    double right = 0.0;
};

class WheelTargetMixer {
public:
    void Configure(const port::RuntimeParameters& params);
    WheelSpeedTargets Compute(double effective_speed_target,
                              int turn_pwm_command,
                              int pwm_limit) const;

private:
    double turn_target_scale_ = 35.0;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
