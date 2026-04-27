#ifndef LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP
#define LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP

#include <array>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct BEVSampleLayer {
    float forward_m = 0.0F;
    std::vector<port::BEVSample> samples{};
};

struct BEVSparseSampleGrid {
    bool valid = false;
    std::array<BEVSampleLayer, port::kBevTrackSampleCount> layers{};
};

BEVSparseSampleGrid SparseMetricSample(const port::LegacyCameraFrame& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP
