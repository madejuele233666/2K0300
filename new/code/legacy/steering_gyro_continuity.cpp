#include "legacy/steering_gyro_continuity.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

constexpr uint64_t kImuGraceWindowMs = 150;
constexpr float kHeadingDeltaLimitDeg = 18.0F;
constexpr float kPredictionPxPerDeg = 1.25F;
constexpr float kImageHeadingQuietThreshold = 0.18F;
constexpr float kImageHeadingStrongThreshold = 0.55F;
constexpr float kGyroHeadingQuietThresholdDeg = 1.0F;
constexpr float kGyroHeadingStrongThresholdDeg = 3.0F;

int SignWithThreshold(float value, float threshold) {
    if (value >= threshold) {
        return 1;
    }
    if (value <= -threshold) {
        return -1;
    }
    return 0;
}

float ClampHeadingDelta(float value) {
    return std::clamp(value, -kHeadingDeltaLimitDeg, kHeadingDeltaLimitDeg);
}

float DecayHeadingDelta(float previous_delta, uint64_t dt_ms) {
    if (dt_ms >= kImuGraceWindowMs) {
        return 0.0F;
    }
    const float decay =
        std::clamp(1.0F - static_cast<float>(dt_ms) / static_cast<float>(kImuGraceWindowMs), 0.0F, 1.0F);
    return previous_delta * decay;
}

}  // namespace

GyroContinuityConstraint ComputeGyroContinuityConstraint(const port::LegacySteeringState& prior_state,
                                                         const port::ImuSample& imu,
                                                         uint64_t reference_time_ms) {
    GyroContinuityConstraint continuity{};
    continuity.next_state = prior_state.gyro_continuity;

    const uint64_t prior_valid_ms = prior_state.gyro_continuity.last_valid_capture_time_ms;
    const uint64_t sample_time_ms =
        imu.valid ? (imu.capture_time_ms != 0 ? imu.capture_time_ms : reference_time_ms) : reference_time_ms;

    if (imu.valid) {
        const uint64_t dt_ms =
            (prior_valid_ms != 0 && sample_time_ms > prior_valid_ms) ? (sample_time_ms - prior_valid_ms) : 0;
        const float filtered = 0.7F * imu.gyro_z + 0.3F * prior_state.gyro_continuity.filtered_yaw_rate;
        continuity.next_state.filtered_yaw_rate = filtered;
        continuity.next_state.heading_delta_deg_150ms =
            ClampHeadingDelta(DecayHeadingDelta(prior_state.gyro_continuity.heading_delta_deg_150ms, dt_ms) +
                              filtered * (static_cast<float>(dt_ms) / 1000.0F));
        continuity.next_state.last_valid_capture_time_ms = sample_time_ms;
        continuity.next_state.imu_grace_active = false;
        continuity.weighting_active = true;
    } else {
        const uint64_t elapsed_ms =
            (prior_valid_ms != 0 && reference_time_ms > prior_valid_ms) ? (reference_time_ms - prior_valid_ms) : 0;
        continuity.next_state.heading_delta_deg_150ms =
            DecayHeadingDelta(prior_state.gyro_continuity.heading_delta_deg_150ms, elapsed_ms);
        continuity.next_state.imu_grace_active = prior_valid_ms != 0 && elapsed_ms <= kImuGraceWindowMs;
        continuity.weighting_active = prior_valid_ms != 0 && !continuity.next_state.imu_grace_active;
    }

    continuity.heading_delta_deg = continuity.next_state.heading_delta_deg_150ms;
    continuity.prior_prediction_offset_px = continuity.heading_delta_deg * kPredictionPxPerDeg;
    continuity.imu_grace_active = continuity.next_state.imu_grace_active;
    return continuity;
}

float ComputeGyroConsistencyScore(float image_heading_px_per_row,
                                  const GyroContinuityConstraint& continuity) {
    if (!continuity.weighting_active) {
        return 1.0F;
    }

    const int image_sign = SignWithThreshold(image_heading_px_per_row, kImageHeadingQuietThreshold);
    const int gyro_sign = SignWithThreshold(continuity.heading_delta_deg, kGyroHeadingQuietThresholdDeg);
    if (image_sign == 0 || gyro_sign == 0) {
        return 1.0F;
    }

    const bool strong_opposition =
        image_sign != gyro_sign &&
        std::abs(image_heading_px_per_row) >= kImageHeadingStrongThreshold &&
        std::abs(continuity.heading_delta_deg) >= kGyroHeadingStrongThresholdDeg;
    if (strong_opposition) {
        return 0.25F;
    }

    if (image_sign != gyro_sign) {
        return 0.6F;
    }

    const float image_norm =
        std::abs(image_heading_px_per_row) / std::max(kImageHeadingStrongThreshold, 1e-3F);
    const float gyro_norm =
        std::abs(continuity.heading_delta_deg) / std::max(kGyroHeadingStrongThresholdDeg, 1e-3F);
    return std::abs(image_norm - gyro_norm) <= 0.7F ? 1.0F : 0.6F;
}

int GyroTrendSign(const GyroContinuityConstraint& continuity) {
    return SignWithThreshold(continuity.heading_delta_deg, kGyroHeadingQuietThresholdDeg);
}

}  // namespace ls2k::legacy
