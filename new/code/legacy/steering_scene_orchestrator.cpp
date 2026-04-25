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
    result.highest_line = context.metrics.highest_line;
    result.farthest_line = context.metrics.farthest_line;
    result.steering_reference_col = scene.steering_reference_col;
    result.lateral_error = scene.lateral_error;
    result.active_module = scene.active_module;
    result.scene_phase = scene.scene_phase;
    result.scene_override_source = scene.scene_override_source;
    result.roadblock_interface_state = context.prior_state.roadblock_interface_state;
    result.last_special_scene_correction = scene.last_special_scene_correction;
    result.roadblock_active = false;
    result.perception_tag = std::string("scene-orchestrator:") + result.active_module;
    result.low_voltage_veto = low_voltage_emergency;
    result.threshold_veto =
        (result.threshold <= context.params.emergency_threshold) || (result.threshold >= 220);
    result.geometry_veto = context.metrics.valid_row_count < 4;
    result.emergency_veto = result.low_voltage_veto || result.threshold_veto || result.geometry_veto;
    analysis.perception = result;
    analysis.special_wide_candidate = scene.special_wide_candidate;
    analysis.special_wide_candidate_streak = scene.special_wide_candidate_streak;
    analysis.special_wide_cross_score_last = scene.special_wide_cross_score;
    analysis.special_wide_circle_left_score_last = scene.special_wide_circle_left_score;
    analysis.special_wide_circle_right_score_last = scene.special_wide_circle_right_score;
    return analysis;
}

}  // namespace ls2k::legacy
