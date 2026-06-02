#include "legacy/steering_otsu_threshold.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ls2k::legacy {
namespace {

int HistogramWeight(const std::array<int, 256>& hist,
                    int begin,
                    int end_exclusive) {
    int weight = 0;
    for (int value = begin; value < end_exclusive; ++value) {
        weight += hist[value];
    }
    return weight;
}

float HistogramQuantile(const std::array<int, 256>& hist,
                        int begin,
                        int end_exclusive,
                        int numerator,
                        int denominator,
                        int weight) {
    if (weight <= 0 || denominator <= 0) {
        return 0.0F;
    }
    const int rank =
        1 + (((weight - 1) * numerator + denominator / 2) / denominator);
    int seen = 0;
    for (int value = begin; value < end_exclusive; ++value) {
        seen += hist[value];
        if (seen >= rank) {
            return static_cast<float>(value);
        }
    }
    return static_cast<float>(std::max(begin, end_exclusive - 1));
}

OtsuThresholdResult BuildThresholdResult(const std::array<int, 256>& hist,
                                         int threshold) {
    OtsuThresholdResult result{};
    result.threshold = threshold;

    const int black_weight = HistogramWeight(hist, 0, threshold + 1);
    const int white_weight = HistogramWeight(hist, threshold + 1, 256);
    if (black_weight <= 0 || white_weight <= 0) {
        return result;
    }

    result.valid = true;
    result.black_upper_decile_gray =
        HistogramQuantile(hist, 0, threshold + 1, 9, 10, black_weight);
    result.white_lower_decile_gray =
        HistogramQuantile(hist, threshold + 1, 256, 1, 10, white_weight);
    return result;
}

}  // namespace

/// ComputeOtsuThreshold 实现
/// 使用Otsu算法（最大类间方差法）自动计算图像二值化阈值
/// 1. 以2为步长采样计算灰度直方图（减少计算量）
/// 2. 遍历所有可能的阈值，计算类间方差
/// 3. 返回使类间方差最大的阈值
OtsuThresholdResult ComputeOtsuThresholdResult(const port::LegacyCameraFrameView& frame) {
    std::array<int, 256> hist{};
    if (!frame.Valid()) {
        return {};
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
        return {};
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
    return BuildThresholdResult(hist, threshold);
}

int ComputeOtsuThreshold(const port::LegacyCameraFrameView& frame) {
    return ComputeOtsuThresholdResult(frame).threshold;
}

}  // namespace ls2k::legacy
