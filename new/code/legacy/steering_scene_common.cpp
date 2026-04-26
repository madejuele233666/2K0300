#include "legacy/steering_scene_common.hpp"

#include <algorithm>
#include <array>
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

struct EdgeSample {
    int row = 0;
    int col = 0;
    EdgeObservationState state = EdgeObservationState::missing;
};

struct LinearFit {
    bool valid = false;
    float slope = 0.0F;
    float intercept = 0.0F;
};

constexpr std::array<float, port::kLaneGeometryAnchorCount> kAnchorQuantiles{{0.2F, 0.5F, 0.8F}};

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

float ClampUnit(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

int SignWithThreshold(float value, int threshold) {
    if (value >= static_cast<float>(threshold)) {
        return 1;
    }
    if (value <= -static_cast<float>(threshold)) {
        return -1;
    }
    return 0;
}

bool HasDirectionTurn(float lower_mid_dx, float mid_upper_dx, int motion_threshold) {
    const int lower_sign = SignWithThreshold(lower_mid_dx, motion_threshold);
    const int upper_sign = SignWithThreshold(mid_upper_dx, motion_threshold);
    return lower_sign != 0 && upper_sign != 0 && lower_sign != upper_sign;
}

int DominantMotionSign(float lower_mid_dx, float mid_upper_dx, int motion_threshold) {
    const float total_dx = lower_mid_dx + mid_upper_dx;
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

bool EdgeHasCircleCurveSignal(float lower_mid_dx,
                              float mid_upper_dx,
                              float curvature,
                              const port::SceneWideClassifierParameters& wide) {
    return AbsFloat(curvature) >= static_cast<float>(wide.edge_curvature_min_px) ||
           HasDirectionTurn(lower_mid_dx, mid_upper_dx, wide.edge_motion_min_px);
}

bool EdgeHasBendCurveSignal(float lower_mid_dx,
                            float mid_upper_dx,
                            float curvature,
                            const port::SceneWideClassifierParameters& wide) {
    return EdgeHasCircleCurveSignal(lower_mid_dx, mid_upper_dx, curvature, wide);
}

bool AnchorAvailable(const EdgeAnchor& anchor) {
    return anchor.state != EdgeObservationState::missing || anchor.current_frame_extrapolated ||
           anchor.history_fallback;
}

void FinalizeBandMetrics(BandAccumulator& band, int& left, int& right, int& width, int& valid_rows) {
    valid_rows = static_cast<int>(band.widths.size());
    left = Median(band.lefts);
    right = Median(band.rights);
    width = Median(band.widths);
}

std::array<int, port::kLaneGeometryAnchorCount> BuildTargetRows(std::vector<int> valid_rows) {
    std::array<int, port::kLaneGeometryAnchorCount> target_rows{};
    if (valid_rows.empty()) {
        return target_rows;
    }
    std::sort(valid_rows.begin(), valid_rows.end());
    for (std::size_t i = 0; i < kAnchorQuantiles.size(); ++i) {
        const float quantile = std::clamp(kAnchorQuantiles[i], 0.0F, 1.0F);
        const std::size_t index =
            static_cast<std::size_t>(std::lround(quantile * static_cast<float>(valid_rows.size() - 1)));
        target_rows[i] = valid_rows[index];
    }
    return target_rows;
}

EdgeAnchor PickNearestObservedAnchor(const std::vector<EdgeSample>& samples, int target_row) {
    EdgeAnchor anchor{};
    anchor.row = target_row;
    if (samples.empty()) {
        return anchor;
    }

    const EdgeSample* best = nullptr;
    int best_distance = 0;
    for (const EdgeSample& sample : samples) {
        const int distance = std::abs(sample.row - target_row);
        if (best == nullptr || distance < best_distance) {
            best = &sample;
            best_distance = distance;
        }
    }
    if (best == nullptr) {
        return anchor;
    }

    anchor.col = best->col;
    anchor.state = best->state;
    return anchor;
}

LinearFit FitVisibleTrend(const std::vector<EdgeSample>& samples) {
    double row_sum = 0.0;
    double col_sum = 0.0;
    double row_sq_sum = 0.0;
    double row_col_sum = 0.0;
    int count = 0;
    for (const EdgeSample& sample : samples) {
        if (sample.state != EdgeObservationState::visible) {
            continue;
        }
        row_sum += static_cast<double>(sample.row);
        col_sum += static_cast<double>(sample.col);
        row_sq_sum += static_cast<double>(sample.row) * static_cast<double>(sample.row);
        row_col_sum += static_cast<double>(sample.row) * static_cast<double>(sample.col);
        ++count;
    }
    if (count < 2) {
        return {};
    }

    const double denom = static_cast<double>(count) * row_sq_sum - row_sum * row_sum;
    if (std::abs(denom) < 1e-6) {
        return {};
    }

    LinearFit fit{};
    fit.valid = true;
    fit.slope = static_cast<float>((static_cast<double>(count) * row_col_sum - row_sum * col_sum) / denom);
    fit.intercept = static_cast<float>((col_sum - static_cast<double>(fit.slope) * row_sum) /
                                       static_cast<double>(count));
    return fit;
}

int PredictCol(const LinearFit& fit, int row, int frame_width) {
    const float predicted = fit.slope * static_cast<float>(row) + fit.intercept;
    return std::clamp(static_cast<int>(std::lround(predicted)), 0, std::max(0, frame_width - 1));
}

void ComputeAnchorShape(const std::array<EdgeAnchor, port::kLaneGeometryAnchorCount>& anchors,
                        int motion_threshold,
                        float& lower_mid_dx,
                        float& mid_upper_dx,
                        float& curvature,
                        int& motion_sign) {
    const bool upper_valid = AnchorAvailable(anchors[0]);
    const bool mid_valid = AnchorAvailable(anchors[1]);
    const bool lower_valid = AnchorAvailable(anchors[2]);
    lower_mid_dx = 0.0F;
    mid_upper_dx = 0.0F;
    curvature = 0.0F;
    motion_sign = 0;

    if (upper_valid && mid_valid && lower_valid) {
        lower_mid_dx = static_cast<float>(anchors[1].col - anchors[2].col);
        mid_upper_dx = static_cast<float>(anchors[0].col - anchors[1].col);
        curvature = mid_upper_dx - lower_mid_dx;
        motion_sign = DominantMotionSign(lower_mid_dx, mid_upper_dx, motion_threshold);
        return;
    }

    if (upper_valid && lower_valid) {
        motion_sign = SignWithThreshold(static_cast<float>(anchors[0].col - anchors[2].col), motion_threshold);
    } else if (mid_valid && lower_valid) {
        lower_mid_dx = static_cast<float>(anchors[1].col - anchors[2].col);
        motion_sign = SignWithThreshold(lower_mid_dx, motion_threshold);
    } else if (upper_valid && mid_valid) {
        mid_upper_dx = static_cast<float>(anchors[0].col - anchors[1].col);
        motion_sign = SignWithThreshold(mid_upper_dx, motion_threshold);
    }
}

LaneEdgeMetrics BuildEdgeMetrics(const std::vector<EdgeSample>& samples,
                                 const std::array<int, port::kLaneGeometryAnchorCount>& target_rows,
                                 const port::LegacySteeringState& prior_state,
                                 bool use_left_side,
                                 int frame_width,
                                 const port::SceneWideClassifierParameters& wide) {
    LaneEdgeMetrics edge{};
    for (std::size_t i = 0; i < target_rows.size(); ++i) {
        edge.observed_anchors[i] = PickNearestObservedAnchor(samples, target_rows[i]);
        if (edge.observed_anchors[i].state == EdgeObservationState::visible) {
            ++edge.visible_anchor_count;
        } else if (edge.observed_anchors[i].state == EdgeObservationState::border_truncated) {
            ++edge.truncated_anchor_count;
        }
        if (edge.observed_anchors[i].state != EdgeObservationState::missing) {
            ++edge.observed_anchor_count;
        }
        edge.bend_anchors[i] = edge.observed_anchors[i];
    }

    ComputeAnchorShape(edge.observed_anchors,
                       wide.edge_motion_min_px,
                       edge.observed_lower_mid_dx,
                       edge.observed_mid_upper_dx,
                       edge.observed_curvature,
                       edge.observed_motion_sign);

    edge.strict_straight = edge.visible_anchor_count == port::kLaneGeometryAnchorCount &&
                           edge.truncated_anchor_count == 0 &&
                           AbsFloat(edge.observed_curvature) <=
                               static_cast<float>(wide.opposite_edge_straight_max_curvature_px);
    edge.circle_curve = EdgeHasCircleCurveSignal(
        edge.observed_lower_mid_dx, edge.observed_mid_upper_dx, edge.observed_curvature, wide);

    const LinearFit fit = FitVisibleTrend(samples);
    for (std::size_t i = 0; i < edge.bend_anchors.size(); ++i) {
        EdgeAnchor& anchor = edge.bend_anchors[i];
        if (anchor.state == EdgeObservationState::visible) {
            continue;
        }

        anchor = {};
        anchor.row = target_rows[i];
        if (fit.valid) {
            anchor.col = PredictCol(fit, anchor.row, frame_width);
            anchor.state = edge.observed_anchors[i].state == EdgeObservationState::border_truncated
                               ? EdgeObservationState::border_truncated
                               : EdgeObservationState::missing;
            anchor.current_frame_extrapolated = true;
            continue;
        }

        const auto* recent = use_left_side ? &prior_state.lane_geometry_recent.left_visible_anchors
                                           : &prior_state.lane_geometry_recent.right_visible_anchors;
        const auto* previous = use_left_side ? &prior_state.lane_geometry_previous.left_visible_anchors
                                             : &prior_state.lane_geometry_previous.right_visible_anchors;
        if ((*recent)[i].valid) {
            anchor.col = (*recent)[i].col;
            anchor.state = EdgeObservationState::visible;
            anchor.history_fallback = true;
        } else if ((*previous)[i].valid) {
            anchor.col = (*previous)[i].col;
            anchor.state = EdgeObservationState::visible;
            anchor.history_fallback = true;
        }
    }

    for (const EdgeAnchor& anchor : edge.bend_anchors) {
        if (!AnchorAvailable(anchor)) {
            continue;
        }
        ++edge.bend_anchor_count;
        if (anchor.current_frame_extrapolated) {
            ++edge.current_frame_extrapolated_anchor_count;
        }
        if (anchor.history_fallback) {
            ++edge.history_fallback_anchor_count;
        }
    }

    ComputeAnchorShape(edge.bend_anchors,
                       wide.edge_motion_min_px,
                       edge.bend_lower_mid_dx,
                       edge.bend_mid_upper_dx,
                       edge.bend_curvature,
                       edge.bend_motion_sign);
    edge.bend_curve = EdgeHasBendCurveSignal(
        edge.bend_lower_mid_dx, edge.bend_mid_upper_dx, edge.bend_curvature, wide);
    return edge;
}

port::LaneGeometryHistorySnapshot BuildHistorySnapshot(const LaneMetrics& metrics) {
    port::LaneGeometryHistorySnapshot snapshot{};
    for (std::size_t i = 0; i < port::kLaneGeometryAnchorCount; ++i) {
        const EdgeAnchor& left = metrics.left_edge.observed_anchors[i];
        if (left.state == EdgeObservationState::visible) {
            snapshot.left_visible_anchors[i].valid = true;
            snapshot.left_visible_anchors[i].row = left.row;
            snapshot.left_visible_anchors[i].col = left.col;
            snapshot.valid = true;
        }
        const EdgeAnchor& right = metrics.right_edge.observed_anchors[i];
        if (right.state == EdgeObservationState::visible) {
            snapshot.right_visible_anchors[i].valid = true;
            snapshot.right_visible_anchors[i].row = right.row;
            snapshot.right_visible_anchors[i].col = right.col;
            snapshot.valid = true;
        }
    }
    return snapshot;
}

port::TrackHistorySnapshot BuildTrackHistorySnapshotImpl(const LaneMetrics& metrics,
                                                         const port::LegacySteeringState& prior_state) {
    if (!metrics.track_valid) {
        port::TrackHistorySnapshot snapshot = prior_state.track_history;
        if (snapshot.valid) {
            snapshot.track_confidence *= 0.85F;
        }
        snapshot.flip_candidate_sign = metrics.track_flip_candidate_sign;
        snapshot.flip_candidate_frames = metrics.track_flip_candidate_frames;
        return snapshot;
    }

    port::TrackHistorySnapshot snapshot{};
    snapshot.valid = true;
    snapshot.center_anchors = metrics.track_center_anchors;
    snapshot.lane_width_px = metrics.tracked_lane_width_px;
    snapshot.heading_px_per_row = metrics.heading_error;
    snapshot.curvature_px_per_row2 = metrics.curvature;
    snapshot.turn_sign = metrics.track_sign;
    snapshot.last_nonzero_turn_sign =
        metrics.track_sign != 0 ? metrics.track_sign : prior_state.track_history.last_nonzero_turn_sign;
    snapshot.zero_turn_sign_frames =
        metrics.track_sign == 0 ? prior_state.track_history.zero_turn_sign_frames + 1 : 0;
    snapshot.track_confidence = metrics.track_confidence;
    snapshot.flip_candidate_sign = metrics.track_flip_candidate_sign;
    snapshot.flip_candidate_frames = metrics.track_flip_candidate_frames;
    return snapshot;
}

}  // namespace

port::TrackHistorySnapshot BuildTrackHistorySnapshot(const LaneMetrics& metrics,
                                                     const port::LegacySteeringState& prior_state) {
    return BuildTrackHistorySnapshotImpl(metrics, prior_state);
}

LaneMetrics ExtractLaneMetrics(const port::LegacyCameraFrame& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               const port::LegacySteeringState& prior_state,
                               const port::ImuSample& imu,
                               uint64_t capture_time_ms) {
    LaneMetrics metrics{};
    metrics.threshold = threshold;
    metrics.steering_reference_col =
        std::clamp(prior_state.steering_reference_col, 0, std::max(0, frame.width - 1));
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

    const int edge_margin_px = std::max(0, wide.edge_margin_px);
    const float full_span_ratio_threshold =
        static_cast<float>(std::clamp(wide.upper_full_span_width_ratio, 0.0, 1.0));
    const GyroContinuityConstraint continuity =
        ComputeGyroContinuityConstraint(prior_state, imu, capture_time_ms);
    const BottomTrackResult track = TrackBottomConnectedLane(
        BottomTrackRequest{frame, threshold, scan_top, scan_bottom, row_step, prior_state, continuity});

    BandAccumulator lower_band{};
    BandAccumulator middle_band{};
    BandAccumulator upper_band{};
    std::vector<int> valid_rows{};
    std::vector<EdgeSample> left_samples{};
    std::vector<EdgeSample> right_samples{};

    metrics.highest_line = 0;
    metrics.farthest_line = 0;
    metrics.track_valid = track.valid;
    metrics.track_seed_col = track.seed_col;
    metrics.track_seed_score = track.seed_score;
    metrics.track_confidence = track.track_confidence;
    metrics.heading_error = track.heading_error;
    metrics.curvature = track.curvature;
    metrics.gyro_heading_delta_deg = track.gyro_heading_delta_deg;
    metrics.gyro_consistency_score = track.gyro_consistency_score;
    metrics.track_sign = track.track_sign;
    metrics.sign_flip_blocked = track.sign_flip_blocked;
    metrics.imu_grace_active = track.imu_grace_active;
    metrics.track_source = ToString(track.source);
    metrics.track_center_anchors = track.center_anchors;
    metrics.tracked_lane_width_px = track.lane_width_px;
    metrics.track_flip_candidate_sign = track.flip_candidate_sign;
    metrics.track_flip_candidate_frames = track.flip_candidate_frames;
    metrics.lateral_error = track.lateral_error;
    metrics.steering_reference_col =
        std::clamp(static_cast<int>(std::lround(frame.width / 2.0F - track.lateral_error)),
                   0,
                   std::max(0, frame.width - 1));

    for (const TrackRowObservation& row : track.rows) {
        if (!row.valid || row.width <= 0) {
            continue;
        }
        ++metrics.valid_row_count;
        metrics.highest_line = metrics.highest_line == 0 ? row.row : std::min(metrics.highest_line, row.row);
        if (row.row_confidence >= 0.55F) {
            metrics.farthest_line = metrics.farthest_line == 0 ? row.row : std::min(metrics.farthest_line, row.row);
        }
    }

    bool strict_bottom_seen = false;
    bool previous_primary_valid = false;
    RowRun previous_primary{};
    int strict_valid_row_count = 0;
    for (int row = scan_bottom; row >= scan_top; row -= row_step) {
        int transitions = 0;
        const std::vector<RowRun> runs = ExtractRuns(frame, row, threshold, transitions);
        if (runs.empty()) {
            continue;
        }

        RowRun primary{};
        if (RowInBand(row, lower_start, lower_end)) {
            primary = PickLowerPrimaryRun(
                runs, metrics.steering_reference_col, frame.width / 2, frame.width, edge_margin_px);
        } else {
            primary = PickUpperPrimaryRun(
                runs, previous_primary, previous_primary_valid, metrics.steering_reference_col, frame.width, edge_margin_px);
        }
        if (primary.width <= 0) {
            continue;
        }

        valid_rows.push_back(row);
        ++strict_valid_row_count;
        if (!strict_bottom_seen) {
            strict_bottom_seen = true;
            metrics.lane_width_bottom = primary.width;
            metrics.transitions_bottom = transitions;
            metrics.left_edge_missing_bottom = primary.left > frame.width / 3;
            metrics.right_edge_missing_bottom = primary.right < (frame.width * 2) / 3;
        }

        left_samples.push_back(
            {row,
             primary.left,
             primary.left <= edge_margin_px ? EdgeObservationState::border_truncated
                                            : EdgeObservationState::visible});
        right_samples.push_back(
            {row,
             primary.right,
             primary.right >= frame.width - 1 - edge_margin_px ? EdgeObservationState::border_truncated
                                                               : EdgeObservationState::visible});

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
    metrics.valid_row_count = std::max(metrics.valid_row_count, strict_valid_row_count);
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

    const std::array<int, port::kLaneGeometryAnchorCount> target_rows = BuildTargetRows(valid_rows);
    metrics.left_edge = BuildEdgeMetrics(left_samples, target_rows, prior_state, true, frame.width, wide);
    metrics.right_edge = BuildEdgeMetrics(right_samples, target_rows, prior_state, false, frame.width, wide);

    metrics.left_dx_lower_mid = static_cast<int>(std::lround(metrics.left_edge.observed_lower_mid_dx));
    metrics.left_dx_mid_upper = static_cast<int>(std::lround(metrics.left_edge.observed_mid_upper_dx));
    metrics.right_dx_lower_mid = static_cast<int>(std::lround(metrics.right_edge.observed_lower_mid_dx));
    metrics.right_dx_mid_upper = static_cast<int>(std::lround(metrics.right_edge.observed_mid_upper_dx));
    metrics.left_curvature = metrics.left_edge.observed_curvature;
    metrics.right_curvature = metrics.right_edge.observed_curvature;
    metrics.left_visible_confident =
        !metrics.left_edge_missing_bottom && metrics.middle_valid_row_count > 0 &&
        metrics.upper_valid_row_count >= 2;
    metrics.right_visible_confident =
        !metrics.right_edge_missing_bottom && metrics.middle_valid_row_count > 0 &&
        metrics.upper_valid_row_count >= 2;

    if (AnchorAvailable(metrics.left_edge.observed_anchors[0]) &&
        AnchorAvailable(metrics.left_edge.observed_anchors[2])) {
        metrics.left_open = static_cast<float>(metrics.left_edge.observed_anchors[2].col -
                                               metrics.left_edge.observed_anchors[0].col);
        metrics.left_contract = -metrics.left_open;
    } else {
        metrics.left_open = static_cast<float>(metrics.lower_left - metrics.upper_left);
        metrics.left_contract = static_cast<float>(metrics.upper_left - metrics.lower_left);
    }
    if (AnchorAvailable(metrics.right_edge.observed_anchors[0]) &&
        AnchorAvailable(metrics.right_edge.observed_anchors[2])) {
        metrics.right_open = static_cast<float>(metrics.right_edge.observed_anchors[0].col -
                                                metrics.right_edge.observed_anchors[2].col);
        metrics.right_contract = -metrics.right_open;
    } else {
        metrics.right_open = static_cast<float>(metrics.upper_right - metrics.lower_right);
        metrics.right_contract = static_cast<float>(metrics.lower_right - metrics.upper_right);
    }

    metrics.same_direction_bend_sign = metrics.track_sign != 0 ? metrics.track_sign
                                                               : (metrics.left_edge.bend_motion_sign != 0 &&
                                                                          metrics.left_edge.bend_motion_sign ==
                                                                              metrics.right_edge.bend_motion_sign
                                                                      ? metrics.left_edge.bend_motion_sign
                                                                      : 0);
    metrics.bend_used_current_frame_extrapolation =
        metrics.left_edge.current_frame_extrapolated_anchor_count > 0 ||
        metrics.right_edge.current_frame_extrapolated_anchor_count > 0;
    metrics.bend_used_history_fallback = std::string(metrics.track_source) == "history_guarded" ||
                                         metrics.left_edge.history_fallback_anchor_count > 0 ||
                                         metrics.right_edge.history_fallback_anchor_count > 0;
    metrics.circle_left_inner_confidence = ComputeCircleEdgeConfidence(metrics.left_edge);
    metrics.circle_right_inner_confidence = ComputeCircleEdgeConfidence(metrics.right_edge);
    metrics.circle_left_opposite_straight_rows = metrics.right_edge.strict_straight
                                                     ? metrics.right_edge.visible_anchor_count
                                                     : 0;
    metrics.circle_right_opposite_straight_rows = metrics.left_edge.strict_straight
                                                      ? metrics.left_edge.visible_anchor_count
                                                      : 0;
    const port::CircleExitParameters& exit = params.circle_exit;
    const float exit_curvature_limit =
        static_cast<float>(std::max(1, exit.opposite_edge_max_curvature_px));
    if (metrics.circle_left_opposite_straight_rows > 0) {
        const float rows_score = ClampUnit(static_cast<float>(metrics.circle_left_opposite_straight_rows) /
                                           static_cast<float>(
                                               std::max(1, exit.opposite_edge_min_visible_rows)));
        const float curvature_score =
            ClampUnit(1.0F - AbsFloat(metrics.right_curvature) / exit_curvature_limit);
        metrics.circle_left_opposite_straight_confidence = rows_score * curvature_score;
    }
    if (metrics.circle_right_opposite_straight_rows > 0) {
        const float rows_score = ClampUnit(static_cast<float>(metrics.circle_right_opposite_straight_rows) /
                                           static_cast<float>(
                                               std::max(1, exit.opposite_edge_min_visible_rows)));
        const float curvature_score =
            ClampUnit(1.0F - AbsFloat(metrics.left_curvature) / exit_curvature_limit);
        metrics.circle_right_opposite_straight_confidence = rows_score * curvature_score;
    }
    if (track.valid && track.lane_width_px > 1.0F) {
        metrics.circle_reference_width_baseline = track.lane_width_px;
    } else if (metrics.middle_width_median > 0) {
        metrics.circle_reference_width_baseline = static_cast<float>(metrics.middle_width_median);
    } else {
        metrics.circle_reference_width_baseline = static_cast<float>(metrics.lower_width_median);
    }

    const float normalized_error =
        std::abs(metrics.lateral_error) / static_cast<float>(std::max(1, frame.width / 4));
    metrics.bend_severity = normalized_error + std::abs(metrics.heading_error) * 1.8F +
                            std::abs(metrics.curvature) * 1.2F;
    if (metrics.left_edge_missing_bottom) {
        metrics.bend_severity += 0.75F;
    }
    if (metrics.right_edge_missing_bottom) {
        metrics.bend_severity += 0.75F;
    }
    if (metrics.valid_row_count < 12) {
        metrics.bend_severity += 0.35F;
    }
    if (metrics.left_edge.bend_curve || metrics.right_edge.bend_curve) {
        metrics.bend_severity += 0.35F;
    }
    if (metrics.same_direction_bend_sign != 0) {
        metrics.bend_severity += 0.35F;
    }
    if (metrics.bend_used_history_fallback) {
        metrics.bend_severity += 0.1F;
    }

    metrics.zebra_candidate =
        metrics.transitions_bottom >= 8 && metrics.lane_width_bottom >= frame.width / 3;
    metrics.cross_candidate =
        HasCrossUpperFullSpanStructure(SteeringSceneContext{frame, params, prior_state, imu, capture_time_ms, metrics});
    metrics.lane_geometry_snapshot = BuildHistorySnapshot(metrics);
    return metrics;
}

bool HasCrossUpperFullSpanStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return context.metrics.upper_full_span_consecutive_rows_max >=
           wide.cross_upper_full_span_consec_rows_min;
}

bool HasCircleLeftEntryStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return context.metrics.left_edge.circle_curve && context.metrics.right_visible_confident &&
           context.metrics.right_upper_border_touch_ratio <=
               static_cast<float>(wide.opposite_edge_border_touch_max_ratio) &&
           std::abs(context.metrics.right_curvature) <=
               static_cast<float>(wide.opposite_edge_straight_max_curvature_px);
}

