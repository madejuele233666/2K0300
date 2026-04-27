#include "legacy/steering_bev_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ls2k::legacy {
namespace {

struct RowRun {
    int left = 0;
    int right = 0;
    int width = 0;
    int center = 0;
};

std::vector<RowRun> ExtractRuns(const port::LegacyCameraFrame& frame, int row, int threshold) {
    std::vector<RowRun> runs{};
    if (row < 0 || row >= frame.height || frame.width <= 0) {
        return runs;
    }

    bool in_run = false;
    int run_start = 0;
    for (int col = 0; col < frame.width; ++col) {
        const bool above =
            frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) + col] > threshold;
        if (above && !in_run) {
            in_run = true;
            run_start = col;
        } else if (!above && in_run) {
            const int run_end = col - 1;
            runs.push_back({run_start, run_end, run_end - run_start + 1, (run_start + run_end) / 2});
            in_run = false;
        }
    }
    if (in_run) {
        const int run_end = frame.width - 1;
        runs.push_back({run_start, run_end, run_end - run_start + 1, (run_start + run_end) / 2});
    }
    return runs;
}

const RowRun* PickPrimaryRun(const std::vector<RowRun>& runs, int reference_col, const RowRun* previous_run) {
    const RowRun* best = nullptr;
    for (const RowRun& run : runs) {
        if (best == nullptr) {
            best = &run;
            continue;
        }
        const int reference = previous_run != nullptr ? previous_run->center : reference_col;
        const int current_distance = std::abs(run.center - reference);
        const int best_distance = std::abs(best->center - reference);
        if (current_distance != best_distance) {
            if (current_distance < best_distance) {
                best = &run;
            }
            continue;
        }
        if (run.width > best->width) {
            best = &run;
        }
    }
    return best;
}

bool SampleFromPriorTrack(const port::LegacySteeringState& prior_state,
                          std::size_t sample_index,
                          port::BEVPathSample& left,
                          port::BEVPathSample& center,
                          port::BEVPathSample& right,
                          float& lane_width_m) {
    const port::BEVTrackEstimate& prior = prior_state.last_bev_track.valid
                                              ? prior_state.last_bev_track
                                              : prior_state.bev_track_memory.previous_track;
    if (!prior.valid || sample_index >= prior.sampled_centerline.size()) {
        return false;
    }
    left = prior.sampled_left_boundary[sample_index];
    center = prior.sampled_centerline[sample_index];
    right = prior.sampled_right_boundary[sample_index];
    if (!center.valid) {
        return false;
    }
    if (left.valid) {
        left.confidence = std::min(left.confidence, 0.25F);
    }
    center.confidence = std::min(center.confidence, 0.25F);
    if (right.valid) {
        right.confidence = std::min(right.confidence, 0.25F);
    }
    lane_width_m = prior.lane_width_profile_m[sample_index];
    return true;
}

int ClampRow(const port::LegacyCameraFrame& frame, int row) {
    return std::clamp(row, 0, std::max(0, frame.height - 1));
}

int ClampCol(const port::LegacyCameraFrame& frame, int col) {
    return std::clamp(col, 0, std::max(0, frame.width - 1));
}

float ComputeHeading(const port::BEVPathSample& near_sample, const port::BEVPathSample& far_sample) {
    const float delta_forward = far_sample.point.forward_m - near_sample.point.forward_m;
    if (std::abs(delta_forward) < 1e-4F) {
        return 0.0F;
    }
    const float delta_lateral = far_sample.point.lateral_m - near_sample.point.lateral_m;
    return std::atan2(delta_lateral, delta_forward);
}

float ComputeCurvature(const port::BEVPathSample& first,
                       const port::BEVPathSample& second,
                       const port::BEVPathSample& third) {
    const float ds1 = std::max(1e-4F, second.point.forward_m - first.point.forward_m);
    const float ds2 = std::max(1e-4F, third.point.forward_m - second.point.forward_m);
    const float slope1 = (second.point.lateral_m - first.point.lateral_m) / ds1;
    const float slope2 = (third.point.lateral_m - second.point.lateral_m) / ds2;
    const float span = std::max(1e-4F, third.point.forward_m - first.point.forward_m);
    return (slope2 - slope1) / span;
}

port::BEVPathSample MakeSample(const port::BEVPoint& point, float confidence) {
    port::BEVPathSample sample{};
    sample.valid = true;
    sample.point = point;
    sample.confidence = confidence;
    return sample;
}

bool IsLeftBoundaryObserved(const RowRun& run, int border_margin_px) {
    return run.left > border_margin_px;
}

bool IsRightBoundaryObserved(const RowRun& run, int frame_width, int border_margin_px) {
    return run.right < frame_width - 1 - border_margin_px;
}

