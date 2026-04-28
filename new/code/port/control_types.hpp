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

struct ImagePoint {
    float row_px = 0.0F;
    float col_px = 0.0F;
};

struct BEVPoint {
    float forward_m = 0.0F;
    float lateral_m = 0.0F;
};

constexpr std::size_t kBevCalibrationPointCount = 4;
constexpr std::size_t kBevTrackSampleCount = 12;

enum class SpecialSceneKind {
    kOrdinary,
    kBend,
    kCross,
    kZebra,
    kCircleLeft,
    kCircleRight
};

enum class SpecialScenePhase {
    kIdle,
    kCandidate,
    kConfirm,
    kEntry,
    kInterior,
    kExit,
    kHold,
    kRelease
};

enum class ReferenceMode {
    kCenterline,
    kInnerOffset,
    kOuterOffset,
    kBlend,
    kHoldLast,
    kEntryHeadingExtension,
    kStableBoundaryOffset,
    kArcFollow,
    kBlendToExit,
    kLostPrediction
};

struct BEVPathSample {
    bool valid = false;
    BEVPoint point{};
    float confidence = 0.0F;
};

enum class BEVSampleClass {
    kInvalidOutsideImage,
    kUnknownLowConfidence,
    kBackground,
    kDrivable
};

struct BEVSample {
    BEVPoint point{};
    ImagePoint image{};
    BEVSampleClass sample_class = BEVSampleClass::kInvalidOutsideImage;
    float raw_intensity = 0.0F;
    float patch_min_intensity = 0.0F;
    float patch_max_intensity = 0.0F;
    float patch_purity = 0.0F;
    float confidence = 0.0F;
    bool valid_image_projection = false;
    bool near_image_border = false;
};

enum class BEVBoundaryEvidenceKind {
    kNone,
    kObservedDrivableBackground,
    kSearchWindowEdge,
    kInvalidOutsideImage,
    kUnknownLowConfidence,
    kImageBorder,
    kNonBackgroundAdjacent
};

struct CorridorInterval {
    float forward_m = 0.0F;
    float lateral_min_m = 0.0F;
    float lateral_max_m = 0.0F;
    float lateral_center_m = 0.0F;
    float width_m = 0.0F;
    bool left_edge_valid = false;
    bool right_edge_valid = false;
    BEVBoundaryEvidenceKind left_boundary_evidence = BEVBoundaryEvidenceKind::kNone;
    BEVBoundaryEvidenceKind right_boundary_evidence = BEVBoundaryEvidenceKind::kNone;
    float left_opening_score = 0.0F;
    float right_opening_score = 0.0F;
    float valid_sample_ratio = 0.0F;
    float confidence = 0.0F;
};

struct CorridorGraphEdge {
    int from_layer = -1;
    int from_interval = -1;
    int to_layer = -1;
    int to_interval = -1;
    float overlap_score = 0.0F;
    float center_jump_cost = 0.0F;
    float width_change_cost = 0.0F;
    float curvature_cost = 0.0F;
    float confidence = 0.0F;
};

struct PathCandidate {
    bool valid = false;
    ReferenceMode mode = ReferenceMode::kCenterline;
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_path{};
    float confidence = 0.0F;
    float mean_width_m = 0.0F;
    float width_stability = 0.0F;
    float curvature = 0.0F;
    float curvature_consistency = 0.0F;
    float start_forward_m = 0.0F;
    float end_forward_m = 0.0F;
};

struct RoadHypotheses {
    PathCandidate ordinary{};
    PathCandidate left_arc{};
    PathCandidate right_arc{};
    PathCandidate forward_exit{};
    PathCandidate left_branch{};
    PathCandidate right_branch{};
    PathCandidate zebra_hold{};
};

struct TopologyEvidence {
    float ordinary_score = 0.0F;
    float bend_curvature_abs = 0.0F;
    float bend_veto_score = 0.0F;
    float cross_score = 0.0F;
    float left_circle_score = 0.0F;
    float right_circle_score = 0.0F;
    float zebra_score = 0.0F;
    float lost_score = 0.0F;
    float bilateral_opening_sync = 0.0F;
    float forward_reacquire_score = 0.0F;
    float left_opening_score = 0.0F;
    float right_opening_score = 0.0F;
    float invalid_edge_penalty = 0.0F;
};

