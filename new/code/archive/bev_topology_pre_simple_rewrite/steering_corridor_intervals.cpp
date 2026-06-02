#include "legacy/steering_corridor_intervals.hpp"

// 走廊间隔提取实现。
// 对每层稀疏采样点按横向扫描顺序遍历，将连续的 Drivable 采样点聚合为 CorridorInterval。
// 核心机制：
// 1. 扫描每层，找到所有连续的 drivable 段
// 2. 对每个段创建 CorridorInterval（包含边界证据分类、开口评分、置信度）
// 3. 边界证据分类区分真实观测到的边缘与截断边缘

#include <algorithm>
#include <cstddef>

namespace ls2k::legacy {
namespace {

// 判断采样点是否属于可行驶类
bool IsDrivable(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kDrivable;
}

// 判断采样点是否属于无效投影
bool IsInvalid(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kInvalidOutsideImage;
}

// 判断采样点是否属于低置信度未知
bool IsUnknown(const port::BEVSample& sample) {
    return sample.sample_class == port::BEVSampleClass::kUnknownLowConfidence;
}

// 判断采样点是否为可信的背景（有效投影 + 非边界 + 置信度足够）
bool IsTrustedBackground(const port::BEVSample& sample,
                         const port::BEVTopologySamplerParameters& sampler_params) {
    return sample.valid_image_projection &&
           sample.sample_class == port::BEVSampleClass::kBackground &&
           !sample.near_image_border &&
           sample.confidence >= sampler_params.unknown_confidence_min;
}

// 对 drivable 区间的边界进行证据分类
// 检查组 edge 外侧的相邻采样点来判断边界性质
port::BEVBoundaryEvidenceKind ClassifyBoundaryEvidence(
    const std::vector<port::BEVSample>& samples,
    std::size_t drivable_edge_index,
    bool left_edge,
    const port::BEVTopologySamplerParameters& sampler_params) {
    const port::BEVSample& drivable_edge = samples[drivable_edge_index];
    // 搜索窗口截断：左边界是第一个采样点 / 右边界是最后一个采样点
    if (left_edge && drivable_edge_index == 0U) {
        return port::BEVBoundaryEvidenceKind::kSearchWindowEdge;
    }
    if (!left_edge && drivable_edge_index + 1U >= samples.size()) {
        return port::BEVBoundaryEvidenceKind::kSearchWindowEdge;
    }
    // 靠近图像边界
    if (drivable_edge.near_image_border) {
        return port::BEVBoundaryEvidenceKind::kImageBorder;
    }

    // 检查边界相邻点（左侧边界的前一个点 / 右侧边界的后一个点）
    const std::size_t adjacent_index = left_edge ? drivable_edge_index - 1U
                                                 : drivable_edge_index + 1U;
    const port::BEVSample& adjacent = samples[adjacent_index];
    if (adjacent.near_image_border) {
        return port::BEVBoundaryEvidenceKind::kImageBorder;
    }
    if (IsInvalid(adjacent)) {
        return port::BEVBoundaryEvidenceKind::kInvalidOutsideImage;
    }
    if (IsUnknown(adjacent)) {
        return port::BEVBoundaryEvidenceKind::kUnknownLowConfidence;
    }
    // 可信背景 → 真实观测到的边缘！
    if (IsTrustedBackground(adjacent, sampler_params)) {
        return port::BEVBoundaryEvidenceKind::kObservedDrivableBackground;
    }
    return port::BEVBoundaryEvidenceKind::kNonBackgroundAdjacent;
}

// 判断边界证据是否代表真实观测到的边缘
bool IsObservedBoundary(port::BEVBoundaryEvidenceKind evidence) {
    return evidence == port::BEVBoundaryEvidenceKind::kObservedDrivableBackground;
}

// 计算某侧边界开口评分（开口表示道路在此方向拓宽/开放）
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

// 从连续 drivable 采样点创建一个 CorridorInterval
// start/end: 连续段的起始/结束下标（含两端）
port::CorridorInterval MakeInterval(const std::vector<port::BEVSample>& samples,
                                    std::size_t start,
                                    std::size_t end,
                                    float step_m,
                                    const port::BEVTopologySamplerParameters& sampler_params) {
    port::CorridorInterval interval{};
    interval.forward_m = samples[start].point.forward_m;
    // 区间边界取采样点半步长外扩
    interval.lateral_min_m = samples[start].point.lateral_m - step_m * 0.5F;
    interval.lateral_max_m = samples[end].point.lateral_m + step_m * 0.5F;
    if (interval.lateral_min_m > interval.lateral_max_m) {
        std::swap(interval.lateral_min_m, interval.lateral_max_m);
    }
    interval.width_m = std::max(0.0F, interval.lateral_max_m - interval.lateral_min_m);
    interval.lateral_center_m = (interval.lateral_min_m + interval.lateral_max_m) * 0.5F;
    // 边界证据分类
    interval.left_boundary_evidence =
        ClassifyBoundaryEvidence(samples, start, true, sampler_params);
    interval.right_boundary_evidence =
        ClassifyBoundaryEvidence(samples, end, false, sampler_params);
    interval.left_edge_valid = IsObservedBoundary(interval.left_boundary_evidence);
    interval.right_edge_valid = IsObservedBoundary(interval.right_boundary_evidence);
    interval.left_opening_score = EdgeOpeningScore(samples, start, true, step_m);
    interval.right_opening_score = EdgeOpeningScore(samples, end, false, step_m);

    // 计算区间置信度（所有 drivable 点的置信度均值）
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
    // 如果边界外是未知区域（low confidence），降低置信度
    const bool unknown_left = start > 0 && IsUnknown(samples[start - 1]);
    const bool unknown_right = end + 1 < samples.size() && IsUnknown(samples[end + 1]);
    if (unknown_left || unknown_right) {
        interval.confidence *= 0.75F;
        interval.valid_sample_ratio = std::min(interval.valid_sample_ratio, 0.75F);
    }
    return interval;
}

}  // namespace

// 主入口：从稀疏采样网格提取所有层的走廊间隔
// 流程：对每层的采样点按横向扫描，将连续 drivable 段聚合成 interval
CorridorIntervalSet ExtractCorridorIntervals(const BEVSparseSampleGrid& samples,
                                             const port::RuntimeParameters& params) {
    CorridorIntervalSet result{};
    if (!samples.valid) {
        return result;
    }
    const float step_m = std::max(1e-4F, std::abs(params.bev_topology_sampler.lateral_step_m));
    bool any_interval = false;
    // 遍历 24 层
    for (std::size_t layer_index = 0; layer_index < port::kBevTrackSampleCount; ++layer_index) {
        const BEVSampleLayer& sample_layer = samples.layers[layer_index];
        CorridorIntervalLayer& interval_layer = result.layers[layer_index];
        interval_layer.forward_m = sample_layer.forward_m;

        // 扫描该层采样点，寻找连续 drivable 段
        std::size_t index = 0;
        while (index < sample_layer.samples.size()) {
            // 跳过非 drivable 点
            while (index < sample_layer.samples.size() && !IsDrivable(sample_layer.samples[index])) {
                ++index;
            }
            if (index >= sample_layer.samples.size()) {
                break;
            }
            const std::size_t start = index;
            // 找到连续的 drivable 段结束位置
            while (index + 1 < sample_layer.samples.size() &&
                   IsDrivable(sample_layer.samples[index + 1])) {
                ++index;
            }
            const std::size_t end = index;
            interval_layer.intervals.push_back(
                MakeInterval(sample_layer.samples, start, end, step_m, params.bev_topology_sampler));
            any_interval = true;
            ++index;
        }
    }
    result.valid = any_interval;
    return result;
}

}  // namespace ls2k::legacy
