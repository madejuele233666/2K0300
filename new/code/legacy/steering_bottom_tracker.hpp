#ifndef LS2K_LEGACY_STEERING_BOTTOM_TRACKER_HPP
#define LS2K_LEGACY_STEERING_BOTTOM_TRACKER_HPP

#include <array>
#include <vector>

#include "legacy/steering_gyro_continuity.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

enum class TrackSource {
    bottom_connected,
    single_edge_completed,
    history_guarded,
    pure_visual_fallback
};

struct TrackRowObservation {
    int row = 0;
    int left = 0;
    int right = 0;
    int center = port::kCompiledCameraFrameWidth / 2;
    int width = 0;
    int transitions = 0;
    bool valid = false;
    bool left_visible = false;
    bool right_visible = false;
    bool width_completed = false;
    bool search_corrected = false;
    bool history_guarded = false;
    float row_confidence = 0.0F;
    float gyro_consistency_score = 1.0F;
    float final_weight = 0.0F;
};

struct BottomTrackResult {
    bool valid = false;
    int seed_col = port::kCompiledCameraFrameWidth / 2;
    float seed_score = 0.0F;
    float lateral_error = 0.0F;
    float heading_error = 0.0F;
    float curvature = 0.0F;
    float lane_width_px = 0.0F;
    float track_confidence = 0.0F;
    float gyro_heading_delta_deg = 0.0F;
    float gyro_consistency_score = 1.0F;
    int track_sign = 0;
    int flip_candidate_sign = 0;
    int flip_candidate_frames = 0;
    bool sign_flip_blocked = false;
    bool imu_grace_active = false;
    TrackSource source = TrackSource::bottom_connected;
    std::array<port::LaneHistoryAnchor, port::kLaneGeometryAnchorCount> center_anchors{};
    std::vector<TrackRowObservation> rows{};
};

struct BottomTrackRequest {
    const port::LegacyCameraFrame& frame;
    int threshold = 0;
    int scan_top = 0;
    int scan_bottom = 0;
    int row_step = 1;
    const port::LegacySteeringState& prior_state;
    const GyroContinuityConstraint& continuity;
};

struct SignFlipDecisionRequest {
    int prior_sign = 0;
    int candidate_sign = 0;
    int supporting_rows = 0;
    float average_row_confidence = 0.0F;
    int prior_candidate_sign = 0;
    int prior_candidate_frames = 0;
    bool imu_grace_active = false;
};

struct SignFlipDecision {
    int resolved_sign = 0;
    int pending_sign = 0;
    int pending_frames = 0;
    bool blocked = false;
};

BottomTrackResult TrackBottomConnectedLane(const BottomTrackRequest& request);
SignFlipDecision EvaluateTrackSignFlip(const SignFlipDecisionRequest& request);
const char* ToString(TrackSource source);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BOTTOM_TRACKER_HPP
