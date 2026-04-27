#include "legacy/steering_bottom_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace ls2k::legacy {
namespace {

struct RowRun {
    int left = 0;
    int right = 0;
    int width = 0;
    int center = 0;
};

struct SeedCandidate {
    int row = 0;
    RowRun run{};
    float score = -std::numeric_limits<float>::infinity();
};

struct TrackMeasurement {
    bool found = false;
    int white_col = 0;
    bool search_corrected = false;
    int transitions = 0;
    RowRun run{};
    bool left_visible = false;
    bool right_visible = false;
};

constexpr int kSeedRowCount = 5;
constexpr int kSearchCorrectionRadiusPx = 12;
constexpr int kSeedAdjacencyPx = 6;
constexpr float kWidthUpdateAlpha = 0.25F;
constexpr int kTurnSignZeroHoldFrames = 3;

int ClampCol(int col, int frame_width) {
    return std::clamp(col, 0, std::max(0, frame_width - 1));
}

int SignWithThreshold(float value, float threshold) {
    if (value >= threshold) {
        return 1;
    }
    if (value <= -threshold) {
        return -1;
    }
    return 0;
}

float LookaheadWeight(int row, int scan_top, int scan_bottom) {
    if (scan_bottom <= scan_top) {
        return 1.0F;
    }
    const float progress =
        std::clamp(static_cast<float>(scan_bottom - row) / static_cast<float>(scan_bottom - scan_top), 0.0F, 1.0F);
    return 1.0F + 1.2F * progress;
}

std::vector<RowRun> ExtractRuns(const port::LegacyCameraFrame& frame, int row, int threshold, int& transitions) {
    std::vector<RowRun> runs{};
    transitions = 0;
    if (row < 0 || row >= frame.height || frame.width <= 0) {
        return runs;
    }

    bool in_run = false;
    bool previous_above = false;
    bool previous_valid = false;
    int run_start = 0;
    for (int col = 0; col < frame.width; ++col) {
        const std::size_t index = static_cast<std::size_t>(row) * frame.width + col;
        const bool above = frame.gray[index] > threshold;
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

bool RunsOverlapOrTouch(const RowRun& lhs, const RowRun& rhs) {
    return std::max(lhs.left, rhs.left) <= std::min(lhs.right, rhs.right) + kSeedAdjacencyPx;
}

int FindNearestWhite(const port::LegacyCameraFrame& frame,
                     int row,
                     int search_center,
                     int threshold,
                     int radius_px,
                     bool& corrected) {
    corrected = false;
    if (row < 0 || row >= frame.height) {
        return -1;
    }

    const int clamped_center = ClampCol(search_center, frame.width);
    const std::size_t center_index = static_cast<std::size_t>(row) * frame.width + clamped_center;
    if (frame.gray[center_index] > threshold) {
        return clamped_center;
    }

    for (int offset = 1; offset <= radius_px; ++offset) {
        const int left = clamped_center - offset;
        if (left >= 0) {
            const std::size_t index = static_cast<std::size_t>(row) * frame.width + left;
            if (frame.gray[index] > threshold) {
                corrected = true;
                return left;
            }
        }
        const int right = clamped_center + offset;
        if (right < frame.width) {
            const std::size_t index = static_cast<std::size_t>(row) * frame.width + right;
            if (frame.gray[index] > threshold) {
                corrected = true;
                return right;
            }
        }
    }
    return -1;
}

TrackMeasurement MeasureRow(const port::LegacyCameraFrame& frame,
                            int row,
                            int threshold,
                            int search_center,
                            int search_half_window) {
    TrackMeasurement measurement{};
    measurement.transitions = 0;
    if (row < 0 || row >= frame.height || frame.width <= 0) {
        return measurement;
    }

    bool corrected = false;
    const int white_col = FindNearestWhite(frame, row, search_center, threshold, kSearchCorrectionRadiusPx, corrected);
    if (white_col < 0) {
        return measurement;
    }

    int left = white_col;
    int right = white_col;
    (void)search_half_window;
    while (left > 0) {
        const std::size_t index = static_cast<std::size_t>(row) * frame.width + (left - 1);
        if (frame.gray[index] <= threshold) {
            break;
        }
        --left;
    }
    while (right < frame.width - 1) {
        const std::size_t index = static_cast<std::size_t>(row) * frame.width + (right + 1);
        if (frame.gray[index] <= threshold) {
            break;
        }
        ++right;
    }

    const bool left_visible = left > 0 &&
                              frame.gray[static_cast<std::size_t>(row) * frame.width + (left - 1)] <= threshold;
    const bool right_visible =
        right < frame.width - 1 &&
        frame.gray[static_cast<std::size_t>(row) * frame.width + (right + 1)] <= threshold;

    int transitions = 0;
    (void)ExtractRuns(frame, row, threshold, transitions);

    measurement.found = true;
    measurement.white_col = white_col;
    measurement.search_corrected = corrected;
    measurement.transitions = transitions;
    measurement.run = {left, right, right - left + 1, (left + right) / 2};
    measurement.left_visible = left_visible;
    measurement.right_visible = right_visible;
    return measurement;
}

float ComputeWidthScore(int width, float prior_width, int max_seed_width) {
    if (width <= 0) {
        return 0.0F;
    }
    if (prior_width > 1.0F) {
        const float error = std::abs(static_cast<float>(width) - prior_width) / prior_width;
        return std::clamp(1.0F - error, 0.0F, 1.0F);
    }
    return std::clamp(static_cast<float>(width) / static_cast<float>(std::max(1, max_seed_width)), 0.0F, 1.0F);
}

float ComputeVerticalContinuityScore(const SeedCandidate& candidate,
                                     const std::vector<std::pair<int, std::vector<RowRun>>>& seed_rows) {
    if (seed_rows.empty()) {
        return 0.0F;
    }
    int supporting_rows = 0;
    for (const auto& [row, runs] : seed_rows) {
        if (row == candidate.row) {
            continue;
        }
        for (const RowRun& run : runs) {
            if (RunsOverlapOrTouch(candidate.run, run)) {
                ++supporting_rows;
                break;
            }
        }
    }
    return std::clamp(static_cast<float>(supporting_rows) /
                          static_cast<float>(std::max(1, static_cast<int>(seed_rows.size()) - 1)),
                      0.0F,
                      1.0F);
}

float ComputePriorPredictionScore(const SeedCandidate& candidate,
                                  const port::LegacySteeringState& prior_state,
                                  float predicted_center) {
    int reference_col = port::kCompiledCameraFrameWidth / 2;
    const auto& anchors = prior_state.track_history.center_anchors;
    if (prior_state.track_history.valid && anchors[2].valid) {
        reference_col = anchors[2].col;
    }
    const float target = predicted_center != 0.0F ? predicted_center : static_cast<float>(reference_col);
    const float distance = std::abs(static_cast<float>(candidate.run.center) - target);
    return std::clamp(1.0F - distance / 42.0F, 0.0F, 1.0F);
}

float BorderTouchPenalty(const RowRun& run, int frame_width) {
    int touches = 0;
    if (run.left <= 0) {
        ++touches;
    }
    if (run.right >= frame_width - 1) {
        ++touches;
    }
    return static_cast<float>(touches);
}

float PredictedCenterForRow(const port::LegacySteeringState& prior_state,
                            int row,
                            float gyro_prediction_offset_px) {
    int center = port::kCompiledCameraFrameWidth / 2;
    if (!prior_state.track_history.valid) {
        return static_cast<float>(center) + gyro_prediction_offset_px;
    }

    const auto& anchors = prior_state.track_history.center_anchors;
    for (int index = port::kLaneGeometryAnchorCount - 1; index >= 0; --index) {
        if (!anchors[static_cast<std::size_t>(index)].valid) {
            continue;
        }
        const float dy = static_cast<float>(row - anchors[static_cast<std::size_t>(index)].row);
        return static_cast<float>(anchors[static_cast<std::size_t>(index)].col) +
               prior_state.track_history.heading_px_per_row * dy +
               0.5F * prior_state.track_history.curvature_px_per_row2 * dy * dy + gyro_prediction_offset_px;
    }
    return static_cast<float>(center) + gyro_prediction_offset_px;
}

std::vector<std::pair<int, std::vector<RowRun>>> CollectSeedRows(const BottomTrackRequest& request) {
    std::vector<std::pair<int, std::vector<RowRun>>> seed_rows{};
    for (int row = request.scan_bottom;
         row >= request.scan_top && static_cast<int>(seed_rows.size()) < kSeedRowCount;
         row -= request.row_step) {
        int transitions = 0;
        std::vector<RowRun> runs = ExtractRuns(request.frame, row, request.threshold, transitions);
        if (!runs.empty()) {
            seed_rows.emplace_back(row, std::move(runs));
        }
    }
    return seed_rows;
}

SeedCandidate PickSeedCandidate(const BottomTrackRequest& request) {
    const std::vector<std::pair<int, std::vector<RowRun>>> seed_rows = CollectSeedRows(request);
    SeedCandidate best{};
    int max_seed_width = 1;
    for (const auto& [_, runs] : seed_rows) {
        for (const RowRun& run : runs) {
            max_seed_width = std::max(max_seed_width, run.width);
        }
    }

    for (const auto& [row, runs] : seed_rows) {
        for (const RowRun& run : runs) {
            SeedCandidate candidate{};
            candidate.row = row;
            candidate.run = run;
            const float width_score =
                ComputeWidthScore(run.width, request.prior_state.track_history.lane_width_px, max_seed_width);
            const float continuity_score = ComputeVerticalContinuityScore(candidate, seed_rows);
            const float prediction_score = ComputePriorPredictionScore(
                candidate,
                request.prior_state,
                PredictedCenterForRow(request.prior_state, row, request.continuity.prior_prediction_offset_px));
            const float border_penalty = BorderTouchPenalty(run, request.frame.width);
            candidate.score = 0.40F * width_score + 0.30F * continuity_score + 0.25F * prediction_score -
                              0.15F * border_penalty;
            if (candidate.score > best.score) {
                best = candidate;
            }
        }
    }
    return best;
}

port::LaneHistoryAnchor MakeHistoryAnchor(const TrackRowObservation& row) {
    port::LaneHistoryAnchor anchor{};
    anchor.valid = row.valid;
    anchor.row = row.row;
    anchor.col = row.center;
    return anchor;
}

int ConsecutiveSupportRows(const std::vector<TrackRowObservation>& rows, int candidate_sign) {
    if (candidate_sign == 0) {
        return 0;
    }

    int best = 0;
    int current = 0;
    bool have_previous = false;
    TrackRowObservation previous{};
    for (const TrackRowObservation& row : rows) {
        if (!row.valid || row.row_confidence < 0.70F) {
            current = 0;
            have_previous = false;
            continue;
        }
        if (!have_previous) {
            previous = row;
            current = 1;
            have_previous = true;
            best = std::max(best, current);
            continue;
        }

        const int row_delta = std::max(1, previous.row - row.row);
        const float slope = static_cast<float>(row.center - previous.center) / static_cast<float>(row_delta);
        if (SignWithThreshold(slope, 0.18F) == candidate_sign) {
            ++current;
        } else {
            current = 1;
        }
        previous = row;
        best = std::max(best, current);
    }
    return best;
}

float AverageSupportingRowConfidence(const std::vector<TrackRowObservation>& rows, int candidate_sign) {
    if (candidate_sign == 0) {
        return 0.0F;
    }

    double confidence_sum = 0.0;
    int confidence_count = 0;
    bool have_previous = false;
    TrackRowObservation previous{};
    for (const TrackRowObservation& row : rows) {
        if (!row.valid || row.row_confidence < 0.70F) {
            have_previous = false;
            continue;
        }
        if (!have_previous) {
            previous = row;
            have_previous = true;
            continue;
        }

        const int row_delta = std::max(1, previous.row - row.row);
        const float slope = static_cast<float>(row.center - previous.center) / static_cast<float>(row_delta);
        if (SignWithThreshold(slope, 0.18F) == candidate_sign) {
            confidence_sum += 0.5 * static_cast<double>(previous.row_confidence + row.row_confidence);
            ++confidence_count;
        }
        previous = row;
    }
    return confidence_count > 0 ? static_cast<float>(confidence_sum / static_cast<double>(confidence_count)) : 0.0F;
}

TrackRowObservation BuildHistoryGuardRow(int row, float width_prior, int center) {
    TrackRowObservation observation{};
    observation.row = row;
    observation.center = center;
    observation.width = static_cast<int>(std::lround(width_prior));
    observation.left = center - observation.width / 2;
    observation.right = observation.left + observation.width - 1;
    observation.valid = true;
    observation.history_guarded = true;
    observation.row_confidence = 0.20F;
    return observation;
}

void FinalizeTrackShape(BottomTrackResult& result, const BottomTrackRequest& request) {
    if (result.rows.empty()) {
        return;
    }

    double lateral_sum = 0.0;
    double lateral_weight_sum = 0.0;
    std::vector<std::pair<float, float>> slopes{};
    std::vector<TrackRowObservation> strong_rows{};
    double consistency_sum = 0.0;
    double consistency_weight_sum = 0.0;

    for (const TrackRowObservation& row : result.rows) {
        if (!row.valid) {
            continue;
        }
        const float lookahead = LookaheadWeight(row.row, request.scan_top, request.scan_bottom);
        const double center_error =
            static_cast<double>(request.frame.width / 2.0F - static_cast<double>(row.center));
        lateral_sum += center_error * static_cast<double>(row.final_weight);
        lateral_weight_sum += row.final_weight;
        consistency_sum += static_cast<double>(row.gyro_consistency_score) * row.row_confidence * lookahead;
        consistency_weight_sum += row.row_confidence * lookahead;
        if (row.row_confidence >= 0.55F) {
            strong_rows.push_back(row);
        }
    }

    for (std::size_t index = 1; index < strong_rows.size(); ++index) {
        const TrackRowObservation& lower = strong_rows[index - 1U];
        const TrackRowObservation& upper = strong_rows[index];
        const int row_delta = std::max(1, lower.row - upper.row);
        float weight = (lower.final_weight + upper.final_weight) * 0.5F;
        if (lower.width_completed || upper.width_completed) {
            weight *= 0.5F;
        }
        const float slope = static_cast<float>(upper.center - lower.center) / static_cast<float>(row_delta);
        slopes.emplace_back(slope, weight);
    }

    if (lateral_weight_sum > 0.0) {
        result.lateral_error = static_cast<float>(lateral_sum / lateral_weight_sum);
    }
    result.gyro_consistency_score =
        consistency_weight_sum > 0.0 ? static_cast<float>(consistency_sum / consistency_weight_sum) : 1.0F;

    double heading_sum = 0.0;
    double heading_weight_sum = 0.0;
    for (std::size_t index = 0; index < slopes.size(); ++index) {
        const float upper_bias =
            1.0F + 0.25F * static_cast<float>(index >= slopes.size() / 2U ? 1.0F : 0.0F);
        heading_sum += static_cast<double>(slopes[index].first) *
                       static_cast<double>(slopes[index].second * upper_bias);
        heading_weight_sum += slopes[index].second * upper_bias;
    }
    if (heading_weight_sum > 0.0) {
        result.heading_error = static_cast<float>(heading_sum / heading_weight_sum);
    }

    if (slopes.size() >= 3U) {
        double curvature_sum = 0.0;
        double curvature_weight_sum = 0.0;
        for (std::size_t index = 1; index < slopes.size(); ++index) {
            const float delta = slopes[index].first - slopes[index - 1U].first;
            const float weight = 0.5F * (slopes[index].second + slopes[index - 1U].second);
            curvature_sum += static_cast<double>(delta) * weight;
            curvature_weight_sum += weight;
        }
        if (curvature_weight_sum > 0.0) {
            result.curvature = static_cast<float>(curvature_sum / curvature_weight_sum);
        }
    }

    const int raw_sign = SignWithThreshold(result.heading_error, 0.18F);
    const int prior_sign =
        request.prior_state.track_history.valid ? EffectivePriorTurnSign(request.prior_state.track_history) : 0;
    const bool opposite_sign = raw_sign != 0 && prior_sign != 0 && raw_sign != prior_sign;
    int pending_sign = 0;
    int pending_frames = 0;
    if (opposite_sign) {
        const int support_rows = ConsecutiveSupportRows(strong_rows, raw_sign);
        const float average_row_confidence = AverageSupportingRowConfidence(strong_rows, raw_sign);
        const SignFlipDecision flip = EvaluateTrackSignFlip(
            {prior_sign,
             raw_sign,
             support_rows,
             average_row_confidence,
             request.prior_state.track_history.flip_candidate_sign,
             request.prior_state.track_history.flip_candidate_frames,
             request.continuity.imu_grace_active});
        pending_sign = flip.pending_sign;
        pending_frames = flip.pending_frames;
        if (flip.blocked) {
            result.sign_flip_blocked = true;
            result.track_sign = flip.resolved_sign;
            if (request.prior_state.track_history.valid && prior_sign != 0) {
                result.heading_error =
                    std::copysign(std::min(std::abs(result.heading_error),
                                           std::max(0.12F,
                                                    std::abs(request.prior_state.track_history.heading_px_per_row))),
                                  static_cast<float>(prior_sign));
                result.curvature =
                    std::copysign(std::min(std::abs(result.curvature),
                                           std::abs(request.prior_state.track_history.curvature_px_per_row2)),
                                  static_cast<float>(prior_sign));
            } else {
                result.heading_error = 0.0F;
                result.curvature = 0.0F;
            }
        }
    }

    if (!result.sign_flip_blocked) {
        result.track_sign = raw_sign;
        pending_sign = 0;
        pending_frames = 0;
    }

    const int effective_sign = result.track_sign;
    const int valid_strong_rows = static_cast<int>(strong_rows.size());
    result.track_confidence = std::clamp(static_cast<float>(valid_strong_rows) / 10.0F, 0.0F, 1.0F) *
                              std::clamp(result.gyro_consistency_score, 0.25F, 1.0F);
    result.valid = valid_strong_rows >= 4 && lateral_weight_sum > 0.0;

    if (!result.valid) {
        result.track_sign = 0;
    }

    if (request.continuity.imu_grace_active) {
        result.source = TrackSource::pure_visual_fallback;
    } else {
        bool used_history_guard = false;
        bool used_single_edge = false;
        int history_guard_rows = 0;
        for (const TrackRowObservation& row : result.rows) {
            used_history_guard = used_history_guard || row.history_guarded;
            used_single_edge = used_single_edge || row.width_completed;
            history_guard_rows += row.history_guarded ? 1 : 0;
        }
        if (used_history_guard && history_guard_rows >= std::max(3, valid_strong_rows)) {
            result.source = TrackSource::history_guarded;
        } else if (used_single_edge) {
            result.source = TrackSource::single_edge_completed;
        } else {
            result.source = TrackSource::bottom_connected;
        }
    }

    std::vector<TrackRowObservation> anchor_rows{};
    for (const TrackRowObservation& row : result.rows) {
        if (row.valid) {
            anchor_rows.push_back(row);
        }
    }
    if (!anchor_rows.empty()) {
        std::sort(anchor_rows.begin(), anchor_rows.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.row < rhs.row;
        });
        constexpr std::array<float, port::kLaneGeometryAnchorCount> kQuantiles{{0.2F, 0.5F, 0.8F}};
        for (std::size_t index = 0; index < kQuantiles.size(); ++index) {
            const std::size_t row_index =
                static_cast<std::size_t>(std::lround(kQuantiles[index] *
                                                     static_cast<float>(anchor_rows.size() - 1U)));
            result.center_anchors[index] = MakeHistoryAnchor(anchor_rows[row_index]);
        }
    }

    result.flip_candidate_sign = pending_sign;
    result.flip_candidate_frames = pending_frames;
    (void)effective_sign;
}

void TrackDirection(const BottomTrackRequest& request,
                    int start_row,
                    int end_row,
                    int step,
                    int frame_center,
                    float& width_estimate,
                    TrackRowObservation& previous_row,
                    BottomTrackResult& result) {
    for (int row = start_row;
         step > 0 ? row <= end_row : row >= end_row;
         row += step) {
        const float width_prior =
            width_estimate > 1.0F ? width_estimate
                                  : (request.prior_state.track_history.lane_width_px > 1.0F
                                         ? request.prior_state.track_history.lane_width_px
                                         : static_cast<float>(std::max(24, previous_row.width)));
        const int predicted_center = static_cast<int>(std::lround(
            PredictedCenterForRow(request.prior_state, row, request.continuity.prior_prediction_offset_px)));
        const int search_center =
            previous_row.valid ? previous_row.center : ClampCol(predicted_center, request.frame.width);
        const int search_half_window =
            std::clamp(static_cast<int>(std::lround(0.35F * width_prior)), 16, 36);
        const TrackMeasurement measurement =
            MeasureRow(request.frame, row, request.threshold, search_center, search_half_window);

        TrackRowObservation observation{};
        observation.row = row;
        observation.transitions = measurement.transitions;
        if (!measurement.found) {
            if (previous_row.valid && width_prior > 1.0F) {
                observation = BuildHistoryGuardRow(row, width_prior, previous_row.center);
                observation.gyro_consistency_score = 1.0F;
                observation.final_weight =
                    observation.row_confidence * LookaheadWeight(row, request.scan_top, request.scan_bottom);
                result.rows.push_back(observation);
                continue;
            }
            continue;
        }

        observation.left = measurement.run.left;
        observation.right = measurement.run.right;
        observation.width = measurement.run.width;
        observation.center = measurement.run.center;
        observation.left_visible = measurement.left_visible;
        observation.right_visible = measurement.right_visible;
        observation.search_corrected = measurement.search_corrected;
        observation.valid = true;

        if (!observation.left_visible && observation.right_visible && width_prior > 1.0F) {
            observation.width_completed = true;
            observation.width = static_cast<int>(std::lround(width_prior));
            observation.left = observation.right - observation.width + 1;
            observation.center = (observation.left + observation.right) / 2;
        } else if (observation.left_visible && !observation.right_visible && width_prior > 1.0F) {
            observation.width_completed = true;
            observation.width = static_cast<int>(std::lround(width_prior));
            observation.right = observation.left + observation.width - 1;
            observation.center = (observation.left + observation.right) / 2;
        } else if (!observation.left_visible && !observation.right_visible) {
            if (previous_row.valid && width_prior > 1.0F) {
                observation = BuildHistoryGuardRow(row, width_prior, previous_row.center);
            } else {
                continue;
            }
        }

        const int max_jump = std::max(22, static_cast<int>(std::lround(0.22F * width_prior)));
        const bool overlaps_previous =
            previous_row.valid && std::max(previous_row.left, observation.left) <=
                                      std::min(previous_row.right, observation.right);
        if (previous_row.valid && std::abs(observation.center - previous_row.center) > max_jump &&
            !overlaps_previous) {
            if (width_prior > 1.0F) {
                observation = BuildHistoryGuardRow(row, width_prior, previous_row.center);
            } else {
                continue;
            }
        }

        if (observation.history_guarded) {
            observation.gyro_consistency_score = 1.0F;
        } else {
            const int row_delta = std::max(1, std::abs(previous_row.row - observation.row));
            const float local_heading =
                previous_row.valid
                    ? static_cast<float>(observation.center - previous_row.center) / static_cast<float>(row_delta)
                    : static_cast<float>(observation.center - frame_center) / static_cast<float>(row_delta);
            observation.gyro_consistency_score = ComputeGyroConsistencyScore(local_heading, request.continuity);
            if (observation.width_completed) {
                observation.row_confidence = 0.65F;
            } else if (observation.search_corrected) {
                observation.row_confidence = 0.55F;
            } else {
                observation.row_confidence = 1.0F;
            }
        }

        const float lookahead = LookaheadWeight(row, request.scan_top, request.scan_bottom);
        observation.final_weight = observation.row_confidence * lookahead * observation.gyro_consistency_score;
        if (observation.left_visible && observation.right_visible && !observation.search_corrected) {
            if (width_estimate <= 1.0F) {
                width_estimate = static_cast<float>(observation.width);
            } else {
                width_estimate =
                    (1.0F - kWidthUpdateAlpha) * width_estimate + kWidthUpdateAlpha * observation.width;
            }
        }
        previous_row = observation;
        result.rows.push_back(observation);
    }
}

}  // namespace

SignFlipDecision EvaluateTrackSignFlip(const SignFlipDecisionRequest& request) {
    SignFlipDecision decision{};
    decision.resolved_sign = request.candidate_sign;
    if (request.candidate_sign == 0 || request.prior_sign == 0 || request.candidate_sign == request.prior_sign) {
        return decision;
    }
    if (request.supporting_rows < 4 || request.average_row_confidence < 0.70F) {
        decision.blocked = true;
        decision.resolved_sign = request.prior_sign;
        return decision;
    }

    decision.pending_sign = request.candidate_sign;
    decision.pending_frames = request.prior_candidate_sign == request.candidate_sign
                                  ? (request.prior_candidate_frames + 1)
                                  : 1;
    const int required_frames = request.imu_grace_active ? 3 : 2;
    if (decision.pending_frames < required_frames) {
        decision.blocked = true;
        decision.resolved_sign = request.prior_sign;
        return decision;
    }

    decision.pending_sign = 0;
    decision.pending_frames = 0;
    return decision;
}

int EffectivePriorTurnSign(const port::TrackHistorySnapshot& history) {
    if (history.turn_sign != 0) {
        return history.turn_sign;
    }
    if (history.zero_turn_sign_frames <= 0 || history.zero_turn_sign_frames > kTurnSignZeroHoldFrames) {
        return 0;
    }
    return history.last_nonzero_turn_sign;
}

BottomTrackResult TrackBottomConnectedLane(const BottomTrackRequest& request) {
    BottomTrackResult result{};
    result.imu_grace_active = request.continuity.imu_grace_active;
    result.gyro_heading_delta_deg = request.continuity.heading_delta_deg;

    const SeedCandidate seed = PickSeedCandidate(request);
    if (seed.score == -std::numeric_limits<float>::infinity()) {
        return result;
    }

    TrackRowObservation seed_row{};
    seed_row.row = seed.row;
    seed_row.left = seed.run.left;
    seed_row.right = seed.run.right;
    seed_row.center = seed.run.center;
    seed_row.width = seed.run.width;
    seed_row.valid = true;
    seed_row.left_visible = seed.run.left > 0;
    seed_row.right_visible = seed.run.right < request.frame.width - 1;
    seed_row.row_confidence = (seed_row.left_visible && seed_row.right_visible) ? 1.0F : 0.65F;
    seed_row.width_completed = !seed_row.left_visible || !seed_row.right_visible;
    seed_row.gyro_consistency_score = 1.0F;
    seed_row.final_weight =
        seed_row.row_confidence * LookaheadWeight(seed.row, request.scan_top, request.scan_bottom);

    result.seed_col = seed.run.center;
    result.seed_score = std::max(0.0F, seed.score);

    float width_estimate =
        request.prior_state.track_history.lane_width_px > 1.0F
            ? request.prior_state.track_history.lane_width_px
            : static_cast<float>(seed.run.width);
    TrackRowObservation cursor = seed_row;

    TrackDirection(request,
                   seed.row + request.row_step,
                   request.scan_bottom,
                   request.row_step,
                   request.frame.width / 2,
                   width_estimate,
                   cursor,
                   result);
    result.rows.push_back(seed_row);
    cursor = seed_row;
    TrackDirection(request,
                   seed.row - request.row_step,
                   request.scan_top,
                   -request.row_step,
                   request.frame.width / 2,
                   width_estimate,
                   cursor,
                   result);

    std::sort(result.rows.begin(), result.rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.row > rhs.row;
    });

    result.lane_width_px = width_estimate;
    FinalizeTrackShape(result, request);
    return result;
}

const char* ToString(TrackSource source) {
    switch (source) {
        case TrackSource::bottom_connected:
            return "bottom_connected";
        case TrackSource::single_edge_completed:
            return "single_edge_completed";
        case TrackSource::history_guarded:
            return "history_guarded";
        case TrackSource::pure_visual_fallback:
            return "pure_visual_fallback";
    }
    return "bottom_connected";
}

}  // namespace ls2k::legacy
