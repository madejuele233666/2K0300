#ifndef LS2K_PORT_REFERENCE_CURVATURE_TYPES_HPP
#define LS2K_PORT_REFERENCE_CURVATURE_TYPES_HPP

#include <string>

namespace ls2k::port {

struct ReferenceCurvatureEstimate {
    bool computed = false;
    float lookahead_distance_m = 0.0F;
    float curvature_command = 0.0F;
    std::string reason = "reference_unusable";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_CURVATURE_TYPES_HPP
