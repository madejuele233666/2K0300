#include "legacy/steering_bev_simple_perception.hpp"

// Simple BEV perception pipeline:
// frame view -> virtual BEV sparse row scan -> row white intervals -> reference path.
// Debug dense BEV remains output-only; runtime element raster is built by
// steering_bev_element_raster.* and is not read back from debug media.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "port/perf_counter.hpp"
#include "legacy/steering_bev_boundary_trace_clip.hpp"
#include "legacy/steering_bev_interval_edges.hpp"
#include "legacy/steering_reference_connectivity.hpp"
#include "legacy/steering_single_boundary_offset.hpp"

namespace ls2k::legacy {
namespace {

constexpr std::uint8_t kInvalidGray = 0U;

float LateralAtIndex(std::size_t index, float lateral_limit, float lateral_step) {
    return -lateral_limit + static_cast<float>(index) * lateral_step;
}

std::size_t ComputeLateralSampleCount(float lateral_limit, float lateral_step) {
    if (lateral_limit <= 0.0F || lateral_step <= 1.0e-5F) {
        return 0;
    }
    return static_cast<std::size_t>(std::floor((2.0F * lateral_limit) / lateral_step + 1.0e-4F)) + 1U;
}

std::size_t ActiveSparseRowCount(const port::RuntimeParameters& params) {
    return static_cast<std::size_t>(
        std::clamp(params.bev_geometry.sparse_row_count,
                   1,
                   static_cast<int>(port::kBevReferenceSampleCount)));
}

bool SameCalibration(const port::BEVProjectorCalibration& lhs,
                     const port::BEVProjectorCalibration& rhs) {
    if (lhs.valid != rhs.valid ||
        lhs.debug_grid_width != rhs.debug_grid_width ||
        lhs.debug_grid_height != rhs.debug_grid_height ||
        lhs.projector_id != rhs.projector_id ||
        lhs.projector_hash != rhs.projector_hash) {
        return false;
    }
    for (std::size_t index = 0; index < port::kBevCalibrationPointCount; ++index) {
        if (lhs.source_points[index].row_px != rhs.source_points[index].row_px ||
            lhs.source_points[index].col_px != rhs.source_points[index].col_px ||
            lhs.target_points[index].forward_m != rhs.target_points[index].forward_m ||
            lhs.target_points[index].lateral_m != rhs.target_points[index].lateral_m) {
            return false;
        }
    }
    return true;
}

bool SameForwardSamples(const std::array<float, port::kBevReferenceSampleCount>& lhs,
                        const std::array<float, port::kBevReferenceSampleCount>& rhs) {
    for (std::size_t index = 0; index < port::kBevReferenceSampleCount; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

port::ReferenceGeometryIdentity MakeReferenceGeometryIdentity(const port::RuntimeParameters& params) {
    port::ReferenceGeometryIdentity identity{};
    identity.initialized = true;
    identity.forward_samples_m = params.bev_geometry.forward_samples_m;
    identity.sparse_row_count = static_cast<int>(ActiveSparseRowCount(params));
    identity.search_lateral_limit_m = params.bev_geometry.search_lateral_limit_m;
    identity.lateral_step_m = params.bev_geometry.lateral_step_m;
    return identity;
}

bool SameReferenceGeometryIdentity(const port::ReferenceGeometryIdentity& lhs,
                                   const port::ReferenceGeometryIdentity& rhs) {
    return lhs.initialized && rhs.initialized &&
           lhs.sparse_row_count == rhs.sparse_row_count &&
           lhs.search_lateral_limit_m == rhs.search_lateral_limit_m &&
           lhs.lateral_step_m == rhs.lateral_step_m &&
           SameForwardSamples(lhs.forward_samples_m, rhs.forward_samples_m);
}

void InitializeReferencePath(port::BEVReferencePath& reference,
                             const port::RuntimeParameters& params,
                             port::ReferenceMode mode) {
    reference.mode = mode;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        port::BEVPathSample& sample = reference.sampled_path[index];
        sample.present = false;
        sample.point.forward_m = params.bev_geometry.forward_samples_m[index];
        sample.point.lateral_m = 0.0F;
        sample.confidence = 0.0F;
        sample.source = port::BEVPathPointSource::kNone;
    }
}

bool LutMatches(const BEVSampleProjectionLut& lut,
                const port::LegacyCameraFrameView& frame,
                const port::RuntimeParameters& params,
                const BEVProjector& projector,
                std::size_t lateral_count,
                float lateral_limit,
                float lateral_step,
                std::size_t active_sparse_rows) {
    return lut.valid &&
           lut.frame_width == frame.width &&
           lut.frame_height == frame.height &&
           lut.frame_stride == frame.stride &&
           lut.sparse_row_count == active_sparse_rows &&
           lut.lateral_sample_count == lateral_count &&
           lut.lateral_limit_m == lateral_limit &&
           lut.lateral_step_m == lateral_step &&
           SameForwardSamples(lut.forward_samples_m, params.bev_geometry.forward_samples_m) &&
           SameCalibration(lut.calibration, projector.Calibration()) &&
           lut.entries.size() == active_sparse_rows * lateral_count;
}

}  // namespace

// 统一的双线性灰度采样入口，供稀疏参考行扫描和 debug 稠密 BEV 复用。
// 采样坐标必须落在原始图像范围内；投影越界时返回 false，而不是把坐标夹到图像边缘。
// 这样可以避免把视野外或投影失败区域误当成边缘像素参与黑白分类。
// out_gray 只在返回 true 时有效。
bool SampleFrameBilinear(const port::LegacyCameraFrameView& frame,
                         float row_px,
                         float col_px,
                         std::uint8_t& out_gray) {
    if (!frame.Valid()) {
        return false;
    }
    if (row_px < 0.0F || col_px < 0.0F || row_px > static_cast<float>(frame.height - 1) ||
        col_px > static_cast<float>(frame.width - 1)) {
        return false;
    }

    const int row0 = static_cast<int>(std::floor(row_px));
    const int col0 = static_cast<int>(std::floor(col_px));
    const int row1 = std::min(row0 + 1, frame.height - 1);
    const int col1 = std::min(col0 + 1, frame.width - 1);
    const float row_frac = row_px - static_cast<float>(row0);
    const float col_frac = col_px - static_cast<float>(col0);

    const auto gray_at = [&frame](int row, int col) -> float {
        const std::size_t index =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.stride) +
            static_cast<std::size_t>(col);
        return static_cast<float>(frame.gray[index]);
    };

