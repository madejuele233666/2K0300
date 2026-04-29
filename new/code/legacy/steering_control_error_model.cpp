#include "legacy/steering_control_error_model.hpp"

// 控制误差模型实现。
// 纯追踪 + 航向角 + 曲率前馈的复合控制律。
// 流程：
// 1. 从参考路径提取近侧误差、远侧航向、预览曲率
// 2. 计算自适应前视距离（基于可见范围和比例）
// 3. 计算前视点处的横向误差和航向误差
// 4. 合成曲率命令（kappa_pp + kappa_heading + kappa_feedforward）
// 5. 应用控制约束（增益缩放 / 限速 / 转向抑制）

#include <algorithm>
#include <cmath>

#include "legacy/steering_path_math.hpp"

namespace ls2k::legacy {
namespace {

// 参考路径的前向距离范围结构
struct ReferenceForwardRange {
    bool valid = false;
    float min_forward_m = 0.0F;
    float max_forward_m = 0.0F;
};

// 计算参考路径的前向距离范围（最小/最大有效前向距离）
ReferenceForwardRange ComputeReferenceForwardRange(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    ReferenceForwardRange range{};
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        if (!range.valid) {
            range.valid = true;
            range.min_forward_m = sample.point.forward_m;
            range.max_forward_m = sample.point.forward_m;
            continue;
        }
        range.min_forward_m = std::min(range.min_forward_m, sample.point.forward_m);
        range.max_forward_m = std::max(range.max_forward_m, sample.point.forward_m);
    }
    return range;
}

// 从参考路径计算两点间的航向角（atan2(Δlateral, Δforward)）
float HeadingFromReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                           int near_index,
                           int far_index) {
    if (near_index < 0 || far_index < 0 || near_index >= static_cast<int>(path.size()) ||
        far_index >= static_cast<int>(path.size()) || far_index <= near_index || !path[near_index].valid ||
        !path[far_index].valid) {
        return 0.0F;
    }
    const float delta_forward = path[far_index].point.forward_m - path[near_index].point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 0.0F;
    }
    return std::atan2(path[far_index].point.lateral_m - path[near_index].point.lateral_m, delta_forward);
}

// 从参考路径计算三点间的曲率（两点斜率变化的二阶差分）
float CurvatureFromReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                             int near_index,
                             int curvature_index) {
    if (near_index <= 0 || curvature_index >= static_cast<int>(path.size()) || curvature_index <= near_index ||
        !path[0].valid || !path[near_index].valid || !path[curvature_index].valid) {
        return 0.0F;
    }
    return PathCurvatureFromThreeSamples(path[0], path[near_index], path[curvature_index]);
}

// 在参考路径上线性插值得到指定前向距离处的路径点
bool InterpolateReferenceAt(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float forward_m,
                            port::BEVPathSample& output) {
    const ReferenceForwardRange range = ComputeReferenceForwardRange(path);
    constexpr float kForwardEpsilonM = 1.0e-4F;
    if (!range.valid || forward_m < range.min_forward_m - kForwardEpsilonM ||
        forward_m > range.max_forward_m + kForwardEpsilonM) {
        return false;
    }

    const port::BEVPathSample* previous = nullptr;
    const port::BEVPathSample* next = nullptr;
    for (const port::BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        if (sample.point.forward_m <= forward_m) {
            previous = &sample;
        }
        if (sample.point.forward_m >= forward_m) {
            next = &sample;
            break;
        }
    }

    if (previous == nullptr) {
        output = *next;
        output.point.forward_m = forward_m;
        return true;
    }
    if (next == nullptr) {
        output = *previous;
        output.point.forward_m = forward_m;
        return true;
    }

    const float span = next->point.forward_m - previous->point.forward_m;
    if (std::abs(span) < 1e-4F) {
        output = *next;
        return true;
    }

    const float t = std::clamp((forward_m - previous->point.forward_m) / span, 0.0F, 1.0F);
    output.valid = true;
    output.point.forward_m = forward_m;
    output.point.lateral_m = previous->point.lateral_m +
                             t * (next->point.lateral_m - previous->point.lateral_m);
    output.confidence = previous->confidence + t * (next->confidence - previous->confidence);
    return true;
}

