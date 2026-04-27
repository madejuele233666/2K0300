#ifndef LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP
#define LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP

#include <array>
#include <vector>

#include "legacy/steering_bev_sparse_sampler.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct CorridorIntervalLayer {
    float forward_m = 0.0F;
    std::vector<port::CorridorInterval> intervals{};
};

struct CorridorIntervalSet {
    bool valid = false;
    std::array<CorridorIntervalLayer, port::kBevTrackSampleCount> layers{};
};

CorridorIntervalSet ExtractCorridorIntervals(const BEVSparseSampleGrid& samples,
                                             const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP
