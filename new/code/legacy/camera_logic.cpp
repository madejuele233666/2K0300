#include "legacy/camera_logic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace ls2k::legacy {
namespace {

int OtsuThresholdFast(const port::LegacyCameraFrame& frame) {
    std::array<int, 256> hist{};
    constexpr int kW = port::kLegacyFrameWidth;
    constexpr int kH = port::kLegacyFrameHeight;

    int samples = 0;
    for (int r = 0; r < kH; r += 2) {
        for (int c = 0; c < kW; c += 2) {
            const uint8_t pixel = frame.gray[static_cast<std::size_t>(r) * kW + c];
            ++hist[pixel];
            ++samples;
        }
    }

    if (samples == 0) {
        return 0;
    }

    double sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        sum += static_cast<double>(i * hist[i]);
    }

    double sum_b = 0.0;
    int weight_b = 0;
    double max_var = -1.0;
    int threshold = 0;
    for (int i = 0; i < 256; ++i) {
        weight_b += hist[i];
        if (weight_b == 0) {
            continue;
        }
        const int weight_f = samples - weight_b;
        if (weight_f == 0) {
            break;
        }
        sum_b += static_cast<double>(i * hist[i]);
        const double mean_b = sum_b / static_cast<double>(weight_b);
        const double mean_f = (sum - sum_b) / static_cast<double>(weight_f);
        const double between = static_cast<double>(weight_b) * static_cast<double>(weight_f) *
                               (mean_b - mean_f) * (mean_b - mean_f);
        if (between > max_var) {
            max_var = between;
            threshold = i;
        }
    }
    return threshold;
}

float EightNeighborEquivalentError(const port::LegacyCameraFrame& frame, int threshold, int* highest_line) {
    constexpr int kW = port::kLegacyFrameWidth;
    constexpr int kH = port::kLegacyFrameHeight;

    int best_row = 0;
    double weighted_center_sum = 0.0;
    double weight_sum = 0.0;
    for (int row = kH - 2; row >= 16; --row) {
        int left = -1;
        int right = -1;
        for (int col = 0; col < kW; ++col) {
            const uint8_t px = frame.gray[static_cast<std::size_t>(row) * kW + col];
            if (px > threshold) {
                if (left < 0) {
                    left = col;
                }
                right = col;
            }
        }
        if (left >= 0 && right >= left) {
            const double center = 0.5 * static_cast<double>(left + right);
            const double weight = 1.0 + static_cast<double>(row) / static_cast<double>(kH);
            weighted_center_sum += center * weight;
            weight_sum += weight;
            if (best_row == 0) {
                best_row = row;
            }
        }
    }

    if (highest_line != nullptr) {
        *highest_line = best_row;
    }
    if (weight_sum == 0.0) {
        return 0.0F;
    }

    const double avg_center = weighted_center_sum / weight_sum;
    return static_cast<float>(static_cast<double>(kW) * 0.5 - avg_center);
}

}  // namespace

port::PerceptionResult AnalyzeFrame(const port::LegacyCameraFrame& frame,
                                    const port::RuntimeParameters& params,
                                    bool low_voltage_emergency,
                                    uint64_t frame_id,
                                    uint64_t capture_time_ms) {
    port::PerceptionResult result{};
    result.published = true;
    result.fresh = true;
    result.frame_id = frame_id;
    result.capture_time_ms = capture_time_ms;
    result.publish_time_ms = capture_time_ms;
    result.perception_tag = "otsu+eight-neighbor-equivalent";

    result.threshold = OtsuThresholdFast(frame);
    result.lateral_error = EightNeighborEquivalentError(frame, result.threshold, &result.highest_line);

    result.low_voltage_veto = low_voltage_emergency;
    result.threshold_veto =
        (result.threshold <= params.emergency_threshold) || (result.threshold >= 220);
    result.geometry_veto = false;
    result.emergency_veto = result.low_voltage_veto || result.threshold_veto;
    return result;
}

}  // namespace ls2k::legacy
