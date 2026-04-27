#ifndef LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
#define LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP

#include "legacy/steering_bev_projector.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

port::BEVTrackEstimate ComputeBevTrackEstimate(const port::LegacyCameraFrame& frame,
                                               int threshold,
                                               const port::RuntimeParameters& params,
                                               const port::LegacySteeringState& prior_state,
                                               const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
