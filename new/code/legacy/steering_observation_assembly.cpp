#include "legacy/steering_observation_assembly.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ls2k::legacy {
namespace {

int CountTransitions(const port::LegacyCameraFrame& frame, int row, int threshold) {
    if (row < 0 || row >= frame.height || frame.width <= 1) {
        return 0;
    }
    int transitions = 0;
    bool previous = frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width)] > threshold;
    for (int col = 1; col < frame.width; ++col) {
        const bool current =
            frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) + col] > threshold;
        if (current != previous) {
            ++transitions;
        }
        previous = current;
    }
    return transitions;
}

float BoundaryHeadingAbsRad(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& boundary) {
    const port::BEVPathSample* first = nullptr;
    const port::BEVPathSample* last = nullptr;
    for (const port::BEVPathSample& sample : boundary) {
        if (!sample.valid) {
            continue;
        }
        if (first == nullptr) {
            first = &sample;
        }
        last = &sample;
    }
    if (first == nullptr || last == nullptr || last == first) {
        return 3.14159F;
    }
    const float delta_forward = last->point.forward_m - first->point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 3.14159F;
    }
    const float heading = std::atan2(last->point.lateral_m - first->point.lateral_m, delta_forward);
    return std::abs(heading);
}

float StraightConfidenceFromHeading(float heading_abs_rad) {
    return std::clamp(1.0F - heading_abs_rad / 0.25F, 0.0F, 1.0F);
}

struct SpanMetrics {
    bool valid = false;
    float near_width_m = 0.0F;
    float max_width_m = 0.0F;
    float left_open_score = 0.0F;
    float right_open_score = 0.0F;
    float left_contract_score = 0.0F;
    float right_contract_score = 0.0F;
};

bool ComputeSpanMetrics(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& left_boundary,
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& right_boundary,
    SpanMetrics& metrics) {
    bool have_reference = false;
    float reference_left_m = 0.0F;
    float reference_right_m = 0.0F;

    for (std::size_t index = 0; index < left_boundary.size(); ++index) {
        const port::BEVPathSample& left = left_boundary[index];
        const port::BEVPathSample& right = right_boundary[index];
        if (!left.valid || !right.valid) {
            continue;
        }
        const float width_m = right.point.lateral_m - left.point.lateral_m;
        if (width_m <= 1e-4F) {
            continue;
        }

        if (!have_reference) {
            have_reference = true;
            metrics.valid = true;
            metrics.near_width_m = width_m;
            metrics.max_width_m = width_m;
            reference_left_m = left.point.lateral_m;
            reference_right_m = right.point.lateral_m;
        } else {
            metrics.max_width_m = std::max(metrics.max_width_m, width_m);
        }

        metrics.left_open_score =
            std::max(metrics.left_open_score, reference_left_m - left.point.lateral_m);
        metrics.right_open_score =
            std::max(metrics.right_open_score, right.point.lateral_m - reference_right_m);
        metrics.left_contract_score =
            std::max(metrics.left_contract_score, left.point.lateral_m - reference_left_m);
        metrics.right_contract_score =
            std::max(metrics.right_contract_score, reference_right_m - right.point.lateral_m);
    }

    return metrics.valid;
}

}  // namespace

