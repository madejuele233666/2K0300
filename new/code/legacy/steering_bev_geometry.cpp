#include "legacy/steering_bev_geometry.hpp"

// BEV 几何工具和管线编排实现。
// 包括：路径法线计算、沿法线偏移、前向距离归一化重采样、
// 以及 RunBEVTopologyPipeline 的管线串联。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_bev_element_evidence.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"

namespace ls2k::legacy {
namespace {

// 前向跨度最小值（避免除零）
constexpr float kMinForwardSpanM = 1.0e-4F;
// 法线拟合窗口最小值（米）
constexpr float kMinNormalFitWindowM = 0.08F;
// 拟合端点权重最小值（降低边缘点影响）
constexpr float kFitEndpointWeight = 0.15F;

// 判断路径采样点是否有效且坐标有限（无 NaN/Inf）
bool IsFinitePathSample(const port::BEVPathSample& sample) {
    return sample.valid && std::isfinite(sample.point.forward_m) &&
           std::isfinite(sample.point.lateral_m);
}

// 计算路径上某点处的法线方向（通过拟合局部切线，然后旋转 90°）
// path: 路径采样点数组，sample_index: 目标点索引
// tangent_fit_window_m: 切线拟合窗口大小
// normal_forward/normal_lateral: 输出法线方向向量
bool FindNormalAt(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                  std::size_t sample_index,
                  float tangent_fit_window_m,
                  float& normal_forward,
                  float& normal_lateral) {
    if (sample_index >= path.size() || !path[sample_index].valid) {
        return false;
    }

    // 寻找目标点前后的有效相邻点，作为切线拟合的备选
    const port::BEVPathSample* previous_by_index = nullptr;
    const port::BEVPathSample* next_by_index = nullptr;
    for (std::size_t index = sample_index; index > 0U; --index) {
        if (path[index - 1U].valid) {
            previous_by_index = &path[index - 1U];
            break;
        }
    }
    for (std::size_t index = sample_index + 1U; index < path.size(); ++index) {
        if (path[index].valid) {
            next_by_index = &path[index];
            break;
        }
    }

    // 加权线性回归拟合局部切线
    const float center_forward = path[sample_index].point.forward_m;
    const float fit_window = std::max(kMinNormalFitWindowM, tangent_fit_window_m);
    float weight_sum = 0.0F;
    float weighted_forward_sum = 0.0F;
    float weighted_lateral_sum = 0.0F;
    int support_count = 0;
    // 收集窗口内的有效点，用距离加权（越近权重越大）
    for (const port::BEVPathSample& sample : path) {
        if (!IsFinitePathSample(sample)) {
            continue;
        }
        const float distance = std::abs(sample.point.forward_m - center_forward);
        if (distance > fit_window) {
            continue;
        }
        const float weight =
            std::clamp(1.0F - distance / fit_window, kFitEndpointWeight, 1.0F);
        weight_sum += weight;
        weighted_forward_sum += weight * sample.point.forward_m;
        weighted_lateral_sum += weight * sample.point.lateral_m;
        ++support_count;
    }

    // 计算加权最小二乘斜率
    float tangent_forward = 0.0F;
    float tangent_lateral = 0.0F;
    if (support_count >= 2 && weight_sum > 0.0F) {
        const float mean_forward = weighted_forward_sum / weight_sum;
        const float mean_lateral = weighted_lateral_sum / weight_sum;
        float variance_forward = 0.0F;
        float covariance = 0.0F;
        for (const port::BEVPathSample& sample : path) {
            if (!IsFinitePathSample(sample)) {
                continue;
            }
            const float distance = std::abs(sample.point.forward_m - center_forward);
            if (distance > fit_window) {
                continue;
            }
            const float weight =
                std::clamp(1.0F - distance / fit_window, kFitEndpointWeight, 1.0F);
            const float forward_delta = sample.point.forward_m - mean_forward;
            variance_forward += weight * forward_delta * forward_delta;
            covariance += weight * forward_delta * (sample.point.lateral_m - mean_lateral);
        }
        // 斜率 = 协方差 / 方差
        if (variance_forward >= kMinForwardSpanM * kMinForwardSpanM) {
            const float slope = covariance / variance_forward;
            tangent_forward = 1.0F;
            tangent_lateral = slope;
        }
    }

    // 如果加权回归失败，回退到相邻点差分
    if (std::abs(tangent_forward) < kMinForwardSpanM &&
        std::abs(tangent_lateral) < kMinForwardSpanM) {
        if (previous_by_index != nullptr && next_by_index != nullptr) {
            tangent_forward = next_by_index->point.forward_m - previous_by_index->point.forward_m;
            tangent_lateral = next_by_index->point.lateral_m - previous_by_index->point.lateral_m;
        } else if (next_by_index != nullptr) {
            tangent_forward = next_by_index->point.forward_m - path[sample_index].point.forward_m;
            tangent_lateral = next_by_index->point.lateral_m - path[sample_index].point.lateral_m;
        } else if (previous_by_index != nullptr) {
            tangent_forward = path[sample_index].point.forward_m - previous_by_index->point.forward_m;
            tangent_lateral = path[sample_index].point.lateral_m - previous_by_index->point.lateral_m;
        } else {
            return false;
        }
    }

    // 归一化切向量
    const float length = std::hypot(tangent_forward, tangent_lateral);
    if (length < 1e-4F) {
        return false;
    }
    tangent_forward /= length;
    tangent_lateral /= length;
    // 法线 = 切向量逆时针旋转 90°
    normal_forward = -tangent_lateral;
    normal_lateral = tangent_forward;
    return true;
}

// 对按前向距离排序的采样点进行线性插值，得到指定前向距离处的路径点
// samples: 排序后的采样数组，count: 有效点数
// forward_m: 目标前向距离，max_extrapolate_m: 最大外推距离
port::BEVPathSample InterpolateSortedSamples(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& samples,
    std::size_t count,
    float forward_m,
    float max_extrapolate_m) {
    port::BEVPathSample output{};
    if (count == 0U) {
        return output;
    }
    // 只有一个有效点时，直接返回（如果外推距离可接受）
    if (count == 1U) {
        if (std::abs(forward_m - samples[0].point.forward_m) > max_extrapolate_m) {
            return output;
        }
        output = samples[0];
        output.point.forward_m = forward_m;
        return output;
    }

    // 二分搜索找到插值区间 [lower, upper]
    std::size_t lower = 0U;
    std::size_t upper = 1U;
    if (forward_m < samples[0].point.forward_m) {
        // 左外推
        if (samples[0].point.forward_m - forward_m > max_extrapolate_m) {
            return output;
        }
    } else if (forward_m > samples[count - 1U].point.forward_m) {
        // 右外推
        if (forward_m - samples[count - 1U].point.forward_m > max_extrapolate_m) {
            return output;
        }
        lower = count - 2U;
        upper = count - 1U;
    } else {
        // 内插：找到 forward_m 所在区间
        for (std::size_t index = 1U; index < count; ++index) {
            if (samples[index].point.forward_m >= forward_m) {
                lower = index - 1U;
                upper = index;
                break;
            }
        }
    }

    // 线性插值 lateral 和 confidence
    const port::BEVPathSample& before = samples[lower];
    const port::BEVPathSample& after = samples[upper];
    const float span = after.point.forward_m - before.point.forward_m;
    if (std::abs(span) < kMinForwardSpanM) {
        output = std::max(before.confidence, after.confidence) == before.confidence ? before : after;
        output.point.forward_m = forward_m;
        return output;
    }

    const float t = (forward_m - before.point.forward_m) / span;
    output.valid = true;
    output.point.forward_m = forward_m;
    output.point.lateral_m =
        before.point.lateral_m + t * (after.point.lateral_m - before.point.lateral_m);
    output.confidence = std::clamp(before.confidence + t * (after.confidence - before.confidence),
                                   0.0F,
                                   1.0F);
    return output;
}

// 计算标准前向采样网格上的最小间距
float MinimumForwardSampleSpacing(
    const std::array<float, port::kBevTrackSampleCount>& forward_samples_m) {
    float min_spacing = std::numeric_limits<float>::infinity();
    for (std::size_t index = 1U; index < forward_samples_m.size(); ++index) {
        const float spacing = std::abs(forward_samples_m[index] - forward_samples_m[index - 1U]);
        if (spacing > kMinForwardSpanM) {
            min_spacing = std::min(min_spacing, spacing);
        }
    }
    return std::isfinite(min_spacing) ? min_spacing : 0.02F;
}

// 合并前向距离相近的采样点（带权平均），减少重采样时的冗余
// sorted: 已排序的采样点，sorted_count: 有效点数
// merge_window_m: 合并窗口，merged: 输出合并后的采样点
// 返回合并后的有效点数
std::size_t MergeForwardClusters(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& sorted,
    std::size_t sorted_count,
    float merge_window_m,
    std::array<port::BEVPathSample, port::kBevTrackSampleCount>& merged) {
    std::size_t merged_count = 0U;
    std::size_t index = 0U;
    while (index < sorted_count) {
        const float cluster_begin_forward = sorted[index].point.forward_m;
        float weight_sum = 0.0F;
        float forward_sum = 0.0F;
        float lateral_sum = 0.0F;
        float confidence_sum = 0.0F;
        // 合并窗口内所有点，以置信度为权重
        while (index < sorted_count &&
               sorted[index].point.forward_m - cluster_begin_forward <= merge_window_m) {
            const float weight = std::max(0.05F, sorted[index].confidence);
            weight_sum += weight;
            forward_sum += weight * sorted[index].point.forward_m;
            lateral_sum += weight * sorted[index].point.lateral_m;
            confidence_sum += weight * sorted[index].confidence;
            ++index;
        }
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = forward_sum / std::max(1e-4F, weight_sum);
        sample.point.lateral_m = lateral_sum / std::max(1e-4F, weight_sum);
        sample.confidence =
            std::clamp(confidence_sum / std::max(1e-4F, weight_sum), 0.0F, 1.0F);
        merged[merged_count] = sample;
        ++merged_count;
    }
    return merged_count;
}

// 对重采样后的路径进行平滑（弦点平滑），抑制横向抖动
// 使用相邻点的中点弦线约束，将当前点向弦线方向微调
void SmoothResampledPath(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                         float max_lateral_adjust_m) {
    const float adjust_limit = std::max(0.0F, max_lateral_adjust_m);
    if (adjust_limit <= 0.0F) {
        return;
    }

    const auto base = path;
    // 用三点弦线平滑：对每个中间点，用前后点的连线中点来约束
    for (std::size_t index = 1U; index + 1U < path.size(); ++index) {
        if (!base[index - 1U].valid || !base[index].valid || !base[index + 1U].valid) {
            continue;
        }
        const float neighbor_span =
            base[index + 1U].point.forward_m - base[index - 1U].point.forward_m;
        if (std::abs(neighbor_span) < kMinForwardSpanM) {
            continue;
        }
        const float t = std::clamp((base[index].point.forward_m - base[index - 1U].point.forward_m) /
                                       neighbor_span,
                                   0.0F,
                                   1.0F);
        // 弦线上对应位置的横向值
        const float chord_lateral =
            base[index - 1U].point.lateral_m +
            t * (base[index + 1U].point.lateral_m - base[index - 1U].point.lateral_m);
        // 向弦线方向调整半个弦距，限幅防止过度平滑
        const float delta = std::clamp(0.5F * (chord_lateral - base[index].point.lateral_m),
                                       -adjust_limit,
                                       adjust_limit);
        path[index].point.lateral_m = base[index].point.lateral_m + delta;
    }
}

}  // namespace

// 沿路径某点的法线方向偏移（默认切线窗口 = 偏移距离）
port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m) {
    return OffsetPathSampleAlongNormal(path, sample_index, offset_m, std::abs(offset_m));
}

