#ifndef LS2K_PORT_HARDWARE_PROFILE_HPP
#define LS2K_PORT_HARDWARE_PROFILE_HPP

#include <string>

namespace ls2k::port {

enum class SubsystemMode {
    kDirectMatch,
    kAdaptationHook,
    kDisabled
};

struct SubsystemProfile {
    SubsystemMode mode = SubsystemMode::kDisabled;
    std::string hook = "unconfigured";
};

struct HardwareProfile {
    SubsystemProfile camera{};
    SubsystemProfile imu{};
    SubsystemProfile encoder{};
    SubsystemProfile motor{};
    SubsystemProfile timer{};
    SubsystemProfile persistence{};
    SubsystemProfile display{SubsystemMode::kDisabled, "phase1-deferred"};
};

inline bool IsEnabled(const SubsystemProfile& profile) {
    return profile.mode != SubsystemMode::kDisabled;
}

const char* ToString(SubsystemMode mode);

}  // namespace ls2k::port

#endif  // LS2K_PORT_HARDWARE_PROFILE_HPP
