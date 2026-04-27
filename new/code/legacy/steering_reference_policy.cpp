#include "legacy/steering_reference_policy.hpp"

#include <algorithm>

namespace ls2k::legacy {
namespace {

port::BEVPathSample ShiftSample(const port::BEVPathSample& sample, float lateral_offset) {
    port::BEVPathSample shifted = sample;
    if (shifted.valid) {
        shifted.point.lateral_m += lateral_offset;
    }
    return shifted;
}

float BlendFactor(const port::SpecialSceneFsmState& scene_state, const port::RuntimeParameters& params) {
    if (scene_state.phase != port::SpecialScenePhase::kExit) {
        return 0.0F;
    }
    return std::clamp(static_cast<float>(scene_state.progress_cycles) /
                          static_cast<float>(std::max(1, params.bev_scene_fsm.circle_release_cycles)),
                      0.0F,
                      1.0F);
}

}  // namespace

ReferencePolicyResult ResolveReferencePolicy(const port::BEVTrackEstimate& track,
                                             const port::BEVSceneObservation& observation,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params) {
    ReferencePolicyResult result{};
    result.state = prior_state;
    result.reference_path.valid = track.valid;
    result.reference_path.mode = port::ReferenceMode::kCenterline;

    const float half_nominal_width = params.bev_geometry.nominal_lane_width_m * 0.5F;
    const float exit_blend = BlendFactor(scene_state, params);
    for (std::size_t i = 0; i < track.sampled_centerline.size(); ++i) {
        result.reference_path.sampled_path[i] = track.sampled_centerline[i];
    }

    if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        result.reference_path.mode =
            exit_blend > 0.0F ? port::ReferenceMode::kBlend : port::ReferenceMode::kInnerOffset;
        for (std::size_t i = 0; i < result.reference_path.sampled_path.size(); ++i) {
            if (!track.sampled_left_boundary[i].valid) {
                continue;
            }
            port::BEVPathSample desired = ShiftSample(track.sampled_left_boundary[i], half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.reference_path.mode =
            exit_blend > 0.0F ? port::ReferenceMode::kBlend : port::ReferenceMode::kInnerOffset;
        for (std::size_t i = 0; i < result.reference_path.sampled_path.size(); ++i) {
            if (!track.sampled_right_boundary[i].valid) {
                continue;
            }
            port::BEVPathSample desired = ShiftSample(track.sampled_right_boundary[i], -half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
    } else if ((scene_state.active_scene == port::SpecialSceneKind::kCross ||
                scene_state.active_scene == port::SpecialSceneKind::kZebra) &&
               prior_state.valid) {
        result.reference_path.mode = port::ReferenceMode::kHoldLast;
        result.reference_path.sampled_path = prior_state.last_reference;
    } else {
        (void)observation;
    }

    result.reference_mode = ToString(result.reference_path.mode);
    result.reference_path.bias_m = 0.0F;
    result.state.valid = result.reference_path.valid;
    result.state.mode = result.reference_path.mode;
    result.state.last_reference = result.reference_path.sampled_path;
    return result;
}

ReferencePolicyResult ResolveReferencePolicy(const port::RoadHypotheses& hypotheses,
                                             const port::TopologyEvidence& evidence,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params) {
    (void)evidence;
    ReferencePolicyResult result{};
    result.state = prior_state;
    result.reference_path.valid = hypotheses.ordinary.valid;
    result.reference_path.mode = port::ReferenceMode::kCenterline;
    result.reference_path.sampled_path = hypotheses.ordinary.sampled_path;

    if ((scene_state.active_scene == port::SpecialSceneKind::kCross ||
         scene_state.active_scene == port::SpecialSceneKind::kZebra) &&
        prior_state.valid && scene_state.phase != port::SpecialScenePhase::kExit) {
        result.reference_path.mode = port::ReferenceMode::kHoldLast;
        result.reference_path.valid = true;
        result.reference_path.sampled_path = prior_state.last_reference;
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCross &&
               scene_state.phase == port::SpecialScenePhase::kExit &&
               hypotheses.forward_exit.valid) {
        result.reference_path.mode = port::ReferenceMode::kBlendToExit;
        result.reference_path.valid = true;
        result.reference_path.sampled_path = hypotheses.forward_exit.sampled_path;
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        const bool can_follow_arc =
            hypotheses.left_arc.valid &&
            hypotheses.left_arc.confidence >= params.bev_reference_policy.arc_follow_confidence_min;
        result.reference_path.mode =
            scene_state.phase == port::SpecialScenePhase::kExit ? port::ReferenceMode::kBlendToExit
                                                                : port::ReferenceMode::kArcFollow;
        if (can_follow_arc) {
            result.reference_path.valid = true;
            result.reference_path.sampled_path =
                scene_state.phase == port::SpecialScenePhase::kExit && hypotheses.forward_exit.valid
                    ? hypotheses.forward_exit.sampled_path
                    : hypotheses.left_arc.sampled_path;
        }
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        const bool can_follow_arc =
            hypotheses.right_arc.valid &&
            hypotheses.right_arc.confidence >= params.bev_reference_policy.arc_follow_confidence_min;
        result.reference_path.mode =
            scene_state.phase == port::SpecialScenePhase::kExit ? port::ReferenceMode::kBlendToExit
                                                                : port::ReferenceMode::kArcFollow;
        if (can_follow_arc) {
            result.reference_path.valid = true;
            result.reference_path.sampled_path =
                scene_state.phase == port::SpecialScenePhase::kExit && hypotheses.forward_exit.valid
                    ? hypotheses.forward_exit.sampled_path
                    : hypotheses.right_arc.sampled_path;
        }
    } else if (!hypotheses.ordinary.valid && prior_state.valid &&
               prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
        result.reference_path.mode = port::ReferenceMode::kLostPrediction;
        result.reference_path.valid = true;
        result.reference_path.sampled_path = prior_state.last_reference;
        result.state.lost_prediction_cycles = prior_state.lost_prediction_cycles + 1;
    } else {
        result.state.lost_prediction_cycles = 0;
    }

    result.reference_mode = ToString(result.reference_path.mode);
    result.state.valid = result.reference_path.valid;
    result.state.mode = result.reference_path.mode;
    result.state.last_reference = result.reference_path.sampled_path;
    if (result.reference_path.mode == port::ReferenceMode::kHoldLast) {
        result.state.hold_cycles = prior_state.hold_cycles + 1;
    } else {
        result.state.hold_cycles = 0;
    }
    return result;
}

const char* ToString(port::ReferenceMode mode) {
    switch (mode) {
        case port::ReferenceMode::kInnerOffset:
            return "inner_offset";
        case port::ReferenceMode::kOuterOffset:
            return "outer_offset";
        case port::ReferenceMode::kBlend:
            return "blend";
        case port::ReferenceMode::kHoldLast:
            return "hold_last";
        case port::ReferenceMode::kEntryHeadingExtension:
            return "entry_heading_extension";
        case port::ReferenceMode::kStableBoundaryOffset:
            return "stable_boundary_offset";
        case port::ReferenceMode::kArcFollow:
            return "arc_follow";
        case port::ReferenceMode::kBlendToExit:
            return "blend_to_exit";
        case port::ReferenceMode::kLostPrediction:
            return "lost_prediction";
        case port::ReferenceMode::kCenterline:
        default:
            return "centerline";
    }
}

}  // namespace ls2k::legacy