struct BEVProjectorCalibration {
    bool valid = true;
    std::array<ImagePoint, kBevCalibrationPointCount> source_points{
        {ImagePoint{220.0F, 19.0F},
         ImagePoint{220.0F, 305.0F},
         ImagePoint{68.0F, 108.0F},
         ImagePoint{68.0F, 220.0F}}};
    std::array<BEVPoint, kBevCalibrationPointCount> target_points{
        {BEVPoint{0.061F, -0.21F},
         BEVPoint{0.061F, 0.21F},
         BEVPoint{0.610F, -0.21F},
         BEVPoint{0.610F, 0.21F}}};
    int debug_grid_width = 160;
    int debug_grid_height = 128;
    std::string projector_id = "bev_projector_true_bev_manual_forward_scale_v5";
    std::string projector_hash = "bev-projector-true-bev-manual-forward-scale-20260428";
};

struct BEVGeometryParameters {
    std::array<float, kBevTrackSampleCount> forward_samples_m{
        {0.040667F,
         0.061F,
         0.081333F,
         0.108444F,
         0.142333F,
         0.183F,
         0.230444F,
         0.284667F,
         0.352444F,
         0.433778F,
         0.521889F,
         0.610F}};
    float search_lateral_limit_m = 0.65F;
    float lateral_step_m = 0.02F;
    float nominal_lane_width_m = 0.42F;
    float min_lane_width_m = 0.20F;
    float max_lane_width_m = 0.75F;
    float min_visible_range_m = 0.108444F;
    float min_track_confidence = 0.25F;
    float continuity_break_threshold_m = 0.28F;
    int sample_row_step_px = 4;
    int image_border_truncation_margin_px = 2;
};

struct BEVSceneFsmParameters {
    float bend_severity_confirm = 0.20F;
    float cross_expand_ratio_min = 1.18F;
    float cross_bilateral_open_min_m = 0.12F;
    int cross_confirm_cycles = 2;
    int cross_hold_cycles = 2;
    float zebra_transition_density_min = 7.0F;
    int zebra_hold_cycles = 2;
    float circle_open_score_min = 0.18F;
    float circle_contract_score_min = 0.12F;
    float circle_opposite_heading_abs_max = 0.05F;
    int circle_confirm_cycles = 2;
    int circle_release_cycles = 3;
    float release_track_confidence_min = 0.55F;
};

struct BEVControlModelParameters {
    int near_sample_index = 1;
    int far_sample_index = 4;
    int curvature_sample_index = 5;
    double lookahead_visible_range_ratio = 0.35;
    double lookahead_min_m = 0.162667;
    double lookahead_max_m = 0.271111;
    double pure_pursuit_gain = 1.0;
    double heading_curvature_gain = 0.35;
    double curvature_feedforward_gain = 0.20;
    double curvature_command_limit = 0.12;
    double curvature_to_w_target_gain = 12000.0;
    double low_confidence_threshold = 0.35;
    double steering_suppression_confidence = 0.12;
    double low_visible_range_m = 0.108444;
    double min_gain_scale = 0.25;
    double min_speed_limit_scale = 0.35;
    double max_reference_bias_m = 0.20;
};

struct BEVTopologySamplerParameters {
    std::array<float, kBevTrackSampleCount> forward_samples_m{
        {0.040667F,
         0.061F,
         0.081333F,
         0.108444F,
         0.142333F,
         0.183F,
         0.230444F,
         0.284667F,
         0.352444F,
         0.433778F,
         0.521889F,
         0.610F}};
    float lateral_min_m = -0.80F;
    float lateral_max_m = 0.80F;
    float lateral_step_m = 0.02F;
    int sample_patch_radius_px = 1;
    float drivable_confidence_min = 0.55F;
    float unknown_confidence_min = 0.25F;
};

struct BEVCorridorGraphParameters {
    float nominal_lane_width_m = 0.42F;
    float min_interval_width_m = 0.16F;
    float max_interval_width_m = 1.20F;
    float max_center_jump_m = 0.28F;
    float max_width_change_m = 0.35F;
    float max_curvature_abs = 0.90F;
    float prior_carry_confidence_scale = 0.25F;
};

