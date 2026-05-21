#include "runtime/steering_reference_time_alignment.hpp"

#include <algorithm>
#include <cmath>

#include "port/perf_counter.hpp"

namespace ls2k::runtime {
namespace {

std::size_t CountPresentSamples(const port::BEVReferencePath& path) {
    std::size_t count = 0;
    for (const port::BEVPathSample& sample : path.sampled_path) {
        if (sample.present) {
            ++count;
        }
    }
    return count;
}

bool IntegrateYawOnly(const MotionHistory& history,
                      uint64_t start_ms,
                      uint64_t end_ms,
                      int max_gap_ms,
                      double& delta_yaw_rad) {
    if (end_ms <= start_ms || history.count < 2) {
        delta_yaw_rad = 0.0;
        return end_ms >= start_ms;
    }
    const std::array<MotionHistorySample, MotionHistory::kCapacity> ordered = history.Ordered();
    bool saw_segment = false;
    for (std::size_t index = 1; index < history.count; ++index) {
        const MotionHistorySample& prev = ordered[index - 1U];
        const MotionHistorySample& curr = ordered[index];
        if (curr.time_ms <= start_ms || prev.time_ms >= end_ms) {
            continue;
        }
        if (!prev.imu_valid || !curr.imu_valid || curr.time_ms <= prev.time_ms) {
            return false;
        }
        const uint64_t gap_ms = curr.time_ms - prev.time_ms;
        if (gap_ms > static_cast<uint64_t>(std::max(1, max_gap_ms))) {
            return false;
        }
        const uint64_t segment_start = std::max(start_ms, prev.time_ms);
        const uint64_t segment_end = std::min(end_ms, curr.time_ms);
        if (segment_end <= segment_start) {
            continue;
        }
        const double dt_s = static_cast<double>(segment_end - segment_start) / 1000.0;
        delta_yaw_rad += static_cast<double>(prev.gyro_z) * dt_s;
        saw_segment = true;
    }
    return saw_segment;
}

port::BEVReferencePath TransformPath(const port::BEVReferencePath& input,
                                     double delta_s_m,
                                     double delta_yaw_rad,
                                     std::size_t& aligned_count) {
    port::BEVReferencePath output{};
    output.mode = input.mode;
    const double c = std::cos(-delta_yaw_rad);
    const double s = std::sin(-delta_yaw_rad);
    std::size_t out_index = 0;
    for (const port::BEVPathSample& sample : input.sampled_path) {
        if (!sample.present ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            continue;
        }
        const double x = static_cast<double>(sample.point.forward_m) - delta_s_m;
        const double y = static_cast<double>(sample.point.lateral_m);
        const double forward = c * x - s * y;
        const double lateral = s * x + c * y;
        if (!std::isfinite(forward) || !std::isfinite(lateral) || forward <= 0.0) {
            continue;
        }
        if (out_index >= output.sampled_path.size()) {
            break;
        }
        port::BEVPathSample& out = output.sampled_path[out_index++];
        out = sample;
        out.point.forward_m = static_cast<float>(forward);
        out.point.lateral_m = static_cast<float>(lateral);
    }
    aligned_count = out_index;
    if (aligned_count == 0U) {
        output.mode = port::ReferenceMode::kNone;
    }
    return output;
}

}  // namespace

ReferenceTimeAlignmentResult AlignReferencePathToControlTime(
    const port::BEVReferencePath& reference_path,
    uint64_t reference_capture_time_ms,
    uint64_t control_time_ms,
    const MotionHistory& motion_history,
    const port::RuntimeParameters& params) {
    LS2K_PERF_SCOPE(port::PerfStage::kReferenceTimeAlignment);
    ReferenceTimeAlignmentResult result{};
    result.reference_path = reference_path;
    port::ReferenceTimeAlignmentFacts& facts = result.facts;
    facts.enabled = params.reference_time_alignment.enabled;
    facts.reference_capture_time_ms = reference_capture_time_ms;
    facts.control_time_ms = control_time_ms;
    facts.input_sample_count = CountPresentSamples(reference_path);
    if (!params.reference_time_alignment.enabled) {
        facts.valid = true;
        facts.reason = "disabled";
        facts.aligned_sample_count = facts.input_sample_count;
        return result;
    }
    if (reference_capture_time_ms == 0 || control_time_ms < reference_capture_time_ms) {
        facts.reason = "invalid_reference_time";
        result.reference_path = {};
        return result;
    }
    facts.age_ms = control_time_ms - reference_capture_time_ms;
    if (facts.age_ms > static_cast<uint64_t>(std::max(1, params.reference_time_alignment.max_age_ms))) {
        facts.reason = "age_exceeded";
        result.reference_path = {};
        return result;
    }

    double delta_yaw = 0.0;
    if (!IntegrateYawOnly(motion_history,
                          reference_capture_time_ms,
                          control_time_ms,
                          params.reference_time_alignment.max_integration_gap_ms,
                          delta_yaw)) {
        facts.reason = "motion_history_unavailable";
        result.reference_path = {};
        return result;
    }
    if (std::fabs(delta_yaw) > params.reference_time_alignment.max_delta_yaw_rad) {
        facts.delta_yaw_rad = delta_yaw;
        facts.reason = "delta_yaw_exceeded";
        result.reference_path = {};
        return result;
    }

    facts.delta_s_m = 0.0;
    facts.delta_yaw_rad = delta_yaw;
    result.reference_path = TransformPath(reference_path, facts.delta_s_m, delta_yaw, facts.aligned_sample_count);
    if (facts.aligned_sample_count <
        static_cast<std::size_t>(std::max(1, params.reference_time_alignment.min_aligned_samples))) {
        facts.reason = "aligned_samples_insufficient";
        result.reference_path = {};
        return result;
    }
    facts.valid = true;
    facts.reason = "aligned_yaw_only";
    return result;
}

}  // namespace ls2k::runtime