bool HasCircleRightEntryStructure(const SteeringSceneContext& context) {
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    return context.metrics.right_edge.circle_curve && context.metrics.left_visible_confident &&
           context.metrics.left_upper_border_touch_ratio <=
               static_cast<float>(wide.opposite_edge_border_touch_max_ratio) &&
           std::abs(context.metrics.left_curvature) <=
               static_cast<float>(wide.opposite_edge_straight_max_curvature_px);
}

bool LooksLikeOrdinaryBend(const SteeringSceneContext& context) {
    if (HasCrossUpperFullSpanStructure(context)) {
        return false;
    }

    if (context.metrics.track_valid && context.metrics.track_confidence >= 0.35F &&
        (std::abs(context.metrics.heading_error) >= 0.18F ||
         std::abs(context.metrics.curvature) >= 0.08F || context.metrics.track_sign != 0)) {
        return true;
    }

    const bool left_curve = context.metrics.left_edge.bend_curve;
    const bool right_curve = context.metrics.right_edge.bend_curve;
    if (left_curve && right_curve) {
        return true;
    }
    if (left_curve && !context.metrics.right_edge.strict_straight) {
        return true;
    }
    if (right_curve && !context.metrics.left_edge.strict_straight) {
        return true;
    }
    if (context.metrics.same_direction_bend_sign != 0) {
        return true;
    }
    return context.metrics.bend_used_history_fallback && (left_curve || right_curve);
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
    return true;
}

