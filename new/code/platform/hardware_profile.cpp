#include "port/hardware_profile.hpp"

namespace ls2k::port {

const char* ToString(SubsystemMode mode) {
    switch (mode) {
        case SubsystemMode::kDirectMatch:
            return "direct-match";
        case SubsystemMode::kAdaptationHook:
            return "adaptation-hook";
        case SubsystemMode::kDisabled:
            return "disabled";
    }
    return "unknown";
}

}  // namespace ls2k::port
