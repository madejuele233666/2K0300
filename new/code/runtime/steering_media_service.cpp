#include "runtime/steering_media_service.hpp"

#include <algorithm>
#include <utility>

namespace ls2k::runtime {
namespace {

bool ResolveSteeringCapture(const RuntimeState& state,
                            const ControlDebugSnapshot& snapshot,
                            port::CameraCapture& capture) {
    if (!snapshot.valid || !snapshot.steering.valid) {
        return false;
    }
    const port::CameraCapture* matched =
        state.recent_camera_captures.FindExact(snapshot.steering.frame_id, snapshot.steering.capture_time_ms);
    if (matched == nullptr || !matched->has_frame) {
        return false;
    }
    capture = *matched;
    return true;
}

}  // namespace

SteeringMediaService::SteeringMediaService(platform::SteeringMediaLink link)
    : link_(std::move(link)) {}

void SteeringMediaService::Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    enabled_ = params.steering_media_enabled;
    config_sent_ = false;
    publish_interval_ms_ = std::max(0, params.steering_media_publish_interval_ms);
    last_image_publish_ms_ = 0;
    last_image_frame_id_ = 0;
    params_ = params;
    if (!enabled_) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "steering_media.disabled",
                          "steering media sidecar disabled by runtime parameters",
                          port::NowMs()});
        return;
    }
    (void)link_.Initialize(params, diagnostics);
}

platform::SteeringMediaConfigSnapshot SteeringMediaService::BuildConfigSnapshot(std::uint64_t now_ms) const {
    platform::SteeringMediaConfigSnapshot snapshot{};
    snapshot.publish_time_ms = now_ms;
    snapshot.media_publish_interval_ms = publish_interval_ms_;
    snapshot.param_snapshot.pid_turn_camera_p = params_.pid_turn_camera_p;
    snapshot.param_snapshot.pid_turn_camera_p_scale = params_.pid_turn_camera_p_scale;
    snapshot.param_snapshot.pid_turn_camera_d = params_.pid_turn_camera_d;
    snapshot.param_snapshot.pid_turn_camera_use_fuzzy = params_.pid_turn_camera_use_fuzzy;
    snapshot.param_snapshot.pid_turn_gyro_camera_p = params_.pid_turn_gyro_camera_p;
    snapshot.param_snapshot.pid_turn_gyro_camera_i = params_.pid_turn_gyro_camera_i;
    snapshot.param_snapshot.pid_turn_gyro_camera_d = params_.pid_turn_gyro_camera_d;
    snapshot.param_snapshot.p_mode = params_.P_Mode;
    snapshot.param_snapshot.speed_base = params_.Speed_base;
    snapshot.param_snapshot.control_period_ms = params_.control_period_ms;
    snapshot.param_snapshot.raw_turn_output_limit = params_.raw_turn_output_limit;
    snapshot.param_snapshot.bev_projector = params_.bev_projector;
    snapshot.param_snapshot.bev_geometry = params_.bev_geometry;
    snapshot.param_snapshot.bev_scene_fsm = params_.bev_scene_fsm;
    snapshot.param_snapshot.bev_control_model = params_.bev_control_model;
    snapshot.param_snapshot.bev_topology_sampler = params_.bev_topology_sampler;
    snapshot.param_snapshot.bev_corridor_graph = params_.bev_corridor_graph;
    snapshot.param_snapshot.bev_topology_evidence = params_.bev_topology_evidence;
    snapshot.param_snapshot.bev_reference_policy = params_.bev_reference_policy;
    return snapshot;
}

