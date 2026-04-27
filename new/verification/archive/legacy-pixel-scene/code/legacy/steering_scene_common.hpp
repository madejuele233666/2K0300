#ifndef LS2K_LEGACY_STEERING_SCENE_COMMON_HPP
#define LS2K_LEGACY_STEERING_SCENE_COMMON_HPP

#include "legacy/steering_bottom_tracker.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

enum class EdgeObservationState {
    visible,
    border_truncated,
    missing
};

struct EdgeAnchor {
    int row = 0;
    int col = port::kCompiledCameraFrameWidth / 2;
    EdgeObservationState state = EdgeObservationState::missing;
    bool current_frame_extrapolated = false;
    bool history_fallback = false;
};

struct LaneEdgeMetrics {
    std::array<EdgeAnchor, port::kLaneGeometryAnchorCount> observed_anchors{};
    std::array<EdgeAnchor, port::kLaneGeometryAnchorCount> bend_anchors{};
    int observed_anchor_count = 0;
    int visible_anchor_count = 0;
    int truncated_anchor_count = 0;
    int bend_anchor_count = 0;
    int current_frame_extrapolated_anchor_count = 0;
    int history_fallback_anchor_count = 0;
    float observed_lower_mid_dx = 0.0F;
    float observed_mid_upper_dx = 0.0F;
    float observed_curvature = 0.0F;
    float bend_lower_mid_dx = 0.0F;
    float bend_mid_upper_dx = 0.0F;
    float bend_curvature = 0.0F;
    int observed_motion_sign = 0;
    int bend_motion_sign = 0;
    bool strict_straight = false;
    bool circle_curve = false;
    bool bend_curve = false;
};

struct LaneMetrics {
    int threshold = 0;
    int highest_line = 0;
    int farthest_line = 0;
    int steering_reference_col = port::kCompiledCameraFrameWidth / 2;
    int lane_width_bottom = 0;
    int transitions_bottom = 0;
    int valid_row_count = 0;
    int lower_valid_row_count = 0;
    int middle_valid_row_count = 0;
    int upper_valid_row_count = 0;
    int lower_left = 0;
    int lower_right = 0;
    int lower_width_median = 0;
    int middle_left = 0;
    int middle_right = 0;
    int middle_width_median = 0;
    int upper_left = 0;
    int upper_right = 0;
    int upper_width_median = 0;
    int left_dx_lower_mid = 0;
    int left_dx_mid_upper = 0;
    int right_dx_lower_mid = 0;
    int right_dx_mid_upper = 0;
    float lateral_error = 0.0F;
    float heading_error = 0.0F;
    float curvature = 0.0F;
    float track_confidence = 0.0F;
    float bend_severity = 0.0F;
    float left_curvature = 0.0F;
    float right_curvature = 0.0F;
    float upper_full_span_ratio = 0.0F;
    float left_upper_border_touch_ratio = 0.0F;
    float right_upper_border_touch_ratio = 0.0F;
    float left_open = 0.0F;
    float right_open = 0.0F;
    float left_contract = 0.0F;
    float right_contract = 0.0F;
    bool left_visible_confident = false;
    bool right_visible_confident = false;
    bool left_edge_missing_bottom = false;
    bool right_edge_missing_bottom = false;
    bool zebra_candidate = false;
    bool cross_candidate = false;
    bool track_valid = false;
    int track_seed_col = port::kCompiledCameraFrameWidth / 2;
    float track_seed_score = 0.0F;
    float gyro_heading_delta_deg = 0.0F;
    float gyro_consistency_score = 1.0F;
    int track_sign = 0;
    bool sign_flip_blocked = false;
    bool imu_grace_active = false;
    const char* track_source = "bottom_connected";
    std::array<port::LaneHistoryAnchor, port::kLaneGeometryAnchorCount> track_center_anchors{};
    float tracked_lane_width_px = 0.0F;
    int track_flip_candidate_sign = 0;
    int track_flip_candidate_frames = 0;
    int upper_full_span_consecutive_rows_max = 0;
    float circle_left_inner_confidence = 0.0F;
    float circle_right_inner_confidence = 0.0F;
    float circle_left_opposite_straight_confidence = 0.0F;
    float circle_right_opposite_straight_confidence = 0.0F;
    int circle_left_opposite_straight_rows = 0;
    int circle_right_opposite_straight_rows = 0;
    float circle_reference_width_baseline = 0.0F;
    LaneEdgeMetrics left_edge{};
    LaneEdgeMetrics right_edge{};
    int same_direction_bend_sign = 0;
    bool bend_used_current_frame_extrapolation = false;
    bool bend_used_history_fallback = false;
    port::LaneGeometryHistorySnapshot lane_geometry_snapshot{};
};

