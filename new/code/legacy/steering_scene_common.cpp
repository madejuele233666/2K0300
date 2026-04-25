#include "legacy/steering_scene_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ls2k::legacy {
namespace {

struct RowRun {
    int left = 0;
    int right = 0;
    int width = 0;
    int center = 0;
};

struct RowSelection {
    bool valid = false;
    RowRun primary{};
    int transitions = 0;
};

struct BandAccumulator {
    std::vector<int> lefts{};
    std::vector<int> rights{};
    std::vector<int> widths{};
    int full_span_rows = 0;
    int full_span_consecutive_rows = 0;
    int full_span_consecutive_rows_max = 0;
    int left_border_touch_rows = 0;
    int right_border_touch_rows = 0;

    void Add(const RowRun& run, bool full_span, int frame_width, int edge_margin_px) {
        lefts.push_back(run.left);
        rights.push_back(run.right);
        widths.push_back(run.width);
        if (full_span) {
            ++full_span_rows;
            ++full_span_consecutive_rows;
            full_span_consecutive_rows_max =
                std::max(full_span_consecutive_rows_max, full_span_consecutive_rows);
        } else {
            full_span_consecutive_rows = 0;
        }
        if (run.left <= edge_margin_px) {
            ++left_border_touch_rows;
        }
        if (run.right >= frame_width - 1 - edge_margin_px) {
            ++right_border_touch_rows;
        }
    }
};

int ClampRow(int row, int frame_height) {
    return std::clamp(row, 0, std::max(0, frame_height - 1));
}

int Median(std::vector<int> values) {
    if (values.empty()) {
        return 0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    int result = values[middle];
    if (values.size() % 2U == 0U && middle > 0U) {
        std::nth_element(values.begin(),
                         values.begin() + static_cast<std::ptrdiff_t>(middle - 1U),
                         values.begin() + static_cast<std::ptrdiff_t>(middle));
        result = (result + values[middle - 1U]) / 2;
    }
    return result;
}

std::vector<RowRun> ExtractRuns(const port::LegacyCameraFrame& frame, int row, int threshold, int& transitions) {
    std::vector<RowRun> runs;
    transitions = 0;
    if (row < 0 || row >= frame.height || frame.width <= 0) {
        return runs;
    }

    bool in_run = false;
    bool previous_above = false;
    bool previous_valid = false;
    int run_start = 0;
    for (int col = 0; col < frame.width; ++col) {
        const uint8_t pixel = frame.gray[static_cast<std::size_t>(row) * frame.width + col];
        const bool above = pixel > threshold;
        if (previous_valid && above != previous_above) {
            ++transitions;
        }
        if (above && !in_run) {
            in_run = true;
            run_start = col;
        } else if (!above && in_run) {
            const int run_end = col - 1;
            runs.push_back({run_start, run_end, run_end - run_start + 1, (run_start + run_end) / 2});
            in_run = false;
        }
        previous_valid = true;
        previous_above = above;
    }

    if (in_run) {
        const int run_end = frame.width - 1;
        runs.push_back({run_start, run_end, run_end - run_start + 1, (run_start + run_end) / 2});
    }
    return runs;
}

int OverlapWidth(const RowRun& lhs, const RowRun& rhs) {
    return std::max(0, std::min(lhs.right, rhs.right) - std::max(lhs.left, rhs.left) + 1);
}

int BorderTouchCount(const RowRun& run, int frame_width, int edge_margin_px) {
    int touches = 0;
    if (run.left <= edge_margin_px) {
        ++touches;
    }
    if (run.right >= frame_width - 1 - edge_margin_px) {
        ++touches;
    }
    return touches;
}

RowRun PickLowerPrimaryRun(const std::vector<RowRun>& runs,
                           int reference_col,
                           int image_center,
                           int frame_width,
                           int edge_margin_px) {
    const RowRun* best = nullptr;
    for (const RowRun& run : runs) {
        const bool run_crosses_center = run.left <= image_center && run.right >= image_center;
        if (best == nullptr) {
            best = &run;
            continue;
        }

        const bool best_crosses_center = best->left <= image_center && best->right >= image_center;
        if (run_crosses_center != best_crosses_center) {
            if (run_crosses_center) {
                best = &run;
            }
            continue;
        }

        if (run_crosses_center) {
            const int run_border_touches = BorderTouchCount(run, frame_width, edge_margin_px);
            const int best_border_touches = BorderTouchCount(*best, frame_width, edge_margin_px);
            if (run_border_touches != best_border_touches) {
                if (run_border_touches < best_border_touches) {
                    best = &run;
                }
                continue;
            }
            const int run_distance = std::abs(run.center - reference_col);
            const int best_distance = std::abs(best->center - reference_col);
            if (run_distance != best_distance) {
                if (run_distance < best_distance) {
                    best = &run;
                }
                continue;
            }
            if (run.width > best->width) {
                best = &run;
            }
            continue;
        }

        const int run_distance = std::abs(run.center - reference_col);
        const int best_distance = std::abs(best->center - reference_col);
        if (run_distance != best_distance) {
            if (run_distance < best_distance) {
                best = &run;
            }
            continue;
        }
        if (run.width > best->width) {
            best = &run;
        }
    }

    return best == nullptr ? RowRun{} : *best;
}

RowRun PickUpperPrimaryRun(const std::vector<RowRun>& runs,
                           const RowRun& previous_primary,
                           bool previous_valid,
                           int reference_col,
                           int frame_width,
                           int edge_margin_px) {
    if (previous_valid) {
        const RowRun* best = nullptr;
        int best_overlap = 0;
        for (const RowRun& run : runs) {
            const int overlap = OverlapWidth(run, previous_primary);
            if (overlap > best_overlap) {
                best_overlap = overlap;
                best = &run;
                continue;
            }
            if (overlap == 0 || best == nullptr || overlap != best_overlap) {
                continue;
            }
            if (run.width != best->width) {
                if (run.width > best->width) {
                    best = &run;
                }
                continue;
            }
            if (std::abs(run.center - reference_col) < std::abs(best->center - reference_col)) {
                best = &run;
            }
        }
        if (best != nullptr && best_overlap > 0) {
            return *best;
        }
    }

    return PickLowerPrimaryRun(
        runs, reference_col, port::kCompiledCameraFrameWidth / 2, frame_width, edge_margin_px);
}

bool RowInBand(int row, int start, int end) {
    return row >= start && row <= end;
}

float SafeRatio(int numerator, int denominator) {
    if (denominator <= 0) {
        return 0.0F;
    }
    return static_cast<float>(numerator) / static_cast<float>(denominator);
}

float AbsFloat(float value) {
    return std::abs(value);
}

int SignWithThreshold(int value, int threshold) {
    if (value >= threshold) {
        return 1;
    }
    if (value <= -threshold) {
        return -1;
    }
    return 0;
}

bool HasDirectionTurn(int lower_mid_dx, int mid_upper_dx, int motion_threshold) {
    const int lower_sign = SignWithThreshold(lower_mid_dx, motion_threshold);
    const int upper_sign = SignWithThreshold(mid_upper_dx, motion_threshold);
    return lower_sign != 0 && upper_sign != 0 && lower_sign != upper_sign;
}

int DominantMotionSign(int lower_mid_dx, int mid_upper_dx, int motion_threshold) {
    const int total_dx = lower_mid_dx + mid_upper_dx;
    const int total_sign = SignWithThreshold(total_dx, motion_threshold);
    if (total_sign != 0) {
        return total_sign;
    }
    const int lower_sign = SignWithThreshold(lower_mid_dx, motion_threshold);
    const int upper_sign = SignWithThreshold(mid_upper_dx, motion_threshold);
    if (lower_sign != 0 && lower_sign == upper_sign) {
        return lower_sign;
    }
    return 0;
}

bool EdgeHasStrongCurveSignal(int lower_mid_dx,
                              int mid_upper_dx,
                              float curvature,
                              const port::SceneWideClassifierParameters& wide) {
    return AbsFloat(curvature) >= static_cast<float>(wide.edge_curvature_min_px) ||
           HasDirectionTurn(lower_mid_dx, mid_upper_dx, wide.edge_motion_min_px);
}

bool EdgeLooksStraight(bool visible_confident,
                       float border_touch_ratio,
                       float curvature,
                       const port::SceneWideClassifierParameters& wide) {
    return visible_confident &&
           border_touch_ratio <= static_cast<float>(wide.opposite_edge_border_touch_max_ratio) &&
           AbsFloat(curvature) <= static_cast<float>(wide.opposite_edge_straight_max_curvature_px);
}

void FinalizeBandMetrics(BandAccumulator& band, int& left, int& right, int& width, int& valid_rows) {
    valid_rows = static_cast<int>(band.widths.size());
    left = Median(band.lefts);
    right = Median(band.rights);
    width = Median(band.widths);
}

}  // namespace

LaneMetrics ExtractLaneMetrics(const port::LegacyCameraFrame& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               int prior_reference_col) {
    LaneMetrics metrics{};
    metrics.threshold = threshold;
    metrics.steering_reference_col = std::clamp(prior_reference_col, 0, std::max(0, frame.width - 1));
    metrics.lateral_error =
        static_cast<float>(frame.width / 2.0F - static_cast<float>(metrics.steering_reference_col));

    if (frame.width <= 0 || frame.height <= 0) {
        return metrics;
    }

    const port::SceneWideClassifierParameters& wide = params.scene_wide_classifier;
    const int scan_top =
        ClampRow(std::min(static_cast<int>(std::lround(params.see_max)), wide.upper_row_start), frame.height);
    const int scan_bottom = ClampRow(wide.lower_row_end, frame.height);
    const int lower_start = ClampRow(wide.lower_row_start, frame.height);
    const int lower_end = ClampRow(wide.lower_row_end, frame.height);
    const int middle_start = ClampRow(wide.middle_row_start, frame.height);
    const int middle_end = ClampRow(wide.middle_row_end, frame.height);
    const int upper_start = ClampRow(wide.upper_row_start, frame.height);
    const int upper_end = ClampRow(wide.upper_row_end, frame.height);
    const int row_step = std::max(1, wide.row_step);

    const int image_center = frame.width / 2;
    const int edge_margin_px = std::max(0, wide.edge_margin_px);
    const float full_span_ratio_threshold =
        static_cast<float>(std::clamp(wide.upper_full_span_width_ratio, 0.0, 1.0));

    BandAccumulator lower_band{};
    BandAccumulator middle_band{};
    BandAccumulator upper_band{};

    bool bottom_seen = false;
    bool previous_primary_valid = false;
    RowRun previous_primary{};
    double weighted_center_sum = 0.0;
    double weight_sum = 0.0;
    metrics.highest_line = 0;
    metrics.farthest_line = 0;

    for (int row = scan_bottom; row >= scan_top; row -= row_step) {
        int transitions = 0;
        const std::vector<RowRun> runs = ExtractRuns(frame, row, threshold, transitions);
        if (runs.empty()) {
            continue;
        }

        RowRun primary{};
        if (RowInBand(row, lower_start, lower_end)) {
            primary = PickLowerPrimaryRun(
                runs, metrics.steering_reference_col, image_center, frame.width, edge_margin_px);
        } else {
            primary = PickUpperPrimaryRun(runs,
                                          previous_primary,
                                          previous_primary_valid,
                                          metrics.steering_reference_col,
                                          frame.width,
                                          edge_margin_px);
        }

        if (primary.width <= 0) {
            continue;
        }

        ++metrics.valid_row_count;
        metrics.highest_line = metrics.highest_line == 0 ? row : std::min(metrics.highest_line, row);
        if (primary.width >= frame.width / 6) {
            metrics.farthest_line = metrics.farthest_line == 0 ? row : std::min(metrics.farthest_line, row);
        }
        if (!bottom_seen) {
            bottom_seen = true;
            metrics.lane_width_bottom = primary.width;
            metrics.transitions_bottom = transitions;
            metrics.left_edge_missing_bottom = primary.left > frame.width / 3;
            metrics.right_edge_missing_bottom = primary.right < (frame.width * 2) / 3;
        }

        const double upward_bias =
            static_cast<double>(scan_bottom - row) / static_cast<double>(std::max(1, scan_bottom - scan_top));
        const double weight = 1.0 + upward_bias * 1.8;
        weighted_center_sum += static_cast<double>(primary.center) * weight;
        weight_sum += weight;

        const bool full_span =
            static_cast<float>(primary.width) / static_cast<float>(frame.width) >= full_span_ratio_threshold;
        if (RowInBand(row, lower_start, lower_end)) {
            lower_band.Add(primary, full_span, frame.width, edge_margin_px);
        } else if (RowInBand(row, middle_start, middle_end)) {
            middle_band.Add(primary, full_span, frame.width, edge_margin_px);
        } else if (RowInBand(row, upper_start, upper_end)) {
            upper_band.Add(primary, full_span, frame.width, edge_margin_px);
        }

        previous_primary = primary;
        previous_primary_valid = true;
    }

    if (weight_sum > 0.0) {
        metrics.steering_reference_col = static_cast<int>(std::lround(weighted_center_sum / weight_sum));
        metrics.lateral_error =
            static_cast<float>(frame.width / 2.0 - static_cast<double>(metrics.steering_reference_col));
    }
    if (metrics.farthest_line == 0) {
        metrics.farthest_line = metrics.highest_line;
    }

    FinalizeBandMetrics(lower_band,
                        metrics.lower_left,
                        metrics.lower_right,
                        metrics.lower_width_median,
                        metrics.lower_valid_row_count);
    FinalizeBandMetrics(middle_band,
                        metrics.middle_left,
                        metrics.middle_right,
                        metrics.middle_width_median,
                        metrics.middle_valid_row_count);
    FinalizeBandMetrics(upper_band,
                        metrics.upper_left,
                        metrics.upper_right,
                        metrics.upper_width_median,
                        metrics.upper_valid_row_count);

    metrics.upper_full_span_ratio = SafeRatio(upper_band.full_span_rows, metrics.upper_valid_row_count);
    metrics.upper_full_span_consecutive_rows_max = upper_band.full_span_consecutive_rows_max;
    metrics.left_upper_border_touch_ratio =
        SafeRatio(upper_band.left_border_touch_rows, metrics.upper_valid_row_count);
    metrics.right_upper_border_touch_ratio =
        SafeRatio(upper_band.right_border_touch_rows, metrics.upper_valid_row_count);
    metrics.left_open = static_cast<float>(metrics.lower_left - metrics.upper_left);
    metrics.right_open = static_cast<float>(metrics.upper_right - metrics.lower_right);
    metrics.left_contract = static_cast<float>(metrics.upper_left - metrics.lower_left);
    metrics.right_contract = static_cast<float>(metrics.lower_right - metrics.upper_right);
    metrics.left_dx_lower_mid = metrics.middle_left - metrics.lower_left;
    metrics.left_dx_mid_upper = metrics.upper_left - metrics.middle_left;
    metrics.right_dx_lower_mid = metrics.middle_right - metrics.lower_right;
    metrics.right_dx_mid_upper = metrics.upper_right - metrics.middle_right;
    metrics.left_curvature =
        static_cast<float>(metrics.left_dx_mid_upper - metrics.left_dx_lower_mid);
    metrics.right_curvature =
        static_cast<float>(metrics.right_dx_mid_upper - metrics.right_dx_lower_mid);
    metrics.left_visible_confident =
        !metrics.left_edge_missing_bottom && metrics.middle_valid_row_count > 0 &&
        metrics.upper_valid_row_count >= 2;
    metrics.right_visible_confident =
        !metrics.right_edge_missing_bottom && metrics.middle_valid_row_count > 0 &&
        metrics.upper_valid_row_count >= 2;

    const float normalized_error =
        std::abs(metrics.lateral_error) / static_cast<float>(std::max(1, frame.width / 4));
    metrics.bend_severity = normalized_error;
    if (metrics.left_edge_missing_bottom) {
        metrics.bend_severity += 0.75F;
    }
    if (metrics.right_edge_missing_bottom) {
        metrics.bend_severity += 0.75F;
    }
    if (metrics.valid_row_count < 12) {
        metrics.bend_severity += 0.35F;
    }

    metrics.zebra_candidate =
        metrics.transitions_bottom >= 8 && metrics.lane_width_bottom >= frame.width / 3;
    metrics.cross_candidate =
        HasCrossUpperFullSpanStructure(SteeringSceneContext{frame, params, port::LegacySteeringState{}, metrics});
    return metrics;
}

bool HasCrossUpperFullSpanStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return context.metrics.upper_full_span_consecutive_rows_max >=
           wide.cross_upper_full_span_consec_rows_min;
}