// 沿路径某点的法线方向偏移指定距离，允许指定切线拟合窗口大小
port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m,
    float tangent_fit_window_m) {
    port::BEVPathSample shifted{};
    if (sample_index >= path.size()) {
        return shifted;
    }
    shifted = path[sample_index];
    if (!shifted.valid) {
        return shifted;
    }

    // 计算法线方向，然后沿法线偏移
    float normal_forward = 0.0F;
    float normal_lateral = 1.0F;
    (void)FindNormalAt(path, sample_index, tangent_fit_window_m, normal_forward, normal_lateral);
    shifted.point.forward_m += normal_forward * offset_m;
    shifted.point.lateral_m += normal_lateral * offset_m;
    return shifted;
}

// 整体沿法线偏移路径（默认窗口）
void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m) {
    OffsetPathAlongNormals(path, offset_m, std::abs(offset_m));
}

// 整体沿法线偏移路径，所有点使用原始路径作为基
void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m,
                            float tangent_fit_window_m) {
    const auto base = path;
    for (std::size_t index = 0; index < path.size(); ++index) {
        path[index] = OffsetPathSampleAlongNormal(base, index, offset_m, tangent_fit_window_m);
    }
}

// 将任意前向分布的路径采样点归一化到标准 24 层前向采样网格
// 流程：排序 → 合并邻近点 → 线性插值到目标网格 → 平滑
int NormalizePathToForwardSamples(
    std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    const std::array<float, port::kBevTrackSampleCount>& forward_samples_m,
    float max_extrapolate_m,
    float max_lateral_smooth_adjust_m) {
    // 步骤 1：提取所有有效点并按前向距离排序
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> sorted{};
    std::size_t sorted_count = 0U;
    for (const port::BEVPathSample& sample : path) {
        if (!IsFinitePathSample(sample)) {
            continue;
        }
        sorted[sorted_count] = sample;
        ++sorted_count;
    }
    // 按前向距离排序，相同距离时置信度高的在前
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted_count),
              [](const port::BEVPathSample& left, const port::BEVPathSample& right) {
                  if (left.point.forward_m == right.point.forward_m) {
                      return left.confidence > right.confidence;
                  }
                  return left.point.forward_m < right.point.forward_m;
              });

    // 步骤 2：合并前向距离相近的采样点
    const float min_sample_spacing = MinimumForwardSampleSpacing(forward_samples_m);
    const float merge_window = std::max(kMinForwardSpanM, min_sample_spacing * 0.75F);
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> unique{};
    const std::size_t unique_count =
        MergeForwardClusters(sorted, sorted_count, merge_window, unique);

    // 步骤 3：插值到标准前向采样网格
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> normalized{};
    int valid_count = 0;
    const float extrapolate = std::max(0.0F, max_extrapolate_m);
    for (std::size_t index = 0U; index < normalized.size(); ++index) {
        normalized[index] =
            InterpolateSortedSamples(unique, unique_count, forward_samples_m[index], extrapolate);
        if (normalized[index].valid) {
            ++valid_count;
        }
    }
    // 步骤 4：平滑处理
    SmoothResampledPath(normalized, max_lateral_smooth_adjust_m);
    path = normalized;
    return valid_count;
}

