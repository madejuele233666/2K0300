#include "legacy/steering_scene_special_wide.hpp"

#include <algorithm>
#include <string>

#include "legacy/steering_scene_circle_entry.hpp"
#include "legacy/steering_scene_cross.hpp"

namespace ls2k::legacy {
namespace {

bool IsOrdinaryScene(const port::LegacySteeringState& state) {
    return state.active_module == "straight" || state.active_module == "bend";
}

SteeringSceneOutput BuildSpecialWideOutput(const SteeringSceneContext& context,
                                           const char* candidate,
                                           int streak,
                                           float cross_score,
                                           float circle_left_score,
                                           float circle_right_score) {
    SteeringSceneOutput output{};
    output.active = true;
    output.active_module = "special_wide";
    output.scene_phase = "sus";
    output.scene_override_source = "scene_module";
    output.last_special_scene_correction = "special_wide_sus";
    output.steering_reference_col = context.metrics.steering_reference_col;
    output.lateral_error = context.metrics.lateral_error;
    output.special_wide_candidate = candidate;
    output.special_wide_candidate_streak = streak;
    output.special_wide_cross_score = cross_score;
    output.special_wide_circle_left_score = circle_left_score;
    output.special_wide_circle_right_score = circle_right_score;
    return output;
}

const char* PickWideCandidate(float cross_score,
                              float circle_score,
                              const port::SceneWideClassifierParameters& wide) {
    if (cross_score - circle_score >= static_cast<float>(wide.to_cross_margin)) {
        return "cross";
    }
    if (circle_score - cross_score >= static_cast<float>(wide.to_circle_margin)) {
        return "circle_entry";
    }
    return "none";
}

int UpdateCandidateStreak(const port::LegacySteeringState& prior_state, const char* candidate) {
    if (prior_state.special_wide_candidate == candidate) {
        return prior_state.special_wide_candidate_streak + 1;
    }
    return 1;
}

}  // namespace

SteeringSceneOutput EvaluateSpecialWideScene(const SteeringSceneContext& context) {
    SteeringSceneOutput output{};
    const port::SceneWideClassifierParameters& wide = context.params.scene_wide_classifier;
    const bool prior_special_wide = context.prior_state.active_module == "special_wide";
    if (!prior_special_wide && !IsOrdinaryScene(context.prior_state)) {
        return output;
    }

    const bool wide_precondition = MeetsSpecialWidePrecondition(context);
    if (!prior_special_wide && !wide_precondition) {
        return output;
    }

    const float cross_score = wide_precondition ? ComputeCrossSceneScore(context) : 0.0F;
    const float circle_left_score = wide_precondition ? ComputeCircleLeftEntryScore(context) : 0.0F;
    const float circle_right_score = wide_precondition ? ComputeCircleRightEntryScore(context) : 0.0F;
    const float circle_score = std::max(circle_left_score, circle_right_score);
    const char* candidate = wide_precondition ? PickWideCandidate(cross_score, circle_score, wide) : "none";
    const int streak = UpdateCandidateStreak(context.prior_state, candidate);

    if (!prior_special_wide) {
        return BuildSpecialWideOutput(
            context, candidate, streak, cross_score, circle_left_score, circle_right_score);
    }

    if (candidate == std::string("cross") && streak >= wide.enter_confirm_cycles) {
        SteeringSceneOutput confirmed = BuildCrossSceneOutput(context);
        confirmed.special_wide_candidate = "cross";
        confirmed.special_wide_candidate_streak = streak;
        confirmed.special_wide_cross_score = cross_score;
        confirmed.special_wide_circle_left_score = circle_left_score;
        confirmed.special_wide_circle_right_score = circle_right_score;
        return confirmed;
    }
    if (candidate == std::string("circle_entry") && streak >= wide.enter_confirm_cycles) {
        SteeringSceneOutput confirmed = BuildCircleEntrySceneOutput(context);
        confirmed.special_wide_candidate = "circle_entry";
        confirmed.special_wide_candidate_streak = streak;
        confirmed.special_wide_cross_score = cross_score;
        confirmed.special_wide_circle_left_score = circle_left_score;
        confirmed.special_wide_circle_right_score = circle_right_score;
        return confirmed;
    }
    if (candidate == std::string("none") && streak >= wide.exit_confirm_cycles) {
        return output;
    }

    return BuildSpecialWideOutput(
        context, candidate, streak, cross_score, circle_left_score, circle_right_score);
}

}  // namespace ls2k::legacy
