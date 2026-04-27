#include "legacy/steering_control_error_model.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

float HeadingFromReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                           int near_index,
                           int far_index) {
    if (near_index < 0 || far_index < 0 || near_index >= static_cast<int>(path.size()) ||
        far_index >= static_cast<int>(path.size()) || far_index <= near_index || !path[near_index].valid ||
        !path[far_index].valid) {
        return 0.0F;
    }
    const float delta_forward = path[far_index].point.forward_m - path[near_index].point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 0.0F;
    }
    return std::atan2(path[far_index].point.lateral_m - path[near_index].point.lateral_m, delta_forward);
}

float CurvatureFromReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                             int near_index,
                             int curvature_index) {
    if (near_index <= 0 || curvature_index >= static_cast<int>(path.size()) || curvature_index <= near_index ||
        !path[0].valid || !path[near_index].valid || !path[curvature_index].valid) {
        return 0.0F;
    }
    const float ds1 = std::max(1e-4F, path[near_index].point.forward_m - path[0].point.forward_m);
    const float ds2 =
        std::max(1e-4F, path[curvature_index].point.forward_m - path[near_index].point.forward_m);
    const float slope1 = (path[near_index].point.lateral_m - path[0].point.lateral_m) / ds1;
    const float slope2 =
        (path[curvature_index].point.lateral_m - path[near_index].point.lateral_m) / ds2;
    return (slope2 - slope1) /
           std::max(1e-4F, path[curvature_index].point.forward_m - path[0].point.forward_m);
}

bool InterpolateReferenceAt(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float forward_m,
                            port::BEVPathSample& output) {
    const port::BEVPathSample* previous = nullptr;
    const port::BEVPathSample* next = nullptr;
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        if (sample.point.forward_m <= forward_m) {
            previous = &sample;
        }
        if (sample.point.forward_m >= forward_m) {
            next = &sample;
            break;
        }
    }

    if (previous == nullptr && next == nullptr) {
        return false;
    }
    if (previous == nullptr) {
        output = *next;
        return true;
    }
    if (next == nullptr) {
        output = *previous;
        return true;
    }

    const float span = next->point.forward_m - previous->point.forward_m;
    if (std::abs(span) < 1e-4F) {
        output = *next;
        return true;
    }

    const float t = std::clamp((forward_m - previous->point.forward_m) / span, 0.0F, 1.0F);
    output.valid = true;
    output.point.forward_m = forward_m;
    output.point.lateral_m = previous->point.lateral_m +
                             t * (next->point.lateral_m - previous->point.lateral_m);
    output.confidence = previous->confidence + t * (next->confidence - previous->confidence);
    return true;
}

float HeadingAtReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                         float forward_m,
                         const port::BEVPathSample& lookahead) {
    const float before_m = std::max(0.0F, forward_m - 0.35F);
    const float after_m = forward_m + 0.35F;
    port::BEVPathSample before{};
    port::BEVPathSample after{};
    if (InterpolateReferenceAt(path, before_m, before) && InterpolateReferenceAt(path, after_m, after)) {
        const float ds = after.point.forward_m - before.point.forward_m;
        if (std::abs(ds) >= 1e-4F) {
            return std::atan2(after.point.lateral_m - before.point.lateral_m, ds);
        }
    }

    port::BEVPathSample origin{};
    if (InterpolateReferenceAt(path, 0.0F, origin)) {
        const float ds = lookahead.point.forward_m - origin.point.forward_m;
        if (std::abs(ds) >= 1e-4F) {
            return std::atan2(lookahead.point.lateral_m - origin.point.lateral_m, ds);
        }
    }
    return 0.0F;
}

float CurvatureAroundLookahead(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                               float lookahead_m) {
    port::BEVPathSample start{};
    port::BEVPathSample mid{};
    port::BEVPathSample end{};
    const float mid_m = std::max(0.0F, lookahead_m * 0.5F);
    if (!InterpolateReferenceAt(path, 0.0F, start) ||
        !InterpolateReferenceAt(path, mid_m, mid) ||
        !InterpolateReferenceAt(path, lookahead_m, end)) {
        return 0.0F;
    }

    const float ds1 = std::max(1e-4F, mid.point.forward_m - start.point.forward_m);
    const float ds2 = std::max(1e-4F, end.point.forward_m - mid.point.forward_m);
    const float slope1 = (mid.point.lateral_m - start.point.lateral_m) / ds1;
    const float slope2 = (end.point.lateral_m - mid.point.lateral_m) / ds2;
    return (slope2 - slope1) / std::max(1e-4F, end.point.forward_m - start.point.forward_m);
}

