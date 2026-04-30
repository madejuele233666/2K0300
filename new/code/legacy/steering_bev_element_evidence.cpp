#include "legacy/steering_bev_element_evidence.hpp"

// 元素证据提取实现。
// 对稀疏采样网格进行进一步分析：
// 1. 行剖面统计（BuildRowProfile）—— 每层可行驶/未知/无效比例
// 2. 十字路口检测（DetectCrossBand）—— 前方横向可行驶带
// 3. 环岛转角检测（DetectCircleCorner）—— 路沿转角特征
// 4. 左右边界追踪（BuildBoundaryTrace）—— 从背景→可行驶的过渡点

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace ls2k::legacy {
namespace {

// 靠近图像边缘时投影不可靠，此处定义安全边距
constexpr int kMinElementImageMarginPx = 12;
// 十字路口稠密检测使用的行数（5 行，间隔 kCrossDenseRowStepM）
constexpr int kCrossDenseRows = 5;
constexpr float kCrossDenseRowStepM = 0.005F;
// 十字路口行剖面可行驶比例必须 ≥ 88%
constexpr float kCrossDrivableRatioMin = 0.88F;
// 十字路口行剖面未知比例必须 ≤ 12%
constexpr float kCrossUnknownRatioMax = 0.12F;
// 环岛转角检测：相邻段之间的夹角必须 ≥ 55°
constexpr float kCircleCornerAngleMinDeg = 55.0F;
// 局部转角检测：相邻采样点的夹角最小门限
constexpr float kCircleLocalAngleMinDeg = 35.0F;
// 环岛转角存在评分门槛
constexpr float kCircleCornerPresentScoreMin = 0.65F;
// 内圆黑区完整 white-black-white 标定评分门槛
constexpr float kInnerIslandPresentScoreMin = 0.58F;

// 用于稠密重采样的内部结构（与 BEVSample 类似，但使用 vector 存储）
struct DenseSample {
    port::BEVPoint point{};
    port::BEVSampleClass sample_class = port::BEVSampleClass::kInvalidOutsideImage;
    bool valid_image_projection = false;
    bool near_image_border = false;
    float confidence = 0.0F;
};

// 从稀疏采样中提取的背景→可行驶边界点
struct BoundaryPoint {
    bool valid = false;              //!< 边界点是否有效
    float forward_m = 0.0F;          //!< 前向距离
    float lateral_m = 0.0F;          //!< 横向位置（边界所在位置）
    float invalid_penalty = 0.0F;    //!< 无效惩罚（靠近图像边界时惩罚高）
};

// 带层索引的边界点
struct LayeredBoundaryPoint {
    BoundaryPoint point{};
    std::size_t layer = 0U;
};

// 局部转角分区评分 —— 用四个象限的 drivable 分布来判断是否是转角特征
struct LocalCornerPartition {
    float score = 0.0F;             //!< 综合评分
    float one_dark_score = 0.0F;    //!< 一个象限暗（背景）、其他亮（可行驶）的评分
    float one_bright_score = 0.0F;  //!< 一个象限亮、其他暗的评分
    float invalid_penalty = 0.0F;   //!< 无效投影惩罚
};

struct ForwardClassRun {
    port::BEVSampleClass sample_class = port::BEVSampleClass::kInvalidOutsideImage;
    float start_forward_m = 0.0F;
    float end_forward_m = 0.0F;
    int sample_count = 0;
    int invalid_count = 0;
};

struct WhiteBlackWhiteRun {
    bool valid = false;
    float score = 0.0F;
    float black_start_forward_m = 0.0F;
    float black_end_forward_m = 0.0F;
    float invalid_penalty = 0.0F;
};

// 限制到 [0, 1]
float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

// 角度转弧度
float DegToRad(float degrees) {
    return degrees * 3.14159265358979323846F / 180.0F;
}

// 检查图像坐标是否在范围内
bool IsInsideImage(const port::LegacyCameraFrame& frame, int row, int col) {
    return row >= 0 && col >= 0 && row < frame.height && col < frame.width;
}

// 根据阈值计算决策带宽（同 sparse_sampler 实现一致）
float DecisionBandForThreshold(float threshold) {
    const float nearest_saturation =
        std::min(std::max(1.0F, threshold), std::max(1.0F, 255.0F - threshold));
    return std::clamp(nearest_saturation * 0.5F, 32.0F, 72.0F);
}

// 基于决策边距的置信度计算（仅基于强度，不含 patch 因子）
float DecisionMarginConfidence(float intensity, int threshold) {
    const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
    return Clamp01(std::abs(intensity - threshold_f) / DecisionBandForThreshold(threshold_f));
}

// 检查 DenseSample 的投影是否可靠（有效投影 + 不靠近边界 + 有效分类）
bool IsReliableProjection(const DenseSample& sample) {
    return sample.valid_image_projection &&
           !sample.near_image_border &&
           sample.sample_class != port::BEVSampleClass::kInvalidOutsideImage;
}

// BEVSample 的重载版本
bool IsReliableProjection(const port::BEVSample& sample) {
    return sample.valid_image_projection &&
           !sample.near_image_border &&
           sample.sample_class != port::BEVSampleClass::kInvalidOutsideImage;
}

// 判断采样点是否属于可行驶类
bool IsDrivable(port::BEVSampleClass sample_class) {
    return sample_class == port::BEVSampleClass::kDrivable;
}

// 判断采样点是否属于背景类
bool IsBackground(port::BEVSampleClass sample_class) {
    return sample_class == port::BEVSampleClass::kBackground;
}

bool SameRunClass(port::BEVSampleClass left, port::BEVSampleClass right) {
    return (IsDrivable(left) && IsDrivable(right)) ||
           (IsBackground(left) && IsBackground(right)) ||
           (left == right &&
            (left == port::BEVSampleClass::kUnknownLowConfidence ||
             left == port::BEVSampleClass::kInvalidOutsideImage));
}

// 单点稠密采样：投影 BEV 点到图像，采样单像素灰度并分类（无 patch 统计）
DenseSample SampleDensePoint(const port::LegacyCameraFrame& frame,
                             int threshold,
                             const port::RuntimeParameters& params,
                             const BEVProjector& projector,
                             const port::BEVPoint& point) {
    DenseSample sample{};
    sample.point = point;
    port::ImagePoint image{};
    // 投影到图像
    if (!projector.ProjectVehicleToImage(point, image)) {
        return sample;
    }
    const int row = static_cast<int>(std::lround(image.row_px));
    const int col = static_cast<int>(std::lround(image.col_px));
    if (!IsInsideImage(frame, row, col)) {
        return sample;
    }

    const int border_margin =
        std::max(kMinElementImageMarginPx, std::max(0, params.bev_geometry.image_border_truncation_margin_px));
    sample.valid_image_projection = true;
    sample.near_image_border =
        row <= border_margin || col <= border_margin ||
        row >= frame.height - 1 - border_margin ||
        col >= frame.width - 1 - border_margin;

    // 采样灰度并分类（单像素，不计算 patch）
    const int intensity =
        frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                   static_cast<std::size_t>(col)];
    sample.confidence = DecisionMarginConfidence(static_cast<float>(intensity), threshold);
    if (sample.confidence < params.bev_topology_sampler.unknown_confidence_min) {
        sample.sample_class = port::BEVSampleClass::kUnknownLowConfidence;
    } else if (intensity > threshold) {
        sample.sample_class =
            sample.confidence >= params.bev_topology_sampler.drivable_confidence_min
                ? port::BEVSampleClass::kDrivable
                : port::BEVSampleClass::kUnknownLowConfidence;
    } else {
        sample.sample_class = port::BEVSampleClass::kBackground;
    }
    return sample;
}

// 从采样点序列构建行剖面证据（模板函数，支持 BEVSample 和 DenseSample）
// 统计每层的可行驶/未知/无效/最大间隙等指标
template <typename SampleT>
port::BEVRowProfileEvidence BuildRowProfileFromSamples(const std::vector<SampleT>& samples,
                                                       float forward_m,
                                                       float step_m) {
    port::BEVRowProfileEvidence profile{};
    profile.forward_m = forward_m;
    if (samples.empty()) {
        return profile;
    }

    int invalid_count = 0;
    int unknown_count = 0;
    int reliable_count = 0;
    int current_gap_count = 0;
    int largest_gap_count = 0;
    bool have_span = false;

    for (const SampleT& sample : samples) {
        if (!IsReliableProjection(sample)) {
            ++invalid_count;
            continue;
        }
        have_span = true;
        // 更新有效采样的横向范围
        profile.valid_lateral_min_m =
            reliable_count == 0 ? sample.point.lateral_m : std::min(profile.valid_lateral_min_m, sample.point.lateral_m);
        profile.valid_lateral_max_m =
            reliable_count == 0 ? sample.point.lateral_m : std::max(profile.valid_lateral_max_m, sample.point.lateral_m);
        ++reliable_count;
        if (sample.sample_class == port::BEVSampleClass::kUnknownLowConfidence) {
            ++unknown_count;
        }
        if (IsDrivable(sample.sample_class)) {
            ++profile.drivable_sample_count;
            largest_gap_count = std::max(largest_gap_count, current_gap_count);
            current_gap_count = 0;
        } else {
            ++current_gap_count;
        }
    }
    largest_gap_count = std::max(largest_gap_count, current_gap_count);

    profile.valid_sample_count = reliable_count;
    const int total_count = static_cast<int>(samples.size());
    if (reliable_count > 0 && have_span) {
        profile.valid = true;
        profile.valid_width_m =
            std::max(0.0F, profile.valid_lateral_max_m - profile.valid_lateral_min_m);
        profile.drivable_ratio =
            static_cast<float>(profile.drivable_sample_count) / static_cast<float>(reliable_count);
        profile.unknown_ratio =
            static_cast<float>(unknown_count) / static_cast<float>(reliable_count);
        profile.largest_non_drivable_gap_m =
            static_cast<float>(largest_gap_count) * std::max(1.0e-4F, step_m);
    }
    if (total_count > 0) {
        profile.invalid_ratio = static_cast<float>(invalid_count) / static_cast<float>(total_count);
    }
    return profile;
}

// 从稀疏采样层构建行剖面
port::BEVRowProfileEvidence BuildSparseRowProfile(const BEVSampleLayer& layer, float step_m) {
    return BuildRowProfileFromSamples(layer.samples, layer.forward_m, step_m);
}

// 从稠密采样（在给定前向距离上以更小步长横向采样）构建行剖面
// 用于十字路口检测的精细分析
port::BEVRowProfileEvidence BuildDenseRowProfile(const port::LegacyCameraFrame& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const BEVProjector& projector,
                                                 float forward_m) {
    std::vector<DenseSample> samples;
    const float lateral_min = std::min(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float lateral_max = std::max(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    // 稠密采样的步长 = 稀疏采样步长的一半，最小 5mm
    const float step = std::max(0.005F, std::abs(params.bev_topology_sampler.lateral_step_m) * 0.5F);
    samples.reserve(static_cast<std::size_t>((lateral_max - lateral_min) / step) + 2U);
    for (float lateral = lateral_min; lateral <= lateral_max + step * 0.5F; lateral += step) {
        samples.push_back(SampleDensePoint(frame, threshold, params, projector, {forward_m, lateral}));
    }
    return BuildRowProfileFromSamples(samples, forward_m, step);
}

// 计算单行剖面的十字路口评分 —— 基于宽度、可行驶比例、间隙、未知比例
float CrossRowScore(const port::BEVRowProfileEvidence& profile,
                    const port::RuntimeParameters& params) {
    if (!profile.valid || profile.valid_sample_count <= 0) {
        return 0.0F;
    }
    // 最小宽度：至少 2× 名义车道宽度或 0.55m
    const float min_width =
        std::max(params.bev_corridor_graph.nominal_lane_width_m * 2.0F,
                 std::max(0.55F, profile.valid_width_m * 0.70F));
    if (profile.valid_width_m < min_width ||
        profile.drivable_ratio < kCrossDrivableRatioMin ||
        profile.unknown_ratio > kCrossUnknownRatioMax) {
        return 0.0F;
    }
    // 最大容忍间隙：4 个采样步长或 8cm
    const float max_gap_m =
        std::max(0.08F, std::abs(params.bev_topology_sampler.lateral_step_m) * 4.0F);
    if (profile.largest_non_drivable_gap_m > max_gap_m) {
        return 0.0F;
    }

    // 多因子加权评分
    const float width_score = Clamp01(profile.valid_width_m / std::max(1.0e-4F, min_width));
    const float gap_score = Clamp01(1.0F - profile.largest_non_drivable_gap_m / std::max(1.0e-4F, max_gap_m));
    const float unknown_score = Clamp01(1.0F - profile.unknown_ratio / kCrossUnknownRatioMax);
    return Clamp01(0.55F * profile.drivable_ratio + 0.20F * width_score +
                   0.15F * gap_score + 0.10F * unknown_score);
}

// 检测十字路口可行驶带（cross band）
// 对每层进行稠密重采样，检测是否存在连续可行驶的横向带
port::BEVCrossBandEvidence DetectCrossBand(const port::LegacyCameraFrame& frame,
                                           int threshold,
                                           const port::RuntimeParameters& params,
                                           const BEVProjector& projector,
                                           const BEVSparseSampleGrid& sparse_grid) {
    port::BEVCrossBandEvidence best{};
    const float forward_min = params.bev_topology_sampler.forward_samples_m.front();
    const float forward_max = params.bev_topology_sampler.forward_samples_m.back();
    // 遍历每层稀疏采样，在每层周围进行稠密重采样
    for (const BEVSampleLayer& layer : sparse_grid.layers) {
        int consecutive_rows = 0;
        float score_sum = 0.0F;
        port::BEVRowProfileEvidence center_profile{};
        // 在当前前向距离附近取 5 个稠密行（步长 kCrossDenseRowStepM = 5mm）
        for (int offset = -(kCrossDenseRows / 2); offset <= kCrossDenseRows / 2; ++offset) {
            const float forward =
                std::clamp(layer.forward_m + static_cast<float>(offset) * kCrossDenseRowStepM,
                           forward_min,
                           forward_max);
            const port::BEVRowProfileEvidence profile =
                BuildDenseRowProfile(frame, threshold, params, projector, forward);
            const float row_score = CrossRowScore(profile, params);
            if (offset == 0) {
                center_profile = profile;
            }
            if (row_score > 0.0F) {
                ++consecutive_rows;
                score_sum += row_score;
            }
        }
        // 至少有 3 个连续稠密行满足条件
        if (consecutive_rows < 3) {
            continue;
        }
        const float score = Clamp01(score_sum / static_cast<float>(consecutive_rows));
        if (!best.present || score > best.score) {
            best.present = score >= 0.70F;
            best.score = score;
            best.forward_m = layer.forward_m;
            best.drivable_ratio = center_profile.drivable_ratio;
            best.valid_width_m = center_profile.valid_width_m;
            best.largest_non_drivable_gap_m = center_profile.largest_non_drivable_gap_m;
            best.invalid_penalty = center_profile.invalid_ratio;
            best.consecutive_dense_rows = consecutive_rows;
        }
    }
    // 如果没有找到有效的 cross band，清空结果
    if (!best.present) {
        best = {};
    }
    return best;
}

// 判断两个相邻采样点之间是否存在"可信的过渡"（背景↔可行驶的边界）
bool TrustedTransition(const port::BEVSample& left, const port::BEVSample& right) {
    return IsReliableProjection(left) && IsReliableProjection(right) &&
           ((IsBackground(left.sample_class) && IsDrivable(right.sample_class)) ||
            (IsDrivable(left.sample_class) && IsBackground(right.sample_class)));
}

// 构建单侧（左侧或右侧）的边界点轨迹，从背景→可行驶的过渡点中提取
// 这是环岛转角检测的基础：提取出左右两侧的路沿候选点
std::array<BoundaryPoint, port::kBevTrackSampleCount> BuildBoundaryTrace(const BEVSparseSampleGrid& sparse_grid,
                                                                         bool left_side) {
    std::array<BoundaryPoint, port::kBevTrackSampleCount> trace{};
    for (std::size_t layer_index = 0; layer_index < sparse_grid.layers.size(); ++layer_index) {
        const BEVSampleLayer& layer = sparse_grid.layers[layer_index];
        BoundaryPoint best{};
        // 遍历该层所有横向相邻点对，寻找 background↔drivable 的过渡
        for (std::size_t sample_index = 1U; sample_index < layer.samples.size(); ++sample_index) {
            const port::BEVSample& before = layer.samples[sample_index - 1U];
            const port::BEVSample& after = layer.samples[sample_index];
            if (!TrustedTransition(before, after)) {
                continue;
            }
            const bool transition_is_left_edge =
                IsBackground(before.sample_class) && IsDrivable(after.sample_class);
            if (transition_is_left_edge != left_side) {
                continue;
            }
            // 取两点中点为边界位置
            const float lateral = (before.point.lateral_m + after.point.lateral_m) * 0.5F;
            if (!best.valid ||
                (left_side ? lateral < best.lateral_m : lateral > best.lateral_m)) {
                best.valid = true;
                best.forward_m = layer.forward_m;
                best.lateral_m = lateral;
                best.invalid_penalty =
                    (before.near_image_border || after.near_image_border) ? 1.0F : 0.0F;
            }
        }
        trace[layer_index] = best;
    }
    return trace;
}

std::vector<ForwardClassRun> BuildForwardRuns(const port::LegacyCameraFrame& frame,
                                              int threshold,
                                              const port::RuntimeParameters& params,
                                              const BEVProjector& projector,
                                              float lateral_m,
                                              float forward_min_m,
                                              float forward_max_m) {
    std::vector<ForwardClassRun> runs;
    const float forward_min =
        std::max(params.bev_topology_sampler.forward_samples_m.front(), forward_min_m);
    const float forward_max =
        std::min(params.bev_topology_sampler.forward_samples_m.back(), forward_max_m);
    const float sample_spacing =
        std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                 params.bev_topology_sampler.forward_samples_m[0]);
    const float step = std::max(0.010F, sample_spacing * 0.50F);
    ForwardClassRun current{};
    bool have_current = false;
    for (float forward = forward_min; forward <= forward_max + step * 0.5F; forward += step) {
        const DenseSample sample =
            SampleDensePoint(frame, threshold, params, projector, {forward, lateral_m});
        port::BEVSampleClass run_class = sample.sample_class;
        if (!IsReliableProjection(sample)) {
            run_class = port::BEVSampleClass::kInvalidOutsideImage;
        }
        if (!have_current || !SameRunClass(current.sample_class, run_class)) {
            if (have_current) {
                runs.push_back(current);
            }
            current = {};
            current.sample_class = run_class;
            current.start_forward_m = forward;
            current.end_forward_m = forward;
            current.sample_count = 1;
            current.invalid_count =
                run_class == port::BEVSampleClass::kInvalidOutsideImage ? 1 : 0;
            have_current = true;
        } else {
            current.end_forward_m = forward;
            ++current.sample_count;
            if (run_class == port::BEVSampleClass::kInvalidOutsideImage) {
                ++current.invalid_count;
            }
        }
    }
    if (have_current) {
        runs.push_back(current);
    }
    return runs;
}

WhiteBlackWhiteRun FindWhiteBlackWhiteRun(const std::vector<ForwardClassRun>& runs,
                                          const port::RuntimeParameters& params) {
    WhiteBlackWhiteRun best{};
    const float sample_spacing =
        std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                 params.bev_topology_sampler.forward_samples_m[0]);
    const float min_white_len =
        std::max(0.040F, sample_spacing * 0.60F);
    const float min_black_len =
        std::max(0.055F, params.bev_corridor_graph.nominal_lane_width_m * 0.12F);
    for (std::size_t index = 1U; index + 1U < runs.size(); ++index) {
        const ForwardClassRun& before = runs[index - 1U];
        const ForwardClassRun& black = runs[index];
        const ForwardClassRun& after = runs[index + 1U];
        if (!IsDrivable(before.sample_class) ||
            !IsBackground(black.sample_class) ||
            !IsDrivable(after.sample_class)) {
            continue;
        }
        const float before_len = before.end_forward_m - before.start_forward_m;
        const float black_len = black.end_forward_m - black.start_forward_m;
        const float after_len = after.end_forward_m - after.start_forward_m;
        if (before_len < min_white_len || black_len < min_black_len || after_len < min_white_len) {
            continue;
        }
        const float white_score =
            Clamp01(std::min(before_len, after_len) / std::max(1.0e-4F, min_white_len * 2.0F));
        const float black_score =
            Clamp01(black_len / std::max(1.0e-4F, min_black_len * 2.0F));
        const float score = Clamp01(0.45F + 0.30F * white_score + 0.25F * black_score);
        if (!best.valid || black.start_forward_m < best.black_start_forward_m ||
            (std::abs(black.start_forward_m - best.black_start_forward_m) < 1.0e-4F &&
             score > best.score)) {
            best.valid = true;
            best.score = score;
            best.black_start_forward_m = black.start_forward_m;
            best.black_end_forward_m = black.end_forward_m;
            best.invalid_penalty = 0.0F;
        }
    }
    return best;
}

struct InnerEdgeCandidate {
    port::BEVPathSample sample{};
};

using InnerEdgeCandidateLayers =
    std::array<std::vector<InnerEdgeCandidate>, port::kBevTrackSampleCount>;

struct InnerEdgeTraceResult {
    bool present = false;
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> edge{};
    float start_forward_m = 0.0F;
    float end_forward_m = 0.0F;
    float confidence = 0.0F;
    int support_layers = 0;
    int gap_layers = 0;
    int rejected_far_segments = 0;
};

float ForwardSampleSpacingM(const port::RuntimeParameters& params) {
    return std::abs(params.bev_topology_sampler.forward_samples_m[1] -
                    params.bev_topology_sampler.forward_samples_m[0]);
}

float InnerTraceMaxJumpM(const port::RuntimeParameters& params) {
    return std::max(std::abs(params.bev_topology_sampler.lateral_step_m) * 4.0F,
                    params.bev_corridor_graph.max_center_jump_m * 0.45F);
}

void AddInnerEdgeCandidate(std::vector<InnerEdgeCandidate>& candidates,
                           const port::BEVPathSample& sample,
                           float merge_tolerance_m) {
    for (InnerEdgeCandidate& candidate : candidates) {
        if (std::abs(candidate.sample.point.lateral_m - sample.point.lateral_m) <=
            merge_tolerance_m) {
            if (sample.confidence > candidate.sample.confidence) {
                candidate.sample = sample;
            }
            return;
        }
    }
    candidates.push_back(InnerEdgeCandidate{sample});
}

InnerEdgeCandidateLayers CollectInnerIslandRoadFacingEdgeCandidates(
    const port::LegacyCameraFrame& frame,
    int threshold,
    const port::RuntimeParameters& params,
    const BEVProjector& projector,
    bool left_side,
    float lateral_hint_m,
    float forward_min_m,
    float forward_max_m) {
    InnerEdgeCandidateLayers candidates{};
    const float lateral_min = std::min(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float lateral_max = std::max(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float step = std::max(0.004F, std::abs(params.bev_topology_sampler.lateral_step_m) * 0.25F);
    const float forward_spacing = ForwardSampleSpacingM(params);
    constexpr std::array<float, 5> kForwardProbeOffsets{{-0.75F, -0.40F, 0.0F, 0.40F, 0.75F}};
    const float window =
        std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.90F,
                 std::abs(params.bev_topology_sampler.lateral_step_m) * 9.0F);
    const float side_limit =
        std::max(0.08F, params.bev_corridor_graph.nominal_lane_width_m * 0.20F);
    const float scan_min =
        std::max(lateral_min, left_side ? lateral_hint_m - window : -side_limit);
    const float scan_max =
        std::min(lateral_max, left_side ? side_limit : lateral_hint_m + window);

    for (std::size_t layer = 0; layer < port::kBevTrackSampleCount; ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        if (forward < forward_min_m - 1.0e-4F || forward > forward_max_m + 1.0e-4F) {
            continue;
        }
        for (float forward_offset_scale : kForwardProbeOffsets) {
            const float probe_forward =
                std::clamp(forward + forward_offset_scale * forward_spacing,
                           params.bev_topology_sampler.forward_samples_m.front(),
                           params.bev_topology_sampler.forward_samples_m.back());
            DenseSample last_known{};
            bool have_last_known = false;
            int mixed_bridge_steps = 0;
            for (float lateral = scan_min; lateral <= scan_max + step * 0.5F; lateral += step) {
                const DenseSample current =
                    SampleDensePoint(frame, threshold, params, projector, {probe_forward, lateral});
                if (!IsReliableProjection(current)) {
                    have_last_known = false;
                    mixed_bridge_steps = 0;
                    continue;
                }
                if (!IsDrivable(current.sample_class) && !IsBackground(current.sample_class)) {
                    if (have_last_known) {
                        ++mixed_bridge_steps;
                        if (mixed_bridge_steps <= 5) {
                            continue;
                        }
                    }
                    have_last_known = false;
                    mixed_bridge_steps = 0;
                    continue;
                }
                const bool transition =
                    left_side
                        ? (have_last_known && IsBackground(last_known.sample_class) &&
                           IsDrivable(current.sample_class))
                        : (have_last_known && IsDrivable(last_known.sample_class) &&
                           IsBackground(current.sample_class));
                if (transition) {
                    const float bridge_width =
                        std::abs(current.point.lateral_m - last_known.point.lateral_m);
                    const bool bridge_ok =
                        bridge_width <=
                        std::max(std::abs(params.bev_topology_sampler.lateral_step_m) * 0.90F,
                                 step * 6.0F);
                    if (!bridge_ok) {
                        last_known = current;
                        mixed_bridge_steps = 0;
                        continue;
                    }
                    const float edge_lateral =
                        (last_known.point.lateral_m + current.point.lateral_m) * 0.5F;
                    const bool side_ok = left_side ? edge_lateral <= side_limit : edge_lateral >= -side_limit;
                    if (side_ok) {
                        port::BEVPathSample sample{};
                        sample.valid = true;
                        sample.point.forward_m = forward;
                        sample.point.lateral_m = edge_lateral;
                        const float probe_confidence_scale =
                            std::abs(forward_offset_scale) < 1.0e-4F ? 1.0F : 0.90F;
                        sample.confidence =
                            std::clamp((last_known.confidence + current.confidence) * 0.5F *
                                           probe_confidence_scale *
                                           (mixed_bridge_steps > 0 ? 0.82F : 1.0F),
                                       0.0F,
                                       1.0F);
                        AddInnerEdgeCandidate(candidates[layer],
                                              sample,
                                              std::abs(params.bev_topology_sampler.lateral_step_m) *
                                                  0.50F);
                    }
                }
                last_known = current;
                have_last_known = true;
                mixed_bridge_steps = 0;
            }
        }
    }
    return candidates;
}

std::array<port::BEVPathSample, port::kBevTrackSampleCount> BestRawInnerEdgeByLayer(
    const InnerEdgeCandidateLayers& candidates,
    float seed_lateral_m) {
    std::array<port::BEVPathSample, port::kBevTrackSampleCount> raw{};
    for (std::size_t layer = 0; layer < candidates.size(); ++layer) {
        const InnerEdgeCandidate* best = nullptr;
        float best_score = std::numeric_limits<float>::infinity();
        for (const InnerEdgeCandidate& candidate : candidates[layer]) {
            const float score =
                std::abs(candidate.sample.point.lateral_m - seed_lateral_m) -
                candidate.sample.confidence * 0.02F;
            if (best == nullptr || score < best_score) {
                best = &candidate;
                best_score = score;
            }
        }
        if (best != nullptr) {
            raw[layer] = best->sample;
        }
    }
    return raw;
}

float MemorySeedLateral(const port::ReferencePolicyState* memory,
                        bool left_side,
                        float fallback_lateral_m) {
    if (memory == nullptr || !memory->circle_inner_island_memory_active ||
        memory->circle_inner_island_memory_left != left_side) {
        return fallback_lateral_m;
    }
    for (const port::BEVPathSample& sample : memory->circle_inner_island_edge) {
        if (sample.valid) {
            return sample.point.lateral_m;
        }
    }
    return memory->circle_inner_island_scan_lateral_m;
}

InnerEdgeTraceResult TraceFromSeed(const InnerEdgeCandidateLayers& candidates,
                                   std::size_t start_layer,
                                   const port::BEVPathSample& start,
                                   const port::RuntimeParameters& params,
                                   int min_support_layers,
                                   int max_gap_layers) {
    InnerEdgeTraceResult trace{};
    const float max_jump = InnerTraceMaxJumpM(params);
    const float max_gap_jump = max_jump * static_cast<float>(max_gap_layers + 1);
    std::size_t previous_layer = start_layer;
    port::BEVPathSample previous = start;
    float trend = 0.0F;
    int pending_gap_layers = 0;
    float confidence_sum = start.confidence;
    trace.edge[start_layer] = start;
    trace.support_layers = 1;

    for (std::size_t layer = start_layer + 1U; layer < candidates.size(); ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const float predicted_lateral =
            previous.point.lateral_m + trend * (forward - previous.point.forward_m);
        const InnerEdgeCandidate* best = nullptr;
        float best_score = std::numeric_limits<float>::infinity();
        const float allowed_jump =
            std::min(max_gap_jump, max_jump * static_cast<float>(pending_gap_layers + 1));
        for (const InnerEdgeCandidate& candidate : candidates[layer]) {
            const float delta =
                std::abs(candidate.sample.point.lateral_m - predicted_lateral);
            if (delta > allowed_jump) {
                continue;
            }
            const float score = delta - candidate.sample.confidence * 0.03F;
            if (best == nullptr || score < best_score) {
                best = &candidate;
                best_score = score;
            }
        }
        if (best == nullptr) {
            ++pending_gap_layers;
            if (pending_gap_layers > max_gap_layers) {
                break;
            }
            continue;
        }

        if (pending_gap_layers > 0) {
            const std::size_t gap_begin = previous_layer + 1U;
            const std::size_t gap_end = layer;
            for (std::size_t gap_layer = gap_begin; gap_layer < gap_end; ++gap_layer) {
                const float gap_forward = params.bev_topology_sampler.forward_samples_m[gap_layer];
                const float span = best->sample.point.forward_m - previous.point.forward_m;
                const float t = span > 1.0e-4F
                                    ? std::clamp((gap_forward - previous.point.forward_m) / span,
                                                 0.0F,
                                                 1.0F)
                                    : 0.0F;
                port::BEVPathSample interpolated{};
                interpolated.valid = true;
                interpolated.point.forward_m = gap_forward;
                interpolated.point.lateral_m =
                    previous.point.lateral_m +
                    (best->sample.point.lateral_m - previous.point.lateral_m) * t;
                interpolated.confidence =
                    std::min(previous.confidence, best->sample.confidence) * 0.45F;
                trace.edge[gap_layer] = interpolated;
                ++trace.gap_layers;
            }
        }

        const float df = best->sample.point.forward_m - previous.point.forward_m;
        if (df > 1.0e-4F) {
            trend = (best->sample.point.lateral_m - previous.point.lateral_m) / df;
        }
        trace.edge[layer] = best->sample;
        previous = best->sample;
        previous_layer = layer;
        pending_gap_layers = 0;
        confidence_sum += best->sample.confidence;
        ++trace.support_layers;
    }

    if (trace.support_layers < std::max(1, min_support_layers)) {
        return {};
    }
    trace.present = true;
    trace.confidence = std::clamp(confidence_sum / static_cast<float>(trace.support_layers),
                                  0.0F,
                                  1.0F);
    for (const port::BEVPathSample& sample : trace.edge) {
        if (!sample.valid) {
            continue;
        }
        if (trace.start_forward_m <= 1.0e-4F) {
            trace.start_forward_m = sample.point.forward_m;
        }
        trace.end_forward_m = sample.point.forward_m;
    }
    return trace;
}

int CountRejectedFarSegments(const InnerEdgeCandidateLayers& candidates,
                             const InnerEdgeTraceResult& trace,
                             const port::RuntimeParameters& params) {
    if (!trace.present) {
        return 0;
    }
    const float sample_spacing = ForwardSampleSpacingM(params);
    const float far_start = trace.end_forward_m + sample_spacing * 2.0F;
    int segments = 0;
    bool in_segment = false;
    for (std::size_t layer = 0; layer < candidates.size(); ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        const bool has_far_candidate = forward >= far_start && !candidates[layer].empty();
        if (has_far_candidate && !in_segment) {
            ++segments;
            in_segment = true;
        } else if (!has_far_candidate) {
            in_segment = false;
        }
    }
    return segments;
}

InnerEdgeTraceResult TraceInnerIslandRoadFacingEdge(
    const InnerEdgeCandidateLayers& candidates,
    const port::RuntimeParameters& params,
    const port::ReferencePolicyState* memory,
    bool left_side,
    float seed_lateral_m,
    float seed_forward_limit_m) {
    const int min_support_layers = std::max(1, params.bev_path_policy.circle_inner_min_layers);
    // Extended 1.5m BEV uses denser forward layers; cap the occlusion bridge by metric
    // distance, not by a fixed row count, while keeping it bounded to avoid far splices.
    const int max_gap_layers =
        std::clamp(static_cast<int>(std::ceil(0.22F / ForwardSampleSpacingM(params))), 2, 4);
    const float near_start_limit =
        std::max(static_cast<float>(params.bev_control_model.lookahead_max_m),
                 params.bev_topology_sampler.forward_samples_m[std::min<std::size_t>(
                     static_cast<std::size_t>(
                         std::max(0, params.bev_control_model.curvature_sample_index)),
                     port::kBevTrackSampleCount - 1U)]) +
        ForwardSampleSpacingM(params);
    const float start_limit = std::max(near_start_limit, seed_forward_limit_m);
    const float memory_lateral = MemorySeedLateral(memory, left_side, seed_lateral_m);
    InnerEdgeTraceResult best_trace{};
    float best_score = -std::numeric_limits<float>::infinity();
    std::size_t best_start_layer = port::kBevTrackSampleCount;
    for (std::size_t layer = 0; layer < candidates.size(); ++layer) {
        const float forward = params.bev_topology_sampler.forward_samples_m[layer];
        if (forward > start_limit + 1.0e-4F) {
            break;
        }
        for (const InnerEdgeCandidate& candidate : candidates[layer]) {
            const InnerEdgeTraceResult trace =
                TraceFromSeed(candidates,
                              layer,
                              candidate.sample,
                              params,
                              min_support_layers,
                              max_gap_layers);
            if (!trace.present) {
                continue;
            }
            const float seed_distance =
                std::min(std::abs(candidate.sample.point.lateral_m - seed_lateral_m),
                         std::abs(candidate.sample.point.lateral_m - memory_lateral));
            const float score =
                static_cast<float>(trace.support_layers) * 3.0F +
                trace.end_forward_m * 0.75F -
                static_cast<float>(trace.gap_layers) * 0.35F -
                static_cast<float>(layer) * 0.30F -
                seed_distance * 0.50F;
            if (!best_trace.present || layer < best_start_layer ||
                (layer == best_start_layer && score > best_score)) {
                best_trace = trace;
                best_score = score;
                best_start_layer = layer;
            }
        }
    }
    if (best_trace.present) {
        best_trace.rejected_far_segments = CountRejectedFarSegments(candidates, best_trace, params);
    }
    return best_trace;
}

port::BEVCircleInnerIslandEvidence DetectCircleInnerIsland(
    const port::LegacyCameraFrame& frame,
    int threshold,
    const port::RuntimeParameters& params,
    const BEVProjector& projector,
    const port::BEVCircleCornerEvidence& corner,
    const port::ReferencePolicyState* reference_memory,
    bool left_side) {
    port::BEVCircleInnerIslandEvidence evidence{};
    if (!corner.present) {
        return evidence;
    }

    const float lateral_min = std::min(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float lateral_max = std::max(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float lateral_step =
        std::max(0.010F, std::abs(params.bev_topology_sampler.lateral_step_m) * 0.75F);
    const float search_radius =
        std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.70F,
                 std::abs(params.bev_topology_sampler.lateral_step_m) * 10.0F);
    const float side_limit =
        std::max(0.08F, params.bev_corridor_graph.nominal_lane_width_m * 0.20F);
    const float scan_min = std::max(lateral_min, corner.lateral_m - search_radius);
    const float scan_max = std::min(lateral_max, corner.lateral_m + search_radius);
    const float forward_search_min = params.bev_topology_sampler.forward_samples_m.front();
    const float forward_search_max = params.bev_topology_sampler.forward_samples_m.back();

    WhiteBlackWhiteRun best_run{};
    float best_lateral = corner.lateral_m;
    int support_lines = 0;
    for (float lateral = scan_min; lateral <= scan_max + lateral_step * 0.5F; lateral += lateral_step) {
        if (left_side && lateral > side_limit) {
            continue;
        }
        if (!left_side && lateral < -side_limit) {
            continue;
        }
        const std::vector<ForwardClassRun> runs =
            BuildForwardRuns(frame,
                             threshold,
                             params,
                             projector,
                             lateral,
                             forward_search_min,
                             forward_search_max);
        const WhiteBlackWhiteRun candidate = FindWhiteBlackWhiteRun(runs, params);
        if (!candidate.valid) {
            continue;
        }
        ++support_lines;
        if (!best_run.valid || candidate.score > best_run.score ||
            (std::abs(candidate.score - best_run.score) < 1.0e-4F &&
             candidate.black_start_forward_m < best_run.black_start_forward_m)) {
            best_run = candidate;
            best_lateral = lateral;
        }
    }

    const float edge_hint = best_run.valid ? best_lateral : corner.lateral_m;
    const float forward_min = params.bev_topology_sampler.forward_samples_m.front();
    const float forward_max = params.bev_topology_sampler.forward_samples_m.back();
    const InnerEdgeCandidateLayers candidates =
        CollectInnerIslandRoadFacingEdgeCandidates(frame,
                                                   threshold,
                                                   params,
                                                   projector,
                                                   left_side,
                                                   edge_hint,
                                                   forward_min,
                                                   forward_max);
    evidence.raw_road_facing_edge = BestRawInnerEdgeByLayer(candidates, edge_hint);
    const float seed_forward_limit =
        best_run.valid
            ? best_run.black_start_forward_m + ForwardSampleSpacingM(params) * 2.0F
            : corner.forward_m + ForwardSampleSpacingM(params);
    const InnerEdgeTraceResult trace =
        TraceInnerIslandRoadFacingEdge(candidates,
                                       params,
                                       reference_memory,
                                       left_side,
                                       edge_hint,
                                       seed_forward_limit);
    evidence.trace_present = trace.present;
    evidence.edge_present = trace.present;
    evidence.road_facing_edge = trace.edge;
    evidence.trace_start_forward_m = trace.start_forward_m;
    evidence.trace_end_forward_m = trace.end_forward_m;
    evidence.trace_confidence = trace.confidence;
    evidence.trace_support_layers = trace.support_layers;
    evidence.trace_gap_layers = trace.gap_layers;
    evidence.rejected_far_segments = trace.rejected_far_segments;
    evidence.support_lines = support_lines;
    evidence.scan_lateral_m = edge_hint;
    if (best_run.valid) {
        evidence.present = best_run.score >= kInnerIslandPresentScoreMin;
        evidence.score = best_run.score;
        evidence.black_start_forward_m = best_run.black_start_forward_m;
        evidence.black_end_forward_m = best_run.black_end_forward_m;
        evidence.invalid_penalty = best_run.invalid_penalty;
    } else if (evidence.edge_present) {
        evidence.score = 0.35F;
    }
    return evidence;
}

// 计算边界点线段拟合的残差（评估点偏离直线的程度）
// 用于区分环岛转角与平滑弯道：转角处中断的点有更高的残差增益
float LineResidual(const std::vector<LayeredBoundaryPoint>& points, std::size_t first, std::size_t last) {
    if (last <= first + 1U) {
        return 0.0F;
    }
    const BoundaryPoint& a = points[first].point;
    const BoundaryPoint& b = points[last].point;
    const float df = b.forward_m - a.forward_m;
    if (std::abs(df) < 1.0e-4F) {
        return 0.0F;
    }
    float residual = 0.0F;
    for (std::size_t index = first + 1U; index < last; ++index) {
        const float t = (points[index].point.forward_m - a.forward_m) / df;
        const float expected = a.lateral_m + t * (b.lateral_m - a.lateral_m);
        residual += std::abs(points[index].point.lateral_m - expected);
    }
    return residual / static_cast<float>(last - first - 1U);
}

// 对候选转角点进行局部象限评分：在点周围四个象限采样，统计 drivable 分布
// 环岛转角的一侧应该是背景（不可行驶），另一侧应该是可行驶
LocalCornerPartition ScoreLocalCornerPartition(const port::LegacyCameraFrame& frame,
                                               int threshold,
                                               const port::RuntimeParameters& params,
                                               const BEVProjector& projector,
                                               const BoundaryPoint& point) {
    LocalCornerPartition partition{};
    constexpr std::array<float, 3> kForwardOffsets{{0.025F, 0.050F, 0.075F}};
    constexpr std::array<float, 3> kLateralOffsets{{0.025F, 0.050F, 0.075F}};
    std::array<int, 4> reliable_count{};
    std::array<int, 4> drivable_count{};
    int total_samples = 0;
    int invalid_samples = 0;
    // 四个象限的采样：前/后 × 左/右
    for (int forward_sign : {-1, 1}) {
        for (int lateral_sign : {-1, 1}) {
            const int quadrant = (forward_sign > 0 ? 2 : 0) + (lateral_sign > 0 ? 1 : 0);
            for (float forward_offset : kForwardOffsets) {
                for (float lateral_offset : kLateralOffsets) {
                    ++total_samples;
                    const DenseSample sample =
                        SampleDensePoint(frame,
                                         threshold,
                                         params,
                                         projector,
                                         {point.forward_m + static_cast<float>(forward_sign) * forward_offset,
                                          point.lateral_m + static_cast<float>(lateral_sign) * lateral_offset});
                    if (!IsReliableProjection(sample)) {
                        ++invalid_samples;
                        continue;
                    }
                    ++reliable_count[static_cast<std::size_t>(quadrant)];
                    if (IsDrivable(sample.sample_class)) {
                        ++drivable_count[static_cast<std::size_t>(quadrant)];
                    }
                }
            }
        }
    }

    // 计算四个象限的 drivable 比例
    std::array<float, 4> ratios{};
    for (std::size_t index = 0; index < ratios.size(); ++index) {
        if (reliable_count[index] > 0) {
            ratios[index] = static_cast<float>(drivable_count[index]) /
                            static_cast<float>(reliable_count[index]);
        }
    }
    // 排序后判断：是否只有一个象限是暗（背景）或只有一个是亮（可行驶）
    std::array<float, 4> sorted = ratios;
    std::sort(sorted.begin(), sorted.end());
    const float dark = sorted[0];
    const float high_three = (sorted[1] + sorted[2] + sorted[3]) / 3.0F;
    const float bright = sorted[3];
    const float low_three = (sorted[0] + sorted[1] + sorted[2]) / 3.0F;
    partition.one_dark_score =
        Clamp01((high_three - 0.55F) / 0.35F) * Clamp01((0.35F - dark) / 0.35F);
    partition.one_bright_score =
        Clamp01((bright - 0.55F) / 0.35F) * Clamp01((0.35F - low_three) / 0.35F);
    partition.invalid_penalty =
        total_samples > 0 ? Clamp01(static_cast<float>(invalid_samples) / static_cast<float>(total_samples))
                          : 1.0F;
    partition.score = Clamp01(std::max(partition.one_dark_score, partition.one_bright_score) *
                              (1.0F - partition.invalid_penalty));
    return partition;
}

// 检查转角候选的横向位置是否与期望侧一致
bool CornerSideConsistent(float lateral_m, bool left_side, const port::RuntimeParameters& params) {
    const float side_margin = std::max(0.04F, params.bev_corridor_graph.nominal_lane_width_m * 0.10F);
    return left_side ? lateral_m <= -side_margin : lateral_m >= side_margin;
}

// 沿边界轨迹追溯连续层数（检查边界是否在连续层中稳定存在）
int TraceContinuationCount(const std::array<BoundaryPoint, port::kBevTrackSampleCount>& trace,
                           std::size_t layer,
                           int direction,
                           float max_lateral_jump_m) {
    int count = 1;
    std::size_t current = layer;
    while (count < 5) {
        if (direction < 0) {
            if (current == 0U) {
                break;
            }
            --current;
        } else {
            if (current + 1U >= trace.size()) {
                break;
            }
            ++current;
        }
        if (!trace[current].valid) {
            continue;
        }
        // 横向突变 → 中断连续性
        if (std::abs(trace[current].lateral_m - trace[layer].lateral_m) > max_lateral_jump_m) {
            break;
        }
        ++count;
    }
    return count;
}

// 在对面侧边界轨迹中寻找横向位置相近的点（用于对侧验证）
const BoundaryPoint* FindNearbyOppositeTracePoint(
    const std::array<BoundaryPoint, port::kBevTrackSampleCount>& opposite_trace,
    std::size_t layer,
    float lateral_m,
    float max_lateral_delta_m) {
    const BoundaryPoint* best = nullptr;
    float best_delta = max_lateral_delta_m;
    const std::size_t first = layer == 0U ? 0U : layer - 1U;
    const std::size_t last = std::min<std::size_t>(opposite_trace.size() - 1U, layer + 1U);
    for (std::size_t candidate_layer = first; candidate_layer <= last; ++candidate_layer) {
        const BoundaryPoint& candidate = opposite_trace[candidate_layer];
        if (!candidate.valid) {
            continue;
        }
        const float delta = std::abs(candidate.lateral_m - lateral_m);
        if (delta <= best_delta) {
            best_delta = delta;
            best = &candidate;
        }
    }
    return best;
}

// 检测环岛转角（circle corner）
// 从边界跟踪点序列中寻找方向突变的点（转角特征）
// 使用三段检测：前-中-后三点之间的方向变化 > kCircleCornerAngleMinDeg 时判定为转角
port::BEVCircleCornerEvidence DetectCircleCorner(
    const std::array<BoundaryPoint, port::kBevTrackSampleCount>& trace,
    const std::array<BoundaryPoint, port::kBevTrackSampleCount>& opposite_trace,
    bool left_side,
    const std::array<port::BEVRowProfileEvidence, port::kBevTrackSampleCount>& row_profiles,
    const port::LegacyCameraFrame& frame,
    int threshold,
    const port::RuntimeParameters& params,
    const BEVProjector& projector) {
    port::BEVCircleCornerEvidence best{};
    float best_selection_score = 0.0F;
    // 收集轨迹中所有有效点
    std::vector<LayeredBoundaryPoint> points;
    points.reserve(trace.size());
    for (std::size_t layer = 0U; layer < trace.size(); ++layer) {
        if (trace[layer].valid) {
            points.push_back({trace[layer], layer});
        }
    }
    // 需要至少 5 个有效点才能做转角检测
    if (points.size() < 5U) {
        return best;
    }

    const float lateral_min = std::min(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float lateral_max = std::max(params.bev_topology_sampler.lateral_min_m,
                                       params.bev_topology_sampler.lateral_max_m);
    const float edge_margin = std::max(0.06F, std::abs(params.bev_topology_sampler.lateral_step_m) * 3.0F);
    const float element_lateral_limit =
        std::min(std::max(std::abs(lateral_min), std::abs(lateral_max)) - edge_margin,
                 std::max(0.45F, params.bev_corridor_graph.nominal_lane_width_m * 1.30F));
    const float corner_forward_max_m = params.bev_topology_sampler.forward_samples_m.back();

    // ===== 第一遍：扫描式转角检测 =====
    // 滑动窗口 [index-2, index, index+2] 的中点为候选转角
    for (std::size_t index = 2U; index + 2U < points.size(); ++index) {
        const BoundaryPoint& prev = points[index - 2U].point;
        const BoundaryPoint& mid = points[index].point;
        if (mid.forward_m > corner_forward_max_m) {
            continue;
        }
        const BoundaryPoint& next = points[index + 2U].point;
        // 排除太靠近采样边界的点
        if (mid.lateral_m <= lateral_min + edge_margin || mid.lateral_m >= lateral_max - edge_margin) {
            continue;
        }
        if (std::abs(mid.lateral_m) > element_lateral_limit) {
            continue;
        }
        if (!CornerSideConsistent(mid.lateral_m, left_side, params)) {
            continue;
        }
        const float before_df = mid.forward_m - prev.forward_m;
        const float after_df = next.forward_m - mid.forward_m;
        if (std::abs(before_df) < 1.0e-4F || std::abs(after_df) < 1.0e-4F) {
            continue;
        }
        // 计算前后段之间的夹角（atan(纵向变化/前向变化) 的差）
        const float slope_before = (mid.lateral_m - prev.lateral_m) / before_df;
        const float slope_after = (next.lateral_m - mid.lateral_m) / after_df;
        const float angle = std::abs(std::atan(slope_after) - std::atan(slope_before));
        // 如果夹角太小（平滑曲线），不是转角
        if (angle < DegToRad(kCircleCornerAngleMinDeg)) {
            continue;
        }

        // 局部角度验证（相邻点之间的夹角）
        const float local_before_df = points[index].point.forward_m - points[index - 1U].point.forward_m;
        const float local_after_df = points[index + 1U].point.forward_m - points[index].point.forward_m;
        if (std::abs(local_before_df) < 1.0e-4F || std::abs(local_after_df) < 1.0e-4F) {
            continue;
        }
        const float local_slope_before =
            (points[index].point.lateral_m - points[index - 1U].point.lateral_m) / local_before_df;
        const float local_slope_after =
            (points[index + 1U].point.lateral_m - points[index].point.lateral_m) / local_after_df;
        const float local_angle =
            std::abs(std::atan(local_slope_after) - std::atan(local_slope_before));
        if (local_angle < DegToRad(kCircleLocalAngleMinDeg)) {
            continue;
        }

        // 局部象限评分：检查转角处的 drivable 分布
        const LocalCornerPartition partition =
            ScoreLocalCornerPartition(frame, threshold, params, projector, mid);

        // 残差增益分析：直线拟合 vs 两段拟合的残差比
        // 转角处两段拟合的残差之和远小于单段拟合 → 残差增益高
        const float single_residual = LineResidual(points, index - 2U, index + 2U);
        const float two_residual =
            0.5F * (LineResidual(points, index - 2U, index) + LineResidual(points, index, index + 2U));
        const float residual_gain =
            single_residual > 1.0e-4F
                ? Clamp01((single_residual - two_residual) / single_residual)
                : 0.0F;
        // 如果残差增益和局部评分都低 → 不是转角
        if (residual_gain < 0.58F && partition.score < 0.70F) {
            continue;
        }
        // 横向顶点幅值：转角处的横向偏移量
        const float lateral_apex =
            std::abs(mid.lateral_m - 0.5F * (prev.lateral_m + next.lateral_m));
        const float apex_score =
            Clamp01(lateral_apex / std::max(1.0e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.20F));
        const float invalid_penalty =
            Clamp01(std::max((prev.invalid_penalty + mid.invalid_penalty + next.invalid_penalty) / 3.0F,
                             partition.invalid_penalty));
        const float angle_score = Clamp01(angle / DegToRad(95.0F));
        const float local_score = Clamp01(local_angle / DegToRad(80.0F));
        // 清晰度 = 残差增益 + 顶点幅值 + 局部角度 + 分区评分的加权组合
        const float clarity = Clamp01(0.35F * residual_gain + 0.25F * apex_score +
                                      0.15F * local_score + 0.25F * partition.score);
        const float score = Clamp01(angle_score * clarity * (1.0F - invalid_penalty));
        const float selection_score = score;
        if (selection_score > best_selection_score) {
            best_selection_score = selection_score;
            best.present = score >= kCircleCornerPresentScoreMin;
            best.score = score;
            best.forward_m = mid.forward_m;
            best.lateral_m = mid.lateral_m;
            best.angle_score = angle_score;
            best.clarity_score = clarity;
            best.smooth_curve_reject_score = Clamp01(1.0F - residual_gain);
            best.invalid_penalty = invalid_penalty;
            best.support_layers = 5;
        }
    }
    // ===== 第二遍：近端点检测 =====
    // 检测近端（临近车辆）的转角候选，主要基于行剖面的紧凑间隙和象限评分
    const float max_lateral_jump_m = std::max(0.10F, params.bev_corridor_graph.max_center_jump_m * 0.75F);
    const float near_endpoint_forward_max_m =
        std::max(params.bev_corridor_graph.nominal_lane_width_m,
                 params.bev_topology_sampler.forward_samples_m[4]);
    const float compact_gap_max_m =
        std::max(params.bev_corridor_graph.nominal_lane_width_m * 0.65F,
                 std::abs(params.bev_topology_sampler.lateral_step_m) * 6.0F);
    for (std::size_t layer = 1U; layer + 1U < trace.size(); ++layer) {
        const BoundaryPoint& point = trace[layer];
        if (!point.valid ||
            point.forward_m > near_endpoint_forward_max_m ||
            point.lateral_m <= lateral_min + edge_margin ||
            point.lateral_m >= lateral_max - edge_margin ||
            std::abs(point.lateral_m) > element_lateral_limit ||
            !CornerSideConsistent(point.lateral_m, left_side, params)) {
            continue;
        }
        // 检查该层行剖面：不能有过大的不可行驶间隙（compact 约束）
        const port::BEVRowProfileEvidence& profile = row_profiles[layer];
        if (!profile.valid ||
            profile.largest_non_drivable_gap_m > compact_gap_max_m) {
            continue;
        }
        // 检查边界连续性：需要前一层支持，不需要后一层支持（端点的特征）
        const bool previous_supported =
            trace[layer - 1U].valid &&
            std::abs(trace[layer - 1U].lateral_m - point.lateral_m) <= max_lateral_jump_m;
        const bool next_supported =
            trace[layer + 1U].valid &&
            std::abs(trace[layer + 1U].lateral_m - point.lateral_m) <= max_lateral_jump_m;
        if (!previous_supported || next_supported) {
            continue;
        }
        const LocalCornerPartition partition =
            ScoreLocalCornerPartition(frame, threshold, params, projector, point);
        const float compact_gap_score =
            Clamp01(1.0F - profile.largest_non_drivable_gap_m / std::max(1.0e-4F, compact_gap_max_m));
        const float endpoint_lateral_score =
            Clamp01((std::abs(point.lateral_m) - params.bev_corridor_graph.nominal_lane_width_m * 0.20F) /
                    std::max(1.0e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.25F));
        const float invalid_penalty = Clamp01(std::max(point.invalid_penalty, partition.invalid_penalty));
        const float clarity =
            Clamp01(0.45F * compact_gap_score + 0.35F * endpoint_lateral_score +
                    0.20F * std::max(partition.score, partition.one_dark_score));
        const float score = Clamp01(clarity * (1.0F - invalid_penalty));
        if (score <= best_selection_score) {
            continue;
        }
        best_selection_score = score;
        best.present = score >= kCircleCornerPresentScoreMin;
        best.score = score;
        best.forward_m = point.forward_m;
        best.lateral_m = point.lateral_m;
        best.angle_score = endpoint_lateral_score;
        best.clarity_score = clarity;
        best.smooth_curve_reject_score = compact_gap_score;
        best.invalid_penalty = invalid_penalty;
        best.support_layers = 2;
    }
    // ===== 第三遍：对侧验证检测 =====
    // 如果当前侧的边界点在横向位置上与对侧的边界点对齐，
    // 则可以验证这是一个真正的通道/入口（环岛入口两侧都有边界点）
    const float opposite_lateral_window_m =
        std::max(0.06F, std::abs(params.bev_topology_sampler.lateral_step_m) * 4.0F);
    for (std::size_t layer = 1U; layer + 1U < trace.size(); ++layer) {
        const BoundaryPoint& point = trace[layer];
        if (!point.valid ||
            point.forward_m > corner_forward_max_m ||
            point.lateral_m <= lateral_min + edge_margin ||
            point.lateral_m >= lateral_max - edge_margin ||
            std::abs(point.lateral_m) > element_lateral_limit ||
            !CornerSideConsistent(point.lateral_m, left_side, params)) {
            continue;
        }
        // 在对侧轨迹中寻找位置匹配的点
        const BoundaryPoint* opposite =
            FindNearbyOppositeTracePoint(opposite_trace, layer, point.lateral_m, opposite_lateral_window_m);
        if (opposite == nullptr) {
            continue;
        }
        // 连续性检查：至少连续 3 层有效
        const int forward_support = std::max(TraceContinuationCount(trace, layer, -1, max_lateral_jump_m),
                                             TraceContinuationCount(trace, layer, 1, max_lateral_jump_m));
        if (forward_support < 3) {
            continue;
        }
        const LocalCornerPartition partition =
            ScoreLocalCornerPartition(frame, threshold, params, projector, point);
        const float lateral_alignment =
            Clamp01(1.0F - std::abs(point.lateral_m - opposite->lateral_m) /
                              std::max(1.0e-4F, opposite_lateral_window_m));
        const float support_score = Clamp01(static_cast<float>(forward_support - 2) / 3.0F);
        const float invalid_penalty =
            Clamp01(std::max((point.invalid_penalty + opposite->invalid_penalty) * 0.5F,
                             partition.invalid_penalty));
        const float clarity =
            Clamp01(0.45F * lateral_alignment + 0.35F * support_score +
                    0.20F * std::max(partition.score, partition.one_dark_score));
        const float score = Clamp01(clarity * (1.0F - invalid_penalty));
        const float selection_score = score + support_score * 0.05F;
        if (selection_score > best_selection_score) {
            best_selection_score = selection_score;
            best.present = score >= kCircleCornerPresentScoreMin;
            best.score = score;
            best.forward_m = (point.forward_m + opposite->forward_m) * 0.5F;
            best.lateral_m = (point.lateral_m + opposite->lateral_m) * 0.5F;
            best.angle_score = lateral_alignment;
            best.clarity_score = clarity;
            best.smooth_curve_reject_score = 1.0F;
            best.invalid_penalty = invalid_penalty;
            best.support_layers = forward_support;
        }
    }
    if (!best.present) {
        best.score = 0.0F;
    }
    return best;
}

}  // namespace

// ExtractBEVElementEvidence 的公开入口
// 流程：
// 1. 构建所有层的行剖面
// 2. 检测十字路口带（cross band）
// 3. 构建左右边界轨迹
// 4. 分别检测左右环岛转角
// 5. 如果 cross band 存在，则抑制 circle corner（场景互斥）
port::BEVElementEvidence ExtractBEVElementEvidence(const port::LegacyCameraFrame& frame,
                                                   int threshold,
                                                   const port::RuntimeParameters& params,
                                                   const BEVProjector& projector,
                                                   const BEVSparseSampleGrid& sparse_grid,
                                                   const port::ReferencePolicyState* reference_memory) {
    port::BEVElementEvidence evidence{};
    if (!sparse_grid.valid || !projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        return evidence;
    }
    evidence.valid = true;
    const float step = std::max(1.0e-4F, std::abs(params.bev_topology_sampler.lateral_step_m));
    // 步骤 1：为每一层构建行剖面
    float invalid_sum = 0.0F;
    int profile_count = 0;
    for (std::size_t layer_index = 0; layer_index < sparse_grid.layers.size(); ++layer_index) {
        evidence.row_profiles[layer_index] = BuildSparseRowProfile(sparse_grid.layers[layer_index], step);
        if (evidence.row_profiles[layer_index].valid) {
            invalid_sum += evidence.row_profiles[layer_index].invalid_ratio;
            ++profile_count;
        }
    }
    if (profile_count > 0) {
        evidence.invalid_edge_penalty = Clamp01(invalid_sum / static_cast<float>(profile_count));
    }
    // 步骤 2：检测十字路口带
    evidence.cross_band = DetectCrossBand(frame, threshold, params, projector, sparse_grid);
    // 步骤 3：构建左右边界轨迹
    const std::array<BoundaryPoint, port::kBevTrackSampleCount> left_trace =
        BuildBoundaryTrace(sparse_grid, true);
    const std::array<BoundaryPoint, port::kBevTrackSampleCount> right_trace =
        BuildBoundaryTrace(sparse_grid, false);
    // 步骤 4：检测左右环岛转角
    evidence.left_circle_corner =
        DetectCircleCorner(left_trace,
                           right_trace,
                           true,
                           evidence.row_profiles,
                           frame,
                           threshold,
                           params,
                           projector);
    evidence.right_circle_corner =
        DetectCircleCorner(right_trace,
                           left_trace,
                           false,
                           evidence.row_profiles,
                           frame,
                           threshold,
                           params,
                           projector);
    evidence.left_inner_island =
        DetectCircleInnerIsland(frame,
                                threshold,
                                params,
                                projector,
                                evidence.left_circle_corner,
                                reference_memory,
                                true);
    evidence.right_inner_island =
        DetectCircleInnerIsland(frame,
                                threshold,
                                params,
                                projector,
                                evidence.right_circle_corner,
                                reference_memory,
                                false);
    // 步骤 5：场景互斥——十字路口存在时抑制环岛检测
    if (evidence.cross_band.present) {
        evidence.left_circle_corner = {};
        evidence.right_circle_corner = {};
        evidence.left_inner_island = {};
        evidence.right_inner_island = {};
    }
    return evidence;
}

}  // namespace ls2k::legacy