struct SteeringSceneContext {
    const port::LegacyCameraFrame& frame;
    const port::RuntimeParameters& params;
    const port::LegacySteeringState& prior_state;
    const port::ImuSample& imu;
    uint64_t capture_time_ms = 0;
    LaneMetrics metrics{};
};

struct SteeringSceneOutput {
    bool active = false;
    const char* active_module = "straight";
    const char* scene_phase = "idle";
    const char* scene_override_source = "none";
    const char* last_special_scene_correction = "none";
    int steering_reference_col = port::kCompiledCameraFrameWidth / 2;
    float lateral_error = 0.0F;
    const char* scene_debug_candidate = "none";
    int scene_debug_candidate_streak = 0;
    float scene_cross_candidate_score = 0.0F;
    float scene_circle_left_candidate_score = 0.0F;
    float scene_circle_right_candidate_score = 0.0F;
    bool circle_state_valid = false;
    std::string circle_active_direction = "none";
    std::string circle_entry_state = "idle";
    std::string circle_exit_state = "idle";
    std::string circle_reference_mode = "none";
    float circle_heading_delta_deg = 0.0F;
    float circle_heading_baseline_deg = 0.0F;
    uint64_t circle_last_imu_capture_time_ms = 0;
    int circle_fixsteer_cycles = 0;
    int circle_handover_cycles = 0;
    std::string circle_fallback_reason = "none";
    int circle_entry_settle_cycles = 0;
    int circle_entry_loss_cycles = 0;
    bool circle_entry_signal_active = false;
    std::string circle_entry_release_reason = "none";
    int circle_opposite_edge_confirm_cycles = 0;
    int circle_release_cycles = 0;
    int circle_last_stable_reference_col = port::kCompiledCameraFrameWidth / 2;
};

struct CircleHeadingIntegrationResult {
    float heading_delta_deg = 0.0F;
    uint64_t capture_time_ms = 0;
    bool imu_invalid = false;
};

struct SteeringAnalysisResult {
    port::PerceptionResult perception{};
    port::BEVTrackEstimate track_estimate{};
    port::BEVSceneObservation scene_observation{};
    port::BEVReferencePath reference_path{};
    port::ControlConstraintSet control_constraints{};
    port::ControlErrorModelOutput control_output{};
    std::string scene_debug_candidate = "none";
    int scene_debug_candidate_streak = 0;
    float scene_cross_candidate_score_last = 0.0F;
    float scene_circle_left_candidate_score_last = 0.0F;
    float scene_circle_right_candidate_score_last = 0.0F;
    port::LaneGeometryHistorySnapshot lane_geometry_snapshot{};
    port::TrackHistorySnapshot track_history_snapshot{};
    port::GyroContinuityState gyro_continuity_state{};
    port::LegacySteeringState steering_state_update{};
    bool steering_state_update_valid = false;
};

LaneMetrics ExtractLaneMetrics(const port::LegacyCameraFrame& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               const port::LegacySteeringState& prior_state,
                               const port::ImuSample& imu,
                               uint64_t capture_time_ms);
port::TrackHistorySnapshot BuildTrackHistorySnapshot(const LaneMetrics& metrics,
                                                     const port::LegacySteeringState& prior_state);
bool HasCrossUpperFullSpanStructure(const SteeringSceneContext& context);
bool HasCircleLeftEntryStructure(const SteeringSceneContext& context);
bool HasCircleRightEntryStructure(const SteeringSceneContext& context);
bool LooksLikeOrdinaryBend(const SteeringSceneContext& context);
bool MeetsSpecialWidePrecondition(const SteeringSceneContext& context);
bool IsLeftCircleDirection(const std::string& direction);
int BlendSteeringReferenceCols(int from_col, int to_col, float to_weight);
int BuildOffsetReferenceFromEdge(const LaneEdgeMetrics& edge,
                                 bool use_left_side,
                                 int near_offset_px,
                                 int far_offset_px,
                                 int fallback_col);
float ComputeCircleEdgeConfidence(const LaneEdgeMetrics& edge);
CircleHeadingIntegrationResult IntegrateCircleHeadingDeltaDeg(float prior_heading_delta_deg,
                                                              uint64_t prior_capture_time_ms,
                                                              const port::ImuSample& imu,
                                                              uint64_t capture_time_ms,
                                                              bool reset_heading);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_COMMON_HPP