float ClampMagnitude(float value, double limit) {
    const float abs_limit = static_cast<float>(std::max(0.0, limit));
    return std::clamp(value, -abs_limit, abs_limit);
}

}  // namespace

port::ControlErrorModelOutput ComputeControlErrorModel(const port::ControlErrorModelInput& input,
                                                       const port::RuntimeParameters& params) {
    port::ControlErrorModelOutput output{};
    output.valid = input.track.valid && input.reference_path.valid;
    output.visible_range_m = input.track.visible_range_m;
    output.track_confidence = input.track.track_confidence;
    if (!output.valid) {
        output.degraded = true;
        output.steering_suppressed = true;
        output.degrade_reason = "reference_or_track_invalid";
        return output;
    }

    const port::BEVControlModelParameters& model = params.bev_control_model;
    const int near_index = std::clamp(model.near_sample_index, 0, static_cast<int>(port::kBevTrackSampleCount - 1));
    const int far_index = std::clamp(model.far_sample_index, near_index, static_cast<int>(port::kBevTrackSampleCount - 1));
    const int curvature_index =
        std::clamp(model.curvature_sample_index, far_index, static_cast<int>(port::kBevTrackSampleCount - 1));

    if (input.reference_path.sampled_path[near_index].valid) {
        output.near_lateral_error = input.reference_path.sampled_path[near_index].point.lateral_m;
    }
    output.far_heading_error = HeadingFromReference(input.reference_path.sampled_path, near_index, far_index);
    output.preview_curvature =
        CurvatureFromReference(input.reference_path.sampled_path, near_index, curvature_index);

    const double min_lookahead = std::max(0.0, std::min(model.lookahead_min_m, model.lookahead_max_m));
    const double max_lookahead = std::max(model.lookahead_min_m, model.lookahead_max_m);
    const double raw_lookahead =
        static_cast<double>(std::max(0.0F, input.track.visible_range_m)) *
        std::max(0.0, model.lookahead_visible_range_ratio);
    const float lookahead_m = static_cast<float>(std::clamp(raw_lookahead, min_lookahead, max_lookahead));
    port::BEVPathSample lookahead{};
    if (!InterpolateReferenceAt(input.reference_path.sampled_path, lookahead_m, lookahead)) {
        output.valid = false;
        output.degraded = true;
        output.steering_suppressed = true;
        output.degrade_reason = "reference_lookahead_unavailable";
        return output;
    }

    output.lookahead_distance_m = lookahead_m;
    output.lookahead_lateral_error = lookahead.point.lateral_m;
    output.lookahead_heading_error =
        HeadingAtReference(input.reference_path.sampled_path, lookahead_m, lookahead);
    output.reference_curvature = CurvatureAroundLookahead(input.reference_path.sampled_path, lookahead_m);

    const float lateral = output.lookahead_lateral_error;
    const float denominator = std::max(1e-4F, lookahead_m * lookahead_m + lateral * lateral);
    const float kappa_pp = static_cast<float>(model.pure_pursuit_gain) * (2.0F * lateral / denominator);
    const float kappa_heading =
        static_cast<float>(model.heading_curvature_gain) * std::sin(output.lookahead_heading_error) /
        std::max(1e-4F, lookahead_m);
    const float kappa_feedforward =
        static_cast<float>(model.curvature_feedforward_gain) * output.reference_curvature;
    output.curvature_command =
        ClampMagnitude(kappa_pp + kappa_heading + kappa_feedforward, model.curvature_command_limit);
    output.yaw_rate_target = input.vehicle.speed_mps * output.curvature_command;

    output.steering_gain_scale = input.constraints.steering_gain_scale;
    output.speed_limit_scale = input.constraints.speed_limit_scale;
    output.turn_limit_scale = input.constraints.turn_limit_scale;
    output.steering_suppressed = input.constraints.steering_suppressed || input.constraints.fail_safe_veto;
    output.degraded = input.constraints.low_confidence_degraded || input.constraints.fail_safe_veto;
    output.degrade_reason = input.constraints.primary_reason;
    return output;
}

}  // namespace ls2k::legacy