bool IsLeftCircleDirection(const std::string& direction) {
    return direction == "left";
}

CircleHeadingIntegrationResult IntegrateCircleHeadingDeltaDeg(float prior_heading_delta_deg,
                                                              uint64_t prior_capture_time_ms,
                                                              const port::ImuSample& imu,
                                                              uint64_t capture_time_ms,
                                                              bool reset_heading) {
    constexpr float kRadToDeg = 57.2957795F;

    CircleHeadingIntegrationResult result{};
    result.heading_delta_deg = reset_heading ? 0.0F : prior_heading_delta_deg;
    result.capture_time_ms = reset_heading ? (imu.valid ? capture_time_ms : 0) : prior_capture_time_ms;
    result.imu_invalid = !imu.valid;
    if (!imu.valid) {
        return result;
    }
    if (reset_heading) {
        return result;
    }
    if (prior_capture_time_ms == 0 || capture_time_ms <= prior_capture_time_ms) {
        result.capture_time_ms = capture_time_ms;
        return result;
    }

    const float dt_s = static_cast<float>(capture_time_ms - prior_capture_time_ms) * 0.001F;
    result.heading_delta_deg += std::abs(imu.gyro_z) * dt_s * kRadToDeg;
    result.capture_time_ms = capture_time_ms;
    return result;
}

