#ifndef LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
#define LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP

#include <array>
#include <cstddef>

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct BEVTopologyPipelineResult {
    BEVSparseSampleGrid sparse_samples{};
    CorridorIntervalSet corridor_intervals{};
    CorridorGraph corridor_graph{};
    port::BEVTrackEstimate track_estimate{};
};

port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m);

port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m,
    float tangent_fit_window_m);

void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m);

void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m,
                            float tangent_fit_window_m);

int NormalizePathToForwardSamples(
    std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    const std::array<float, port::kBevTrackSampleCount>& forward_samples_m,
    float max_extrapolate_m,
    float max_lateral_smooth_adjust_m);

port::BEVTrackEstimate ComputeBevTrackEstimate(const port::LegacyCameraFrame& frame,
                                               int threshold,
                                               const port::RuntimeParameters& params,
                                               const port::LegacySteeringState& prior_state,
                                               const BEVProjector& projector);

BEVTopologyPipelineResult RunBEVTopologyPipeline(const port::LegacyCameraFrame& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const port::LegacySteeringState& prior_state,
                                                 const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
