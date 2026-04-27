#include "legacy/steering_scene_orchestrator.hpp"

#include "legacy/steering_scene_bend.hpp"
#include "legacy/steering_scene_circle_entry.hpp"
#include "legacy/steering_scene_circle_exit.hpp"
#include "legacy/steering_scene_circle_interior.hpp"
#include "legacy/steering_scene_cross.hpp"
#include "legacy/steering_scene_roadblock_stub.hpp"
#include "legacy/steering_scene_special_wide.hpp"
#include "legacy/steering_scene_straight.hpp"
#include "legacy/steering_scene_zebra.hpp"

namespace ls2k::legacy {
namespace {

SteeringSceneOutput PickSceneOutput(const SteeringSceneContext& context) {
    const SteeringSceneOutput roadblock = EvaluateRoadblockStubScene(context);
    if (roadblock.active) {
        return roadblock;
    }

    const SteeringSceneOutput zebra = EvaluateZebraScene(context);
    if (zebra.active) {
        return zebra;
    }

    const SteeringSceneOutput circle_exit = EvaluateCircleExitScene(context);
    if (circle_exit.active) {
        return circle_exit;
    }

    const SteeringSceneOutput circle_interior = EvaluateCircleInteriorScene(context);
    if (circle_interior.active) {
        return circle_interior;
    }

    const SteeringSceneOutput cross = EvaluateCrossScene(context);
    if (cross.active) {
        return cross;
    }

    const SteeringSceneOutput circle_entry = EvaluateCircleEntryScene(context);
    if (circle_entry.active) {
        return circle_entry;
    }

    const SteeringSceneOutput special_wide = EvaluateSpecialWideScene(context);
    if (special_wide.active) {
        return special_wide;
    }

    const SteeringSceneOutput bend = EvaluateBendScene(context);
    if (bend.active) {
        return bend;
    }

    return EvaluateStraightScene(context);
}

}  // namespace

SteeringAnalysisResult OrchestrateSteeringScenes(const SteeringSceneContext& context,
                                                 bool low_voltage_emergency,
                                                 std::uint64_t frame_id,
                                                 std::uint64_t capture_time_ms) {
    const SteeringSceneOutput scene = PickSceneOutput(context);

    SteeringAnalysisResult analysis{};
    port::PerceptionResult result{};
    result.published = true;
    result.fresh = true;
    result.frame_id = frame_id;
    result.capture_time_ms = capture_time_ms;
    result.publish_time_ms = capture_time_ms;
    result.threshold = context.metrics.threshold;
    result.lateral_error = scene.lateral_error;
    result.heading_error = context.metrics.heading_error;
    result.curvature = context.metrics.curvature;
    result.near_lateral_error = scene.lateral_error;
    result.far_heading_error = context.metrics.heading_error;
    result.preview_curvature = context.metrics.curvature;
    result.track_confidence = context.metrics.track_confidence;
    result.track_valid = context.metrics.track_valid;
    result.gyro_heading_delta_deg = context.metrics.gyro_heading_delta_deg;
    result.gyro_consistency_score = context.metrics.gyro_consistency_score;
    result.sign_flip_blocked = context.metrics.sign_flip_blocked;
    result.imu_grace_active = context.metrics.imu_grace_active;
    result.active_module = scene.active_module;
    result.scene_phase = scene.scene_phase;
    result.scene_override_source = scene.scene_override_source;
    result.roadblock_interface_state = context.prior_state.roadblock_interface_state;
    result.last_special_scene_correction = scene.last_special_scene_correction;
    result.roadblock_active = false;
    result.circle_direction = scene.circle_state_valid ? scene.circle_active_direction : "none";
    result.circle_reference_mode = scene.circle_state_valid ? scene.circle_reference_mode : "none";
    result.circle_heading_delta_deg = scene.circle_state_valid ? scene.circle_heading_delta_deg : 0.0F;
    result.circle_fallback_reason = scene.circle_state_valid ? scene.circle_fallback_reason : "none";
    result.circle_entry_signal_active = scene.circle_entry_signal_active;
    result.circle_entry_release_reason = scene.circle_entry_release_reason;
    result.perception_tag = std::string("scene-orchestrator:") + result.active_module;
    result.low_voltage_veto = low_voltage_emergency;
    result.threshold_veto =
        (result.threshold <= context.params.emergency_threshold) || (result.threshold >= 220);
    result.geometry_veto = context.metrics.valid_row_count < 4;
    result.emergency_veto = result.low_voltage_veto || result.threshold_veto || result.geometry_veto;
    analysis.perception = result;
    analysis.scene_debug_candidate = scene.scene_debug_candidate;
    analysis.scene_debug_candidate_streak = scene.scene_debug_candidate_streak;
    analysis.scene_cross_candidate_score_last = scene.scene_cross_candidate_score;
    analysis.scene_circle_left_candidate_score_last = scene.scene_circle_left_candidate_score;
    analysis.scene_circle_right_candidate_score_last = scene.scene_circle_right_candidate_score;
    analysis.lane_geometry_snapshot = context.metrics.lane_geometry_snapshot;
    analysis.steering_state_update = context.prior_state;
    analysis.steering_state_update.circle_active_direction =
        scene.circle_state_valid ? scene.circle_active_direction : "none";
    analysis.steering_state_update.circle_entry_state =
        scene.circle_state_valid ? scene.circle_entry_state : "idle";
    analysis.steering_state_update.circle_exit_state =
        scene.circle_state_valid ? scene.circle_exit_state : "idle";
    analysis.steering_state_update.circle_reference_mode =
        scene.circle_state_valid ? scene.circle_reference_mode : "none";
    analysis.steering_state_update.circle_heading_delta_deg =
        scene.circle_state_valid ? scene.circle_heading_delta_deg : 0.0F;
    analysis.steering_state_update.circle_heading_baseline_deg =
        scene.circle_state_valid ? scene.circle_heading_baseline_deg : 0.0F;
    analysis.steering_state_update.circle_last_imu_capture_time_ms =
        scene.circle_state_valid ? scene.circle_last_imu_capture_time_ms : 0;
    analysis.steering_state_update.circle_fixsteer_cycles =
        scene.circle_state_valid ? scene.circle_fixsteer_cycles : 0;
    analysis.steering_state_update.circle_handover_cycles =
        scene.circle_state_valid ? scene.circle_handover_cycles : 0;
    analysis.steering_state_update.circle_fallback_reason =
        scene.circle_state_valid ? scene.circle_fallback_reason : "none";
    analysis.steering_state_update.circle_entry_settle_cycles =
        scene.circle_state_valid ? scene.circle_entry_settle_cycles : 0;
    analysis.steering_state_update.circle_entry_loss_cycles =
        scene.circle_state_valid ? scene.circle_entry_loss_cycles : 0;
    analysis.steering_state_update.circle_entry_release_reason = scene.circle_entry_release_reason;
    analysis.steering_state_update.circle_opposite_edge_confirm_cycles =
        scene.circle_state_valid ? scene.circle_opposite_edge_confirm_cycles : 0;
    analysis.steering_state_update.circle_release_cycles =
        scene.circle_state_valid ? scene.circle_release_cycles : 0;
    analysis.steering_state_update.circle_last_stable_reference_col =
        scene.circle_state_valid ? scene.circle_last_stable_reference_col
                                 : port::kCompiledCameraFrameWidth / 2;
    analysis.steering_state_update_valid = true;
    return analysis;
}

}  // namespace ls2k::legacy
