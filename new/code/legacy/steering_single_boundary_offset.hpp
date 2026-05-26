#ifndef LS2K_LEGACY_STEERING_SINGLE_BOUNDARY_OFFSET_HPP
#define LS2K_LEGACY_STEERING_SINGLE_BOUNDARY_OFFSET_HPP

#include <vector>

#include "port/bev_geometry_types.hpp"

namespace ls2k::legacy {

/// Build leading BEV points by offsetting one visible boundary trace along its
/// local normal while resampling at caller-provided forward positions.
std::vector<port::BEVPoint> BuildSingleBoundaryOffsetReference(
    const std::vector<port::BEVPoint>& boundary_trace,
    const std::vector<float>& target_forward_samples,
    float signed_normal_offset_m);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SINGLE_BOUNDARY_OFFSET_HPP