// 计算参考路径上指定前向距离处的航向角（用前后窗口平均）
float HeadingAtReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                         float forward_m,
                         const port::BEVPathSample& lookahead) {
    const ReferenceForwardRange range = ComputeReferenceForwardRange(path);
    if (!range.valid) {
        return 0.0F;
    }
    const float heading_window_m = std::max(0.02F, forward_m * 0.25F);
    const float before_m =
        std::clamp(forward_m - heading_window_m, range.min_forward_m, range.max_forward_m);
    const float after_m =
        std::clamp(forward_m + heading_window_m, range.min_forward_m, range.max_forward_m);
    port::BEVPathSample before{};
    port::BEVPathSample after{};
    if (InterpolateReferenceAt(path, before_m, before) && InterpolateReferenceAt(path, after_m, after)) {
        const float ds = after.point.forward_m - before.point.forward_m;
        if (std::abs(ds) >= 1e-4F) {
            return std::atan2(after.point.lateral_m - before.point.lateral_m, ds);
        }
    }

    port::BEVPathSample origin{};
    if (InterpolateReferenceAt(path, range.min_forward_m, origin)) {
        const float ds = lookahead.point.forward_m - origin.point.forward_m;
        if (std::abs(ds) >= 1e-4F) {
            return std::atan2(lookahead.point.lateral_m - origin.point.lateral_m, ds);
        }
    }
    return 0.0F;
}

// 计算前视点附近的曲率（三点法：起点 → 中点 → 前视点）
float CurvatureAroundLookahead(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                               float lookahead_m) {
    const ReferenceForwardRange range = ComputeReferenceForwardRange(path);
    if (!range.valid) {
        return 0.0F;
    }
    port::BEVPathSample start{};
    port::BEVPathSample mid{};
    port::BEVPathSample end{};
    const float mid_m = range.min_forward_m + (lookahead_m - range.min_forward_m) * 0.5F;
    if (!InterpolateReferenceAt(path, range.min_forward_m, start) ||
        !InterpolateReferenceAt(path, mid_m, mid) ||
        !InterpolateReferenceAt(path, lookahead_m, end)) {
        return 0.0F;
    }

    return PathCurvatureFromThreeSamples(start, mid, end);
}

// 将值限幅到 [-limit, +limit] 范围
float ClampMagnitude(float value, double limit) {
    const float abs_limit = static_cast<float>(std::max(0.0, limit));
    return std::clamp(value, -abs_limit, abs_limit);
}

}  // namespace