// ========== BEV 拓扑感知管线入口 ==========
// 这是整条感知路径的装配点，按顺序：
// 1. 稀疏网格采样（SparseMetricSample）
// 2. 元素证据提取（ExtractBEVElementEvidence，cross band / circle corner）
// 3. 走廊间隔提取（ExtractCorridorIntervals）
// 4. 走廊图构建（BuildCorridorGraph）
// 5. 轨迹估计（BuildBEVTrackEstimateFromCorridorGraph）
BEVTopologyPipelineResult RunBEVTopologyPipeline(const port::LegacyCameraFrame& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const port::LegacySteeringState& prior_state,
                                                 const BEVProjector& projector) {
    BEVTopologyPipelineResult result{};
    result.track_estimate.calibration_valid = projector.Valid();
    result.track_estimate.source = "bev_corridor_topology";
    // 检查前置条件：投影器有效且帧尺寸有效
    if (!projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        result.track_estimate.fallback_mode = "projector_invalid";
        return result;
    }

    // 步骤 1：BEV 稀疏采样
    result.sparse_samples = SparseMetricSample(frame, threshold, params, projector);
    // 步骤 2：元素证据提取（十字路口带 / 环岛转角检测）
    result.element_evidence =
        ExtractBEVElementEvidence(frame, threshold, params, projector, result.sparse_samples);
    // 步骤 3：走廊间隔（每层的 Drivable 支撑区间）
    result.corridor_intervals = ExtractCorridorIntervals(result.sparse_samples, params);
    // 步骤 4：走廊图（跨层连接，形成路径候选）
    result.corridor_graph = BuildCorridorGraph(result.corridor_intervals, params, prior_state);
    // 步骤 5：从走廊图生成轨迹估计
    result.track_estimate =
        BuildBEVTrackEstimateFromCorridorGraph(result.corridor_intervals, result.corridor_graph, params);
    result.track_estimate.calibration_valid = projector.Valid();
    if (!result.track_estimate.valid && result.track_estimate.fallback_mode.empty()) {
        result.track_estimate.fallback_mode = "corridor_topology_unavailable";
    }
    return result;
}

}  // namespace ls2k::legacy
