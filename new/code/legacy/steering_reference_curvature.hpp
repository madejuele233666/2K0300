#ifndef LS2K_LEGACY_STEERING_REFERENCE_CURVATURE_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_CURVATURE_HPP

#include "port/bev_reference_types.hpp"
#include "port/reference_curvature_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

port::ReferenceCurvatureEstimate ComputeReferenceCurvature(const port::BEVReferencePath& reference_path,
                                                           const port::ReferenceUsability& usability,
                                                           const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_CURVATURE_HPP
