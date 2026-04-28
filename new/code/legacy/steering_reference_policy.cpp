#include "legacy/steering_reference_policy.hpp"

#include <algorithm>
#include <cmath>

#include "legacy/steering_bev_geometry.hpp"

namespace ls2k::legacy {
namespace {

float BlendFactor(const port::SpecialSceneFsmState& scene_state, const port::RuntimeParameters& params) {
    if (scene_state.phase != port::SpecialScenePhase::kExit) {
        return 0.0F;
    }
    return std::clamp(static_cast<float>(scene_state.progress_cycles) /
                          static_cast<float>(std::max(1, params.bev_scene_fsm.circle_release_cycles)),
                      0.0F,
                      1.0F);
}

std::size_t ClampedSampleIndex(int index) {
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, index)),
        port::kBevTrackSampleCount - 1U);
}

int CountValidSamples(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    int count = 0;
    for (const port::BEVPathSample& sample : path) {
        if (sample.valid) {
            ++count;
        }
    }
    return count;
}

bool HasUsableReference(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path) {
    return CountValidSamples(path) >= 2;
}

port::BEVReferencePath ReferenceFromCandidate(const port::PathCandidate& candidate,
                                              port::ReferenceMode mode) {
    port::BEVReferencePath reference{};
    reference.valid = candidate.valid;
    reference.mode = mode;
    reference.sampled_path = candidate.sampled_path;
    return reference;
}

port::BEVReferencePath ReferenceFromPrior(const port::ReferencePolicyState& prior_state,
                                          port::ReferenceMode mode) {
    port::BEVReferencePath reference{};
    if (!prior_state.valid || !HasUsableReference(prior_state.last_reference)) {
        return reference;
    }
    reference.valid = true;
    reference.mode = mode;
    reference.sampled_path = prior_state.last_reference;
    return reference;
}

bool CopyBaseReference(const port::ReferencePolicyState& prior_state,
                       const port::PathCandidate& fallback,
                       std::array<port::BEVPathSample, port::kBevTrackSampleCount>& base) {
    if (prior_state.valid && HasUsableReference(prior_state.last_reference)) {
        base = prior_state.last_reference;
        return true;
    }
    if (fallback.valid && HasUsableReference(fallback.sampled_path)) {
        base = fallback.sampled_path;
        return true;
    }
    return false;
}

port::BEVReferencePath BuildEntryHeadingExtension(const port::ReferencePolicyState& prior_state,
                                                  const port::PathCandidate& fallback,
                                                  const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    reference.mode = port::ReferenceMode::kEntryHeadingExtension;

    std::array<port::BEVPathSample, port::kBevTrackSampleCount> base{};
    if (!CopyBaseReference(prior_state, fallback, base)) {
        return reference;
    }

    std::size_t anchor = ClampedSampleIndex(params.bev_control_model.near_sample_index);
    if (!base[anchor].valid) {
        for (std::size_t index = 0; index < base.size(); ++index) {
            if (base[index].valid) {
                anchor = index;
                break;
            }
        }
    }

    std::size_t heading_sample = ClampedSampleIndex(params.bev_control_model.far_sample_index);
    if (heading_sample <= anchor || !base[heading_sample].valid) {
        heading_sample = anchor;
        for (std::size_t index = anchor + 1U; index < base.size(); ++index) {
            if (base[index].valid) {
                heading_sample = index;
            }
        }
    }
    if (heading_sample <= anchor || !base[anchor].valid || !base[heading_sample].valid) {
        return reference;
    }

    const float ds = base[heading_sample].point.forward_m - base[anchor].point.forward_m;
    if (std::abs(ds) < 1e-4F) {
        return reference;
    }
    const float slope = (base[heading_sample].point.lateral_m - base[anchor].point.lateral_m) / ds;
    const float lateral_limit =
        static_cast<float>(std::max(0.0, params.bev_control_model.max_reference_bias_m));
    const float confidence = std::clamp(base[anchor].confidence, 0.0F, 1.0F);
    int valid_count = 0;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        const float forward = params.bev_topology_sampler.forward_samples_m[index];
        port::BEVPathSample sample{};
        sample.valid = true;
        sample.point.forward_m = forward;
        sample.point.lateral_m =
            std::clamp(base[anchor].point.lateral_m + slope * (forward - base[anchor].point.forward_m),
                       -lateral_limit,
                       lateral_limit);
        sample.confidence = confidence;
        reference.sampled_path[index] = sample;
        ++valid_count;
    }
    reference.valid = valid_count >= 2;
    return reference;
}

float CircleCandidateThreshold(const port::PathCandidate& candidate,
                               const port::RuntimeParameters& params) {
    return candidate.mode == port::ReferenceMode::kStableBoundaryOffset
               ? params.bev_reference_policy.stable_boundary_confidence_min
               : params.bev_reference_policy.arc_follow_confidence_min;
}

