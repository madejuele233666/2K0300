#ifndef LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP
#define LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP

#include <string>

namespace ls2k::port {

struct ReferenceControlReadiness {
    bool ready = false;
    bool degraded = false;
    std::string reason = "reference_unusable";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP
