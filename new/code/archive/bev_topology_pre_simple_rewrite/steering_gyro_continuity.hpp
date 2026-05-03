#ifndef LS2K_LEGACY_STEERING_GYRO_CONTINUITY_HPP
#define LS2K_LEGACY_STEERING_GYRO_CONTINUITY_HPP

#include "port/sensor_sample_types.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::legacy {

struct GyroContinuityConstraint {
    port::GyroContinuityState next_state{};
    float heading_delta_deg = 0.0F;
    float prior_prediction_offset_px = 0.0F;
    bool imu_grace_active = false;
    bool weighting_active = false;
};

GyroContinuityConstraint ComputeGyroContinuityConstraint(
    const port::SteeringPerceptionMemory& prior_memory,
    const port::ImuSample& imu,
    uint64_t reference_time_ms);
float ComputeGyroConsistencyScore(float image_heading_px_per_row,
                                  const GyroContinuityConstraint& continuity);
int GyroTrendSign(const GyroContinuityConstraint& continuity);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_GYRO_CONTINUITY_HPP
