#ifndef LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP

#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

port::TopologyEvidence ScoreTopologyEvidence(const port::RoadHypotheses& hypotheses,
                                             const CorridorIntervalSet& intervals,
                                             const port::VehicleContext& vehicle,
                                             const port::RuntimeParameters& params,
                                             const port::TopologyEvidenceAccumulator& prior_accumulator);

port::TopologyEvidenceAccumulator UpdateTopologyEvidenceAccumulator(
    const port::TopologyEvidence& evidence,
    const port::RuntimeParameters& params,
    const port::TopologyEvidenceAccumulator& prior_accumulator);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_TOPOLOGY_EVIDENCE_HPP