    const float top = gray_at(row0, col0) * (1.0F - col_frac) + gray_at(row0, col1) * col_frac;
    const float bottom = gray_at(row1, col0) * (1.0F - col_frac) + gray_at(row1, col1) * col_frac;
    const float gray = top * (1.0F - row_frac) + bottom * row_frac;
    out_gray = static_cast<std::uint8_t>(std::lround(std::clamp(gray, 0.0F, 255.0F)));
    return true;
}

namespace {

// 根据 Otsu 阈值计算黑白分类的置信度带宽。
// 当阈值接近 0 或 255 时，画面本身接近饱和，靠近阈值的像素更容易受噪声影响。
// 因此这里保留较宽的不确定区间，让低 margin 像素落入 unknown，而不是过早成为 black/white 事实。
float DecisionBandForThreshold(float threshold) {
    const float nearest_saturation =
        std::min(std::max(1.0F, threshold), std::max(1.0F, 255.0F - threshold));
    return std::clamp(nearest_saturation * 0.5F, 32.0F, 72.0F);
}

}  // namespace

BEVSimplePixelClass ClassifyBevPixel(std::uint8_t gray,
                                     int threshold,
                                     const port::BEVClassificationParameters& classification) {
    const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
    const float margin = std::abs(static_cast<float>(gray) - threshold_f);
    const float confidence = std::clamp(margin / DecisionBandForThreshold(threshold_f), 0.0F, 1.0F);
    if (confidence < classification.unknown_confidence_min) {
        return BEVSimplePixelClass::kUnknown;
    }
    if (gray > threshold) {
        return confidence >= classification.white_confidence_min ? BEVSimplePixelClass::kWhite
                                                                 : BEVSimplePixelClass::kUnknown;
    }
    return BEVSimplePixelClass::kBlack;
}

namespace {

port::BEVPoint PixelToBevPoint(int x,
                               int y,
                               int width,
                               int height,
                               float lateral_limit_m,
                               float forward_max_m) {
    const float normalized_x = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.5F;
    const float normalized_y = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 1.0F;
    port::BEVPoint point{};
    point.lateral_m = normalized_x * (2.0F * lateral_limit_m) - lateral_limit_m;
    point.forward_m = (1.0F - normalized_y) * forward_max_m;
    return point;
}

}  // namespace

// 构建 debug 用的稠密 BEV 图像，只用于展示和离线观察。
// runtime 参考线提取仍然使用后面的稀疏行扫描，不从这个 debug 图反向读取事实。
// 这保持了“显示辅助”和“控制事实”的边界，避免 debug 数据影响实际寻线。
BEVSimpleImage BuildDebugDenseBevImage(const port::LegacyCameraFrameView& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector) {
    BEVSimpleImage image{};
    if (!projector.Valid() || !frame.Valid()) {
        return image;
    }

    const float lateral_limit = std::max(0.1F, params.bev_geometry.search_lateral_limit_m);
    const float forward_max = params.bev_geometry.forward_samples_m.back();
    const int width = std::max(2, params.bev_projector.debug_grid_width * 2);
    const float scale_px_per_m = static_cast<float>(width) / std::max(1.0e-4F, lateral_limit * 2.0F);
    const int height = std::max(2, static_cast<int>(std::lround(forward_max * scale_px_per_m)));

    image.valid = true;
    image.width = width;
    image.height = height;
    image.lateral_limit_m = lateral_limit;
    image.forward_max_m = forward_max;
    image.gray.assign(static_cast<std::size_t>(width * height), kInvalidGray);
    image.classes.assign(static_cast<std::size_t>(width * height), BEVSimplePixelClass::kInvalid);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const port::BEVPoint bev_point = PixelToBevPoint(x, y, width, height, lateral_limit, forward_max);
            port::ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage(bev_point, image_point)) {
                continue;
            }
            std::uint8_t gray = 0;
            if (!SampleFrameBilinear(frame, image_point.row_px, image_point.col_px, gray)) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            image.gray[index] = gray;
            image.classes[index] = ClassifyBevPixel(gray, threshold, params.bev_classification);
        }
    }

    return image;
}