struct BEVTopologyEvidenceParameters {
    float cross_enter_score = 0.70F;
    float cross_release_score = 0.45F;
    float circle_enter_score = 1.0F;
    float circle_release_score = 0.45F;
    float zebra_enter_score = 1.0F;
    float zebra_release_score = 0.45F;
    float ordinary_release_score = 0.75F;
    float evidence_decay = 0.65F;
};

struct BEVReferencePolicyParameters {
    int hold_last_max_cycles = 8;
    int blend_min_cycles = 3;
    float arc_follow_confidence_min = 0.55F;
    float stable_boundary_confidence_min = 0.55F;
};

struct BEVTrackEstimate {
    bool valid = false;
    bool calibration_valid = false;
    bool continuity_valid = false;
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_left_boundary{};
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_centerline{};
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_right_boundary{};
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_drivable_left_boundary{};
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_drivable_right_boundary{};
    std::array<float, kBevTrackSampleCount> lane_width_profile_m{};
    std::array<float, kBevTrackSampleCount> drivable_width_profile_m{};
    float visible_range_m = 0.0F;
    float track_confidence = 0.0F;
    float near_lateral_error = 0.0F;
    float far_heading_error = 0.0F;
    float preview_curvature = 0.0F;
    std::string source = "bev_sparse_sampler";
    std::string fallback_mode = "none";
};

struct VehicleContext {
    bool low_voltage_emergency = false;
    bool imu_valid = false;
    float gyro_z = 0.0F;
    float yaw_rate_deg_s = 0.0F;
    float speed_mps = 0.0F;
    float encoder_mean = 0.0F;
    int drive_cycle_count = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
};

struct TopologyEvidenceAccumulator {
    TopologyEvidence value{};
    int update_cycles = 0;
};

struct ControlConstraintSet {
    bool steering_suppressed = false;
    bool fail_safe_veto = false;
    bool low_confidence_degraded = false;
    double steering_gain_scale = 1.0;
    double speed_limit_scale = 1.0;
    double turn_limit_scale = 1.0;
    std::string primary_reason = "none";
};

struct ControlErrorModelOutput {
    bool valid = false;
    float near_lateral_error = 0.0F;
    float far_heading_error = 0.0F;
    float preview_curvature = 0.0F;
    float visible_range_m = 0.0F;
    float track_confidence = 0.0F;
    float lookahead_distance_m = 0.0F;
    float lookahead_lateral_error = 0.0F;
    float lookahead_heading_error = 0.0F;
    float reference_curvature = 0.0F;
    float curvature_command = 0.0F;
    float yaw_rate_target = 0.0F;
    double steering_gain_scale = 1.0;
    double speed_limit_scale = 1.0;
    double turn_limit_scale = 1.0;
    bool steering_suppressed = false;
    bool degraded = false;
    std::string degrade_reason = "none";
};

struct BEVReferencePath {
    bool valid = false;
    ReferenceMode mode = ReferenceMode::kCenterline;
    float bias_m = 0.0F;
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_path{};
};

struct ControlErrorModelInput {
    BEVTrackEstimate track{};
    BEVReferencePath reference_path{};
    VehicleContext vehicle{};
    ControlConstraintSet constraints{};
};

struct BEVSceneObservation {
    bool valid = false;
    BEVTrackEstimate track{};
    VehicleContext vehicle{};
    bool ordinary_bend_veto = false;
    bool cross_candidate = false;
    bool zebra_candidate = false;
    bool circle_left_candidate = false;
    bool circle_right_candidate = false;
    float bend_severity = 0.0F;
    float width_expand_ratio = 1.0F;
    float bottom_transition_density = 0.0F;
    float left_open_score = 0.0F;
    float right_open_score = 0.0F;
    float left_contract_score = 0.0F;
    float right_contract_score = 0.0F;
    float cross_bilateral_open_score_m = 0.0F;
    bool cross_bilateral_open = false;
    float left_boundary_heading_abs_rad = 0.0F;
    float right_boundary_heading_abs_rad = 0.0F;
    bool circle_left_opposite_straight = false;
    bool circle_right_opposite_straight = false;
    float left_opposite_straight_confidence = 0.0F;
    float right_opposite_straight_confidence = 0.0F;
};