ObservationAssemblyResult AssembleObservation(const port::LegacyCameraFrame& frame,
                                              int threshold,
                                              const port::RuntimeParameters& params,
                                              const port::LegacySteeringState& prior_state,
                                              const port::ImuSample& imu,
                                              bool low_voltage_emergency,
                                              uint64_t frame_id,
                                              uint64_t capture_time_ms,
                                              const port::BEVTrackEstimate& track,
                                              const BEVProjector& projector) {
    (void)projector;
    ObservationAssemblyResult result{};
    result.vehicle.low_voltage_emergency = low_voltage_emergency;
    result.vehicle.imu_valid = imu.valid;
    result.vehicle.gyro_z = imu.gyro_z;
    result.vehicle.yaw_rate_deg_s = imu.gyro_z;
    result.vehicle.drive_cycle_count = prior_state.drive_cycle_count;
    result.vehicle.frame_id = frame_id;
    result.vehicle.capture_time_ms = capture_time_ms;

    result.observation.valid = track.valid;
    result.observation.track = track;
    result.observation.vehicle = result.vehicle;

    SpanMetrics span_metrics{};
    if (!ComputeSpanMetrics(track.sampled_drivable_left_boundary,
                            track.sampled_drivable_right_boundary,
                            span_metrics)) {
        (void)ComputeSpanMetrics(track.sampled_left_boundary, track.sampled_right_boundary, span_metrics);
    }
    if (span_metrics.valid) {
        result.observation.width_expand_ratio =
            span_metrics.near_width_m > 1e-4F
                ? std::max(1.0F, span_metrics.max_width_m / span_metrics.near_width_m)
                : 1.0F;
        result.observation.left_open_score = span_metrics.left_open_score;
        result.observation.right_open_score = span_metrics.right_open_score;
        result.observation.left_contract_score = span_metrics.left_contract_score;
        result.observation.right_contract_score = span_metrics.right_contract_score;
    } else {
        float near_width = params.bev_geometry.nominal_lane_width_m;
        float max_width = near_width;
        bool near_width_set = false;
        for (float width : track.lane_width_profile_m) {
            if (width <= 0.0F) {
                continue;
            }
            max_width = std::max(max_width, width);
            if (!near_width_set) {
                near_width = width;
                near_width_set = true;
            }
        }
        result.observation.width_expand_ratio =
            near_width > 1e-4F ? std::max(1.0F, max_width / near_width) : 1.0F;
    }

    const int reference_row = std::clamp(frame.height - 16, 0, std::max(0, frame.height - 1));
    result.observation.bottom_transition_density =
        static_cast<float>(CountTransitions(frame, reference_row, threshold));

    result.observation.left_boundary_heading_abs_rad =
        BoundaryHeadingAbsRad(track.sampled_left_boundary);
    result.observation.right_boundary_heading_abs_rad =
        BoundaryHeadingAbsRad(track.sampled_right_boundary);
    result.observation.left_opposite_straight_confidence =
        StraightConfidenceFromHeading(result.observation.right_boundary_heading_abs_rad);
    result.observation.right_opposite_straight_confidence =
        StraightConfidenceFromHeading(result.observation.left_boundary_heading_abs_rad);

    result.observation.bend_severity = std::abs(track.far_heading_error) * 1.8F +
                                       std::abs(track.preview_curvature) * 1.2F +
                                       std::abs(track.near_lateral_error) * 0.8F;
    result.observation.ordinary_bend_veto =
        result.observation.bend_severity >= params.bev_scene_fsm.bend_severity_confirm;
    result.observation.cross_bilateral_open_score_m =
        std::min(result.observation.left_open_score, result.observation.right_open_score);
    result.observation.cross_bilateral_open =
        result.observation.cross_bilateral_open_score_m >= params.bev_scene_fsm.cross_bilateral_open_min_m;
    result.observation.circle_left_opposite_straight =
        result.observation.right_boundary_heading_abs_rad <=
        params.bev_scene_fsm.circle_opposite_heading_abs_max;
    result.observation.circle_right_opposite_straight =
        result.observation.left_boundary_heading_abs_rad <=
        params.bev_scene_fsm.circle_opposite_heading_abs_max;
    result.observation.cross_candidate =
        result.observation.width_expand_ratio >= params.bev_scene_fsm.cross_expand_ratio_min &&
        result.observation.cross_bilateral_open;
    result.observation.zebra_candidate =
        result.observation.bottom_transition_density >= params.bev_scene_fsm.zebra_transition_density_min;
    result.observation.circle_left_candidate =
        result.observation.left_open_score >= params.bev_scene_fsm.circle_open_score_min &&
        result.observation.circle_left_opposite_straight &&
        !result.observation.cross_bilateral_open;
    result.observation.circle_right_candidate =
        result.observation.right_open_score >= params.bev_scene_fsm.circle_open_score_min &&
        result.observation.circle_right_opposite_straight &&
        !result.observation.cross_bilateral_open;

    result.constraints.fail_safe_veto = !track.calibration_valid || !track.valid;
    result.constraints.low_confidence_degraded =
        track.track_confidence < static_cast<float>(params.bev_control_model.low_confidence_threshold) ||
        track.visible_range_m < static_cast<float>(params.bev_control_model.low_visible_range_m);
    result.constraints.steering_suppressed =
        result.constraints.fail_safe_veto ||
        track.track_confidence < static_cast<float>(params.bev_control_model.steering_suppression_confidence);
    result.constraints.steering_gain_scale = 1.0;
    result.constraints.speed_limit_scale = 1.0;
    result.constraints.turn_limit_scale = 1.0;
    if (result.constraints.low_confidence_degraded) {
        const float confidence_scale =
            std::clamp(track.track_confidence /
                           static_cast<float>(std::max(1e-4, params.bev_control_model.low_confidence_threshold)),
                       static_cast<float>(params.bev_control_model.min_gain_scale),
                       1.0F);
        const float visible_scale =
            std::clamp(track.visible_range_m /
                           static_cast<float>(std::max(1e-4, params.bev_control_model.low_visible_range_m)),
                       static_cast<float>(params.bev_control_model.min_speed_limit_scale),
                       1.0F);
        const float limited_scale = std::min(confidence_scale, visible_scale);
        result.constraints.steering_gain_scale = limited_scale;
        result.constraints.speed_limit_scale = visible_scale;
        result.constraints.turn_limit_scale = limited_scale;
        result.constraints.primary_reason =
            track.visible_range_m < static_cast<float>(params.bev_control_model.low_visible_range_m)
                ? "short_visible_range"
                : "low_track_confidence";
    } else if (!track.fallback_mode.empty() && track.fallback_mode != "none") {
        result.constraints.primary_reason = track.fallback_mode;
    }
    if (result.constraints.fail_safe_veto) {
        result.constraints.primary_reason = !track.calibration_valid ? "projector_invalid" : "track_invalid";
    }
    return result;
}

}  // namespace ls2k::legacy
