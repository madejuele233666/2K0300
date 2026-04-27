#include "legacy/camera_logic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_control_error_model.hpp"
#include "legacy/steering_gyro_continuity.hpp"
#include "legacy/steering_observation_assembly.hpp"
#include "legacy/steering_reference_policy.hpp"
#include "legacy/steering_scene_fsm.hpp"

namespace ls2k::legacy {
namespace {

int OtsuThresholdFast(const port::LegacyCameraFrame& frame) {
    std::array<int, 256> hist{};
    if (frame.width <= 0 || frame.height <= 0) {
        return 0;
    }

    int samples = 0;
    for (int row = 0; row < frame.height; row += 2) {
        for (int col = 0; col < frame.width; col += 2) {
            const uint8_t pixel =
                frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) + col];
            ++hist[pixel];
            ++samples;
        }
    }
    if (samples == 0) {
        return 0;
    }

    double sum = 0.0;
    for (int value = 0; value < 256; ++value) {
        sum += static_cast<double>(value * hist[value]);
    }

    double sum_background = 0.0;
    int weight_background = 0;
    double max_variance = -1.0;
    int threshold = 0;
    for (int value = 0; value < 256; ++value) {
        weight_background += hist[value];
        if (weight_background == 0) {
            continue;
        }
        const int weight_foreground = samples - weight_background;
        if (weight_foreground == 0) {
            break;
        }
        sum_background += static_cast<double>(value * hist[value]);
        const double mean_background = sum_background / static_cast<double>(weight_background);
        const double mean_foreground = (sum - sum_background) / static_cast<double>(weight_foreground);
        const double between_variance =
            static_cast<double>(weight_background) * static_cast<double>(weight_foreground) *
            (mean_background - mean_foreground) * (mean_background - mean_foreground);
        if (between_variance > max_variance) {
            max_variance = between_variance;
            threshold = value;
        }
    }
    return threshold;
}

std::array<std::size_t, port::kLaneGeometryAnchorCount> AnchorIndices() {
    return {1U, 3U, 6U};
}

port::LaneGeometryHistorySnapshot BuildLaneHistorySnapshot(const port::BEVTrackEstimate& track,
                                                           const BEVProjector& projector,
                                                           int frame_width,
                                                           int frame_height) {
    port::LaneGeometryHistorySnapshot snapshot{};
    const std::array<std::size_t, port::kLaneGeometryAnchorCount> indices = AnchorIndices();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t sample_index = std::min(indices[i], port::kBevTrackSampleCount - 1);
        if (track.sampled_left_boundary[sample_index].valid) {
            port::ImagePoint image{};
            if (projector.ProjectVehicleToImage(track.sampled_left_boundary[sample_index].point, image)) {
                snapshot.left_visible_anchors[i].valid = true;
                snapshot.left_visible_anchors[i].row =
                    std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
                snapshot.left_visible_anchors[i].col =
                    std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
                snapshot.valid = true;
            }
        }
        if (track.sampled_right_boundary[sample_index].valid) {
            port::ImagePoint image{};
            if (projector.ProjectVehicleToImage(track.sampled_right_boundary[sample_index].point, image)) {
                snapshot.right_visible_anchors[i].valid = true;
                snapshot.right_visible_anchors[i].row =
                    std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
                snapshot.right_visible_anchors[i].col =
                    std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
                snapshot.valid = true;
            }
        }
    }
    return snapshot;
}

int TrackTurnSign(const port::BEVTrackEstimate& track) {
    if (track.far_heading_error > 0.02F) {
        return 1;
    }
    if (track.far_heading_error < -0.02F) {
        return -1;
    }
    return 0;
}

