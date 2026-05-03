#include "legacy/steering_otsu_threshold.hpp"

#include <array>
#include <cstdint>
#include <cstddef>

namespace ls2k::legacy {

int ComputeOtsuThreshold(const port::LegacyCameraFrameView& frame) {
    std::array<int, 256> hist{};
    if (!frame.Valid()) {
        return 0;
    }

    int samples = 0;
    for (int row = 0; row < frame.height; row += 2) {
        for (int col = 0; col < frame.width; col += 2) {
            const std::uint8_t pixel =
                frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.stride) +
                           static_cast<std::size_t>(col)];
            ++hist[pixel];
            ++samples;
        }
    }
    if (samples == 0) {
        return 0;
    }

    double sum = 0.0;
    for (int value = 0; value < 256; ++value) {
        sum += static_cast<double>(value * hist[value]);
    }

    double sum_background = 0.0;
    int weight_background = 0;
    double max_variance = -1.0;
    int threshold = 0;
    for (int value = 0; value < 256; ++value) {
        weight_background += hist[value];
        if (weight_background == 0) {
            continue;
        }
        const int weight_foreground = samples - weight_background;
        if (weight_foreground == 0) {
            break;
        }
        sum_background += static_cast<double>(value * hist[value]);
        const double mean_background = sum_background / static_cast<double>(weight_background);
        const double mean_foreground = (sum - sum_background) / static_cast<double>(weight_foreground);
        const double between_variance =
            static_cast<double>(weight_background) * static_cast<double>(weight_foreground) *
            (mean_background - mean_foreground) * (mean_background - mean_foreground);
        if (between_variance > max_variance) {
            max_variance = between_variance;
            threshold = value;
        }
    }
    return threshold;
}

}  // namespace ls2k::legacy
