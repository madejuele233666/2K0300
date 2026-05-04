#ifndef LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP
#define LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP

#include <cstddef>
#include <string>

namespace ls2k::port {

struct ReferenceLateralErrorEstimate {
    bool computed = false;
    float weighted_lateral_error_m = 0.0F;
    std::size_t weighted_sample_count = 0;
    float weight_sum = 0.0F;
    std::string reason = "reference_unusable";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_LATERAL_ERROR_TYPES_HPP
