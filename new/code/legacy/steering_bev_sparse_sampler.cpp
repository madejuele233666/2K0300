#include "legacy/steering_bev_sparse_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

bool IsInsideImage(const port::LegacyCameraFrame& frame, int row, int col) {
    return row >= 0 && col >= 0 && row < frame.height && col < frame.width;
}

port::BEVSampleClass ClassifySample(float intensity,
                                    float confidence,
                                    int threshold,
                                    const port::BEVTopologySamplerParameters& params) {
    if (confidence < params.unknown_confidence_min) {
        return port::BEVSampleClass::kUnknownLowConfidence;
    }
    if (intensity > static_cast<float>(threshold)) {
        return confidence >= params.drivable_confidence_min
                   ? port::BEVSampleClass::kDrivable
                   : port::BEVSampleClass::kUnknownLowConfidence;
    }
    return port::BEVSampleClass::kBackground;
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

            int sample_count = 0;
            int intensity_max = 0;
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
                    intensity_max = std::max(intensity_max, intensity);
                    ++sample_count;
                }
            }
            if (sample_count == 0) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }

            const float raw_intensity = static_cast<float>(intensity_max);
            const float patch_ratio =
                expected_patch_count > 0
                    ? static_cast<float>(sample_count) / static_cast<float>(expected_patch_count)
                    : 1.0F;
            const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
            const float contrast =
                raw_intensity > threshold_f
                    ? (raw_intensity - threshold_f) / std::max(1.0F, 255.0F - threshold_f)
                    : (threshold_f - raw_intensity) / std::max(1.0F, threshold_f);
            const float confidence = std::clamp(contrast * patch_ratio, 0.0F, 1.0F);

            port::BEVSample sample{};
            sample.point = point;
            sample.image = image;
            sample.raw_intensity = raw_intensity;
            sample.confidence = confidence;
            sample.valid_image_projection = true;
            sample.sample_class = ClassifySample(raw_intensity, confidence, threshold, sampler);
            layer.samples.push_back(sample);
            any_valid_projection = true;
        }
    }

    grid.valid = any_valid_projection;
    return grid;
}

}  // namespace ls2k::legacy
