#include "legacy/steering_corridor_intervals.hpp"

#include <algorithm>
#include <cstddef>

namespace ls2k::legacy {
namespace {

bool IsDrivable(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kDrivable;
}

bool IsBackground(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kBackground;
}

bool IsInvalid(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kInvalidOutsideImage;
}

bool IsUnknown(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kUnknownLowConfidence;
}

float EdgeOpeningScore(const std::vector<port::BEVSample>& samples,
                       std::size_t edge_index,
                       bool left_edge,
                       float step_m) {
    if (samples.empty()) {
        return 0.0F;
    }
    if (left_edge) {
        if (edge_index == 0) {
            return samples[edge_index].valid_image_projection ? step_m : 0.0F;
        }
        const port::BEVSample& adjacent = samples[edge_index - 1];
        return IsInvalid(adjacent) ? 0.0F : 0.0F;
    }
    if (edge_index + 1 >= samples.size()) {
        return samples[edge_index].valid_image_projection ? step_m : 0.0F;
    }
    const port::BEVSample& adjacent = samples[edge_index + 1];
    return IsInvalid(adjacent) ? 0.0F : 0.0F;
}

port::CorridorInterval MakeInterval(const std::vector<port::BEVSample>& samples,
                                    std::size_t start,
                                    std::size_t end,
                                    float step_m) {
    port::CorridorInterval interval{};
    interval.forward_m = samples[start].point.forward_m;
    interval.lateral_min_m = samples[start].point.lateral_m - step_m * 0.5F;
    interval.lateral_max_m = samples[end].point.lateral_m + step_m * 0.5F;
    if (interval.lateral_min_m > interval.lateral_max_m) {
        std::swap(interval.lateral_min_m, interval.lateral_max_m);
    }
    interval.width_m = std::max(0.0F, interval.lateral_max_m - interval.lateral_min_m);
    interval.lateral_center_m = (interval.lateral_min_m + interval.lateral_max_m) * 0.5F;
    interval.left_edge_valid = start > 0 && IsBackground(samples[start - 1]);
    interval.right_edge_valid = end + 1 < samples.size() && IsBackground(samples[end + 1]);
    interval.left_opening_score = EdgeOpeningScore(samples, start, true, step_m);
    interval.right_opening_score = EdgeOpeningScore(samples, end, false, step_m);

    float confidence_sum = 0.0F;
    int drivable_count = 0;
    int valid_projection_count = 0;
    for (std::size_t index = start; index <= end; ++index) {
        if (samples[index].valid_image_projection) {
            ++valid_projection_count;
        }
        if (IsDrivable(samples[index])) {
            confidence_sum += samples[index].confidence;
            ++drivable_count;
        }
    }
    const int span_count = static_cast<int>(end - start + 1U);
    interval.valid_sample_ratio =
        span_count > 0 ? static_cast<float>(valid_projection_count) / static_cast<float>(span_count) : 0.0F;
    interval.confidence =
        drivable_count > 0 ? std::clamp(confidence_sum / static_cast<float>(drivable_count), 0.0F, 1.0F)
                           : 0.0F;
    const bool unknown_left = start > 0 && IsUnknown(samples[start - 1]);
    const bool unknown_right = end + 1 < samples.size() && IsUnknown(samples[end + 1]);
    if (unknown_left || unknown_right) {
        interval.confidence *= 0.75F;
        interval.valid_sample_ratio = std::min(interval.valid_sample_ratio, 0.75F);
    }
    return interval;
}

}  // namespace

CorridorIntervalSet ExtractCorridorIntervals(const BEVSparseSampleGrid& samples,
                                             const port::RuntimeParameters& params) {
    CorridorIntervalSet result{};
    if (!samples.valid) {
        return result;
    }
    const float step_m = std::max(1e-4F, std::abs(params.bev_topology_sampler.lateral_step_m));
    bool any_interval = false;
    for (std::size_t layer_index = 0; layer_index < port::kBevTrackSampleCount; ++layer_index) {
        const BEVSampleLayer& sample_layer = samples.layers[layer_index];
        CorridorIntervalLayer& interval_layer = result.layers[layer_index];
        interval_layer.forward_m = sample_layer.forward_m;

        std::size_t index = 0;
        while (index < sample_layer.samples.size()) {
            while (index < sample_layer.samples.size() && !IsDrivable(sample_layer.samples[index])) {
                ++index;
            }
            if (index >= sample_layer.samples.size()) {
                break;
            }
            const std::size_t start = index;
            while (index + 1 < sample_layer.samples.size() &&
                   IsDrivable(sample_layer.samples[index + 1])) {
                ++index;
            }
            const std::size_t end = index;
            interval_layer.intervals.push_back(MakeInterval(sample_layer.samples, start, end, step_m));
            any_interval = true;
            ++index;
        }
    }
    result.valid = any_interval;
    return result;
}

}  // namespace ls2k::legacy
