#include "legacy/steering_bev_geometry.hpp"

#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"

namespace ls2k::legacy {

port::BEVTrackEstimate ComputeBevTrackEstimate(const port::LegacyCameraFrame& frame,
                                               int threshold,
                                               const port::RuntimeParameters& params,
                                               const port::LegacySteeringState& prior_state,
                                               const BEVProjector& projector) {
    port::BEVTrackEstimate track{};
    track.calibration_valid = projector.Valid();
    track.source = "bev_corridor_topology";
    if (!projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        track.fallback_mode = "projector_invalid";
        return track;
    }

    const BEVSparseSampleGrid samples = SparseMetricSample(frame, threshold, params, projector);
    const CorridorIntervalSet intervals = ExtractCorridorIntervals(samples, params);
    const CorridorGraph graph = BuildCorridorGraph(intervals, params, prior_state);
    track = BuildBEVTrackEstimateFromCorridorGraph(intervals, graph, params);
    track.calibration_valid = projector.Valid();
    if (!track.valid && track.fallback_mode.empty()) {
        track.fallback_mode = "corridor_topology_unavailable";
    }
    return track;
}

}  // namespace ls2k::legacy