namespace {

// 扫描单条稀疏 BEV 行，产出该行的黑/白/未知/不可用计数以及白色连续区间。
// 这些 row facts 同时服务基础 line reference 和 element evidence，是当前视觉事实的公共输入。
// 这里不做 cross/circle 的语义判断，只描述这一行本身看到了什么。
BEVSimpleRowScan ScanSparseRow(const port::LegacyCameraFrameView& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               const BEVSampleProjectionLut& lut,
                               std::size_t row_index) {
    BEVSimpleRowScan row{};
    if (!lut.valid || row_index >= port::kBevReferenceSampleCount || lut.lateral_sample_count == 0) {
        return row;
    }

    row.valid = true;
    row.forward_m = params.bev_geometry.forward_samples_m[row_index];
    row.row_px = static_cast<int>(row_index);
    const float min_width_m = std::max(0.02F, params.bev_geometry.lateral_step_m * 1.5F);
    int run_begin = -1;
    bool have_sampleable_lateral = false;
    bool left_unknown_prefix_active = true;
    bool left_unknown_prefix_seen = false;
    float left_unknown_prefix_right_m = 0.0F;
    bool right_unknown_suffix_active = false;
    float right_unknown_suffix_left_m = 0.0F;
    for (std::size_t lateral_index = 0; lateral_index <= lut.lateral_sample_count; ++lateral_index) {
        bool white = false;
        if (lateral_index < lut.lateral_sample_count) {
            const BEVSampleProjectionEntry& entry =
                lut.entries[row_index * lut.lateral_sample_count + lateral_index];
            BEVSimplePixelClass pixel_class = BEVSimplePixelClass::kInvalid;
            float lateral = 0.0F;
            bool have_lateral = false;
            if (entry.state == BEVSampleProjectionState::kSampleable) {
                std::uint8_t gray = 0;
                if (SampleFrameBilinear(frame, entry.image_row_px, entry.image_col_px, gray)) {
                    pixel_class = ClassifyBevPixel(gray, threshold, params.bev_classification);
                    lateral = LateralAtIndex(lateral_index,
                                             lut.lateral_limit_m,
                                             lut.lateral_step_m);
                    have_lateral = true;
                    if (!have_sampleable_lateral) {
                        row.sampleable_left_m = lateral;
                        row.sampleable_right_m = lateral;
                        have_sampleable_lateral = true;
                    } else {
                        row.sampleable_left_m = std::min(row.sampleable_left_m, lateral);
                        row.sampleable_right_m = std::max(row.sampleable_right_m, lateral);
                    }
                }
            }
            if (pixel_class == BEVSimplePixelClass::kWhite) {
                ++row.white_count;
            } else if (pixel_class == BEVSimplePixelClass::kBlack) {
                ++row.black_count;
            } else if (pixel_class == BEVSimplePixelClass::kUnknown) {
                ++row.unknown_count;
            } else {
                ++row.unavailable_count;
            }
            if (pixel_class != BEVSimplePixelClass::kInvalid) {
                ++row.sampleable_count;
                white = pixel_class == BEVSimplePixelClass::kWhite;
                if (have_lateral) {
                    if (left_unknown_prefix_active) {
                        if (pixel_class == BEVSimplePixelClass::kUnknown) {
                            left_unknown_prefix_seen = true;
                            left_unknown_prefix_right_m = lateral;
                        } else {
                            left_unknown_prefix_active = false;
                        }
                    }
                    if (pixel_class == BEVSimplePixelClass::kUnknown) {
                        if (!right_unknown_suffix_active) {
                            right_unknown_suffix_left_m = lateral;
                        }
                        right_unknown_suffix_active = true;
                    } else {
                        right_unknown_suffix_active = false;
                    }
                }
            }
        }

        if (white && run_begin < 0) {
            run_begin = static_cast<int>(lateral_index);
        }
        if ((!white || lateral_index == lut.lateral_sample_count) && run_begin >= 0) {
            const int run_end = static_cast<int>(lateral_index) - 1;
            const float left =
                LateralAtIndex(static_cast<std::size_t>(run_begin), lut.lateral_limit_m, lut.lateral_step_m);
            const float right =
                LateralAtIndex(static_cast<std::size_t>(run_end), lut.lateral_limit_m, lut.lateral_step_m);
            const float width = std::max(0.0F, right - left);
            if (width >= min_width_m) {
                BEVSimpleWhiteInterval interval{};
                interval.forward_m = row.forward_m;
                interval.left_m = left;
                interval.right_m = right;
                interval.center_m = 0.5F * (left + right);
                interval.width_m = width;
                interval.left_px = run_begin;
                interval.right_px = run_end;
                row.intervals.push_back(interval);
            }
            run_begin = -1;
        }
    }
    if (have_sampleable_lateral) {
        row.sampleable_width_m = std::max(0.0F, row.sampleable_right_m - row.sampleable_left_m);
    }
    if (left_unknown_prefix_seen) {
        row.sampleable_left_unknown_run = true;
        row.sampleable_left_unknown_run_right_m = left_unknown_prefix_right_m;
    }
    if (right_unknown_suffix_active) {
        row.sampleable_right_unknown_run = true;
        row.sampleable_right_unknown_run_left_m = right_unknown_suffix_left_m;
    }
    return row;
}

std::vector<BEVSimpleRowScan> ScanSparseRows(const port::LegacyCameraFrameView& frame,
                                             int threshold,
                                             const port::RuntimeParameters& params,
                                             const BEVSampleProjectionLut& lut) {
    std::vector<BEVSimpleRowScan> rows;
    const std::size_t active_sparse_rows = ActiveSparseRowCount(params);
    rows.reserve(active_sparse_rows);
    for (std::size_t index = 0; index < active_sparse_rows; ++index) {
        rows.push_back(ScanSparseRow(frame, threshold, params, lut, index));
    }
    return rows;
}

struct CenterCandidate {
    float forward_m = 0.0F;
    float lateral_m = 0.0F;
};

enum class SingleEdgeKind {
    kLow,
    kHigh,
};

using CenterCandidateRows =
    std::array<std::vector<CenterCandidate>, port::kBevReferenceSampleCount>;

float ReferenceMaxJump(const port::RuntimeParameters& params) {
    return params.bev_geometry.reference_lateral_jump_gate_m;
}

BEVBoundaryTraceClipOptions BoundaryTraceClipOptionsFromParams(
    const port::RuntimeParameters& params) {
    return BEVBoundaryTraceClipOptions{
        params.bev_geometry.boundary_trace_max_adjacent_distance_m};
}

BEVIntervalEdgeVisibilityOptions ReferenceEdgeVisibilityOptions() {
    BEVIntervalEdgeVisibilityOptions options{};
    options.treat_unknown_sampleable_edge_as_boundary = true;
    return options;
}

float EdgeLateral(const BEVSimpleWhiteInterval& interval, SingleEdgeKind kind) {
    return kind == SingleEdgeKind::kLow ? interval.left_m : interval.right_m;
}

bool EdgeVisible(const BEVSimpleRowScan& row,
                 const BEVSimpleWhiteInterval& interval,
                 SingleEdgeKind kind,
                 const BEVIntervalEdgeVisibilityOptions& options) {
    const BEVIntervalEdgeVisibility visibility =
        EvaluateIntervalEdgeVisibility(row, interval, options);
    return kind == SingleEdgeKind::kLow ? visibility.low_visible
                                        : visibility.high_visible;
}

port::BEVPoint EdgePoint(const BEVSimpleRowScan& row,
                         const BEVSimpleWhiteInterval& interval,
                         SingleEdgeKind kind) {
    return port::BEVPoint{row.forward_m, EdgeLateral(interval, kind)};
}

bool SameBoundaryTracePoint(const BEVBoundaryTracePoint& point,
                            std::size_t row_index,
                            const port::BEVPoint& edge_point) {
    return point.row_index == row_index &&
           point.point.forward_m == edge_point.forward_m &&
           point.point.lateral_m == edge_point.lateral_m;
}

std::vector<BEVBoundaryTracePoint> BuildBoundaryTraceForEdge(
    const std::vector<BEVSimpleRowScan>& rows,
    SingleEdgeKind kind,
    const BEVIntervalEdgeVisibilityOptions& options,
    std::size_t current_row_index,
    std::size_t current_interval_index,
    float anchor_lateral_m) {
    std::vector<BEVBoundaryTracePoint> trace;
    const std::size_t count =
        std::min(rows.size(), static_cast<std::size_t>(port::kBevReferenceSampleCount));
    trace.reserve(count);
    for (std::size_t row_index = 0U; row_index < count; ++row_index) {
        const BEVSimpleRowScan& row = rows[row_index];
        if (!row.valid) {
            continue;
        }
        const BEVSimpleWhiteInterval* selected = nullptr;
        float best_cost = 0.0F;
        for (std::size_t interval_index = 0U;
             interval_index < row.intervals.size();
             ++interval_index) {
            const BEVSimpleWhiteInterval& interval = row.intervals[interval_index];
            if (!EdgeVisible(row, interval, kind, options)) {
                continue;
            }
            if (row_index == current_row_index &&
                interval_index == current_interval_index) {
                selected = &interval;
                break;
            }
            const float cost =
                std::fabs(EdgeLateral(interval, kind) - anchor_lateral_m);
            if (selected == nullptr || cost < best_cost) {
                selected = &interval;
                best_cost = cost;
            }
        }
        if (selected != nullptr) {
            trace.push_back(BEVBoundaryTracePoint{
                row_index,
                EdgePoint(row, *selected, kind),
            });
        }
    }
    return trace;
}

struct BoundarySupport {
    bool current_kept = false;
    bool has_neighbor = false;
    port::BEVPoint neighbor{};
};

BoundarySupport FindBoundarySupport(
    const std::vector<BEVSimpleRowScan>& rows,
    SingleEdgeKind kind,
    const BEVIntervalEdgeVisibilityOptions& visibility_options,
    const BEVBoundaryTraceClipOptions& clip_options,
    std::size_t row_index,
    std::size_t interval_index) {
    BoundarySupport support{};
    if (row_index >= rows.size() ||
        interval_index >= rows[row_index].intervals.size()) {
        return support;
    }
    const BEVSimpleRowScan& row = rows[row_index];
    const BEVSimpleWhiteInterval& interval = row.intervals[interval_index];
    if (!EdgeVisible(row, interval, kind, visibility_options)) {
        return support;
    }
    const port::BEVPoint current = EdgePoint(row, interval, kind);
    const std::vector<BEVBoundaryTracePoint> raw_trace =
        BuildBoundaryTraceForEdge(rows,
                                  kind,
                                  visibility_options,
                                  row_index,
                                  interval_index,
                                  current.lateral_m);
    const std::vector<BEVBoundaryTracePoint> clipped_trace =
        ClipBoundaryTraceOutliers(raw_trace, clip_options);

    std::size_t best_row_gap = 0U;
    float best_lateral_gap = 0.0F;
    for (const BEVBoundaryTracePoint& point : clipped_trace) {
        if (SameBoundaryTracePoint(point, row_index, current)) {
            support.current_kept = true;
            continue;
        }
        if (point.row_index <= row_index) {
            continue;
        }
        const std::size_t row_gap = point.row_index - row_index;
        const float lateral_gap = std::fabs(point.point.lateral_m - current.lateral_m);
        if (!support.has_neighbor ||
            row_gap < best_row_gap ||
            (row_gap == best_row_gap && lateral_gap < best_lateral_gap)) {
            support.has_neighbor = true;
            support.neighbor = point.point;
            best_row_gap = row_gap;
            best_lateral_gap = lateral_gap;
        }
    }
    if (!support.current_kept) {
        support.has_neighbor = false;
    }
    return support;
}

BEVIntervalEdgeVisibility EvaluateContinuityFilteredVisibility(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const BEVIntervalEdgeVisibilityOptions& visibility_options,
    std::size_t row_index,
    std::size_t interval_index) {
    BEVIntervalEdgeVisibility visibility{};
    const BEVBoundaryTraceClipOptions clip_options =
        BoundaryTraceClipOptionsFromParams(params);
    const BoundarySupport low_support =
        FindBoundarySupport(rows,
                            SingleEdgeKind::kLow,
                            visibility_options,
                            clip_options,
                            row_index,
                            interval_index);
    visibility.low_visible = low_support.current_kept;
    const BoundarySupport high_support =
        FindBoundarySupport(rows,
                            SingleEdgeKind::kHigh,
                            visibility_options,
                            clip_options,
                            row_index,
                            interval_index);
    visibility.high_visible = high_support.current_kept;
    return visibility;
}

bool IntervalSupportsMidpointCandidate(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const ReferenceConnectivityFrameView* connectivity_frame,
    const BEVIntervalEdgeVisibilityOptions& options,
    std::size_t row_index,
    std::size_t interval_index) {
    if (row_index >= rows.size() ||
        interval_index >= rows[row_index].intervals.size()) {
        return false;
    }
    const BEVSimpleRowScan& row = rows[row_index];
    const BEVSimpleWhiteInterval& interval = row.intervals[interval_index];
    const BEVIntervalEdgeVisibility visibility =
        EvaluateContinuityFilteredVisibility(rows,
                                             params,
                                             options,
                                             row_index,
                                             interval_index);
    if (!visibility.low_visible || !visibility.high_visible) {
        return false;
    }
    if (connectivity_frame == nullptr) {
        return true;
    }
    return BEVSegmentHasNoBlackPixels(
        *connectivity_frame,
        port::BEVPoint{row.forward_m, interval.left_m},
        port::BEVPoint{row.forward_m, interval.right_m});
}

bool IsSingleEdgeInterval(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const BEVIntervalEdgeVisibilityOptions& options,
    std::size_t row_index,
    std::size_t interval_index,
    SingleEdgeKind kind) {
    const BEVIntervalEdgeVisibility visibility =
        EvaluateContinuityFilteredVisibility(rows,
                                             params,
                                             options,
                                             row_index,
                                             interval_index);
    if (kind == SingleEdgeKind::kLow) {
        return visibility.low_visible && !visibility.high_visible;
    }
    return !visibility.low_visible && visibility.high_visible;
}

float SignedNormalOffset(const port::RuntimeParameters& params, SingleEdgeKind kind) {
    const float nominal = params.bev_geometry.nominal_road_half_width_m;
    return kind == SingleEdgeKind::kLow ? nominal : -nominal;
}

void AddSingleEdgeCandidates(const std::vector<BEVSimpleRowScan>& rows,
                             SingleEdgeKind kind,
                             const port::RuntimeParameters& params,
                             const BEVIntervalEdgeVisibilityOptions& options,
                             CenterCandidateRows& candidate_rows) {
    const std::size_t count =
        std::min(rows.size(), static_cast<std::size_t>(port::kBevReferenceSampleCount));
    const BEVBoundaryTraceClipOptions clip_options =
        BoundaryTraceClipOptionsFromParams(params);
    for (std::size_t row_index = 0; row_index < count; ++row_index) {
        const BEVSimpleRowScan& row = rows[row_index];
        if (!row.valid) {
            continue;
        }
        for (std::size_t interval_index = 0U;
             interval_index < row.intervals.size();
             ++interval_index) {
            const BEVSimpleWhiteInterval& interval = row.intervals[interval_index];
            if (!IsSingleEdgeInterval(rows,
                                      params,
                                      options,
                                      row_index,
                                      interval_index,
                                      kind)) {
                continue;
            }
            const BoundarySupport support =
                FindBoundarySupport(rows,
                                    kind,
                                    options,
                                    clip_options,
                                    row_index,
                                    interval_index);
            if (!support.current_kept || !support.has_neighbor) {
                continue;
            }
            std::vector<port::BEVPoint> boundary_trace{
                EdgePoint(row, interval, kind),
                support.neighbor,
            };
            std::vector<float> target_forward_samples{row.forward_m};
            const std::vector<port::BEVPoint> center_points =
                BuildSingleBoundaryOffsetReference(boundary_trace,
                                                   target_forward_samples,
                                                   SignedNormalOffset(params, kind));
            if (center_points.empty()) {
                continue;
            }
            candidate_rows[row_index].push_back(
                CenterCandidate{center_points.front().forward_m,
                                center_points.front().lateral_m});
        }
    }
}

CenterCandidateRows BuildOrdinaryCenterCandidates(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const ReferenceConnectivityFrameView* connectivity_frame) {
    CenterCandidateRows candidate_rows{};
    const std::size_t count =
        std::min(rows.size(), static_cast<std::size_t>(port::kBevReferenceSampleCount));
    const BEVIntervalEdgeVisibilityOptions options = ReferenceEdgeVisibilityOptions();

    for (std::size_t row_index = 0; row_index < count; ++row_index) {
        const BEVSimpleRowScan& row = rows[row_index];
        if (!row.valid) {
            continue;
        }
        for (std::size_t interval_index = 0U;
             interval_index < row.intervals.size();
             ++interval_index) {
            const BEVSimpleWhiteInterval& interval = row.intervals[interval_index];
            if (!IntervalSupportsMidpointCandidate(rows,
                                                   params,
                                                   connectivity_frame,
                                                   options,
                                                   row_index,
                                                   interval_index)) {
                continue;
            }
            candidate_rows[row_index].push_back(
                CenterCandidate{row.forward_m,
                                0.5F * (interval.left_m + interval.right_m)});
        }
    }

    AddSingleEdgeCandidates(rows,
                            SingleEdgeKind::kLow,
                            params,
                            options,
                            candidate_rows);
    AddSingleEdgeCandidates(rows,
                            SingleEdgeKind::kHigh,
                            params,
                            options,
                            candidate_rows);
    return candidate_rows;
}

const CenterCandidate* ChooseCenterCandidate(const std::vector<CenterCandidate>& candidates,
                                             bool have_previous,
                                             float previous_lateral,
                                             const port::RuntimeParameters& params) {
    const CenterCandidate* best = nullptr;
    float best_cost = 0.0F;
    const float max_jump = ReferenceMaxJump(params);
    for (const CenterCandidate& candidate : candidates) {
        const float target = have_previous ? previous_lateral : 0.0F;
        const float cost = std::fabs(candidate.lateral_m - target);
        if (have_previous && cost > max_jump) {
            continue;
        }
        if (best == nullptr || cost < best_cost) {
            best = &candidate;
            best_cost = cost;
        }
    }
    return best;
}

}  // namespace