struct BEVTrackMemory {
    bool has_previous_track = false;
    BEVTrackEstimate previous_track{};
    int carry_cycles = 0;
};

struct SpecialSceneFsmState {
    SpecialSceneKind active_scene = SpecialSceneKind::kOrdinary;
    SpecialSceneKind candidate_scene = SpecialSceneKind::kOrdinary;
    SpecialScenePhase phase = SpecialScenePhase::kIdle;
    int candidate_streak = 0;
    int progress_cycles = 0;
    int release_cycles = 0;
    bool latched = false;
    bool circle_entry_signal_active = false;
    float circle_heading_delta_deg = 0.0F;
    std::string circle_direction = "none";
    std::string debug_candidate = "none";
    float debug_candidate_score = 0.0F;
};

struct ReferencePolicyState {
    bool valid = false;
    ReferenceMode mode = ReferenceMode::kCenterline;
    float carry_bias_m = 0.0F;
    int hold_cycles = 0;
    int lost_prediction_cycles = 0;
    std::array<BEVPathSample, kBevTrackSampleCount> last_reference{};
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
    bool track_valid = false;
    float gyro_heading_delta_deg = 0.0F;
    float gyro_consistency_score = 1.0F;
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
    std::string perception_tag = "none";
    std::string circle_direction = "none";
    std::string circle_reference_mode = "none";
    float circle_heading_delta_deg = 0.0F;
    bool circle_entry_signal_active = false;
    float near_lateral_error = 0.0F;
    float far_heading_error = 0.0F;
    float preview_curvature = 0.0F;
    float visible_range_m = 0.0F;
    std::string reference_mode = "centerline";
    BEVTrackEstimate bev_track{};
    RoadHypotheses road_hypotheses{};
    TopologyEvidence topology_evidence{};
    BEVSceneObservation scene_observation{};
    ControlConstraintSet control_constraints{};
    ControlErrorModelOutput control_model{};
};

struct BEVControllerMemory {
    float w_target_last = 0.0F;
    float camera_error_last = 0.0F;
    float gyro_error_last = 0.0F;
    float gyro_i_accumulator = 0.0F;
    float last_gain_scale = 1.0F;
};

using LegacySteeringControllerMemory = BEVControllerMemory;

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
    int last_nonzero_turn_sign = 0;
    int zero_turn_sign_frames = 0;
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
    bool roadblock_active = false;
    int drive_cycle_count = 0;
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string scene_debug_candidate = "none";
    int scene_debug_candidate_streak = 0;
    float scene_cross_candidate_score_last = 0.0F;
    float scene_circle_left_candidate_score_last = 0.0F;
    float scene_circle_right_candidate_score_last = 0.0F;
    LaneGeometryHistorySnapshot lane_geometry_recent{};
    LaneGeometryHistorySnapshot lane_geometry_previous{};
    TrackHistorySnapshot track_history{};
    GyroContinuityState gyro_continuity{};
    BEVTrackEstimate last_bev_track{};
    BEVTrackMemory bev_track_memory{};
    SpecialSceneFsmState scene_fsm{};
    ReferencePolicyState reference_policy{};
    RoadHypotheses last_road_hypotheses{};
    TopologyEvidence last_topology_evidence{};
    TopologyEvidenceAccumulator topology_evidence_accumulator{};
    int lost_prediction_cycles = 0;
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

struct RuntimeParameters {
    double Speed_base = 77.0;
    double see_max = 35.0;
    double pid_turn_camera_p = 14.75;
    double pid_turn_camera_p_scale = 1.0;
    double pid_turn_camera_d = 0.0;
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
    BEVProjectorCalibration bev_projector{};
    BEVGeometryParameters bev_geometry{};
    BEVSceneFsmParameters bev_scene_fsm{};
    BEVControlModelParameters bev_control_model{};
    BEVTopologySamplerParameters bev_topology_sampler{};
    BEVCorridorGraphParameters bev_corridor_graph{};
    BEVTopologyEvidenceParameters bev_topology_evidence{};
    BEVReferencePolicyParameters bev_reference_policy{};
    bool startup_critical_applied = false;
    bool loaded_from_defaults = false;
    bool parse_failure = false;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CONTROL_TYPES_HPP
