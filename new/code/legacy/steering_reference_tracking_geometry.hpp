#ifndef LS2K_LEGACY_STEERING_REFERENCE_TRACKING_GEOMETRY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_TRACKING_GEOMETRY_HPP

#include "port/bev_geometry_types.hpp"
#include "port/bev_reference_types.hpp"
#include "port/reference_tracking_geometry_types.hpp"
#include "port/reference_usability_types.hpp"

namespace ls2k::legacy {

port::ReferenceTrackingGeometry ComputeReferenceTrackingGeometry(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::BEVControlModelParameters& control_model);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_TRACKING_GEOMETRY_HPP