// 提取基础 line reference 的第一个连续可用段。
// 近端行暂时无候选时允许继续向远端寻找起点；一旦连续段开始，遇到第一个缺口就停止，
// 不跨缺口重连后续远端点。参考路径按真实点顺序紧凑输出，forward_m 保留原始行距离。
port::BEVReferencePath ExtractStrictLeadingReferenceSegment(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const ReferenceConnectivityFrameView* connectivity_frame) {
    port::BEVReferencePath reference{};
    InitializeReferencePath(reference, params, port::ReferenceMode::kNone);
    const CenterCandidateRows candidate_rows =
        BuildOrdinaryCenterCandidates(rows, params, connectivity_frame);
    bool have_previous = false;
    float previous_lateral = 0.0F;
    bool segment_started = false;
    std::size_t output_index = 0U;

    for (std::size_t index = 0; index < rows.size() && index < reference.sampled_path.size(); ++index) {
        const CenterCandidate* candidate =
            ChooseCenterCandidate(candidate_rows[index],
                                  have_previous,
                                  previous_lateral,
                                  params);
        if (candidate == nullptr) {
            if (segment_started) {
                break;
            }
            continue;
        }

        if (output_index >= reference.sampled_path.size()) {
            break;
        }
        port::BEVPathSample& sample = reference.sampled_path[output_index];
        reference.mode = port::ReferenceMode::kIntervalCenter;
        sample.present = true;
        sample.point.forward_m = candidate->forward_m;
        sample.point.lateral_m = candidate->lateral_m;
        sample.confidence = 1.0F;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
        previous_lateral = candidate->lateral_m;
        have_previous = true;
        segment_started = true;
        ++output_index;
    }
    return reference;
}