int BlendSteeringReferenceCols(int from_col, int to_col, float to_weight) {
    const float weight = ClampUnit(to_weight);
    return static_cast<int>(std::lround(static_cast<float>(from_col) * (1.0F - weight) +
                                        static_cast<float>(to_col) * weight));
}

int BuildOffsetReferenceFromEdge(const LaneEdgeMetrics& edge,
                                 bool use_left_side,
                                 int near_offset_px,
                                 int far_offset_px,
                                 int fallback_col) {
    const auto& lower = edge.bend_anchors[2];
    const auto& upper = edge.bend_anchors[0];
    if (!AnchorAvailable(lower) && !AnchorAvailable(upper)) {
        return fallback_col;
    }

    const auto apply_offset = [use_left_side](int col, int offset) {
        return use_left_side ? col + offset : col - offset;
    };

    int weighted_sum = 0;
    int weight_total = 0;
    if (AnchorAvailable(lower)) {
        weighted_sum += 2 * apply_offset(lower.col, near_offset_px);
        weight_total += 2;
    }
    if (AnchorAvailable(upper)) {
        weighted_sum += apply_offset(upper.col, far_offset_px);
        weight_total += 1;
    }
    if (weight_total == 0) {
        return fallback_col;
    }
    return weighted_sum / weight_total;
}

float ComputeCircleEdgeConfidence(const LaneEdgeMetrics& edge) {
    float confidence =
        static_cast<float>(edge.visible_anchor_count) /
        static_cast<float>(std::max(1, port::kLaneGeometryAnchorCount));
    if (edge.history_fallback_anchor_count > 0) {
        confidence *= 0.75F;
    }
    if (edge.current_frame_extrapolated_anchor_count > 0) {
        confidence *= 0.85F;
    }
    return ClampUnit(confidence);
}

}  // namespace ls2k::legacy
