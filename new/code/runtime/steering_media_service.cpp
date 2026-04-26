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
    snapshot.param_snapshot.scene_wide_classifier = params_.scene_wide_classifier;
    snapshot.param_snapshot.circle_scene = params_.circle_scene;
    snapshot.param_snapshot.circle_entry = params_.circle_entry;
    snapshot.param_snapshot.circle_interior = params_.circle_interior;
    snapshot.param_snapshot.circle_exit = params_.circle_exit;
    snapshot.param_snapshot.circle_fallback = params_.circle_fallback;
    return snapshot;
}

platform::SteeringMediaSnapshotView SteeringMediaService::BuildSnapshotView(
    const SteeringDebugSnapshot& snapshot) const {
    platform::SteeringMediaSnapshotView view{};
    view.lateral_error = snapshot.lateral_error;
    view.heading_error = snapshot.heading_error;
    view.curvature = snapshot.curvature;
    view.track_confidence = snapshot.track_confidence;
    view.highest_line = snapshot.highest_line;
    view.farthest_line = snapshot.farthest_line;
    view.steering_reference_col = snapshot.steering_reference_col;
    view.track_valid = snapshot.track_valid;
    view.track_seed_col = snapshot.track_seed_col;
    view.track_seed_score = snapshot.track_seed_score;
    view.track_sign = snapshot.track_sign;
    view.sign_flip_blocked = snapshot.sign_flip_blocked;
    view.imu_grace_active = snapshot.imu_grace_active;
    view.gyro_heading_delta_deg = snapshot.gyro_heading_delta_deg;
    view.gyro_consistency_score = snapshot.gyro_consistency_score;
    view.threshold = snapshot.threshold;
    view.threshold_veto = snapshot.threshold_veto;
    view.active_module = snapshot.active_module;
    view.scene_phase = snapshot.scene_phase;
    view.scene_override_source = snapshot.scene_override_source;
    view.roadblock_interface_state = snapshot.roadblock_interface_state;
    view.last_special_scene_correction = snapshot.last_special_scene_correction;
    view.track_source = snapshot.track_source;
    view.circle_direction = snapshot.circle_direction;
    view.circle_reference_mode = snapshot.circle_reference_mode;
    view.circle_heading_delta_deg = snapshot.circle_heading_delta_deg;
    view.circle_fallback_reason = snapshot.circle_fallback_reason;
    view.circle_entry_signal_active = snapshot.circle_entry_signal_active;
    view.circle_entry_release_reason = snapshot.circle_entry_release_reason;
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