port::BEVReferencePath BuildReferencePath(const std::vector<BEVSimpleRowScan>& rows,
                                          const port::RuntimeParameters& params,
                                          const ReferenceConnectivityFrameView* connectivity_frame) {
    return ExtractStrictLeadingReferenceSegment(rows, params, connectivity_frame);
}

port::ReferenceHoldState MakeReferenceHoldState(const port::BEVReferencePath& current_visual_reference,
                                                uint64_t reference_capture_time_ms,
                                                const port::RuntimeParameters& params) {
    port::ReferenceHoldState state{};
    state.hold_cycles = 0;
    state.last_reference = current_visual_reference.sampled_path;
    state.geometry_identity = MakeReferenceGeometryIdentity(params);
    state.reference_capture_time_ms = reference_capture_time_ms;
    return state;
}

// 构建 hold-last 候选，仅用于视觉短暂丢失时的连续性桥接。
// hold 必须沿用同一组 BEV 几何和采样配置；几何变化或保持周期超限时直接失效。
// 这里会逐点衰减 confidence，并且不会在历史参考之后补造新点。
port::ReferenceContinuityResult BuildReferenceHoldCandidate(const port::ReferenceHoldState& prior_hold,
                                                            const port::RuntimeParameters& params) {
    port::ReferenceContinuityResult result{};
    result.next_hold_state = prior_hold;
    const port::ReferenceGeometryIdentity current_identity = MakeReferenceGeometryIdentity(params);
    const bool hold_allowed =
        prior_hold.hold_cycles < params.bev_classification.hold_last_max_cycles &&
        SameReferenceGeometryIdentity(prior_hold.geometry_identity, current_identity);
    if (!hold_allowed) {
        result.next_hold_state = {};
        return result;
    }

    InitializeReferencePath(result.reference_path, params, port::ReferenceMode::kHoldLast);
    std::size_t copied = 0;
    for (std::size_t index = 0; index < result.reference_path.sampled_path.size(); ++index) {
        port::BEVPathSample sample = prior_hold.last_reference[index];
        if (!sample.present ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            break;
        }
        sample.confidence *= 0.75F;
        sample.source = port::BEVPathPointSource::kHold;
        result.reference_path.sampled_path[index] = sample;
        ++copied;
    }
    if (copied == 0) {
        result.reference_path = {};
        result.next_hold_state = {};
        return result;
    }

    result.mode = port::ReferenceMode::kHoldLast;
    result.source = "hold";
    result.hold_selected = true;
    result.reference_capture_time_ms = prior_hold.reference_capture_time_ms;
    result.next_hold_state = prior_hold;
    result.next_hold_state.hold_cycles = prior_hold.hold_cycles + 1;
    return result;
}

