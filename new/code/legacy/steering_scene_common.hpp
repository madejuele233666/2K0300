#ifndef LS2K_LEGACY_STEERING_SCENE_COMMON_HPP
#define LS2K_LEGACY_STEERING_SCENE_COMMON_HPP

#include "port/control_types.hpp"

namespace ls2k::legacy {

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
    int upper_full_span_consecutive_rows_max = 0;
};

struct SteeringSceneContext {
    const port::LegacyCameraFrame& frame;
    const port::RuntimeParameters& params;
    const port::LegacySteeringState& prior_state;
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
    const char* special_wide_candidate = "none";
    int special_wide_candidate_streak = 0;
    float special_wide_cross_score = 0.0F;
    float special_wide_circle_left_score = 0.0F;
    float special_wide_circle_right_score = 0.0F;
};

struct SteeringAnalysisResult {
    port::PerceptionResult perception{};
    std::string special_wide_candidate = "none";
    int special_wide_candidate_streak = 0;
    float special_wide_cross_score_last = 0.0F;
    float special_wide_circle_left_score_last = 0.0F;
    float special_wide_circle_right_score_last = 0.0F;
};

LaneMetrics ExtractLaneMetrics(const port::LegacyCameraFrame& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               int prior_reference_col);
bool HasCrossUpperFullSpanStructure(const SteeringSceneContext& context);
bool HasCircleLeftEntryStructure(const SteeringSceneContext& context);
bool HasCircleRightEntryStructure(const SteeringSceneContext& context);
bool LooksLikeOrdinaryBend(const SteeringSceneContext& context);
bool MeetsSpecialWidePrecondition(const SteeringSceneContext& context);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_SCENE_COMMON_HPP
