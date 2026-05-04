#ifndef LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
#define LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP

namespace ls2k::legacy {

struct WheelSpeedTargets {
    double left = 0.0;
    double right = 0.0;
};

class WheelTargetMixer {
public:
    WheelSpeedTargets Compute(double effective_speed_target,
                              int applied_turn_output) const;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