port::BEVReferencePath CircleReferenceFromCandidate(const port::PathCandidate& candidate,
                                                    const port::SpecialSceneFsmState& scene_state,
                                                    const port::PathCandidate& forward_exit,
                                                    const port::RuntimeParameters& params) {
    if (scene_state.phase == port::SpecialScenePhase::kExit && forward_exit.valid) {
        return ReferenceFromCandidate(forward_exit, port::ReferenceMode::kBlendToExit);
    }
    if (!candidate.valid || candidate.confidence < CircleCandidateThreshold(candidate, params)) {
        return {};
    }
    const port::ReferenceMode mode =
        scene_state.phase == port::SpecialScenePhase::kInterior &&
                candidate.mode == port::ReferenceMode::kStableBoundaryOffset
            ? port::ReferenceMode::kStableBoundaryOffset
            : port::ReferenceMode::kArcFollow;
    return ReferenceFromCandidate(candidate, mode);
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
    bool normal_offset_reference = false;
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
            port::BEVPathSample desired =
                OffsetPathSampleAlongNormal(
                    track.sampled_left_boundary, i, half_nominal_width, half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.forward_m =
                    desired.point.forward_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.forward_m * exit_blend;
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
        normal_offset_reference = true;
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.reference_path.mode =
            exit_blend > 0.0F ? port::ReferenceMode::kBlend : port::ReferenceMode::kInnerOffset;
        for (std::size_t i = 0; i < result.reference_path.sampled_path.size(); ++i) {
            if (!track.sampled_right_boundary[i].valid) {
                continue;
            }
            port::BEVPathSample desired =
                OffsetPathSampleAlongNormal(
                    track.sampled_right_boundary, i, -half_nominal_width, half_nominal_width);
            if (exit_blend > 0.0F && track.sampled_centerline[i].valid) {
                desired.point.forward_m =
                    desired.point.forward_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.forward_m * exit_blend;
                desired.point.lateral_m =
                    desired.point.lateral_m * (1.0F - exit_blend) +
                    track.sampled_centerline[i].point.lateral_m * exit_blend;
            }
            result.reference_path.sampled_path[i] = desired;
        }
        normal_offset_reference = true;
    } else if ((scene_state.active_scene == port::SpecialSceneKind::kCross ||
                scene_state.active_scene == port::SpecialSceneKind::kZebra) &&
               prior_state.valid) {
        result.reference_path.mode = port::ReferenceMode::kHoldLast;
        result.reference_path.sampled_path = prior_state.last_reference;
    } else {
        (void)observation;
    }

    if (normal_offset_reference) {
        (void)NormalizePathToForwardSamples(result.reference_path.sampled_path,
                                            params.bev_topology_sampler.forward_samples_m,
                                            half_nominal_width,
                                            std::max(std::abs(params.bev_topology_sampler.lateral_step_m),
                                                     std::abs(params.bev_geometry.lateral_step_m)) *
                                                2.0F);
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
    ReferencePolicyResult result{};
    result.state = prior_state;
    result.reference_path.valid = hypotheses.ordinary.valid;
    result.reference_path.mode = port::ReferenceMode::kCenterline;
    result.reference_path.sampled_path = hypotheses.ordinary.sampled_path;

    if (scene_state.active_scene == port::SpecialSceneKind::kCross) {
        result.reference_path = {};
        if (scene_state.phase == port::SpecialScenePhase::kExit) {
            if (hypotheses.forward_exit.valid) {
                result.reference_path =
                    ReferenceFromCandidate(hypotheses.forward_exit, port::ReferenceMode::kBlendToExit);
            } else {
                result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
            }
        } else if (scene_state.phase == port::SpecialScenePhase::kEntry) {
            result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
            if (!result.reference_path.valid) {
                result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
            }
        } else {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
            if (!result.reference_path.valid) {
                result.reference_path = BuildEntryHeadingExtension(prior_state, hypotheses.ordinary, params);
            }
        }
    } else if (scene_state.active_scene == port::SpecialSceneKind::kZebra) {
        result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kHoldLast);
        if (!result.reference_path.valid && hypotheses.zebra_hold.valid) {
            result.reference_path =
                ReferenceFromCandidate(hypotheses.zebra_hold, port::ReferenceMode::kHoldLast);
        }
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleLeft) {
        result.reference_path =
            CircleReferenceFromCandidate(hypotheses.left_arc, scene_state, hypotheses.forward_exit, params);
        if (!result.reference_path.valid && prior_state.valid &&
            prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
        }
    } else if (scene_state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.reference_path =
            CircleReferenceFromCandidate(hypotheses.right_arc, scene_state, hypotheses.forward_exit, params);
        if (!result.reference_path.valid && prior_state.valid &&
            prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
            result.reference_path = ReferenceFromPrior(prior_state, port::ReferenceMode::kLostPrediction);
        }
    } else if (!hypotheses.ordinary.valid && evidence.lost_score > 0.65F && prior_state.valid &&
               prior_state.lost_prediction_cycles < params.bev_reference_policy.hold_last_max_cycles) {
        result.reference_path.mode = port::ReferenceMode::kLostPrediction;
        result.reference_path.valid = true;
        result.reference_path.sampled_path = prior_state.last_reference;
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
    if (result.reference_path.mode == port::ReferenceMode::kLostPrediction) {
        result.state.lost_prediction_cycles = prior_state.lost_prediction_cycles + 1;
    } else {
        result.state.lost_prediction_cycles = 0;
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