// 确保稀疏采样投影 LUT 与当前帧几何、BEV 参数和投影器标定一致。
// LUT 只缓存 BEV 采样点到图像坐标的几何关系；每帧的 gray 和分类结果仍在扫描时实时采样。
// 这样既避免重复投影计算，又不会把上一帧的图像事实混入当前帧。
bool EnsureBEVSampleProjectionLut(BEVSampleProjectionLut& lut,
                                  const port::LegacyCameraFrameView& frame,
                                  const port::RuntimeParameters& params,
                                  const BEVProjector& projector) {
    const float lateral_limit = std::max(0.1F, params.bev_geometry.search_lateral_limit_m);
    const float lateral_step = std::max(0.005F, params.bev_geometry.lateral_step_m);
    const std::size_t lateral_count = ComputeLateralSampleCount(lateral_limit, lateral_step);
    const std::size_t active_sparse_rows = ActiveSparseRowCount(params);
    if (!projector.Valid() || !frame.Valid() || lateral_count == 0) {
        lut = {};
        return false;
    }
    if (LutMatches(lut,
                   frame,
                   params,
                   projector,
                   lateral_count,
                   lateral_limit,
                   lateral_step,
                   active_sparse_rows)) {
        return true;
    }

    BEVSampleProjectionLut rebuilt{};
    rebuilt.valid = true;
    rebuilt.calibration = projector.Calibration();
    rebuilt.frame_width = frame.width;
    rebuilt.frame_height = frame.height;
    rebuilt.frame_stride = frame.stride;
    rebuilt.forward_samples_m = params.bev_geometry.forward_samples_m;
    rebuilt.sparse_row_count = active_sparse_rows;
    rebuilt.lateral_limit_m = lateral_limit;
    rebuilt.lateral_step_m = lateral_step;
    rebuilt.lateral_sample_count = lateral_count;
    rebuilt.entries.resize(active_sparse_rows * lateral_count);

    for (std::size_t row_index = 0; row_index < active_sparse_rows; ++row_index) {
        const float forward_m = params.bev_geometry.forward_samples_m[row_index];
        for (std::size_t lateral_index = 0; lateral_index < lateral_count; ++lateral_index) {
            BEVSampleProjectionEntry& entry =
                rebuilt.entries[row_index * lateral_count + lateral_index];
            entry.forward_m = forward_m;
            entry.lateral_m = LateralAtIndex(lateral_index, lateral_limit, lateral_step);
            port::ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage({entry.forward_m, entry.lateral_m}, image_point)) {
                entry.state = BEVSampleProjectionState::kProjectionFailed;
                continue;
            }
            entry.image_row_px = image_point.row_px;
            entry.image_col_px = image_point.col_px;
            if (image_point.row_px < 0.0F || image_point.col_px < 0.0F ||
                image_point.row_px > static_cast<float>(frame.height - 1) ||
                image_point.col_px > static_cast<float>(frame.width - 1)) {
                entry.state = BEVSampleProjectionState::kOutsideFrame;
            } else {
                entry.state = BEVSampleProjectionState::kSampleable;
            }
        }
    }

    lut = std::move(rebuilt);
    return true;
}

