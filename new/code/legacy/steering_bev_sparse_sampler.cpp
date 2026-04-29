#include "legacy/steering_bev_sparse_sampler.hpp"

// BEV 稀疏采样器实现。
// 对 BEV 空间中规则网格上的每个点：投影到图像 → 采样灰度 patch → 计算分类置信度。
// 输出被 corridor interval 提取和 element evidence 共用。

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

// 推断边界证据时偏离图像边缘的最小像素距离
constexpr int kMinBoundaryEvidenceImageMarginPx = 12;

// 检查图像坐标是否在有效范围内
bool IsInsideImage(const port::LegacyCameraFrame& frame, int row, int col) {
    return row >= 0 && col >= 0 && row < frame.height && col < frame.width;
}

// 根据阈值计算决策带宽。阈值靠近 0 或 255 时带宽受饱和限制，
// 确保在极端亮度条件下仍然有合理的置信度区分度
float DecisionBandForThreshold(float threshold) {
    const float nearest_saturation =
        std::min(std::max(1.0F, threshold), std::max(1.0F, 255.0F - threshold));
    return std::clamp(nearest_saturation * 0.5F, 32.0F, 72.0F);
}

// 基于中心灰度值和置信度对采样点进行分类
port::BEVSampleClass ClassifySample(float center_intensity,
                                    float confidence,
                                    int threshold,
                                    const port::BEVTopologySamplerParameters& params) {
    // 置信度过低 → 未知
    if (confidence < params.unknown_confidence_min) {
        return port::BEVSampleClass::kUnknownLowConfidence;
    }
    // 灰度高于阈值 → 可行驶，但需要置信度足够
    if (center_intensity > static_cast<float>(threshold)) {
        return confidence >= params.drivable_confidence_min
                   ? port::BEVSampleClass::kDrivable
                   : port::BEVSampleClass::kUnknownLowConfidence;
    }
    // 灰度低于阈值 → 背景（不可行驶）
    return port::BEVSampleClass::kBackground;
}

// 计算基于灰度决策边距的置信度，考虑 patch 有效比例和纯度
float DecisionMarginConfidence(float intensity, int threshold, float patch_ratio, float patch_purity) {
    const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
    const float margin = std::abs(intensity - threshold_f);
    const float confidence = margin / DecisionBandForThreshold(threshold_f);
    return std::clamp(confidence * patch_ratio * std::clamp(patch_purity, 0.0F, 1.0F), 0.0F, 1.0F);
}

// 构造一个"投影无效"的采样点（投影到图像外或采样失败）
port::BEVSample MakeInvalidSample(const port::BEVPoint& point, const port::ImagePoint& image) {
    port::BEVSample sample{};
    sample.point = point;
    sample.image = image;
    sample.sample_class = port::BEVSampleClass::kInvalidOutsideImage;
    sample.valid_image_projection = false;
    return sample;
}

}  // namespace

// 对一帧灰度图执行完整的 BEV 稀疏网格采样
// 遍历 24 层前向距离 × 横向扫描线，每层横向从 lateral_min 到 lateral_max
BEVSparseSampleGrid SparseMetricSample(const port::LegacyCameraFrame& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector) {
    BEVSparseSampleGrid grid{};
    if (!projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        return grid;
    }

    const port::BEVTopologySamplerParameters& sampler = params.bev_topology_sampler;
    // 确保横向范围正向（小→大）
    const float lateral_min = std::min(sampler.lateral_min_m, sampler.lateral_max_m);
    const float lateral_max = std::max(sampler.lateral_min_m, sampler.lateral_max_m);
    const float step = std::max(1e-4F, std::abs(sampler.lateral_step_m));
    const int radius = std::max(0, sampler.sample_patch_radius_px);
    const int expected_patch_count = (radius * 2 + 1) * (radius * 2 + 1);

    bool any_valid_projection = false;
    for (std::size_t layer_index = 0; layer_index < port::kBevTrackSampleCount; ++layer_index) {
        BEVSampleLayer& layer = grid.layers[layer_index];
        layer.forward_m = sampler.forward_samples_m[layer_index];
        // 在当前前向距离上，从左到右横向扫描
        for (float lateral_m = lateral_min; lateral_m <= lateral_max + step * 0.5F; lateral_m += step) {
            const port::BEVPoint point{layer.forward_m, lateral_m};
            port::ImagePoint image{};
            // 步骤 1：BEV → 图像投影
            if (!projector.ProjectVehicleToImage(point, image)) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }

            const int center_row = static_cast<int>(std::lround(image.row_px));
            const int center_col = static_cast<int>(std::lround(image.col_px));
            // 步骤 2：检查投影点是否在图像内
            if (!IsInsideImage(frame, center_row, center_col)) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }
            // 步骤 3：判断是否靠近图像边界（靠近边界时投影可能不准确）
            const int border_margin =
                std::max(kMinBoundaryEvidenceImageMarginPx,
                         std::max(0, params.bev_geometry.image_border_truncation_margin_px)) +
                radius;
            const bool near_image_border =
                center_row <= border_margin || center_col <= border_margin ||
                center_row >= frame.height - 1 - border_margin ||
                center_col >= frame.width - 1 - border_margin;

            // 步骤 4：在图像上采集局部 patch 灰度统计信息
            int sample_count = 0;
            int intensity_min = 255;
            int intensity_max = 0;
            int same_side_count = 0;
            const int center_intensity =
                frame.gray[static_cast<std::size_t>(center_row) * static_cast<std::size_t>(frame.width) +
                           static_cast<std::size_t>(center_col)];
            const bool center_drivable_side = center_intensity > threshold;
            // 遍历 patch 内的每个像素
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
                    // 统计与中心灰度同侧的像素数量（用于纯度计算）
                    if ((intensity > threshold) == center_drivable_side) {
                        ++same_side_count;
                    }
                    ++sample_count;
                }
            }
            // 如果没有取到任何像素点，标记为无效
            if (sample_count == 0) {
                layer.samples.push_back(MakeInvalidSample(point, image));
                continue;
            }

            // 步骤 5：计算置信度并填充 BEVSample
            const float raw_intensity = static_cast<float>(center_intensity);
            // patch_ratio：实际采样像素数 / 预期像素数（处理图像边缘截断）
            const float patch_ratio =
                expected_patch_count > 0
                    ? static_cast<float>(sample_count) / static_cast<float>(expected_patch_count)
                    : 1.0F;
            // patch_purity：与中心同侧的像素比例（越高表示该点分类越清晰）
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