// ========== 主入口：计算控制误差模型 ==========
// 步骤：
// 1. 验证输入有效性（track + reference_path 均有效）
// 2. 提取近侧横向误差、远侧航向误差、预览曲率
// 3. 计算自适应前视距离（基于可见范围 × ratio，限幅到 [min, max]）
// 4. 计算前视点横向误差、航向误差、参考曲率
// 5. 合成曲率命令：kappa_pp (纯追踪) + kappa_heading (航向) + kappa_feedforward (前馈)
// 6. 应用控制约束（增益缩放、限速、转向抑制）
port::ControlErrorModelOutput ComputeControlErrorModel(const port::ControlErrorModelInput& input,
                                                       const port::RuntimeParameters& params) {
    port::ControlErrorModelOutput output{};
    output.valid = input.track.valid && input.reference_path.valid;
    output.visible_range_m = input.track.visible_range_m;
    output.track_confidence = input.track.track_confidence;
    if (!output.valid) {
        output.degraded = true;
        output.steering_suppressed = true;
        output.degrade_reason = "reference_or_track_invalid";
        return output;
    }

    // 步骤 2：提取固定采样点的误差
    const port::BEVControlModelParameters& model = params.bev_control_model;
    const int near_index = std::clamp(model.near_sample_index, 0, static_cast<int>(port::kBevTrackSampleCount - 1));
    const int far_index = std::clamp(model.far_sample_index, near_index, static_cast<int>(port::kBevTrackSampleCount - 1));
    const int curvature_index =
        std::clamp(model.curvature_sample_index, far_index, static_cast<int>(port::kBevTrackSampleCount - 1));
    const ReferenceForwardRange reference_range =
        ComputeReferenceForwardRange(input.reference_path.sampled_path);
    if (!reference_range.valid) {
        output.valid = false;
        output.degraded = true;
        output.steering_suppressed = true;
        output.degrade_reason = "reference_support_unavailable";
        return output;
    }

    if (input.reference_path.sampled_path[near_index].valid) {
        output.near_lateral_error = input.reference_path.sampled_path[near_index].point.lateral_m;
    }
    output.far_heading_error = HeadingFromReference(input.reference_path.sampled_path, near_index, far_index);
    output.preview_curvature =
        CurvatureFromReference(input.reference_path.sampled_path, near_index, curvature_index);

    // 步骤 3：计算自适应前视距离
    const double min_lookahead = std::max(0.0, std::min(model.lookahead_min_m, model.lookahead_max_m));
    const double max_lookahead = std::max(model.lookahead_min_m, model.lookahead_max_m);
    const double raw_lookahead =
        static_cast<double>(std::max(0.0F, input.track.visible_range_m)) *
        std::max(0.0, model.lookahead_visible_range_ratio);
    const float requested_lookahead_m =
        static_cast<float>(std::clamp(raw_lookahead, min_lookahead, max_lookahead));
    const float lookahead_m =
        std::clamp(requested_lookahead_m, reference_range.min_forward_m, reference_range.max_forward_m);
    port::BEVPathSample lookahead{};
    if (!InterpolateReferenceAt(input.reference_path.sampled_path, lookahead_m, lookahead)) {
        output.valid = false;
        output.degraded = true;
        output.steering_suppressed = true;
        output.degrade_reason = "reference_lookahead_unavailable";
        return output;
    }

    // 步骤 4：计算前视点的横向/航向误差和参考曲率
    output.lookahead_distance_m = lookahead_m;
    output.lookahead_lateral_error = lookahead.point.lateral_m;
    output.lookahead_heading_error =
        HeadingAtReference(input.reference_path.sampled_path, lookahead_m, lookahead);
    output.reference_curvature = CurvatureAroundLookahead(input.reference_path.sampled_path, lookahead_m);

    // 步骤 5：合成曲率命令（纯追踪 + 航向误差 + 曲率前馈）
    const float lateral = output.lookahead_lateral_error;
    const float denominator = std::max(1e-4F, lookahead_m * lookahead_m + lateral * lateral);
    const float kappa_pp = static_cast<float>(model.pure_pursuit_gain) * (2.0F * lateral / denominator);
    const float kappa_heading =
        static_cast<float>(model.heading_curvature_gain) * std::sin(output.lookahead_heading_error) /
        std::max(1e-4F, lookahead_m);
    const float kappa_feedforward =
        static_cast<float>(model.curvature_feedforward_gain) * output.reference_curvature;
    output.curvature_command =
        ClampMagnitude(kappa_pp + kappa_heading + kappa_feedforward, model.curvature_command_limit);
    output.yaw_rate_target = input.vehicle.speed_mps * output.curvature_command;

    output.steering_gain_scale = input.constraints.steering_gain_scale;
    output.speed_limit_scale = input.constraints.speed_limit_scale;
    output.turn_limit_scale = input.constraints.turn_limit_scale;
    output.steering_suppressed = input.constraints.steering_suppressed || input.constraints.fail_safe_veto;
    output.degraded = input.constraints.low_confidence_degraded || input.constraints.fail_safe_veto;
    output.degrade_reason = input.constraints.primary_reason;
    return output;
}

}  // namespace ls2k::legacy