const char* ToString(BEVSimplePixelClass class_kind) {
    switch (class_kind) {
        case BEVSimplePixelClass::kInvalid:
            return "invalid";
        case BEVSimplePixelClass::kUnknown:
            return "unknown";
        case BEVSimplePixelClass::kBlack:
            return "black";
        case BEVSimplePixelClass::kWhite:
            return "white";
    }
    return "invalid";
}

const char* ToString(BEVSampleProjectionState state) {
    switch (state) {
        case BEVSampleProjectionState::kSampleable:
            return "sampleable";
        case BEVSampleProjectionState::kOutsideFrame:
            return "outside_frame";
        case BEVSampleProjectionState::kProjectionFailed:
            return "projection_failed";
    }
    return "projection_failed";
}

const char* ToString(port::ReferenceMode mode) {
    switch (mode) {
        case port::ReferenceMode::kNone:
            return "none";
        case port::ReferenceMode::kIntervalCenter:
            return "interval_center";
        case port::ReferenceMode::kHoldLast:
            return "hold_last";
    }
    return "none";
}

const char* ToString(port::BEVPathPointSource source) {
    switch (source) {
        case port::BEVPathPointSource::kNone:
            return "none";
        case port::BEVPathPointSource::kIntervalCenter:
            return "interval_center";
        case port::BEVPathPointSource::kHold:
            return "hold";
    }
    return "none";
}

