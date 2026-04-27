#ifndef LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP
#define LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

struct SteeringAnalysisResult {
    port::PerceptionResult perception{};
    port::BEVTrackEstimate track_estimate{};
    port::RoadHypotheses road_hypotheses{};
    port::TopologyEvidence topology_evidence{};
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

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP
