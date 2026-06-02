#include "vision/elements/cross_exit_element_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ls2k::vision {
namespace {

/// 十字出口检测的最小连续宽行数
constexpr std::size_t kCrossMinContiguousWideRows = 3U;
/// 每行最小可采样数
constexpr std::size_t kCrossMinSampleablePerRow = 8U;
/// 宽行最小宽度（米）
constexpr float kCrossMinWideWidthM = 0.90F;
/// 宽行占可采样区域的最小宽度比例
constexpr float kCrossMinSampleableWidthRatio = 0.65F;
/// 双侧最小伸达距离（米）
constexpr float kCrossMinBilateralReachM = 0.35F;
/// 双侧平衡度最小值
constexpr float kCrossMinBilateralBalance = 0.50F;
/// 不确定像素最大比例
constexpr float kCrossUnknownRatioMax = 0.25F;
/// 十字出口存在的最小置信度
constexpr float kCrossPresentConfidenceMin = 0.70F;
/// 比率计算的分母最小值
constexpr float kRatioDenominatorFloor = 1.0e-4F;
/// 双侧开口的固定增长比例门限
constexpr float kCrossOpeningExpansionRatioMin = 0.10F;
/// 开口判断的持续行数
constexpr std::size_t kOpeningSustainRows = 2U;

/// 将值钳制到[0, 1]范围
float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

/// 十字出口游程累加器，累计连续宽行行的统计数据和得分
struct CrossRunAccumulator {
    std::size_t row_count = 0;         ///< 累加的行数
    float score_sum = 0.0F;            ///< 综合评分之和
    float forward_min_m = 0.0F;        ///< 前向距离最小值（米）
    float forward_max_m = 0.0F;        ///< 前向距离最大值（米）
    float lateral_min_m = 0.0F;        ///< 横向最小值（米）
    float lateral_max_m = 0.0F;        ///< 横向最大值（米）
    std::size_t sampleable_count = 0;         ///< 总计可采样数
    std::size_t supporting_white_count = 0;   ///< 总计白色支撑数
    std::size_t unknown_count = 0;            ///< 总计不确定数
};

/// 边界开口事实
struct BoundaryOpeningFacts {
    bool left_open = false;   ///< 左侧是否开口
    bool right_open = false;  ///< 右侧是否开口
};

/// 十字出口边界观测值
struct CrossBoundaryObservation {
    float forward_m = 0.0F;   ///< 前向距离（米）
    float left_reach_m = 0.0F;   ///< 左侧伸达距离（米）
    float right_reach_m = 0.0F;  ///< 右侧伸达距离（米）
};

/// 找出一行中最宽的白色区间
/// @return 指向最宽区间的指针，若无区间则返回nullptr
const BEVSimpleWhiteInterval* WidestInterval(const BEVSimpleRowScan& row) {
    const BEVSimpleWhiteInterval* best = nullptr;
    for (const BEVSimpleWhiteInterval& interval : row.intervals) {
        if (best == nullptr || interval.width_m > best->width_m) {
            best = &interval;
        }
    }
    return best;
}

/// 将一行的数据累加到十字出口运行累加器中
void AddWideRow(CrossRunAccumulator& run,
                const BEVSimpleRowScan& row,
                const BEVSimpleWhiteInterval& interval,
                float score) {
    if (run.row_count == 0U) {
        run.forward_min_m = row.forward_m;
        run.forward_max_m = row.forward_m;
        run.lateral_min_m = interval.left_m;
        run.lateral_max_m = interval.right_m;
    } else {
        run.forward_min_m = std::min(run.forward_min_m, row.forward_m);
        run.forward_max_m = std::max(run.forward_max_m, row.forward_m);
        run.lateral_min_m = std::min(run.lateral_min_m, interval.left_m);
        run.lateral_max_m = std::max(run.lateral_max_m, interval.right_m);
    }
    ++run.row_count;
    run.score_sum += score;
    run.sampleable_count += row.sampleable_count;
    run.supporting_white_count += row.white_count;
    run.unknown_count += row.unknown_count;
}

/// 比较两个累加器，判断哪个更优（行数优先，评分其次）
bool BetterRun(const CrossRunAccumulator& candidate, const CrossRunAccumulator& best) {
    if (candidate.row_count != best.row_count) {
        return candidate.row_count > best.row_count;
    }
    return candidate.score_sum > best.score_sum;
}

/// 计算近端到远端的增长率
float GrowthRatio(float near_reach, float far_reach) {
    return (far_reach - near_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

/// 从边界观测中获取指定侧的伸达距离
float Reach(const CrossBoundaryObservation& observation, bool use_left) {
    return use_left ? observation.left_reach_m : observation.right_reach_m;
}

/// 计算持续多行的边界扩张增长率
/// 遍历所有分割点，计算后续持续行相对于前一点的增长率，取最大值
float SustainedGrowthRatio(const std::vector<CrossBoundaryObservation>& observations,
                           bool use_left) {
    if (observations.size() <= kOpeningSustainRows) {
        return 0.0F;
    }
    float best = 0.0F;
    for (std::size_t split = 1U;
         split + kOpeningSustainRows <= observations.size();
         ++split) {
        float sustained_reach = Reach(observations[split], use_left);
        for (std::size_t offset = 1U; offset < kOpeningSustainRows; ++offset) {
            sustained_reach =
                std::min(sustained_reach, Reach(observations[split + offset], use_left));
        }
        best = std::max(best,
                        GrowthRatio(Reach(observations[split - 1U], use_left),
                                    sustained_reach));
    }
    return best;
}

/// 评估十字路口的双侧开口情况
/// 通过分析最宽白色区间的左右伸达距离变化趋势判断开口
BoundaryOpeningFacts AssessCrossOpenings(const std::vector<BEVSimpleRowScan>& rows,
                                         const port::RuntimeParameters& params) {
    std::vector<CrossBoundaryObservation> observations;
    for (const BEVSimpleRowScan& row : rows) {
        if (!row.valid || row.sampleable_count < kCrossMinSampleablePerRow) {
            continue;
        }
        const BEVSimpleWhiteInterval* interval = WidestInterval(row);
        if (interval == nullptr) {
            continue;
        }
        CrossBoundaryObservation observation{};
        observation.forward_m = row.forward_m;
        observation.left_reach_m = std::max(0.0F, -interval->left_m);
        observation.right_reach_m = std::max(0.0F, interval->right_m);
        observations.push_back(observation);
    }
    std::sort(observations.begin(),
              observations.end(),
              [](const CrossBoundaryObservation& lhs, const CrossBoundaryObservation& rhs) {
                  return lhs.forward_m < rhs.forward_m;
              });
    if (observations.size() < kCrossMinContiguousWideRows) {
        return {};
    }

    (void)params;
    const float opening_ratio_min =
        std::max(kRatioDenominatorFloor, kCrossOpeningExpansionRatioMin);

    BoundaryOpeningFacts facts{};
    facts.left_open = SustainedGrowthRatio(observations, true) >= opening_ratio_min;
    facts.right_open = SustainedGrowthRatio(observations, false) >= opening_ratio_min;
    return facts;
}

/// 检查参考路径是否有有效的前导视觉参考（无间隙、无无限值、首个采样点存在）
bool HasLeadingVisualReference(const port::BEVReferencePath& reference) {
    if (reference.mode != port::ReferenceMode::kIntervalCenter ||
        !reference.sampled_path[0].present) {
        return false;
    }
    bool gap_seen = false;
    for (const port::BEVPathSample& sample : reference.sampled_path) {
        if (!sample.present) {
            gap_seen = true;
            continue;
        }
        if (gap_seen ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            return false;
        }
    }
    return true;
}

}  // namespace

/// DetectCrossExitEvidence 实现
/// 检测十字出口元素证据：
/// 1. 遍历所有行，识别满足条件的宽行（宽度/白色占比/双侧平衡度）
/// 2. 将连续宽行分组为运行累加器
/// 3. 检查双侧开口情况
/// 4. 计算置信度，判断是否构成有效十字出口证据
port::CrossExitElementEvidence DetectCrossExitEvidence(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params) {
    port::CrossExitElementEvidence evidence{};
    evidence.reason = "no_sparse_rows";
    if (rows.empty()) {
        return evidence;
    }

    bool saw_supported_row = false;
    bool saw_wide_row = false;
    CrossRunAccumulator current{};
    CrossRunAccumulator best{};
    const float expected_sampleable_width =
        std::max(0.0F, 2.0F * params.bev_geometry.search_lateral_limit_m);
    const BoundaryOpeningFacts opening = AssessCrossOpenings(rows, params);
    const float white_ratio_min =
        std::clamp(params.bev_element.cross_wide_row_white_ratio_min, 0.0F, 1.0F);

    const auto finish_run = [&current, &best]() {
        if (current.row_count > 0U && BetterRun(current, best)) {
            best = current;
        }
        current = {};
    };

    for (const BEVSimpleRowScan& row : rows) {
        evidence.sampleable_count += row.sampleable_count;
        evidence.supporting_white_count += row.white_count;
        evidence.unknown_count += row.unknown_count;

        const bool supported = row.valid && row.sampleable_count >= kCrossMinSampleablePerRow;
        saw_supported_row = saw_supported_row || supported;
        if (!supported) {
            finish_run();
            continue;
        }

        const float unknown_ratio =
            static_cast<float>(row.unknown_count) /
            std::max(1.0F, static_cast<float>(row.sampleable_count));
        const float white_ratio =
            static_cast<float>(row.white_count) /
            std::max(1.0F, static_cast<float>(row.sampleable_count));
        const BEVSimpleWhiteInterval* interval = WidestInterval(row);
        if (interval == nullptr || unknown_ratio > kCrossUnknownRatioMax) {
            finish_run();
            continue;
        }
        if (white_ratio < white_ratio_min) {
            finish_run();
            continue;
        }

        const float row_sampleable_width =
            row.sampleable_width_m > 0.0F ? row.sampleable_width_m : expected_sampleable_width;
        const float width_threshold =
            std::max(kCrossMinWideWidthM, row_sampleable_width * kCrossMinSampleableWidthRatio);
        if (interval->width_m < width_threshold) {
            finish_run();
            continue;
        }

        const float left_reach_m = std::max(0.0F, -interval->left_m);
        const float right_reach_m = std::max(0.0F, interval->right_m);
        const float bilateral_balance =
            std::min(left_reach_m, right_reach_m) /
            std::max(1.0e-4F, std::max(left_reach_m, right_reach_m));
        if (left_reach_m < kCrossMinBilateralReachM ||
            right_reach_m < kCrossMinBilateralReachM ||
            bilateral_balance < kCrossMinBilateralBalance) {
            finish_run();
            continue;
        }

        saw_wide_row = true;
        const float width_score = Clamp01(interval->width_m / std::max(1.0e-4F, width_threshold));
        const float white_score =
            Clamp01(white_ratio / std::max(kRatioDenominatorFloor, white_ratio_min));
        const float unknown_score = Clamp01(1.0F - unknown_ratio / kCrossUnknownRatioMax);
        const float balance_score = Clamp01(bilateral_balance / kCrossMinBilateralBalance);
        const float score = Clamp01(0.40F * width_score + 0.30F * white_score +
                                    0.15F * unknown_score + 0.15F * balance_score);
        AddWideRow(current, row, *interval, score);
    }
    finish_run();

    if (!saw_supported_row) {
        evidence.reason = "insufficient_sampleable_support";
        return evidence;
    }
    if (!saw_wide_row || best.row_count < kCrossMinContiguousWideRows) {
        evidence.reason = "wide_white_rows_absent";
        return evidence;
    }
    if (!opening.left_open || !opening.right_open) {
        evidence.reason = "wide_white_rows_absent";
        return evidence;
    }

    evidence.confidence = best.score_sum / static_cast<float>(best.row_count);
    evidence.forward_min_m = best.forward_min_m;
    evidence.forward_max_m = best.forward_max_m;
    evidence.lateral_min_m = best.lateral_min_m;
    evidence.lateral_max_m = best.lateral_max_m;
    evidence.sampleable_count = best.sampleable_count;
    evidence.supporting_white_count = best.supporting_white_count;
    evidence.unknown_count = best.unknown_count;
    if (evidence.confidence < kCrossPresentConfidenceMin) {
        evidence.reason = "low_confidence";
        return evidence;
    }

    evidence.present = true;
    evidence.reason = "present";
    return evidence;
}

/// BuildCrossExitVisualReferenceCandidate 实现
/// 从十字出口证据和车道线候选构建视觉参考候选
/// 直接继承车道线候选的参考路径（十字出口不改变路径方向）
port::VisualReferenceCandidate BuildCrossExitVisualReferenceCandidate(
    const port::CrossExitElementEvidence& evidence,
    const port::VisualReferenceCandidate& line_candidate,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary) {
    port::VisualReferenceCandidate candidate{};
    summary = {};
    summary.takeover_enabled = params.bev_element.cross_exit_takeover_enabled;
    if (!evidence.present) {
        summary.reason = evidence.reason.empty() ? "evidence_absent" : evidence.reason;
        return candidate;
    }
    if (!line_candidate.present || !HasLeadingVisualReference(line_candidate.reference_path)) {
        summary.reason = "line_candidate_absent";
        return candidate;
    }

    candidate.present = true;
    candidate.kind = port::VisualReferenceCandidateKind::kCrossExit;
    candidate.reference_path = line_candidate.reference_path;
    candidate.confidence = evidence.confidence;
    candidate.source = "cross_exit";
    candidate.reason = "cross_exit_evidence_candidate";

    summary.built = true;
    summary.included_in_arbitration = summary.takeover_enabled;
    summary.reason = summary.included_in_arbitration ? "included_in_arbitration"
                                                     : "takeover_disabled";
    return candidate;
}

}  // namespace ls2k::vision
