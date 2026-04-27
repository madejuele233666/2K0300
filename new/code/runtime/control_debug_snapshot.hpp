#ifndef LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
#define LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP

#include <cstdint>
#include <string>

#include "runtime/control_decision.hpp"
#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

struct SteeringDebugSnapshot {
    bool valid = false;
    std::uint64_t frame_id = 0;
    std::uint64_t capture_time_ms = 0;
    double near_lateral_error = 0.0;
    double far_heading_error = 0.0;
    double preview_curvature = 0.0;
    double lookahead_distance_m = 0.0;
    double lookahead_lateral_error = 0.0;
    double lookahead_heading_error = 0.0;
    double reference_curvature = 0.0;
    double curvature_command = 0.0;
    double yaw_rate_target = 0.0;
    double visible_range_m = 0.0;
    double scene_width_expand_ratio = 1.0;
    double scene_cross_bilateral_open_score_m = 0.0;
    bool scene_cross_bilateral_open = false;
    bool scene_cross_candidate = false;
    bool scene_zebra_candidate = false;
    bool scene_circle_left_candidate = false;
    bool scene_circle_right_candidate = false;
    double scene_left_open_score = 0.0;
    double scene_right_open_score = 0.0;
    double scene_left_contract_score = 0.0;
    double scene_right_contract_score = 0.0;
    double scene_left_boundary_heading_abs_rad = 0.0;
    double scene_right_boundary_heading_abs_rad = 0.0;
    bool scene_circle_left_opposite_straight = false;
    bool scene_circle_right_opposite_straight = false;
    double lateral_error = 0.0;
    double heading_error = 0.0;
    double curvature = 0.0;
    double track_confidence = 0.0;
    bool track_valid = false;
    bool sign_flip_blocked = false;
    bool imu_grace_active = false;
    double gyro_heading_delta_deg = 0.0;
    double gyro_consistency_score = 1.0;
    int threshold = 0;
    bool threshold_veto = false;
    double resolved_fuzzy_p = 0.0;
    double camera_p_term = 0.0;
    double camera_d_term = 0.0;
    double w_target = 0.0;
    double gyro_z = 0.0;
    double gyro_error = 0.0;
    double gyro_p_term = 0.0;
    double gyro_d_term = 0.0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    bool roadblock_active = false;
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string reference_mode = "centerline";
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string circle_direction = "none";
    std::string circle_reference_mode = "none";
    double circle_heading_delta_deg = 0.0;
    bool circle_entry_signal_active = false;
};

struct ControlDebugSnapshot {
    bool valid = false;
    uint64_t cycle_count = 0;
    uint64_t timestamp_ms = 0;
    MotionPhase motion_phase = MotionPhase::kDisarmed;
    bool veto_active = true;
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    double effective_speed_target = 0.0;
    double left_speed_target = 0.0;
    double right_speed_target = 0.0;
    double left_measured_speed = 0.0;
    double right_measured_speed = 0.0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    int turn_pwm_command = 0;
    int left_pwm_command = 0;
    int right_pwm_command = 0;
    bool emergency_stop = true;
    SteeringDebugSnapshot steering{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
