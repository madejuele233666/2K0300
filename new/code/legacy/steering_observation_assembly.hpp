#ifndef LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP
#define LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP

#include "legacy/steering_bev_projector.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct ObservationAssemblyResult {
    port::VehicleContext vehicle{};
    port::BEVSceneObservation observation{};
    port::ControlConstraintSet constraints{};
};

ObservationAssemblyResult AssembleObservation(const port::LegacyCameraFrame& frame,
                                              int threshold,
                                              const port::RuntimeParameters& params,
                                              const port::LegacySteeringState& prior_state,
                                              const port::ImuSample& imu,
                                              bool low_voltage_emergency,
                                              uint64_t frame_id,
                                              uint64_t capture_time_ms,
                                              const port::BEVTrackEstimate& track,
                                              const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP
