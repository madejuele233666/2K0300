#include "legacy/steering_scene_fsm.hpp"

#include <algorithm>

namespace ls2k::legacy {
namespace {

float CandidateScore(port::SpecialSceneKind scene, const port::BEVSceneObservation& observation) {
    switch (scene) {
        case port::SpecialSceneKind::kCross:
            return observation.cross_candidate ? observation.width_expand_ratio : 0.0F;
        case port::SpecialSceneKind::kZebra:
            return observation.zebra_candidate ? observation.bottom_transition_density * 0.1F : 0.0F;
        case port::SpecialSceneKind::kCircleLeft:
            return observation.circle_left_candidate
                       ? observation.left_open_score + 0.5F * observation.left_opposite_straight_confidence
                       : 0.0F;
        case port::SpecialSceneKind::kCircleRight:
            return observation.circle_right_candidate
                       ? observation.right_open_score + 0.5F * observation.right_opposite_straight_confidence
                       : 0.0F;
        case port::SpecialSceneKind::kBend:
            return observation.ordinary_bend_veto ? observation.bend_severity : 0.0F;
        case port::SpecialSceneKind::kOrdinary:
        default:
            return 0.0F;
    }
}

port::SpecialSceneKind PickCandidate(const port::BEVSceneObservation& observation) {
    port::SpecialSceneKind candidate = port::SpecialSceneKind::kOrdinary;
    float best_score = 0.0F;
    for (port::SpecialSceneKind scene :
         {port::SpecialSceneKind::kCross,
          port::SpecialSceneKind::kZebra,
          port::SpecialSceneKind::kCircleLeft,
          port::SpecialSceneKind::kCircleRight}) {
        const float score = CandidateScore(scene, observation);
        if (score > best_score) {
            best_score = score;
            candidate = scene;
        }
    }
    if (candidate != port::SpecialSceneKind::kOrdinary) {
        return candidate;
    }
    if (observation.ordinary_bend_veto) {
        return port::SpecialSceneKind::kBend;
    }
    return candidate;
}

std::string ResolveCircleDirection(port::SpecialSceneKind scene) {
    return scene == port::SpecialSceneKind::kCircleLeft
               ? "left"
               : (scene == port::SpecialSceneKind::kCircleRight ? "right" : "none");
}

float TopologyCandidateScore(port::SpecialSceneKind scene, const port::TopologyEvidence& evidence) {
    switch (scene) {
        case port::SpecialSceneKind::kCross:
            return evidence.cross_score;
        case port::SpecialSceneKind::kZebra:
            return evidence.zebra_score;
        case port::SpecialSceneKind::kCircleLeft:
            return evidence.left_circle_score;
        case port::SpecialSceneKind::kCircleRight:
            return evidence.right_circle_score;
        case port::SpecialSceneKind::kOrdinary:
        case port::SpecialSceneKind::kBend:
        default:
            return evidence.ordinary_score;
    }
}

port::SpecialSceneKind PickTopologyCandidate(const port::TopologyEvidence& evidence,
                                             const port::RuntimeParameters& params) {
    port::SpecialSceneKind best = port::SpecialSceneKind::kOrdinary;
    float best_score = params.bev_topology_evidence.ordinary_release_score;
    if (evidence.cross_score >= params.bev_topology_evidence.cross_enter_score &&
        evidence.cross_score > best_score) {
        best = port::SpecialSceneKind::kCross;
        best_score = evidence.cross_score;
    }
    if (evidence.zebra_score >= params.bev_topology_evidence.zebra_enter_score &&
        evidence.zebra_score > best_score) {
        best = port::SpecialSceneKind::kZebra;
        best_score = evidence.zebra_score;
    }
    if (evidence.left_circle_score >= params.bev_topology_evidence.circle_enter_score &&
        evidence.left_circle_score > best_score) {
        best = port::SpecialSceneKind::kCircleLeft;
        best_score = evidence.left_circle_score;
    }
    if (evidence.right_circle_score >= params.bev_topology_evidence.circle_enter_score &&
        evidence.right_circle_score > best_score) {
        best = port::SpecialSceneKind::kCircleRight;
    }
    return best;
}

}  // namespace

SceneFsmResult UpdateSceneFsm(const port::BEVSceneObservation& observation,
                              const port::RuntimeParameters& params,
                              const port::SpecialSceneFsmState& prior_state) {
    SceneFsmResult result{};
    result.state = prior_state;

    const port::SpecialSceneKind candidate = PickCandidate(observation);
    const float candidate_score = CandidateScore(candidate, observation);
    result.state.debug_candidate = candidate == port::SpecialSceneKind::kOrdinary ? "none" : ToString(candidate);
    result.state.debug_candidate_score = candidate_score;

    if (candidate == result.state.candidate_scene) {
        ++result.state.candidate_streak;
    } else {
        result.state.candidate_scene = candidate;
        result.state.candidate_streak = candidate == port::SpecialSceneKind::kOrdinary ? 0 : 1;
    }

    if (result.state.active_scene == port::SpecialSceneKind::kCircleLeft ||
        result.state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.state.latched = true;
        result.state.circle_direction = ResolveCircleDirection(result.state.active_scene);
        const bool same_candidate = candidate == result.state.active_scene;
        if (result.state.phase == port::SpecialScenePhase::kEntry) {
            ++result.state.progress_cycles;
            if (result.state.progress_cycles >= 2) {
                result.state.phase = port::SpecialScenePhase::kInterior;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kInterior) {
            if (!same_candidate) {
                ++result.state.release_cycles;
                if (result.state.release_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                    result.state.phase = port::SpecialScenePhase::kExit;
                    result.state.progress_cycles = 0;
                }
            } else {
                result.state.release_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kExit) {
            ++result.state.progress_cycles;
            const bool recovered =
                observation.track.track_confidence >= params.bev_scene_fsm.release_track_confidence_min &&
                !observation.circle_left_candidate && !observation.circle_right_candidate;
            if (recovered || result.state.progress_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                result.state = {};
            }
        }
    } else if (result.state.active_scene == port::SpecialSceneKind::kCross) {
        ++result.state.progress_cycles;
        if (result.state.progress_cycles >= params.bev_scene_fsm.cross_hold_cycles) {
            result.state = {};
        }
    } else if (result.state.active_scene == port::SpecialSceneKind::kZebra) {
        ++result.state.progress_cycles;
        if (result.state.progress_cycles >= params.bev_scene_fsm.zebra_hold_cycles) {
            result.state = {};
        }
    } else {
        const int required_cycles =
            candidate == port::SpecialSceneKind::kCross
                ? params.bev_scene_fsm.cross_confirm_cycles
                : (candidate == port::SpecialSceneKind::kCircleLeft || candidate == port::SpecialSceneKind::kCircleRight
                       ? params.bev_scene_fsm.circle_confirm_cycles
                       : 1);
        if (candidate != port::SpecialSceneKind::kOrdinary &&
            result.state.candidate_streak >= required_cycles) {
            result.state.active_scene = candidate;
            result.state.progress_cycles = 0;
            result.state.release_cycles = 0;
            result.state.latched = candidate == port::SpecialSceneKind::kCircleLeft ||
                                   candidate == port::SpecialSceneKind::kCircleRight;
            if (candidate == port::SpecialSceneKind::kCircleLeft ||
                candidate == port::SpecialSceneKind::kCircleRight) {
                result.state.phase = port::SpecialScenePhase::kEntry;
                result.state.circle_direction = ResolveCircleDirection(candidate);
                result.state.circle_entry_signal_active = true;
            } else if (candidate == port::SpecialSceneKind::kCross ||
                       candidate == port::SpecialSceneKind::kZebra) {
                result.state.phase = port::SpecialScenePhase::kHold;
            } else if (candidate == port::SpecialSceneKind::kBend) {
                result.state.phase = port::SpecialScenePhase::kHold;
            }
        } else if (candidate == port::SpecialSceneKind::kBend) {
            result.state.active_scene = port::SpecialSceneKind::kBend;
            result.state.phase = port::SpecialScenePhase::kHold;
        } else if (!observation.ordinary_bend_veto) {
            result.state.active_scene = port::SpecialSceneKind::kOrdinary;
            result.state.phase = port::SpecialScenePhase::kIdle;
        }
    }

    switch (result.state.active_scene) {
        case port::SpecialSceneKind::kCross:
            result.active_module = "cross";
            result.scene_phase = "cross_hold";
            result.scene_override_source = "scene_fsm";
            break;
        case port::SpecialSceneKind::kZebra:
            result.active_module = "zebra";
            result.scene_phase = "zebra_hold";
            result.scene_override_source = "scene_fsm";
            break;
        case port::SpecialSceneKind::kCircleLeft:
        case port::SpecialSceneKind::kCircleRight:
            result.active_module = "circle";
            result.scene_override_source = "scene_fsm";
            result.scene_phase = result.state.phase == port::SpecialScenePhase::kEntry
                                     ? "circle_entry"
                                     : (result.state.phase == port::SpecialScenePhase::kInterior
                                            ? "circle_interior"
                                            : "circle_exit");
            break;
        case port::SpecialSceneKind::kBend:
            result.active_module = "bend";
            result.scene_phase = "bend_veto";
            break;
        case port::SpecialSceneKind::kOrdinary:
        default:
            result.active_module = "straight";
            result.scene_phase = "idle";
            break;
    }

    return result;
}

SceneFsmResult UpdateTopologySceneFsm(const port::TopologyEvidence& evidence,
                                      const port::RuntimeParameters& params,
                                      const port::SpecialSceneFsmState& prior_state) {
    SceneFsmResult result{};
    result.state = prior_state;

    const port::SpecialSceneKind candidate = PickTopologyCandidate(evidence, params);
    const float candidate_score = TopologyCandidateScore(candidate, evidence);
    result.state.debug_candidate =
        evidence.lost_score > 0.65F ? "lost" : (candidate == port::SpecialSceneKind::kOrdinary ? "none" : ToString(candidate));
    result.state.debug_candidate_score =
        evidence.lost_score > 0.65F ? evidence.lost_score : candidate_score;

    if (candidate == result.state.candidate_scene) {
        ++result.state.candidate_streak;
    } else {
        result.state.candidate_scene = candidate;
        result.state.candidate_streak = candidate == port::SpecialSceneKind::kOrdinary ? 0 : 1;
    }

    if (result.state.active_scene == port::SpecialSceneKind::kCircleLeft ||
        result.state.active_scene == port::SpecialSceneKind::kCircleRight) {
        result.state.latched = true;
        result.state.circle_direction = ResolveCircleDirection(result.state.active_scene);
        const float direction_score = result.state.active_scene == port::SpecialSceneKind::kCircleLeft
                                          ? evidence.left_circle_score
                                          : evidence.right_circle_score;
        if (result.state.phase == port::SpecialScenePhase::kEntry) {
            ++result.state.progress_cycles;
            if (result.state.progress_cycles >= 2) {
                result.state.phase = port::SpecialScenePhase::kInterior;
                result.state.progress_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kInterior) {
            if (direction_score < params.bev_topology_evidence.circle_release_score) {
                ++result.state.release_cycles;
                if (result.state.release_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                    result.state.phase = port::SpecialScenePhase::kExit;
                    result.state.progress_cycles = 0;
                }
            } else {
                result.state.release_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kExit) {
            ++result.state.progress_cycles;
            if (evidence.ordinary_score >= params.bev_topology_evidence.ordinary_release_score ||
                result.state.progress_cycles >= params.bev_scene_fsm.circle_release_cycles) {
                result.state = {};
            }
        }
    } else if (result.state.active_scene == port::SpecialSceneKind::kCross) {
        if (result.state.phase == port::SpecialScenePhase::kEntry) {
            ++result.state.progress_cycles;
            result.state.phase = port::SpecialScenePhase::kHold;
        } else if (result.state.phase == port::SpecialScenePhase::kHold) {
            ++result.state.progress_cycles;
            if (evidence.cross_score < params.bev_topology_evidence.cross_release_score ||
                result.state.progress_cycles >= params.bev_scene_fsm.cross_hold_cycles) {
                result.state.phase = port::SpecialScenePhase::kExit;
                result.state.progress_cycles = 0;
            }
        } else if (result.state.phase == port::SpecialScenePhase::kExit) {
            ++result.state.progress_cycles;
            if (evidence.ordinary_score >= params.bev_topology_evidence.ordinary_release_score ||
                result.state.progress_cycles >= params.bev_reference_policy.blend_min_cycles) {
                result.state = {};
            }
        }
    } else if (result.state.active_scene == port::SpecialSceneKind::kZebra) {
        ++result.state.progress_cycles;
        if (evidence.zebra_score < params.bev_topology_evidence.zebra_release_score ||
            result.state.progress_cycles >= params.bev_scene_fsm.zebra_hold_cycles) {
            result.state = {};
        }
    } else {
        const int required_cycles =
            candidate == port::SpecialSceneKind::kCross
                ? params.bev_scene_fsm.cross_confirm_cycles
                : (candidate == port::SpecialSceneKind::kCircleLeft || candidate == port::SpecialSceneKind::kCircleRight
                       ? params.bev_scene_fsm.circle_confirm_cycles
                       : 1);
        if (candidate != port::SpecialSceneKind::kOrdinary &&
            result.state.candidate_streak >= required_cycles) {
            result.state.active_scene = candidate;
            result.state.progress_cycles = 0;
            result.state.release_cycles = 0;
            result.state.latched = candidate == port::SpecialSceneKind::kCircleLeft ||
                                   candidate == port::SpecialSceneKind::kCircleRight;
            result.state.circle_direction = ResolveCircleDirection(candidate);
            result.state.circle_entry_signal_active = result.state.latched;
            result.state.phase =
                candidate == port::SpecialSceneKind::kCross ? port::SpecialScenePhase::kEntry
                                                            : port::SpecialScenePhase::kEntry;
            if (candidate == port::SpecialSceneKind::kZebra) {
                result.state.phase = port::SpecialScenePhase::kHold;
            }
        } else {
            result.state.active_scene = port::SpecialSceneKind::kOrdinary;
            result.state.phase =
                evidence.lost_score > 0.65F ? port::SpecialScenePhase::kHold : port::SpecialScenePhase::kIdle;
        }
    }

    switch (result.state.active_scene) {
        case port::SpecialSceneKind::kCross:
            result.active_module = "cross";
            result.scene_override_source = "topology_evidence";
            result.scene_phase = result.state.phase == port::SpecialScenePhase::kEntry
                                     ? "cross_approach"
                                     : (result.state.phase == port::SpecialScenePhase::kExit
                                            ? "cross_reacquire"
                                            : "cross_hold");
            break;
        case port::SpecialSceneKind::kZebra:
            result.active_module = "zebra";
            result.scene_phase = "zebra_hold";
            result.scene_override_source = "topology_evidence";
            break;
        case port::SpecialSceneKind::kCircleLeft:
        case port::SpecialSceneKind::kCircleRight:
            result.active_module = "circle";
            result.scene_override_source = "topology_evidence";
            result.scene_phase = result.state.phase == port::SpecialScenePhase::kEntry
                                     ? "circle_entry"
                                     : (result.state.phase == port::SpecialScenePhase::kInterior
                                            ? "circle_interior"
                                            : "circle_exit");
            break;
        case port::SpecialSceneKind::kOrdinary:
        case port::SpecialSceneKind::kBend:
        default:
            result.active_module = "straight";
            result.scene_phase =
                evidence.lost_score > 0.65F ? "lost_prediction" : "idle";
            result.scene_override_source =
                evidence.lost_score > 0.65F ? "topology_evidence" : "none";
            break;
    }

    return result;
}

const char* ToString(port::SpecialSceneKind kind) {
    switch (kind) {
        case port::SpecialSceneKind::kBend:
            return "bend";
        case port::SpecialSceneKind::kCross:
            return "cross";
        case port::SpecialSceneKind::kZebra:
            return "zebra";
        case port::SpecialSceneKind::kCircleLeft:
            return "circle_left";
        case port::SpecialSceneKind::kCircleRight:
            return "circle_right";
        case port::SpecialSceneKind::kOrdinary:
        default:
            return "ordinary";
    }
}

const char* ToString(port::SpecialScenePhase phase) {
    switch (phase) {
        case port::SpecialScenePhase::kCandidate:
            return "candidate";
        case port::SpecialScenePhase::kConfirm:
            return "confirm";
        case port::SpecialScenePhase::kEntry:
            return "entry";
        case port::SpecialScenePhase::kInterior:
            return "interior";
        case port::SpecialScenePhase::kExit:
            return "exit";
        case port::SpecialScenePhase::kHold:
            return "hold";
        case port::SpecialScenePhase::kRelease:
            return "release";
        case port::SpecialScenePhase::kIdle:
        default:
            return "idle";
    }
}

}  // namespace ls2k::legacy
