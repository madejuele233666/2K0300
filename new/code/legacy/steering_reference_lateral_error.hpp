#ifndef LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP

#include "port/bev_reference_types.hpp"
#include "port/reference_lateral_error_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

port::ReferenceLateralErrorEstimate ComputeReferenceLateralError(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_LATERAL_ERROR_HPP
