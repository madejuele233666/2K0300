#include "legacy/steering_circle_element_evidence.hpp"

#include "legacy/steering_bev_element_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace ls2k::legacy {
namespace {

/// 比率计算的分母最小值，防止除零
constexpr float kRatioDenominatorFloor = 1.0e-4F;
/// 开口判断所需的持续行数
constexpr std::size_t kOpeningSustainRows = 2U;

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

/// 前沿支撑点（边界前沿点及其对应的中心线点）
struct FrontierSupportPoint {
    port::BEVPoint frontier{};   ///< 边界前沿点（位于白色/黑色交界处）
    port::BEVPoint centerline{}; ///< 对应的中心线点（沿道路中线偏移）
};

/// 前沿链（一系列在空间上连续的前沿支撑点）
struct FrontierChain {
    std::vector<FrontierSupportPoint> points{}; ///< 链中的前沿支撑点序列
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
                                        const port::BEVElementParameters& params,
                                        port::VisualElementEvidenceRecord& left,
                                        port::VisualElementEvidenceRecord& right) {
    std::vector<RowObservation> rows;
    for (const BEVSimpleRowScan& sparse_row : sparse_rows) {
        RowObservation row{};
        if (!BuildRowObservation(sparse_row, params.circle_min_sampleable_per_row, row)) {
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
                                const port::BEVElementParameters& params) {
    BoundaryLineFit fit{};
    if (rows.size() < static_cast<std::size_t>(std::max(2, params.circle_min_support_rows))) {
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
    const float drift_max = std::max(1.0e-4F, params.circle_opposite_straight_drift_max_m);
    const float near_reach = ReachFromBoundaryValue(slope * min_forward + intercept, use_left);
    const float far_reach = ReachFromBoundaryValue(slope * max_forward + intercept, use_left);
    const float fitted_reach_drift_m = std::abs(far_reach - near_reach);
    const bool fitted_shrink =
        near_reach > far_reach &&
        ShrinkRatio(near_reach, far_reach) >=
            std::max(kRatioDenominatorFloor, params.circle_opposite_shrink_ratio_min);
    const float shrink_delta_min_m =
        std::max(kRatioDenominatorFloor, params.circle_open_expansion_min_m);
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
                           const port::BEVElementParameters& params) {
    SideAssessment assessment{};
    const BoundaryChangeEvidence left_growth = SustainedGrowthEvidence(rows, true);
    const BoundaryChangeEvidence right_growth = SustainedGrowthEvidence(rows, false);
    const float opening_ratio_min =
        std::max(kRatioDenominatorFloor, params.circle_opening_expansion_ratio_min);
    const float opening_delta_min =
        std::max(kRatioDenominatorFloor, params.circle_open_expansion_min_m);
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
                static_cast<float>(std::max(1, params.circle_min_support_rows)));
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

/// 检查两行的白色区间是否有重叠（+1容差）
bool IntervalsOverlap(const RowObservation& lhs, const RowObservation& rhs) {
    return lhs.first_x <= rhs.last_x + 1 && rhs.first_x <= lhs.last_x + 1;
}

/// 提取在栅格空间中连续连接的行（白色区间有重叠）
/// 从第一行开始，直到遇到不重叠的行则停止
std::vector<RowObservation> NearConnectedRows(const std::vector<RowObservation>& rows) {
    std::vector<RowObservation> connected;
    if (rows.empty()) {
        return connected;
    }
    connected.push_back(rows.front());
    RowObservation previous = rows.front();
    for (std::size_t index = 1U; index < rows.size(); ++index) {
        const RowObservation& row = rows[index];
        if (!IntervalsOverlap(previous, row)) {
            break;
        }
        connected.push_back(row);
        previous = row;
    }
    return connected;
}

/// 计算一组值的中位数
float Median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0F;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    if ((values.size() % 2U) == 1U) {
        return values[middle];
    }
    return 0.5F * (values[middle - 1U] + values[middle]);
}

float NominalForwardStep(const port::BEVGeometryParameters& geometry) {
    float previous = geometry.forward_samples_m.front();
    for (std::size_t index = 1U; index < geometry.forward_samples_m.size(); ++index) {
        const float current = geometry.forward_samples_m[index];
        if (current > previous) {
            return current - previous;
        }
        previous = current;
    }
    return 0.02F;
}

float MedianForwardStep(const std::vector<RowObservation>& rows,
                        const port::BEVGeometryParameters& geometry) {
    std::vector<float> deltas;
    deltas.reserve(rows.size());
    for (std::size_t index = 1U; index < rows.size(); ++index) {
        const float delta = rows[index].forward_m - rows[index - 1U].forward_m;
        if (std::isfinite(delta) && delta > 1.0e-4F) {
            deltas.push_back(delta);
        }
    }
    if (!deltas.empty()) {
        return Median(deltas);
    }
    return NominalForwardStep(geometry);
}

float AllowedSupportGap(float max_interpolation_gap_m, float nominal_step_m) {
    return std::max(1.0e-4F, max_interpolation_gap_m) +
           std::max(0.0F, nominal_step_m);
}

float AllowedJoinJump(float max_join_jump_m, const port::BEVGeometryParameters& geometry) {
    return std::max(0.0F, max_join_jump_m) +
           std::max(0.0F, geometry.lateral_step_m);
}

bool IsSampleableClass(const BEVMetricClassSampler& sampler,
                       const port::BEVPoint& point,
                       port::BEVElementRasterCellClass expected) {
    const BEVMetricClassSample sample = sampler.Sample(point);
    return sample.projection_state == port::BEVElementRasterProjectionState::kSampleable &&
           sample.class_kind == expected;
}

/// 检查指定 metric point 的后方或侧后方是否存在真实 sampleable black
bool HasRearSideBlack(const BEVMetricClassSampler& sampler,
                      const port::BEVPoint& point,
                      bool left_side,
                      float forward_step_m,
                      float lateral_step_m) {
    port::BEVPoint rear = point;
    rear.forward_m -= forward_step_m;
    if (rear.forward_m > 0.0F &&
        IsSampleableClass(sampler, rear, port::BEVElementRasterCellClass::kBlack)) {
        return true;
    }
    port::BEVPoint side_rear = rear;
    side_rear.lateral_m += left_side ? -lateral_step_m : lateral_step_m;
    return side_rear.forward_m > 0.0F &&
           IsSampleableClass(sampler, side_rear, port::BEVElementRasterCellClass::kBlack);
}

/// 在行观测中查找 rear-side 黑白边线点
bool FindRearFrontierPoint(const BEVMetricClassSampler& sampler,
                           const RowObservation& row,
                           const port::BEVElementParameters& element_params,
                           const port::BEVGeometryParameters& geometry,
                           bool left_side,
                           float forward_step_m,
                           port::BEVPoint& point) {
    const float lateral_step =
        std::max(1.0e-4F, std::min(std::max(0.005F, geometry.lateral_step_m),
                                   std::max(0.005F, element_params.circle_entry_max_join_jump_m)));
    const float begin = left_side ? row.left_m : row.right_m;
    const float end = left_side ? row.right_m : row.left_m;
    const float signed_step = left_side ? lateral_step : -lateral_step;
    for (float lateral = begin;
         left_side ? lateral <= end + 1.0e-5F : lateral >= end - 1.0e-5F;
         lateral += signed_step) {
        const port::BEVPoint candidate{row.forward_m, lateral};
        if (!IsSampleableClass(sampler, candidate, port::BEVElementRasterCellClass::kWhite) ||
            !HasRearSideBlack(sampler, candidate, left_side, forward_step_m, lateral_step)) {
            continue;
        }
        point = candidate;
        return true;
    }
    return false;
}

bool RasterSampleableClass(const BEVElementRasterFrame& raster,
                           int x,
                           int y,
                           port::BEVElementRasterCellClass expected) {
    if (!raster.InBounds(x, y)) {
        return false;
    }
    const std::size_t index = raster.Index(x, y);
    return index < raster.projection_states.size() &&
           index < raster.classes.size() &&
           raster.projection_states[index] ==
               port::BEVElementRasterProjectionState::kSampleable &&
           raster.classes[index] == expected;
}

bool RasterInteriorSampleableClass(const BEVElementRasterFrame& raster,
                                   int x,
                                   int y,
                                   port::BEVElementRasterCellClass expected) {
    if (x <= 0 || y <= 0 || x >= raster.width - 1 || y >= raster.height - 1) {
        return false;
    }
    return RasterSampleableClass(raster, x, y, expected);
}

bool RasterHasRearSideBlack(const BEVElementRasterFrame& raster, int x, int y, bool left_side) {
    const int rear_y = y + 1;
    if (rear_y >= raster.height) {
        return false;
    }
    if (RasterInteriorSampleableClass(raster,
                                      x,
                                      rear_y,
                                      port::BEVElementRasterCellClass::kBlack)) {
        return true;
    }
    const int side_x = left_side ? x - 1 : x + 1;
    return RasterInteriorSampleableClass(raster,
                                         side_x,
                                         rear_y,
                                         port::BEVElementRasterCellClass::kBlack);
}

bool RasterFindRearFrontierPoint(const BEVElementRasterFrame& raster,
                                 const RowObservation& row,
                                 bool left_side,
                                 port::BEVPoint& point) {
    if (row.y <= 0 || row.y >= raster.height - 1) {
        return false;
    }
    const int step = left_side ? 1 : -1;
    const int begin = left_side ? row.first_x : row.last_x;
    const int end = left_side ? row.last_x : row.first_x;
    for (int x = begin; left_side ? x <= end : x >= end; x += step) {
        if (x <= 0 || x >= raster.width - 1) {
            continue;
        }
        if (!RasterSampleableClass(raster, x, row.y, port::BEVElementRasterCellClass::kWhite) ||
            !RasterHasRearSideBlack(raster, x, row.y, left_side)) {
            continue;
        }
        point = raster.CellToMetric(x, row.y);
        return true;
    }
    return false;
}

/// 向点列表中添加一个点（去重：若前向距离相同则替换最后一个点）
void AddPoint(std::vector<port::BEVPoint>& points, const port::BEVPoint& point) {
    if (!std::isfinite(point.forward_m) || !std::isfinite(point.lateral_m)) {
        return;
    }
    if (!points.empty() && std::fabs(points.back().forward_m - point.forward_m) <= 1.0e-5F) {
        points.back() = point;
        return;
    }
    points.push_back(point);
}

/// BuildEntryFacts 实现
/// 构建环形交叉口入口路径事实
/// 1. 从近端连接行中提取道路半宽
/// 2. 查找每行的前沿点（白色区域后紧邻黑色的边缘）
/// 3. 将前沿点链接成链，选择最佳链（方向正确、点数最多）
/// 4. 输出近端中心线点、前沿点和中心线点
CircleEntryPathFacts BuildEntryFacts(const BEVMetricClassSampler& sampler,
                                     const std::vector<RowObservation>& rows,
                                     const port::RuntimeParameters& params,
                                     bool left_side) {
    CircleEntryPathFacts facts{};
    if (!sampler.Valid()) {
        facts.reason = "roi_sampler_unavailable";
        return facts;
    }
    const int support_rows_min = std::max(1, params.bev_element.circle_min_support_rows);
    const std::vector<RowObservation> component = NearConnectedRows(rows);
    if (component.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }

    std::vector<float> near_widths;
    near_widths.reserve(static_cast<std::size_t>(support_rows_min));
    for (std::size_t index = 0U;
         index < component.size() && near_widths.size() < static_cast<std::size_t>(support_rows_min);
         ++index) {
        const float width = component[index].right_m - component[index].left_m;
        if (std::isfinite(width) && width > 0.0F) {
            near_widths.push_back(width);
        }
    }
    if (near_widths.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }

    facts.road_half_width_m = 0.5F * Median(near_widths);
    if (!std::isfinite(facts.road_half_width_m) || facts.road_half_width_m <= 0.0F) {
        facts.reason = "road_half_width_unavailable";
        return facts;
    }

    const float forward_step_m = MedianForwardStep(component, params.bev_geometry);
    std::vector<FrontierSupportPoint> frontier_candidates;
    for (const RowObservation& row : component) {
        port::BEVPoint near_center{};
        near_center.forward_m = row.forward_m;
        near_center.lateral_m = 0.5F * (row.left_m + row.right_m);
        AddPoint(facts.near_centerline_points, near_center);

        port::BEVPoint frontier{};
        if (!FindRearFrontierPoint(sampler,
                                   row,
                                   params.bev_element,
                                   params.bev_geometry,
                                   left_side,
                                   forward_step_m,
                                   frontier)) {
            continue;
        }

        port::BEVPoint center = frontier;
        center.lateral_m += left_side ? facts.road_half_width_m : -facts.road_half_width_m;
        frontier_candidates.push_back({frontier, center});
    }

    const int frontier_min = std::max(1, params.bev_element.circle_entry_min_frontier_points);
    if (frontier_candidates.size() < static_cast<std::size_t>(frontier_min)) {
        facts.reason = "frontier_points_insufficient";
        return facts;
    }

    const float max_gap =
        AllowedSupportGap(params.bev_element.circle_entry_max_interpolation_gap_m,
                          forward_step_m);
    std::vector<FrontierChain> chains;
    FrontierChain current_chain{};
    for (const FrontierSupportPoint& candidate : frontier_candidates) {
        if (!current_chain.points.empty()) {
            const float gap =
                candidate.frontier.forward_m - current_chain.points.back().frontier.forward_m;
            if (gap > max_gap) {
                chains.push_back(current_chain);
                current_chain = {};
            }
        }
        current_chain.points.push_back(candidate);
    }
    if (!current_chain.points.empty()) {
        chains.push_back(current_chain);
    }

    const float lateral_min =
        std::max(0.0F, params.bev_element.circle_entry_direction_min_lateral_m);
    const FrontierChain* selected_chain = nullptr;
    std::size_t selected_count = 0U;
    float selected_abs_lateral_delta = 0.0F;
    bool has_supported_chain = false;
    bool has_forward_chain = false;
    float best_failed_forward_delta = 0.0F;
    float best_failed_lateral_delta = 0.0F;
    float best_failed_abs_lateral_delta = -1.0F;
    for (const FrontierChain& chain : chains) {
        if (chain.points.size() < static_cast<std::size_t>(frontier_min)) {
            continue;
        }
        has_supported_chain = true;
        const port::BEVPoint& first = chain.points.front().frontier;
        const port::BEVPoint& last = chain.points.back().frontier;
        const float forward_delta = last.forward_m - first.forward_m;
        const float lateral_delta = last.lateral_m - first.lateral_m;
        if (forward_delta <= 0.0F) {
            continue;
        }
        has_forward_chain = true;
        const float abs_lateral_delta = std::fabs(lateral_delta);
        if (abs_lateral_delta > best_failed_abs_lateral_delta) {
            best_failed_abs_lateral_delta = abs_lateral_delta;
            best_failed_forward_delta = forward_delta;
            best_failed_lateral_delta = lateral_delta;
        }
        const bool direction_ok =
            left_side ? lateral_delta <= -lateral_min : lateral_delta >= lateral_min;
        if (!direction_ok) {
            continue;
        }
        if (selected_chain == nullptr ||
            chain.points.size() > selected_count ||
            (chain.points.size() == selected_count &&
             abs_lateral_delta > selected_abs_lateral_delta)) {
            selected_chain = &chain;
            selected_count = chain.points.size();
            selected_abs_lateral_delta = abs_lateral_delta;
            facts.direction_delta_forward_m = forward_delta;
            facts.direction_delta_lateral_m = lateral_delta;
        }
    }
    if (!has_supported_chain) {
        facts.reason = "interpolation_gap_exceeded";
        return facts;
    }
    if (!has_forward_chain) {
        facts.reason = "frontier_direction_not_forward";
        return facts;
    }
    if (selected_chain == nullptr) {
        facts.direction_delta_forward_m = best_failed_forward_delta;
        facts.direction_delta_lateral_m = best_failed_lateral_delta;
        facts.reason = "frontier_direction_insufficient";
        return facts;
    }

    for (const FrontierSupportPoint& point : selected_chain->points) {
        AddPoint(facts.frontier_points, point.frontier);
        AddPoint(facts.centerline_points, point.centerline);
    }
    facts.present = true;
    facts.reason = "present";
    return facts;
}

CircleEntryPathFacts BuildEntryFactsFromRaster(const BEVElementRasterFrame& raster,
                                               const std::vector<RowObservation>& rows,
                                               const port::BEVElementParameters& params,
                                               bool left_side) {
    CircleEntryPathFacts facts{};
    const int support_rows_min = std::max(1, params.circle_min_support_rows);
    const std::vector<RowObservation> component = NearConnectedRows(rows);
    if (component.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }

    std::vector<float> near_widths;
    for (const RowObservation& row : component) {
        if (near_widths.size() >= static_cast<std::size_t>(support_rows_min)) {
            break;
        }
        const float width = row.right_m - row.left_m;
        if (std::isfinite(width) && width > 0.0F) {
            near_widths.push_back(width);
        }
    }
    if (near_widths.size() < static_cast<std::size_t>(support_rows_min)) {
        facts.reason = "near_component_insufficient_width_support";
        return facts;
    }
    facts.road_half_width_m = 0.5F * Median(near_widths);
    if (!std::isfinite(facts.road_half_width_m) || facts.road_half_width_m <= 0.0F) {
        facts.reason = "road_half_width_unavailable";
        return facts;
    }

    std::vector<FrontierSupportPoint> frontier_candidates;
    for (const RowObservation& row : component) {
        AddPoint(facts.near_centerline_points,
                 {row.forward_m, 0.5F * (row.left_m + row.right_m)});
        port::BEVPoint frontier{};
        if (!RasterFindRearFrontierPoint(raster, row, left_side, frontier)) {
            continue;
        }
        port::BEVPoint center = frontier;
        center.lateral_m += left_side ? facts.road_half_width_m : -facts.road_half_width_m;
        frontier_candidates.push_back({frontier, center});
    }

    const int frontier_min = std::max(1, params.circle_entry_min_frontier_points);
    if (frontier_candidates.size() < static_cast<std::size_t>(frontier_min)) {
        facts.reason = "frontier_points_insufficient";
        return facts;
    }
    const float max_gap =
        AllowedSupportGap(params.circle_entry_max_interpolation_gap_m,
                          MedianForwardStep(component, port::BEVGeometryParameters{}));
    std::vector<FrontierChain> chains;
    FrontierChain current_chain{};
    for (const FrontierSupportPoint& candidate : frontier_candidates) {
        if (!current_chain.points.empty()) {
            const float gap =
                candidate.frontier.forward_m - current_chain.points.back().frontier.forward_m;
            if (gap > max_gap) {
                chains.push_back(current_chain);
                current_chain = {};
            }
        }
        current_chain.points.push_back(candidate);
    }
    if (!current_chain.points.empty()) {
        chains.push_back(current_chain);
    }

    const float lateral_min = std::max(0.0F, params.circle_entry_direction_min_lateral_m);
    const FrontierChain* selected_chain = nullptr;
    for (const FrontierChain& chain : chains) {
        if (chain.points.size() < static_cast<std::size_t>(frontier_min)) {
            continue;
        }
        const port::BEVPoint& first = chain.points.front().frontier;
        const port::BEVPoint& last = chain.points.back().frontier;
        const float forward_delta = last.forward_m - first.forward_m;
        const float lateral_delta = last.lateral_m - first.lateral_m;
        if (forward_delta <= 0.0F) {
            continue;
        }
        const bool direction_ok =
            left_side ? lateral_delta <= -lateral_min : lateral_delta >= lateral_min;
        if (!direction_ok) {
            facts.direction_delta_forward_m = forward_delta;
            facts.direction_delta_lateral_m = lateral_delta;
            continue;
        }
        selected_chain = &chain;
        facts.direction_delta_forward_m = forward_delta;
        facts.direction_delta_lateral_m = lateral_delta;
        break;
    }
    if (selected_chain == nullptr) {
        facts.reason = "frontier_direction_insufficient";
        return facts;
    }
    for (const FrontierSupportPoint& point : selected_chain->points) {
        AddPoint(facts.frontier_points, point.frontier);
        AddPoint(facts.centerline_points, point.centerline);
    }
    facts.present = true;
    facts.reason = "present";
    return facts;
}

/// 将证据记录标记为存在，设置置信度和原因
void FinishPresent(port::VisualElementEvidenceRecord& record, float confidence) {
    record.present = true;
    record.confidence = confidence;
    record.reason = "present";
}

/// BuildCircleReferencePath 实现
/// 从入口路径事实构建BEV参考路径
/// 将近端中心线点和前沿中心线点拼接，通过线性插值生成标准参考路径
bool BuildCircleReferencePath(const CircleEntryPathFacts& entry,
                              const port::RuntimeParameters& params,
                              port::BEVReferencePath& path,
                              std::string& reason) {
    std::vector<port::BEVPoint> support;
    const float nominal_step = NominalForwardStep(params.bev_geometry);
    const float max_gap =
        AllowedSupportGap(params.bev_element.circle_entry_max_interpolation_gap_m, nominal_step);
    const float max_join =
        AllowedJoinJump(params.bev_element.circle_entry_max_join_jump_m, params.bev_geometry);
    const float first_frontier_forward =
        entry.centerline_points.empty() ? 0.0F : entry.centerline_points.front().forward_m;

    for (const port::BEVPoint& point : entry.near_centerline_points) {
        if (point.forward_m <= first_frontier_forward + 1.0e-5F) {
            AddPoint(support, point);
        }
    }
    if (!support.empty() && !entry.centerline_points.empty() &&
        std::fabs(support.back().lateral_m - entry.centerline_points.front().lateral_m) >
            max_join) {
        reason = "join_jump_exceeded";
        return false;
    }
    for (const port::BEVPoint& point : entry.centerline_points) {
        AddPoint(support, point);
    }
    if (support.size() < 2U) {
        reason = "path_support_insufficient";
        return false;
    }
    std::sort(support.begin(), support.end(), [](const port::BEVPoint& lhs,
                                                 const port::BEVPoint& rhs) {
        return lhs.forward_m < rhs.forward_m;
    });
    for (std::size_t index = 1U; index < support.size(); ++index) {
        const float gap = support[index].forward_m - support[index - 1U].forward_m;
        if (gap <= 1.0e-5F) {
            reason = "path_support_not_ordered";
            return false;
        }
        if (gap > max_gap) {
            reason = "interpolation_gap_exceeded";
            return false;
        }
    }

    path = {};
    path.mode = port::ReferenceMode::kIntervalCenter;
    std::size_t support_index = 0U;
    for (std::size_t sample_index = 0U;
         sample_index < port::kBevReferenceSampleCount;
         ++sample_index) {
        const float forward = params.bev_geometry.forward_samples_m[sample_index];
        if (forward < support.front().forward_m - max_gap) {
            break;
        }
        while (support_index + 1U < support.size() &&
               support[support_index + 1U].forward_m < forward) {
            ++support_index;
        }

        float lateral = 0.0F;
        if (forward <= support.front().forward_m) {
            lateral = support.front().lateral_m;
        } else if (support_index + 1U < support.size()) {
            const port::BEVPoint& before = support[support_index];
            const port::BEVPoint& after = support[support_index + 1U];
            const float gap = after.forward_m - before.forward_m;
            if (forward < before.forward_m || forward > after.forward_m || gap > max_gap) {
                break;
            }
            const float ratio = (forward - before.forward_m) / gap;
            lateral = before.lateral_m + ratio * (after.lateral_m - before.lateral_m);
        } else {
            break;
        }

        port::BEVPathSample& sample = path.sampled_path[sample_index];
        sample.present = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m = lateral;
        sample.confidence = 1.0F;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
    }

    if (!path.sampled_path[0].present) {
        reason = "missing_leading_reference_sample";
        path = {};
        return false;
    }
    reason = "present";
    return true;
}

}  // namespace

BEVMetricClassSampler::BEVMetricClassSampler(const port::LegacyCameraFrameView& frame,
                                             int threshold,
                                             const port::RuntimeParameters& params,
                                             const BEVProjector& projector)
    : frame_(frame),
      threshold_(threshold),
      params_(params),
      projector_(projector) {}

bool BEVMetricClassSampler::Valid() const {
    return frame_.Valid() && projector_.Valid();
}

BEVMetricClassSample BEVMetricClassSampler::Sample(const port::BEVPoint& point) const {
    BEVMetricClassSample sample{};
    if (!Valid() ||
        !std::isfinite(point.forward_m) ||
        !std::isfinite(point.lateral_m)) {
        return sample;
    }

    port::ImagePoint image_point{};
    if (!projector_.ProjectVehicleToImage(point, image_point)) {
        sample.projection_state = port::BEVElementRasterProjectionState::kProjectionFailed;
        return sample;
    }
    if (image_point.row_px < 0.0F || image_point.col_px < 0.0F ||
        image_point.row_px > static_cast<float>(frame_.height - 1) ||
        image_point.col_px > static_cast<float>(frame_.width - 1)) {
        sample.projection_state = port::BEVElementRasterProjectionState::kOutsideFrame;
        return sample;
    }

    std::uint8_t gray = 0;
    if (!SampleFrameBilinear(frame_, image_point.row_px, image_point.col_px, gray)) {
        sample.projection_state = port::BEVElementRasterProjectionState::kOutsideFrame;
        return sample;
    }
    sample.projection_state = port::BEVElementRasterProjectionState::kSampleable;
    switch (ClassifyBevPixel(gray, threshold_, params_.bev_classification)) {
        case BEVSimplePixelClass::kWhite:
            sample.class_kind = port::BEVElementRasterCellClass::kWhite;
            break;
        case BEVSimplePixelClass::kBlack:
            sample.class_kind = port::BEVElementRasterCellClass::kBlack;
            break;
        case BEVSimplePixelClass::kUnknown:
            sample.class_kind = port::BEVElementRasterCellClass::kUnknown;
            break;
        case BEVSimplePixelClass::kInvalid:
            sample.class_kind = port::BEVElementRasterCellClass::kInvalid;
            break;
    }
    return sample;
}

/// DetectCircleElementEvidence 实现
/// 检测环形交叉口元素的入口证据
/// 1. 收集行观测
/// 2. 评估两侧开口/直线度/收缩情况
/// 3. 根据评估结果判断是否为环形入口
CircleElementEvidenceResult DetectCircleElementEvidence(
    const std::vector<BEVSimpleRowScan>& sparse_rows,
    const port::RuntimeParameters& params) {
    CircleElementEvidenceResult result{};
    result.left_raw = MakeRecord("circle_left_raw", "not_evaluated");
    result.right_raw = MakeRecord("circle_right_raw", "not_evaluated");

    if (!params.bev_element.circle_evidence_enabled) {
        result.left_raw.reason = "circle_evidence_disabled";
        result.right_raw.reason = "circle_evidence_disabled";
        return result;
    }
    if (sparse_rows.empty()) {
        result.left_raw.reason = "no_sparse_rows";
        result.right_raw.reason = "no_sparse_rows";
        return result;
    }

    const std::vector<RowObservation> rows =
        CollectRows(sparse_rows, params.bev_element, result.left_raw, result.right_raw);
    if (rows.size() < static_cast<std::size_t>(std::max(1, params.bev_element.circle_min_support_rows))) {
        result.left_raw.reason = "insufficient_sampleable_support";
        result.right_raw.reason = "insufficient_sampleable_support";
        return result;
    }

    const SideAssessment assessment = AssessSides(rows, params.bev_element);
    const float present_min = params.bev_element.circle_present_confidence_min;
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
    CircleElementEvidenceResult result = DetectCircleElementEvidence(sparse_rows, params);
    if (result.left_raw.present || result.right_raw.present) {
        port::VisualElementEvidenceRecord scratch_left = MakeRecord("circle_left_entry", "not_evaluated");
        port::VisualElementEvidenceRecord scratch_right = MakeRecord("circle_right_entry", "not_evaluated");
        const std::vector<RowObservation> rows =
            CollectRows(sparse_rows, params.bev_element, scratch_left, scratch_right);
        if (result.left_raw.present) {
            result.left_entry = BuildEntryFactsFromRaster(*raster, rows, params.bev_element, true);
        }
        if (result.right_raw.present) {
            result.right_entry = BuildEntryFactsFromRaster(*raster, rows, params.bev_element, false);
        }
    }
    return result;
}

CircleEntryPathFacts BuildCircleEntryPathFacts(const BEVMetricClassSampler& sampler,
                                               const std::vector<BEVSimpleRowScan>& sparse_rows,
                                               const port::RuntimeParameters& params,
                                               bool left_side) {
    port::VisualElementEvidenceRecord scratch_left = MakeRecord("circle_left_entry", "not_evaluated");
    port::VisualElementEvidenceRecord scratch_right = MakeRecord("circle_right_entry", "not_evaluated");
    const std::vector<RowObservation> rows =
        CollectRows(sparse_rows, params.bev_element, scratch_left, scratch_right);
    if (rows.size() < static_cast<std::size_t>(std::max(1, params.bev_element.circle_min_support_rows))) {
        CircleEntryPathFacts facts{};
        facts.reason = "insufficient_sampleable_support";
        return facts;
    }
    return BuildEntryFacts(sampler, rows, params, left_side);
}

/// BuildCircleEntryVisualReferenceCandidate 实现
/// 从环形入口证据和路径事实构建视觉参考候选
/// 入口路径构建成功后方可生成候选，并受takeover_enabled控制是否参与仲裁
port::VisualReferenceCandidate BuildCircleEntryVisualReferenceCandidate(
    const port::VisualElementEvidenceRecord& evidence,
    const CircleEntryPathFacts& entry,
    port::VisualReferenceCandidateKind kind,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary) {
    port::VisualReferenceCandidate candidate{};
    summary = {};
    summary.takeover_enabled = params.bev_element.circle_entry_takeover_enabled;
    if (!evidence.present) {
        summary.reason = evidence.reason.empty() ? "evidence_absent" : evidence.reason;
        return candidate;
    }
    if (!entry.present) {
        summary.reason = entry.reason.empty() ? "entry_facts_absent" : entry.reason;
        return candidate;
    }

    port::BEVReferencePath path{};
    std::string reason;
    if (!BuildCircleReferencePath(entry, params, path, reason)) {
        summary.reason = reason.empty() ? "reference_path_invalid" : reason;
        return candidate;
    }

    candidate.present = true;
    candidate.kind = kind;
    candidate.reference_path = path;
    candidate.confidence = evidence.confidence;
    if (kind == port::VisualReferenceCandidateKind::kCircleLeft) {
        candidate.source = "circle_left";
        candidate.reason = "circle_left_entry_frontier_candidate";
    } else {
        candidate.source = "circle_right";
        candidate.reason = "circle_right_entry_frontier_candidate";
    }

    summary.built = true;
    summary.included_in_arbitration = summary.takeover_enabled;
    summary.reason = summary.included_in_arbitration ? "included_in_arbitration"
                                                     : "takeover_disabled";
    return candidate;
}

}  // namespace ls2k::legacy