port::TrackHistorySnapshot BuildTrackHistorySnapshot(const port::BEVTrackEstimate& track,
                                                     const BEVProjector& projector,
                                                     int frame_width,
                                                     int frame_height,
                                                     const port::LegacySteeringState& prior_state) {
    port::TrackHistorySnapshot snapshot{};
    if (!track.valid) {
        snapshot = prior_state.track_history;
        if (snapshot.valid) {
            snapshot.track_confidence *= 0.85F;
        }
        return snapshot;
    }

    snapshot.valid = true;
    const std::array<std::size_t, port::kLaneGeometryAnchorCount> indices = AnchorIndices();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t sample_index = std::min(indices[i], port::kBevTrackSampleCount - 1);
        if (!track.sampled_centerline[sample_index].valid) {
            continue;
        }
        port::ImagePoint image{};
        if (!projector.ProjectVehicleToImage(track.sampled_centerline[sample_index].point, image)) {
            continue;
        }
        snapshot.center_anchors[i].valid = true;
        snapshot.center_anchors[i].row =
            std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
        snapshot.center_anchors[i].col =
            std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
    }
    snapshot.lane_width_px = track.lane_width_profile_m[0] * 100.0F;
    snapshot.heading_px_per_row = track.far_heading_error;
    snapshot.curvature_px_per_row2 = track.preview_curvature;
    const int turn_sign = TrackTurnSign(track);
    snapshot.turn_sign = turn_sign;
    snapshot.last_nonzero_turn_sign =
        turn_sign != 0 ? turn_sign : prior_state.track_history.last_nonzero_turn_sign;
    snapshot.zero_turn_sign_frames = turn_sign == 0 ? prior_state.track_history.zero_turn_sign_frames + 1 : 0;
    snapshot.track_confidence = track.track_confidence;
    return snapshot;
}

std::string CircleReferenceModeForCompatibility(const std::string& reference_mode,
                                                const std::string& circle_direction) {
    if (circle_direction == "none") {
        return "none";
    }
    return reference_mode;
}

}  // namespace

