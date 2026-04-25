#ifndef LS2K_PORT_CONTROL_TYPES_HPP
#define LS2K_PORT_CONTROL_TYPES_HPP

#include <array>
#include <cstdint>
#include <string>

namespace ls2k::port {

constexpr int kCompiledCameraFrameWidth = 320;
constexpr int kCompiledCameraFrameHeight = 240;

enum class CameraGeometryMarker {
    kPhase1Adapted,
    kNonPhase1Geometry,
    kEmptyFrame,
    kAdapterNotReady,
    kAdaptationHookRouted
};

struct LegacyCameraFrame {
    std::array<uint8_t, kCompiledCameraFrameWidth * kCompiledCameraFrameHeight> gray{};
    int width = 0;
    int height = 0;

    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

struct CameraCapture {
    bool has_frame = false;
    LegacyCameraFrame frame{};
    CameraGeometryMarker marker = CameraGeometryMarker::kAdapterNotReady;
    int source_width = 0;
    int source_height = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
};

struct PerceptionResult {
    bool published = false;
    bool fresh = false;
    bool emergency_veto = true;
    bool low_voltage_veto = false;
    bool threshold_veto = false;
    bool geometry_veto = false;
    float lateral_error = 0.0F;
    float heading_error = 0.0F;
    float curvature = 0.0F;
    float track_confidence = 0.0F;
    int threshold = 0;
    int highest_line = 0;
    int farthest_line = 0;
    int steering_reference_col = kCompiledCameraFrameWidth / 2;
    bool track_valid = false;
    int track_seed_col = kCompiledCameraFrameWidth / 2;
    float track_seed_score = 0.0F;
    float gyro_heading_delta_deg = 0.0F;
    float gyro_consistency_score = 1.0F;
    int track_sign = 0;
    bool sign_flip_blocked = false;
    bool imu_grace_active = false;
    bool roadblock_active = false;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
    uint64_t publish_time_ms = 0;
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string last_special_scene_correction = "none";
    std::string perception_tag = "none";
    std::string track_source = "bottom_connected";
};

struct LegacySteeringControllerMemory {
    float w_target_last = 0.0F;
    float camera_error_last = 0.0F;
    float gyro_error_last = 0.0F;
    float gyro_i_accumulator = 0.0F;
};

constexpr int kLaneGeometryAnchorCount = 3;

struct LaneHistoryAnchor {
    bool valid = false;
    int row = 0;
    int col = kCompiledCameraFrameWidth / 2;
};

struct LaneGeometryHistorySnapshot {
    bool valid = false;
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> left_visible_anchors{};
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> right_visible_anchors{};
};

struct TrackHistorySnapshot {
    bool valid = false;
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> center_anchors{};
    float lane_width_px = 0.0F;
    float heading_px_per_row = 0.0F;
    float curvature_px_per_row2 = 0.0F;
    int turn_sign = 0;
    float track_confidence = 0.0F;
    int flip_candidate_sign = 0;
    int flip_candidate_frames = 0;
};

struct GyroContinuityState {
    uint64_t last_valid_capture_time_ms = 0;
    float filtered_yaw_rate = 0.0F;
    float heading_delta_deg_150ms = 0.0F;
    bool imu_grace_active = false;
};

struct LegacySteeringState {
    int highest_line = 0;
    int farthest_line = 0;
    int steering_reference_col = kCompiledCameraFrameWidth / 2;
    bool roadblock_active = false;
    int drive_cycle_count = 0;
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string last_special_scene_correction = "none";
    std::string special_wide_candidate = "none";
    int special_wide_candidate_streak = 0;
    float special_wide_cross_score_last = 0.0F;
    float special_wide_circle_left_score_last = 0.0F;
    float special_wide_circle_right_score_last = 0.0F;
    LaneGeometryHistorySnapshot lane_geometry_recent{};
    LaneGeometryHistorySnapshot lane_geometry_previous{};
    TrackHistorySnapshot track_history{};
    GyroContinuityState gyro_continuity{};
    LegacySteeringControllerMemory controller_memory{};
};

struct ImuSample {
    bool valid = false;
    float acc_x = 0.0F;
    float acc_y = 0.0F;
    float acc_z = 0.0F;
    float gyro_x = 0.0F;
    float gyro_y = 0.0F;
    float gyro_z = 0.0F;
    uint64_t capture_time_ms = 0;
};

struct EncoderDelta {
    bool valid = false;
    int left = 0;
    int right = 0;
    uint64_t capture_time_ms = 0;
};

struct LowVoltageSample {
    bool valid = false;
    bool emergency = true;
    int raw_value = -1;
    int threshold = 0;
    uint64_t capture_time_ms = 0;
    std::string source = "unavailable";
};

struct ActuatorCommand {
    int left_pwm = 0;
    int right_pwm = 0;
    bool emergency_stop = true;
};

struct WheelPidParameters {
    double p = 240.0;
    double i = 10.0;
    double d = 20.0;
    double integral_limit = 2200.0;
    double measurement_filter_alpha = 0.3;
};

struct AssistantTcpParameters {
    std::string host = "192.168.2.32";
    int port = 8888;
};

struct SceneWideClassifierParameters {
    int lower_row_start = 156;
    int lower_row_end = 184;
    int middle_row_start = 120;
    int middle_row_end = 148;
    int upper_row_start = 80;
    int upper_row_end = 112;
    int row_step = 4;
    int edge_margin_px = 12;
    double upper_full_span_width_ratio = 0.95;

