#include "legacy/steering_bev_sparse_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

constexpr int kMinBoundaryEvidenceImageMarginPx = 12;

bool IsInsideImage(const port::LegacyCameraFrame& frame, int row, int col) {
    return row >= 0 && col >= 0 && row < frame.height && col < frame.width;
}

float DecisionBandForThreshold(float threshold) {
    const float nearest_saturation =
        std::min(std::max(1.0F, threshold), std::max(1.0F, 255.0F - threshold));
    return std::clamp(nearest_saturation * 0.5F, 32.0F, 72.0F);
}

port::BEVSampleClass ClassifySample(float center_intensity,
                                    float confidence,
                                    int threshold,
                                    const port::BEVTopologySamplerParameters& params) {
    if (confidence < params.unknown_confidence_min) {
        return port::BEVSampleClass::kUnknownLowConfidence;
    }
    if (center_intensity > static_cast<float>(threshold)) {
        return confidence >= params.drivable_confidence_min
                   ? port::BEVSampleClass::kDrivable
                   : port::BEVSampleClass::kUnknownLowConfidence;
    }
    return port::BEVSampleClass::kBackground;
}

float DecisionMarginConfidence(float intensity, int threshold, float patch_ratio, float patch_purity) {
    const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
    const float margin = std::abs(intensity - threshold_f);
    const float confidence = margin / DecisionBandForThreshold(threshold_f);
    return std::clamp(confidence * patch_ratio * std::clamp(patch_purity, 0.0F, 1.0F), 0.0F, 1.0F);
}

port::BEVSample MakeInvalidSample(const port::BEVPoint& point, const port::ImagePoint& image) {
    port::BEVSample sample{};
    sample.point = point;
    sample.image = image;
    sample.sample_class = port::BEVSampleClass::kInvalidOutsideImage;
    sample.valid_image_projection = false;
    return sample;
}

}  // namespace

BEVSparseSampleGrid SparseMetricSample(const port::LegacyCameraFrame& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector) {
    BEVSparseSampleGrid grid{};
    if (!projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        return grid;
    }

    const port::BEVTopologySamplerParameters& sampler = params.bev_topology_sampler;
    const float lateral_min = std::min(sampler.lateral_min_m, sampler.lateral_max_m);
    const float lateral_max = std::max(sampler.lateral_min_m, sampler.lateral_max_m);
    const float step = std::max(1e-4F, std::abs(sampler.lateral_step_m));
    const int radius = std::max(0, sampler.sample_patch_radius_px);
    const int expected_patch_count = (radius * 2 + 1) * (radius * 2 + 1);

    bool any_valid_projection = false;
    for (std::size_t layer_index = 0; layer_index < port::kBevTrackSampleCount; ++layer_index) {
        BEVSampleLayer& layer = grid.layers[layer_index];
        layer.forward_m = sampler.forward_samples_m[layer_index];
        for (float lateral_m = lateral_min; lateral_m <= lateral_max + step * 0.5F; lateral_m += step) {
            const port::BEVPoint point{layer.forward_m, lateral_m};
            port::ImagePoint image{};
            if (!projector.ProjectVehicleToImage(point, image)) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }

            const int center_row = static_cast<int>(std::lround(image.row_px));
            const int center_col = static_cast<int>(std::lround(image.col_px));
            if (!IsInsideImage(frame, center_row, center_col)) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }
            const int border_margin =
                std::max(kMinBoundaryEvidenceImageMarginPx,
                         std::max(0, params.bev_geometry.image_border_truncation_margin_px)) +
                radius;
            const bool near_image_border =
                center_row <= border_margin || center_col <= border_margin ||
                center_row >= frame.height - 1 - border_margin ||
                center_col >= frame.width - 1 - border_margin;

            int sample_count = 0;
            int intensity_min = 255;
            int intensity_max = 0;
            int same_side_count = 0;
            const int center_intensity =
                frame.gray[static_cast<std::size_t>(center_row) * static_cast<std::size_t>(frame.width) +
                           static_cast<std::size_t>(center_col)];
            const bool center_drivable_side = center_intensity > threshold;
            for (int dr = -radius; dr <= radius; ++dr) {
                for (int dc = -radius; dc <= radius; ++dc) {
                    const int row = center_row + dr;
                    const int col = center_col + dc;
                    if (!IsInsideImage(frame, row, col)) {
                        continue;
                    }
                    const int intensity =
                        frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                                   static_cast<std::size_t>(col)];
                    intensity_min = std::min(intensity_min, intensity);
                    intensity_max = std::max(intensity_max, intensity);
                    if ((intensity > threshold) == center_drivable_side) {
                        ++same_side_count;
                    }
                    ++sample_count;
                }
            }
            if (sample_count == 0) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }

            const float raw_intensity = static_cast<float>(center_intensity);
            const float patch_ratio =
                expected_patch_count > 0
                    ? static_cast<float>(sample_count) / static_cast<float>(expected_patch_count)
                    : 1.0F;
            const float patch_purity =
                sample_count > 0 ? static_cast<float>(same_side_count) / static_cast<float>(sample_count) : 0.0F;
            const float confidence =
                DecisionMarginConfidence(raw_intensity, threshold, patch_ratio, patch_purity);

            port::BEVSample sample{};
            sample.point = point;
            sample.image = image;
            sample.raw_intensity = raw_intensity;
            sample.patch_min_intensity = static_cast<float>(intensity_min);
            sample.patch_max_intensity = static_cast<float>(intensity_max);
            sample.patch_purity = patch_purity;
            sample.confidence = confidence;
            sample.valid_image_projection = true;
            sample.near_image_border = near_image_border;
            sample.sample_class = ClassifySample(raw_intensity, confidence, threshold, sampler);
            layer.samples.push_back(sample);
            any_valid_projection = true;
        }
    }

    grid.valid = any_valid_projection;
    return grid;
}

}  // namespace ls2k::legacy