SteeringAnalysisResult AnalyzeFrame(const port::LegacyCameraFrame& frame,
                                    const port::RuntimeParameters& params,
                                    const port::LegacySteeringState& prior_state,
                                    const port::ImuSample& imu,
                                    bool low_voltage_emergency,
                                    uint64_t frame_id,
                                    uint64_t capture_time_ms) {
    SteeringAnalysisResult analysis{};
    const int threshold = OtsuThresholdFast(frame);

    BEVProjector projector{};
    (void)projector.Configure(params.bev_projector);
    const port::BEVTrackEstimate track =
        ComputeBevTrackEstimate(frame, threshold, params, prior_state, projector);
    const ObservationAssemblyResult assembled =
        AssembleObservation(frame,
                            threshold,
                            params,
                            prior_state,
                            imu,
                            low_voltage_emergency,
                            frame_id,
                            capture_time_ms,
                            track,
                            projector);
    const SceneFsmResult scene = UpdateSceneFsm(assembled.observation, params, prior_state.scene_fsm);
    const ReferencePolicyResult reference =
        ResolveReferencePolicy(track, assembled.observation, scene.state, prior_state.reference_policy, params);
    const port::ControlErrorModelOutput control_output =
        ComputeControlErrorModel({track, reference.reference_path, assembled.vehicle, assembled.constraints}, params);
    const GyroContinuityConstraint continuity =
        ComputeGyroContinuityConstraint(prior_state, imu, capture_time_ms);

    port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.low_voltage_veto = low_voltage_emergency;
    perception.threshold = threshold;
    perception.threshold_veto = false;
    perception.geometry_veto = !track.valid;
    perception.emergency_veto = low_voltage_emergency || control_output.steering_suppressed;
    perception.frame_id = frame_id;
    perception.capture_time_ms = capture_time_ms;
    perception.active_module = scene.active_module;
    perception.scene_phase = scene.scene_phase;
    perception.scene_override_source = scene.scene_override_source;
    perception.reference_mode = reference.reference_mode;
    perception.circle_direction = scene.state.circle_direction;
    perception.circle_reference_mode =
        CircleReferenceModeForCompatibility(reference.reference_mode, scene.state.circle_direction);
    perception.circle_heading_delta_deg = continuity.next_state.heading_delta_deg_150ms;
    perception.circle_entry_signal_active = scene.state.circle_entry_signal_active;
    perception.track_confidence = track.track_confidence;
    perception.track_valid = track.valid;
    perception.sign_flip_blocked = false;
    perception.imu_grace_active = continuity.imu_grace_active;
    perception.gyro_heading_delta_deg = continuity.heading_delta_deg;
    perception.gyro_consistency_score = 1.0F;
    perception.perception_tag = "bev_first";
    perception.lateral_error = control_output.near_lateral_error;
    perception.heading_error = control_output.far_heading_error;
    perception.curvature = control_output.preview_curvature;
    perception.near_lateral_error = control_output.near_lateral_error;
    perception.far_heading_error = control_output.far_heading_error;
    perception.preview_curvature = control_output.preview_curvature;
    perception.visible_range_m = control_output.visible_range_m;
    perception.bev_track = track;
    perception.scene_observation = assembled.observation;
    perception.control_constraints = assembled.constraints;
    perception.control_model = control_output;
    perception.roadblock_active = false;
    perception.roadblock_interface_state = "supported_not_implemented";

    analysis.perception = perception;
    analysis.track_estimate = track;
    analysis.scene_observation = assembled.observation;
    analysis.reference_path = reference.reference_path;
    analysis.control_constraints = assembled.constraints;
    analysis.control_output = control_output;
    analysis.scene_debug_candidate = scene.state.debug_candidate;
    analysis.scene_debug_candidate_streak = scene.state.candidate_streak;
    analysis.scene_cross_candidate_score_last = assembled.observation.cross_candidate
                                                    ? assembled.observation.width_expand_ratio
                                                    : 0.0F;
    analysis.scene_circle_left_candidate_score_last =
        assembled.observation.left_open_score + assembled.observation.left_opposite_straight_confidence;
    analysis.scene_circle_right_candidate_score_last =
        assembled.observation.right_open_score + assembled.observation.right_opposite_straight_confidence;
    analysis.gyro_continuity_state = continuity.next_state;
    analysis.lane_geometry_snapshot = BuildLaneHistorySnapshot(track, projector, frame.width, frame.height);
    analysis.track_history_snapshot =
        BuildTrackHistorySnapshot(track, projector, frame.width, frame.height, prior_state);

    analysis.steering_state_update = prior_state;
    analysis.steering_state_update.active_module = scene.active_module;
    analysis.steering_state_update.scene_phase = scene.scene_phase;
    analysis.steering_state_update.scene_override_source = scene.scene_override_source;
    analysis.steering_state_update.roadblock_active = false;
    analysis.steering_state_update.scene_debug_candidate = scene.state.debug_candidate;
    analysis.steering_state_update.scene_debug_candidate_streak = scene.state.candidate_streak;
    analysis.steering_state_update.scene_cross_candidate_score_last = analysis.scene_cross_candidate_score_last;
    analysis.steering_state_update.scene_circle_left_candidate_score_last =
        analysis.scene_circle_left_candidate_score_last;
    analysis.steering_state_update.scene_circle_right_candidate_score_last =
        analysis.scene_circle_right_candidate_score_last;
    analysis.steering_state_update.last_bev_track = track;
    analysis.steering_state_update.bev_track_memory.has_previous_track = track.valid;
    if (track.valid) {
        analysis.steering_state_update.bev_track_memory.previous_track = track;
        analysis.steering_state_update.bev_track_memory.carry_cycles = 0;
    } else {
        ++analysis.steering_state_update.bev_track_memory.carry_cycles;
    }
    analysis.steering_state_update.scene_fsm = scene.state;
    analysis.steering_state_update.reference_policy = reference.state;
    analysis.steering_state_update.lane_geometry_previous = prior_state.lane_geometry_recent;
    analysis.steering_state_update.lane_geometry_recent = analysis.lane_geometry_snapshot;
    analysis.steering_state_update.track_history = analysis.track_history_snapshot;
    analysis.steering_state_update.gyro_continuity = continuity.next_state;
    analysis.steering_state_update_valid = true;
    return analysis;
}

}  // namespace ls2k::legacy