    double special_wide_lower_width_min_ratio = 0.38;
    int special_wide_valid_rows_min = 10;
    int edge_motion_min_px = 8;
    int edge_curvature_min_px = 6;
    int opposite_edge_straight_max_curvature_px = 5;
    double opposite_edge_border_touch_max_ratio = 0.45;
    int circle_open_min_px = 24;
    int circle_contract_min_px = 14;
    int cross_upper_full_span_consec_rows_min = 3;
    double cross_upper_full_span_min_ratio = 0.45;
    double to_cross_margin = 0.2;
    double to_circle_margin = 0.2;
    int enter_confirm_cycles = 2;
    int exit_confirm_cycles = 2;

    double cross_weight_full_span = 1.25;
    double cross_weight_both_open = 0.4;
    double circle_curve_weight = 1.2;
    double circle_opposite_straight_weight = 1.0;
    double circle_weight_open = 0.25;
    double circle_weight_contract = 0.2;
};

struct RuntimeParameters {
    double Speed_base = 77.0;
    double see_max = 35.0;
    double pid_turn_camera_p = 14.75;
    double pid_turn_camera_p_scale = 1.0;
    double pid_turn_camera_d = 5.0;
    double pid_turn_gyro_camera_p = 20.0;
    double pid_turn_gyro_camera_i = 0.0;
    double pid_turn_gyro_camera_d = 9.0;
    int P_Mode = 0;
    int exp_light = 65;

    // Additional phase-1 runtime policy values.
    int emergency_threshold = 40;
    int control_period_ms = 5;
    int perception_stale_ms = 120;
    int pwm_limit = 9000;
    int raw_turn_output_limit = 3000;
    int pwm_floor = 0;
    bool prohibit_reverse_pwm = false;
    int prohibit_reverse_pwm_step_limit = 280;
    int motion_unveto_confirm_cycles = 3;
    int motion_spinup_ms = 600;
    double motion_turn_limit_spinup = 0.35;
    int motion_pwm_step_limit = 280;
    int motion_stop_ms = 300;
    int motion_stop_encoder_threshold = 8;
    int motion_fault_rearm_hold_ms = 600;
    double wheel_turn_target_scale = 35.0;
    WheelPidParameters left_wheel_pid{};
    WheelPidParameters right_wheel_pid{};
    int control_snapshot_emit_interval_ms = 100;
    bool assistant_enabled = false;
    int assistant_waveform_publish_interval_ms = 40;
    int assistant_image_publish_interval_ms = 80;
    AssistantTcpParameters assistant_tcp{};
    bool steering_media_enabled = false;
    int steering_media_port = 8890;
    int steering_media_publish_interval_ms = 80;
    bool pid_turn_camera_use_fuzzy = false;
    int camera_frame_width = 320;
    int camera_frame_height = 240;
    SceneWideClassifierParameters scene_wide_classifier{};
    bool startup_critical_applied = false;
    bool loaded_from_defaults = false;
    bool parse_failure = false;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CONTROL_TYPES_HPP
