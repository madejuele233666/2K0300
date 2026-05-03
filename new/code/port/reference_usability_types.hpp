#ifndef LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP
#define LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP

#include <cstddef>
#include <string>

namespace ls2k::port {

struct ReferenceUsability {
    bool usable = false;
    std::size_t leading_usable_samples = 0;
    float leading_min_forward_m = 0.0F;
    float leading_max_forward_m = 0.0F;
    float lookahead_distance_m = 0.0F;
    std::string reason = "no_reference_facts";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_USABILITY_TYPES_HPP