platform::SteeringMediaSnapshotView SteeringMediaService::BuildSnapshotView(
    const SteeringDebugSnapshot& snapshot) const {
    platform::SteeringMediaSnapshotView view{};
    view.near_lateral_error = snapshot.near_lateral_error;
    view.far_heading_error = snapshot.far_heading_error;
    view.preview_curvature = snapshot.preview_curvature;
    view.lookahead_distance_m = snapshot.lookahead_distance_m;
    view.lookahead_lateral_error = snapshot.lookahead_lateral_error;
    view.lookahead_heading_error = snapshot.lookahead_heading_error;
    view.reference_curvature = snapshot.reference_curvature;
    view.curvature_command = snapshot.curvature_command;
    view.yaw_rate_target = snapshot.yaw_rate_target;
    view.visible_range_m = snapshot.visible_range_m;
    view.scene_width_expand_ratio = snapshot.scene_width_expand_ratio;
    view.scene_cross_bilateral_open_score_m = snapshot.scene_cross_bilateral_open_score_m;
    view.scene_cross_bilateral_open = snapshot.scene_cross_bilateral_open;
    view.scene_cross_candidate = snapshot.scene_cross_candidate;
    view.scene_zebra_candidate = snapshot.scene_zebra_candidate;
    view.scene_circle_left_candidate = snapshot.scene_circle_left_candidate;
    view.scene_circle_right_candidate = snapshot.scene_circle_right_candidate;
    view.scene_left_open_score = snapshot.scene_left_open_score;
    view.scene_right_open_score = snapshot.scene_right_open_score;
    view.scene_left_contract_score = snapshot.scene_left_contract_score;
    view.scene_right_contract_score = snapshot.scene_right_contract_score;
    view.scene_left_boundary_heading_abs_rad = snapshot.scene_left_boundary_heading_abs_rad;
    view.scene_right_boundary_heading_abs_rad = snapshot.scene_right_boundary_heading_abs_rad;
    view.scene_circle_left_opposite_straight = snapshot.scene_circle_left_opposite_straight;
    view.scene_circle_right_opposite_straight = snapshot.scene_circle_right_opposite_straight;
    view.lateral_error = snapshot.lateral_error;
    view.heading_error = snapshot.heading_error;
    view.curvature = snapshot.curvature;
    view.track_confidence = snapshot.track_confidence;
    view.track_valid = snapshot.track_valid;
    view.sign_flip_blocked = snapshot.sign_flip_blocked;
    view.imu_grace_active = snapshot.imu_grace_active;
    view.gyro_heading_delta_deg = snapshot.gyro_heading_delta_deg;
    view.gyro_consistency_score = snapshot.gyro_consistency_score;
    view.threshold = snapshot.threshold;
    view.threshold_veto = snapshot.threshold_veto;
    view.active_module = snapshot.active_module;
    view.scene_phase = snapshot.scene_phase;
    view.scene_override_source = snapshot.scene_override_source;
    view.reference_mode = snapshot.reference_mode;
    view.roadblock_interface_state = snapshot.roadblock_interface_state;
    view.circle_direction = snapshot.circle_direction;
    view.circle_reference_mode = snapshot.circle_reference_mode;
    view.circle_heading_delta_deg = snapshot.circle_heading_delta_deg;
    view.circle_entry_signal_active = snapshot.circle_entry_signal_active;
    view.roadblock_active = snapshot.roadblock_active;
    view.resolved_fuzzy_p = snapshot.resolved_fuzzy_p;
    view.camera_p_term = snapshot.camera_p_term;
    view.camera_d_term = snapshot.camera_d_term;
    view.w_target = snapshot.w_target;
    view.gyro_z = snapshot.gyro_z;
    view.gyro_error = snapshot.gyro_error;
    view.gyro_p_term = snapshot.gyro_p_term;
    view.gyro_d_term = snapshot.gyro_d_term;
    view.raw_turn_output = snapshot.raw_turn_output;
    view.applied_turn_output = snapshot.applied_turn_output;
    return view;
}

void SteeringMediaService::Tick(RuntimeState& state, port::DiagnosticSink& diagnostics) {
    if (!configured_ || !enabled_) {
        return;
    }

    const std::uint64_t now_ms = port::NowMs();
    const platform::SteeringMediaLinkPollResult poll_result = link_.Poll(diagnostics);
    if (poll_result.became_ready || poll_result.connection_lost) {
        config_sent_ = false;
        last_image_frame_id_ = 0;
    }

    (void)link_.FlushPendingImage(diagnostics);
    if (!poll_result.ready) {
        return;
    }

    if (!config_sent_) {
        config_sent_ = link_.PublishConfigSnapshot(BuildConfigSnapshot(now_ms), diagnostics);
        if (!config_sent_) {
            return;
        }
    }

    ControlDebugSnapshot snapshot{};
    port::CameraCapture capture{};
    bool have_capture = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        snapshot = state.control_debug_snapshot;
        have_capture = ResolveSteeringCapture(state, snapshot, capture);
    }

    if (!have_capture || snapshot.steering.frame_id == 0) {
        return;
    }
    if (capture.frame_id == last_image_frame_id_) {
        return;
    }
    if (publish_interval_ms_ > 0 && last_image_publish_ms_ != 0 &&
        now_ms >= last_image_publish_ms_ &&
        now_ms - last_image_publish_ms_ < static_cast<std::uint64_t>(publish_interval_ms_)) {
        return;
    }

    platform::SteeringMediaImageFrame frame{};
    frame.frame_id = capture.frame_id;
    frame.capture_time_ms = capture.capture_time_ms;
    frame.publish_time_ms = now_ms;
    frame.width = capture.frame.width;
    frame.height = capture.frame.height;
    frame.motion_phase = ToString(snapshot.motion_phase);
    frame.steering_snapshot = BuildSnapshotView(snapshot.steering);
    frame.pixel_data = capture.frame.gray.data();
    frame.pixel_size = capture.frame.PixelCount();

    const platform::SteeringMediaPublishResult result = link_.PublishImageFrame(frame, diagnostics);
    if (result == platform::SteeringMediaPublishResult::kSent ||
        result == platform::SteeringMediaPublishResult::kQueued) {
        last_image_publish_ms_ = now_ms;
        last_image_frame_id_ = capture.frame_id;
    }
}

}  // namespace ls2k::runtime