bool CenterWithinSearchLimit(const port::BEVPathSample& center,
                             const port::BEVGeometryParameters& geometry) {
    return std::abs(center.point.lateral_m) <=
           geometry.search_lateral_limit_m + geometry.nominal_lane_width_m * 0.5F;
}

}  // namespace

port::BEVTrackEstimate ComputeBevTrackEstimate(const port::LegacyCameraFrame& frame,
                                               int threshold,
                                               const port::RuntimeParameters& params,
                                               const port::LegacySteeringState& prior_state,
                                               const BEVProjector& projector) {
    port::BEVTrackEstimate track{};
    track.calibration_valid = projector.Valid();
    if (!projector.Valid() || frame.width <= 0 || frame.height <= 0) {
        track.fallback_mode = "projector_invalid";
        return track;
    }

    const port::BEVGeometryParameters& geometry = params.bev_geometry;
    RowRun previous_run{};
    const RowRun* previous_run_ptr = nullptr;
    int valid_count = 0;
    float confidence_sum = 0.0F;
    float visible_range_m = 0.0F;
    bool continuity_valid = true;
    float previous_center_lateral = 0.0F;
    bool previous_center_valid = false;
    bool used_single_edge_reconstruction = false;
    int reference_col = frame.width / 2;
    const port::BEVTrackEstimate& prior_track = prior_state.last_bev_track.valid
                                                    ? prior_state.last_bev_track
                                                    : prior_state.bev_track_memory.previous_track;
    for (const port::BEVPathSample& sample : prior_track.sampled_centerline) {
        if (!sample.valid) {
            continue;
        }
        port::ImagePoint prior_image{};
        if (projector.ProjectVehicleToImage(sample.point, prior_image)) {
            reference_col =
                ClampCol(frame, static_cast<int>(std::lround(prior_image.col_px)));
        }
        break;
    }
    bool used_prior_fallback = false;
    const int border_margin_px = std::max(0, geometry.image_border_truncation_margin_px);

    for (std::size_t i = 0; i < geometry.forward_samples_m.size(); ++i) {
        const float forward_m = geometry.forward_samples_m[i];
        port::ImagePoint center_image{};
        if (!projector.ProjectVehicleToImage({forward_m, 0.0F}, center_image)) {
            continue;
        }
        const int sample_row = ClampRow(frame, static_cast<int>(std::lround(center_image.row_px)));
        const std::vector<RowRun> runs = ExtractRuns(frame, sample_row, threshold);
        const RowRun* run = PickPrimaryRun(runs, reference_col, previous_run_ptr);
        port::BEVPathSample left{};
        port::BEVPathSample center{};
        port::BEVPathSample right{};
        float lane_width_m = 0.0F;

        if (run == nullptr || run->width <= 0) {
            if (!SampleFromPriorTrack(prior_state, i, left, center, right, lane_width_m)) {
                continue;
            }
            used_prior_fallback = true;
        } else {
            port::BEVPoint drivable_left_point{};
            port::BEVPoint drivable_right_point{};
            const bool drivable_left_ok = projector.ProjectImageToVehicle(
                {static_cast<float>(sample_row), static_cast<float>(ClampCol(frame, run->left))},
                drivable_left_point);
            const bool drivable_right_ok = projector.ProjectImageToVehicle(
                {static_cast<float>(sample_row), static_cast<float>(ClampCol(frame, run->right))},
                drivable_right_point);
            if (drivable_left_ok && drivable_right_ok &&
                drivable_left_point.lateral_m < drivable_right_point.lateral_m) {
                track.sampled_drivable_left_boundary[i] = MakeSample(drivable_left_point, 1.0F);
                track.sampled_drivable_right_boundary[i] = MakeSample(drivable_right_point, 1.0F);
                track.drivable_width_profile_m[i] =
                    drivable_right_point.lateral_m - drivable_left_point.lateral_m;
            }

            previous_run = *run;
            previous_run_ptr = &previous_run;
            reference_col = run->center;

            port::BEVPoint left_point{};
            port::BEVPoint center_point{};
            port::BEVPoint right_point{};
            const bool left_observed = IsLeftBoundaryObserved(*run, border_margin_px);
            const bool right_observed = IsRightBoundaryObserved(*run, frame.width, border_margin_px);
            const bool left_ok = left_observed && projector.ProjectImageToVehicle(
                {static_cast<float>(sample_row), static_cast<float>(ClampCol(frame, run->left))}, left_point);
            const bool center_ok = projector.ProjectImageToVehicle(
                {static_cast<float>(sample_row), static_cast<float>(ClampCol(frame, run->center))}, center_point);
            const bool right_ok = right_observed && projector.ProjectImageToVehicle(
                {static_cast<float>(sample_row), static_cast<float>(ClampCol(frame, run->right))}, right_point);
            if (!center_ok && !left_ok && !right_ok) {
                if (!SampleFromPriorTrack(prior_state, i, left, center, right, lane_width_m)) {
                    continue;
                }
                used_prior_fallback = true;
            } else {
                if (left_ok) {
                    left = MakeSample(left_point, 1.0F);
                }
                if (right_ok) {
                    right = MakeSample(right_point, 1.0F);
                }

                if (left.valid && right.valid) {
                    lane_width_m = right.point.lateral_m - left.point.lateral_m;
                    const bool width_valid = lane_width_m >= geometry.min_lane_width_m &&
                                             lane_width_m <= geometry.max_lane_width_m &&
                                             left.point.lateral_m < right.point.lateral_m;
                    if (!width_valid) {
                        if (!SampleFromPriorTrack(prior_state, i, left, center, right, lane_width_m)) {
                            continue;
                        }
                        used_prior_fallback = true;
                    } else {
                        if (center_ok) {
                            center = MakeSample(center_point, 1.0F);
                        } else {
                            center = MakeSample({(left.point.forward_m + right.point.forward_m) * 0.5F,
                                                 (left.point.lateral_m + right.point.lateral_m) * 0.5F},
                                                0.85F);
                        }
                    }
                } else if (left.valid || right.valid) {
                    lane_width_m = geometry.nominal_lane_width_m;
                    const float half_width = lane_width_m * 0.5F;
                    if (left.valid) {
                        center = MakeSample({left.point.forward_m, left.point.lateral_m + half_width}, 0.55F);
                    } else {
                        center = MakeSample({right.point.forward_m, right.point.lateral_m - half_width}, 0.55F);
                    }
                    if (!CenterWithinSearchLimit(center, geometry)) {
                        center.valid = false;
                    }
                    used_single_edge_reconstruction = used_single_edge_reconstruction || center.valid;
                }

                if (!center.valid) {
                    if (!SampleFromPriorTrack(prior_state, i, left, center, right, lane_width_m)) {
                        continue;
                    }
                    used_prior_fallback = true;
                } else {
                    if (previous_center_valid &&
                        std::abs(center.point.lateral_m - previous_center_lateral) >
                            geometry.continuity_break_threshold_m) {
                        continuity_valid = false;
                        left.confidence *= 0.5F;
                        center.confidence *= 0.5F;
                        right.confidence *= 0.5F;
                    }
                }
            }
        }

        if (!center.valid) {
            continue;
        }
        track.sampled_left_boundary[i] = left;
        track.sampled_centerline[i] = center;
        track.sampled_right_boundary[i] = right;
        track.lane_width_profile_m[i] = lane_width_m;
        visible_range_m = std::max(visible_range_m, center.point.forward_m);
        confidence_sum += center.confidence;
        ++valid_count;
        previous_center_lateral = center.point.lateral_m;
        previous_center_valid = true;
    }

    track.valid = valid_count >= 3;
    track.continuity_valid = continuity_valid;
    track.visible_range_m = visible_range_m;
    track.track_confidence =
        valid_count > 0 ? std::clamp(confidence_sum / static_cast<float>(valid_count), 0.0F, 1.0F) : 0.0F;
    if (used_prior_fallback && track.fallback_mode == "none") {
        track.fallback_mode = "track_memory";
    }
    if (used_single_edge_reconstruction && track.fallback_mode == "none") {
        track.fallback_mode = "single_edge_reconstruction";
    }

    const std::size_t near_index = std::min<std::size_t>(1, track.sampled_centerline.size() - 1);
    std::size_t far_index = near_index;
    for (std::size_t i = track.sampled_centerline.size(); i-- > 0;) {
        if (track.sampled_centerline[i].valid) {
            far_index = i;
            break;
        }
    }
    if (track.sampled_centerline[near_index].valid) {
        track.near_lateral_error = track.sampled_centerline[near_index].point.lateral_m;
    }
    if (track.sampled_centerline[near_index].valid && track.sampled_centerline[far_index].valid &&
        far_index > near_index) {
        track.far_heading_error =
            ComputeHeading(track.sampled_centerline[near_index], track.sampled_centerline[far_index]);
    }

    std::size_t curvature_index = std::min<std::size_t>(4, track.sampled_centerline.size() - 1);
    if (track.sampled_centerline[0].valid && track.sampled_centerline[near_index].valid &&
        track.sampled_centerline[curvature_index].valid && curvature_index > near_index) {
        track.preview_curvature =
            ComputeCurvature(track.sampled_centerline[0],
                             track.sampled_centerline[near_index],
                             track.sampled_centerline[curvature_index]);
    }

    if (track.track_confidence < geometry.min_track_confidence) {
        track.fallback_mode = track.fallback_mode == "none" ? "low_confidence" : track.fallback_mode;
    }
    if (track.visible_range_m < geometry.min_visible_range_m) {
        track.fallback_mode = track.fallback_mode == "none" ? "short_visible_range" : track.fallback_mode;
    }
    return track;
}

}  // namespace ls2k::legacy