BEVSimplePerceptionResult RunBEVSimplePerception(const port::LegacyCameraFrameView& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const BEVProjector& projector,
                                                 BEVSampleProjectionLut* lut) {
    BEVSimplePerceptionResult result{};
    result.threshold = threshold;
    BEVSampleProjectionLut local_lut{};
    BEVSampleProjectionLut& active_lut = lut == nullptr ? local_lut : *lut;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kBevSimpleLut);
        if (!EnsureBEVSampleProjectionLut(active_lut, frame, params, projector)) {
            return result;
        }
    }

    {
        LS2K_PERF_SCOPE(port::PerfStage::kBevSimpleScanRows);
        result.rows = ScanSparseRows(frame, threshold, params, active_lut);
    }
    {
        LS2K_PERF_SCOPE(port::PerfStage::kBevSimpleBuildReference);
        const ReferenceConnectivityFrameView connectivity_frame{
            frame,
            projector,
            threshold,
            params.bev_classification,
        };
        result.reference_path = BuildReferencePath(result.rows, params, &connectivity_frame);
    }
    result.reference_mode = ToString(result.reference_path.mode);
    result.reference_source =
        result.reference_path.mode == port::ReferenceMode::kIntervalCenter ? "simple_interval_center" : "none";
    return result;
}

}  // namespace ls2k::legacy