bool HasCircleLeftEntryStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return EdgeHasStrongCurveSignal(context.metrics.left_dx_lower_mid,
                                    context.metrics.left_dx_mid_upper,
                                    context.metrics.left_curvature,
                                    wide) &&
           EdgeLooksStraight(context.metrics.right_visible_confident,
                             context.metrics.right_upper_border_touch_ratio,
                             context.metrics.right_curvature,
                             wide);
}

bool HasCircleRightEntryStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return EdgeHasStrongCurveSignal(context.metrics.right_dx_lower_mid,
                                    context.metrics.right_dx_mid_upper,
                                    context.metrics.right_curvature,
                                    wide) &&
           EdgeLooksStraight(context.metrics.left_visible_confident,
                             context.metrics.left_upper_border_touch_ratio,
                             context.metrics.left_curvature,
                             wide);
}

bool LooksLikeOrdinaryBend(const SteeringSceneContext& context) {
    if (HasCrossUpperFullSpanStructure(context)) {
        return false;
    }

    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const bool left_curve = EdgeHasStrongCurveSignal(context.metrics.left_dx_lower_mid,
                                                     context.metrics.left_dx_mid_upper,
                                                     context.metrics.left_curvature,
                                                     wide);
    const bool right_curve = EdgeHasStrongCurveSignal(context.metrics.right_dx_lower_mid,
                                                      context.metrics.right_dx_mid_upper,
                                                      context.metrics.right_curvature,
                                                      wide);
    const bool left_straight = EdgeLooksStraight(context.metrics.left_visible_confident,
                                                 context.metrics.left_upper_border_touch_ratio,
                                                 context.metrics.left_curvature,
                                                 wide);
    const bool right_straight = EdgeLooksStraight(context.metrics.right_visible_confident,
                                                  context.metrics.right_upper_border_touch_ratio,
                                                  context.metrics.right_curvature,
                                                  wide);
    if (left_curve && right_curve) {
        return true;
    }
    if (left_curve && !right_straight) {
        return true;
    }
    if (right_curve && !left_straight) {
        return true;
    }

    const int left_motion_sign =
        DominantMotionSign(context.metrics.left_dx_lower_mid,
                           context.metrics.left_dx_mid_upper,
                           wide.edge_motion_min_px);
    const int right_motion_sign =
        DominantMotionSign(context.metrics.right_dx_lower_mid,
                           context.metrics.right_dx_mid_upper,
                           wide.edge_motion_min_px);
    return left_motion_sign != 0 && left_motion_sign == right_motion_sign &&
           !HasCircleLeftEntryStructure(context) && !HasCircleRightEntryStructure(context);
}

bool MeetsSpecialWidePrecondition(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    if (context.metrics.zebra_candidate) {
        return false;
    }
    if (context.metrics.valid_row_count < wide.special_wide_valid_rows_min) {
        return false;
    }
    if (context.metrics.lower_valid_row_count == 0) {
        return false;
    }
    const float lower_width_ratio =
        static_cast<float>(context.metrics.lower_width_median) /
        static_cast<float>(std::max(1, context.frame.width));
    if (lower_width_ratio < static_cast<float>(wide.special_wide_lower_width_min_ratio)) {
        return false;
    }

    const bool cross_precondition = HasCrossUpperFullSpanStructure(context);
    const bool circle_precondition =
        HasCircleLeftEntryStructure(context) || HasCircleRightEntryStructure(context);
    if (!cross_precondition && !circle_precondition) {
        return false;
    }
    if (LooksLikeOrdinaryBend(context)) {
        return false;
    }
    return true;
}

}  // namespace ls2k::legacy
