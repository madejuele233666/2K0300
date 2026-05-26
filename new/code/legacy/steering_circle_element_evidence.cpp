#include "legacy/steering_circle_element_evidence.hpp"

#include "legacy/steering_bev_element_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

#include "port/perf_counter.hpp"

namespace ls2k::legacy {
namespace {

/// 比率计算的分母最小值，防止除零
constexpr float kRatioDenominatorFloor = 1.0e-4F;
/// 开口判断所需的持续行数
constexpr std::size_t kOpeningSustainRows = 2U;

struct Phase1CircleCueParameters {
    int min_support_rows = 4;
    int min_sampleable_per_row = 16;
    float open_expansion_min_m = 0.05F;
    float opening_expansion_ratio_min = 0.10F;
    float opposite_straight_drift_max_m = 0.06F;
    float opposite_shrink_ratio_min = 0.10F;
    float present_confidence_min = 0.65F;
};

constexpr Phase1CircleCueParameters kPhase1CircleCueParams{};

/// 行观测数据，记录栅格中一行的白色连通区间和各类像素统计
struct RowObservation {
    int y = 0;                   ///< 栅格行索引
    int first_x = -1;            ///< 最佳白色区间的起始列
    int last_x = -1;             ///< 最佳白色区间的结束列
    float forward_m = 0.0F;      ///< 该行对应的前向距离（米）
    float left_m = 0.0F;         ///< 区间左边界横向坐标（米）
    float right_m = 0.0F;        ///< 区间右边界横向坐标（米）
    std::size_t sampleable_count = 0; ///< 可采样的栅格单元数
    std::size_t white_count = 0;      ///< 白色单元计数
    std::size_t black_count = 0;      ///< 黑色单元计数
    std::size_t unknown_count = 0;    ///< 不确定单元计数
};

/// 两侧开口/直线/收缩的综合评估结果
struct SideAssessment {
    bool left_open = false;      ///< 左侧是否开口
    bool right_open = false;     ///< 右侧是否开口
    bool left_straight = false;  ///< 左侧边界是否近似直线
    bool right_straight = false; ///< 右侧边界是否近似直线
    bool left_shrink = false;    ///< 左侧是否收缩
    bool right_shrink = false;   ///< 右侧是否收缩
    float left_confidence = 0.0F;  ///< 左侧开口置信度
    float right_confidence = 0.0F; ///< 右侧开口置信度
};

/// 边界线拟合结果（用于判断一侧边界是否近似直线或收缩）
struct BoundaryLineFit {
    bool straight = false;     ///< 是否近似直线
    bool shrink = false;       ///< 是否收缩（向内侧收窄）
    float confidence = 0.0F;   ///< 拟合置信度
};

/// 边界变化证据（开口的增长比率和绝对变化量）
struct BoundaryChangeEvidence {
    float ratio = 0.0F;    ///< 增长率（相对变化）
    float delta_m = 0.0F;   ///< 绝对变化量（米）
};

/// 将值钳制到[0, 1]范围
float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

/// 创建仅包含"evidence_only"原因的候选摘要
port::VisualElementCandidateSummary EvidenceOnlyCandidate() {
    port::VisualElementCandidateSummary candidate{};
    candidate.reason = "evidence_only";
    return candidate;
}

/// 创建证据记录，设置ID和原因，候选摘要设为evidence_only
port::VisualElementEvidenceRecord MakeRecord(const char* id, const char* reason) {
    port::VisualElementEvidenceRecord record{};
    record.id = id;
    record.reason = reason;
    record.candidate = EvidenceOnlyCandidate();
    return record;
}

/// 将一行的统计值累加到证据记录的support字段中
void AddSupport(port::VisualElementEvidenceRecord& record, const RowObservation& row) {
    record.support.sampleable_count += row.sampleable_count;
    record.support.supporting_white_count += row.white_count;
    record.support.supporting_black_count += row.black_count;
    record.support.unknown_count += row.unknown_count;
}

/// 将行观测的边界扩展到证据记录的bounds字段
/// @param first 是否为第一行（若是则直接赋值，否则取并集）
void AddBounds(port::VisualElementEvidenceRecord& record,
               const RowObservation& row,
               bool first) {
    if (first) {
        record.bounds.forward_min_m = row.forward_m;
        record.bounds.forward_max_m = row.forward_m;
        record.bounds.lateral_min_m = row.left_m;
        record.bounds.lateral_max_m = row.right_m;
        return;
    }
    record.bounds.forward_min_m = std::min(record.bounds.forward_min_m, row.forward_m);
    record.bounds.forward_max_m = std::max(record.bounds.forward_max_m, row.forward_m);
    record.bounds.lateral_min_m = std::min(record.bounds.lateral_min_m, row.left_m);
    record.bounds.lateral_max_m = std::max(record.bounds.lateral_max_m, row.right_m);
}

/// 选择一行中最宽的白色区间
const BEVSimpleWhiteInterval* WidestInterval(const BEVSimpleRowScan& scan) {
    const BEVSimpleWhiteInterval* best = nullptr;
    for (const BEVSimpleWhiteInterval& interval : scan.intervals) {
        if (best == nullptr || interval.width_m > best->width_m) {
            best = &interval;
        }
    }
    return best;
}

/// 从 sparse row fact 构建行观测数据
/// 找出该行中最宽白色区间，并记录其左右边界和各类统计
/// @return 是否成功找到有效的白色区间
bool BuildRowObservation(const BEVSimpleRowScan& scan,
                         int min_sampleable_per_row,
                         RowObservation& row) {
    if (!scan.valid ||
        scan.sampleable_count < static_cast<std::size_t>(std::max(1, min_sampleable_per_row))) {
        return false;
    }
    const BEVSimpleWhiteInterval* interval = WidestInterval(scan);
    if (interval == nullptr || interval->right_m < interval->left_m) {
        return false;
    }
    row.y = scan.row_px;
    row.first_x = interval->left_px;
    row.last_x = interval->right_px;
    row.forward_m = scan.forward_m;
    row.left_m = interval->left_m;
    row.right_m = interval->right_m;
    row.sampleable_count = scan.sampleable_count;
    row.white_count = scan.white_count;
    row.black_count = scan.black_count;
    row.unknown_count = scan.unknown_count;
    return true;
}

/// 收集 sparse rows 中所有有效的行观测数据，按前向距离排序
/// 同时累加左右两侧的证据统计和边界
std::vector<RowObservation> CollectRows(const std::vector<BEVSimpleRowScan>& sparse_rows,
                                        const Phase1CircleCueParameters& params,
                                        port::VisualElementEvidenceRecord& left,
                                        port::VisualElementEvidenceRecord& right) {
    std::vector<RowObservation> rows;
    for (const BEVSimpleRowScan& sparse_row : sparse_rows) {
        RowObservation row{};
        if (!BuildRowObservation(sparse_row, params.min_sampleable_per_row, row)) {
            continue;
        }
        AddSupport(left, row);
        AddSupport(right, row);
        AddBounds(left, row, rows.empty());
        AddBounds(right, row, rows.empty());
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const RowObservation& lhs, const RowObservation& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    return rows;
}

std::vector<BEVSimpleRowScan> BuildSparseRowsFromRasterForCompatibility(
    const BEVElementRasterFrame& raster) {
    std::vector<BEVSimpleRowScan> rows;
    if (!raster.enabled || !raster.valid || raster.width <= 1 || raster.height <= 1) {
        return rows;
    }
    rows.reserve(static_cast<std::size_t>(raster.height));
    for (int y = 0; y < raster.height; ++y) {
        BEVSimpleRowScan row{};
        row.valid = true;
        row.row_px = y;
        row.forward_m = raster.CellToMetric(raster.width / 2, y).forward_m;
        bool have_sampleable = false;
        int current_first = -1;
        int current_last = -1;
        const auto finish_interval = [&]() {
            if (current_first < 0 || current_last < current_first) {
                return;
            }
            const float left_m = raster.CellToMetric(current_first, y).lateral_m;
            const float right_m = raster.CellToMetric(current_last, y).lateral_m;
            BEVSimpleWhiteInterval interval{};
            interval.forward_m = row.forward_m;
            interval.left_m = std::min(left_m, right_m);
            interval.right_m = std::max(left_m, right_m);
            interval.center_m = 0.5F * (interval.left_m + interval.right_m);
            interval.width_m = interval.right_m - interval.left_m;
            interval.left_px = current_first;
            interval.right_px = current_last;
            row.intervals.push_back(interval);
            current_first = -1;
            current_last = -1;
        };
        for (int x = 0; x < raster.width; ++x) {
            const std::size_t index = raster.Index(x, y);
            const bool sampleable =
                index < raster.projection_states.size() &&
                index < raster.classes.size() &&
                raster.projection_states[index] ==
                    port::BEVElementRasterProjectionState::kSampleable;
            if (!sampleable) {
                finish_interval();
                ++row.unavailable_count;
                continue;
            }
            const float lateral = raster.CellToMetric(x, y).lateral_m;
            if (!have_sampleable) {
                row.sampleable_left_m = lateral;
                row.sampleable_right_m = lateral;
                have_sampleable = true;
            } else {
                row.sampleable_left_m = std::min(row.sampleable_left_m, lateral);
                row.sampleable_right_m = std::max(row.sampleable_right_m, lateral);
            }
            ++row.sampleable_count;
            switch (raster.classes[index]) {
                case port::BEVElementRasterCellClass::kWhite:
                    ++row.white_count;
                    if (current_first < 0) {
                        current_first = x;
                    }
                    current_last = x;
                    break;
                case port::BEVElementRasterCellClass::kBlack:
                    finish_interval();
                    ++row.black_count;
                    break;
                case port::BEVElementRasterCellClass::kUnknown:
                    finish_interval();
                    ++row.unknown_count;
                    break;
                case port::BEVElementRasterCellClass::kInvalid:
                    finish_interval();
                    break;
            }
        }
        finish_interval();
        row.sampleable_width_m = have_sampleable
                                     ? row.sampleable_right_m - row.sampleable_left_m
                                     : 0.0F;
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const BEVSimpleRowScan& lhs,
                                           const BEVSimpleRowScan& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    return rows;
}

/// 计算一行在指定侧的伸达距离（离车辆中心的横向距离）
float Reach(const RowObservation& row, bool use_left) {
    return use_left ? std::max(0.0F, -row.left_m) : std::max(0.0F, row.right_m);
}

/// 计算近端到远端的增长率（相对变化）
float GrowthRatio(float near_reach, float far_reach) {
    return (far_reach - near_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

/// 计算近端到远端的收缩率（相对变化）
float ShrinkRatio(float near_reach, float far_reach) {
    return (near_reach - far_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

/// 检测持续扩张的边界变化证据
/// 遍历所有可能的分割点，计算后续持续行相对于锚点的增长
/// @param rows 行观测数组（已按前向距离排序）
/// @param use_left true检测左侧，false检测右侧
/// @return 最佳的增长变化证据
BoundaryChangeEvidence SustainedGrowthEvidence(const std::vector<RowObservation>& rows,
                                               bool use_left) {
    BoundaryChangeEvidence best{};
    if (rows.size() <= kOpeningSustainRows) {
        return best;
    }
    for (std::size_t split = 1U; split + kOpeningSustainRows <= rows.size(); ++split) {
        const std::size_t anchor_begin =
            split > kOpeningSustainRows ? split - kOpeningSustainRows : 0U;
        float anchor_reach = Reach(rows[anchor_begin], use_left);
        for (std::size_t index = anchor_begin + 1U; index < split; ++index) {
            anchor_reach = std::max(anchor_reach, Reach(rows[index], use_left));
        }
        float sustained_reach = Reach(rows[split], use_left);
        for (std::size_t offset = 1U; offset < kOpeningSustainRows; ++offset) {
            sustained_reach =
                std::min(sustained_reach, Reach(rows[split + offset], use_left));
        }
        const BoundaryChangeEvidence candidate{
            GrowthRatio(anchor_reach, sustained_reach),
            sustained_reach - anchor_reach};
        if (candidate.ratio > best.ratio) {
            best = candidate;
        }
    }
    return best;
}

/// 获取行观测边界值（左侧取left_m，右侧取right_m）
float BoundaryValue(const RowObservation& row, bool use_left) {
    return use_left ? row.left_m : row.right_m;
}

/// 从边界值计算伸达距离（取绝对值，确保非负）
float ReachFromBoundaryValue(float boundary_m, bool use_left) {
    return use_left ? std::max(0.0F, -boundary_m) : std::max(0.0F, boundary_m);
}

/// 对边界点进行线性拟合，判断边界是直线还是收缩
/// 使用最小二乘法拟合直线，通过RMSE判断直线度，通过斜率判断收缩
/// @return 拟合结果（是否为直线/收缩及置信度）
BoundaryLineFit FitBoundaryLine(const std::vector<RowObservation>& rows,
                                bool use_left,
                                const Phase1CircleCueParameters& params) {
    BoundaryLineFit fit{};
    if (rows.size() < static_cast<std::size_t>(std::max(2, params.min_support_rows))) {
        return fit;
    }

    float sum_x = 0.0F;
    float sum_y = 0.0F;
    for (const RowObservation& row : rows) {
        sum_x += row.forward_m;
        sum_y += BoundaryValue(row, use_left);
    }
    const float count = static_cast<float>(rows.size());
    const float mean_x = sum_x / count;
    const float mean_y = sum_y / count;

    float var_x = 0.0F;
    float cov_xy = 0.0F;
    for (const RowObservation& row : rows) {
        const float dx = row.forward_m - mean_x;
        var_x += dx * dx;
        cov_xy += dx * (BoundaryValue(row, use_left) - mean_y);
    }
    if (var_x <= kRatioDenominatorFloor) {
        return fit;
    }

    const float slope = cov_xy / var_x;
    const float intercept = mean_y - slope * mean_x;
    float min_forward = rows.front().forward_m;
    float max_forward = rows.front().forward_m;
    std::vector<float> squared_errors;
    squared_errors.reserve(rows.size());
    for (const RowObservation& row : rows) {
        min_forward = std::min(min_forward, row.forward_m);
        max_forward = std::max(max_forward, row.forward_m);
        const float expected = slope * row.forward_m + intercept;
        const float error = BoundaryValue(row, use_left) - expected;
        squared_errors.push_back(error * error);
    }
    std::sort(squared_errors.begin(), squared_errors.end());
    const std::size_t retained_count = std::max<std::size_t>(
        1U,
        (squared_errors.size() * 9U + 9U) / 10U);
    const float retained_squared_error_sum =
        std::accumulate(squared_errors.begin(),
                        squared_errors.begin() + static_cast<std::ptrdiff_t>(retained_count),
                        0.0F);

    const float rmse = std::sqrt(retained_squared_error_sum /
                                 static_cast<float>(retained_count));
    const float drift_max = std::max(1.0e-4F, params.opposite_straight_drift_max_m);
    const float near_reach = ReachFromBoundaryValue(slope * min_forward + intercept, use_left);
    const float far_reach = ReachFromBoundaryValue(slope * max_forward + intercept, use_left);
    const float fitted_reach_drift_m = std::abs(far_reach - near_reach);
    const bool fitted_shrink =
        near_reach > far_reach &&
        ShrinkRatio(near_reach, far_reach) >=
            std::max(kRatioDenominatorFloor, params.opposite_shrink_ratio_min);
    const float shrink_delta_min_m =
        std::max(kRatioDenominatorFloor, params.open_expansion_min_m);
    const bool fitted_shrink_exceeded =
        fitted_shrink && fitted_reach_drift_m > shrink_delta_min_m;
    fit.shrink = fitted_shrink_exceeded;
    fit.straight = rmse <= drift_max && !fit.shrink;
    const float residual_score = Clamp01(1.0F - rmse / drift_max);
    fit.confidence = residual_score;
    return fit;
}

/// 综合评估两侧边界的开口、直线度和收缩情况
/// 计算左右两侧的开口置信度（加权组合开口评分、对侧直线度和行支持度）
SideAssessment AssessSides(const std::vector<RowObservation>& rows,
                           const Phase1CircleCueParameters& params) {
    SideAssessment assessment{};
    const BoundaryChangeEvidence left_growth = SustainedGrowthEvidence(rows, true);
    const BoundaryChangeEvidence right_growth = SustainedGrowthEvidence(rows, false);
    const float opening_ratio_min =
        std::max(kRatioDenominatorFloor, params.opening_expansion_ratio_min);
    const float opening_delta_min =
        std::max(kRatioDenominatorFloor, params.open_expansion_min_m);
    const BoundaryLineFit left_fit = FitBoundaryLine(rows, true, params);
    const BoundaryLineFit right_fit = FitBoundaryLine(rows, false, params);

    assessment.left_open =
        left_growth.ratio >= opening_ratio_min && left_growth.delta_m >= opening_delta_min;
    assessment.right_open =
        right_growth.ratio >= opening_ratio_min && right_growth.delta_m >= opening_delta_min;
    assessment.left_straight = left_fit.straight;
    assessment.right_straight = right_fit.straight;
    assessment.left_shrink = left_fit.shrink;
    assessment.right_shrink = right_fit.shrink;

    const float left_open_score = Clamp01(left_growth.ratio / opening_ratio_min);
    const float right_open_score = Clamp01(right_growth.ratio / opening_ratio_min);
    const float support_score =
        Clamp01(static_cast<float>(rows.size()) /
                static_cast<float>(std::max(1, params.min_support_rows)));
    assessment.left_confidence =
        Clamp01(0.80F * left_open_score +
                0.10F * right_fit.confidence +
                0.10F * support_score);
    assessment.right_confidence =
        Clamp01(0.80F * right_open_score +
                0.10F * left_fit.confidence +
                0.10F * support_score);
    return assessment;
}

/// 将证据记录标记为存在，设置置信度和原因
void FinishPresent(port::VisualElementEvidenceRecord& record, float confidence) {
    record.present = true;
    record.confidence = confidence;
    record.reason = "present";
}

}  // namespace

/// DetectCircleElementEvidence 实现
/// 检测环形交叉口元素的入口证据
/// 1. 收集行观测
/// 2. 评估两侧开口/直线度/收缩情况
/// 3. 根据评估结果判断是否为环形入口
CircleElementEvidenceResult DetectCircleElementEvidence(
    const std::vector<BEVSimpleRowScan>& sparse_rows,
    const port::RuntimeParameters& params) {
    (void)params;
    LS2K_PERF_SCOPE(port::PerfStage::kCirclePhase1Rows);
    CircleElementEvidenceResult result{};
    result.left_raw = MakeRecord("circle_left_raw", "not_evaluated");
    result.right_raw = MakeRecord("circle_right_raw", "not_evaluated");

    if (sparse_rows.empty()) {
        result.left_raw.reason = "no_sparse_rows";
        result.right_raw.reason = "no_sparse_rows";
        return result;
    }

    const std::vector<RowObservation> rows =
        CollectRows(sparse_rows, kPhase1CircleCueParams, result.left_raw, result.right_raw);
    if (rows.size() < static_cast<std::size_t>(
                          std::max(1, kPhase1CircleCueParams.min_support_rows))) {
        result.left_raw.reason = "insufficient_sampleable_support";
        result.right_raw.reason = "insufficient_sampleable_support";
        return result;
    }

    const SideAssessment assessment = AssessSides(rows, kPhase1CircleCueParams);
    const float present_min = kPhase1CircleCueParams.present_confidence_min;
    if (assessment.left_open && assessment.right_open) {
        result.left_raw.reason = "both_sides_open";
        result.right_raw.reason = "both_sides_open";
        return result;
    }
    if (!assessment.left_open && !assessment.right_open) {
        result.left_raw.reason = "no_opening";
        result.right_raw.reason = "no_opening";
        return result;
    }

    if (assessment.left_open) {
        if (!assessment.right_straight) {
            result.left_raw.reason =
                assessment.right_shrink ? "bend" : "opposite_straight_drift_exceeded";
        } else if (assessment.left_confidence < present_min) {
            result.left_raw.confidence = assessment.left_confidence;
            result.left_raw.reason = "low_confidence";
        } else {
            FinishPresent(result.left_raw, assessment.left_confidence);
        }
        result.right_raw.reason = "no_opening";
        return result;
    }

    if (!assessment.left_straight) {
        result.right_raw.reason =
            assessment.left_shrink ? "bend" : "opposite_straight_drift_exceeded";
    } else if (assessment.right_confidence < present_min) {
        result.right_raw.confidence = assessment.right_confidence;
        result.right_raw.reason = "low_confidence";
    } else {
        FinishPresent(result.right_raw, assessment.right_confidence);
    }
    result.left_raw.reason = "no_opening";
    return result;
}

CircleElementEvidenceResult DetectCircleElementEvidence(
    const BEVElementRasterFrame* raster,
    const port::RuntimeParameters& params) {
    if (raster == nullptr) {
        CircleElementEvidenceResult result{};
        result.left_raw = MakeRecord("circle_left_raw", "raster_unavailable");
        result.right_raw = MakeRecord("circle_right_raw", "raster_unavailable");
        return result;
    }
    const std::vector<BEVSimpleRowScan> sparse_rows =
        BuildSparseRowsFromRasterForCompatibility(*raster);
    return DetectCircleElementEvidence(sparse_rows, params);
}

}  // namespace ls2k::legacy
