#ifndef LS2K_LEGACY_STEERING_PATH_MATH_HPP
#define LS2K_LEGACY_STEERING_PATH_MATH_HPP

#include <algorithm>

#include "port/control_types.hpp"

namespace ls2k::legacy {

inline float PathCurvatureFromThreeSamples(const port::BEVPathSample& first,
                                           const port::BEVPathSample& second,
                                           const port::BEVPathSample& third) {
    const float ds1 = std::max(1e-4F, second.point.forward_m - first.point.forward_m);
    const float ds2 = std::max(1e-4F, third.point.forward_m - second.point.forward_m);
    const float slope1 = (second.point.lateral_m - first.point.lateral_m) / ds1;
    const float slope2 = (third.point.lateral_m - second.point.lateral_m) / ds2;
    return (slope2 - slope1) / std::max(1e-4F, third.point.forward_m - first.point.forward_m);
}

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_PATH_MATH_HPP
