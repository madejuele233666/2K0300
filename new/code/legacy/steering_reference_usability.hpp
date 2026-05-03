#ifndef LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP

#include "port/bev_reference_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

port::ReferenceUsability EvaluateReferenceUsability(const port::BEVReferencePath& reference_path,
                                                    const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_USABILITY_HPP
