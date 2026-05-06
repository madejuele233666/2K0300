#include "runtime/steering_media_service.hpp"

// 转向媒体服务实现 —— 将调试快照与相机帧打包为媒体帧并发布。
// 通过 SteeringMediaLink 发送到外部媒体接收端（如远程监控）。

#include <algorithm>
#include <sstream>
#include <utility>

#include "port/perf_counter.hpp"

namespace ls2k::runtime {
namespace {

// 从运行时状态的相机缓存中查找匹配的快照帧
bool ResolveSteeringCapture(const RuntimeState& state,
                            const ControlDebugSnapshot& snapshot,
                            port::LegacyCameraFrame& frame,
                            CameraFrameHandle& handle) {
    if (!snapshot.valid || !snapshot.steering.valid) {
        return false;
    }
    const CameraFrameHandle* matched =
        state.recent_camera_captures.FindExact(snapshot.steering.frame_id, snapshot.steering.capture_time_ms);
    if (matched == nullptr) {
        return false;
    }
    handle = *matched;
    return CopyOwnedCameraFrameByHandle(state.camera_frame_slots, handle, frame);
}

}  // namespace

SteeringMediaService::SteeringMediaService(platform::SteeringMediaLink link)
    : link_(std::move(link)) {}

// 启动媒体服务：配置参数、初始化媒体链路（若启用）
void SteeringMediaService::Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    enabled_ = params.steering_media_enabled;
    config_sent_ = false;
    publish_disarmed_ = params.steering_media_publish_disarmed;
    publish_interval_ms_ = std::max(0, params.steering_media_publish_interval_ms);
    last_image_publish_ms_ = 0;
    last_image_frame_id_ = 0;
    last_summary_ms_ = port::NowMs();
    ResetWindowStats();
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

void SteeringMediaService::ResetWindowStats() {
    window_stats_ = WindowStats{};
}

void SteeringMediaService::MaybeEmitWindowSummary(std::uint64_t now_ms,
                                                  port::DiagnosticSink& diagnostics) {
    if (last_summary_ms_ == 0) {
        last_summary_ms_ = now_ms;
        return;
    }
    if (now_ms < last_summary_ms_ || now_ms - last_summary_ms_ < 1000U) {
        return;
    }

    std::ostringstream message;
    message << "ticks=" << window_stats_.ticks
            << " ready=" << (link_.Ready() ? "true" : "false")
            << " config_sent=" << (config_sent_ ? "true" : "false")
            << " publish_interval_ms=" << publish_interval_ms_
            << " last_frame_id=" << last_image_frame_id_
            << " not_ready=" << window_stats_.not_ready
            << " pending_flush_sent=" << window_stats_.pending_flush_sent
            << " config_attempts=" << window_stats_.config_attempts
            << " config_sent_count=" << window_stats_.config_sent
            << " config_wait=" << window_stats_.config_wait
            << " skip_no_capture=" << window_stats_.skip_no_capture
            << " skip_zero_frame=" << window_stats_.skip_zero_frame
            << " skip_disarmed=" << window_stats_.skip_disarmed
            << " skip_duplicate=" << window_stats_.skip_duplicate
            << " skip_interval=" << window_stats_.skip_interval
            << " image_sent=" << window_stats_.image_sent
            << " image_queued=" << window_stats_.image_queued
            << " image_unavailable=" << window_stats_.image_unavailable;
    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "steering_media.summary",
                      message.str(),
                      now_ms});
    last_summary_ms_ = now_ms;
    ResetWindowStats();
}

// 构建参数配置快照 —— 导出当前运行时参数到媒体协议格式
platform::SteeringMediaConfigSnapshot SteeringMediaService::BuildConfigSnapshot(std::uint64_t now_ms) const {
    platform::SteeringMediaConfigSnapshot snapshot{};
    snapshot.publish_time_ms = now_ms;
    snapshot.media_publish_interval_ms = publish_interval_ms_;
    snapshot.param_snapshot.running_speed_target = params_.running_speed_target;
    snapshot.param_snapshot.yaw_rate_pid_p = params_.yaw_rate_pid_p;
    snapshot.param_snapshot.yaw_rate_pid_i = params_.yaw_rate_pid_i;
    snapshot.param_snapshot.yaw_rate_pid_d = params_.yaw_rate_pid_d;
    snapshot.param_snapshot.control_period_ms = params_.control_period_ms;
    snapshot.param_snapshot.low_voltage_sample_interval_ms = params_.low_voltage_sample_interval_ms;
    snapshot.param_snapshot.low_voltage_raw_threshold = params_.low_voltage_raw_threshold;
    snapshot.param_snapshot.raw_turn_output_limit = params_.raw_turn_output_limit;
    snapshot.param_snapshot.bev_projector = params_.bev_projector;
    snapshot.param_snapshot.bev_geometry = params_.bev_geometry;
    snapshot.param_snapshot.bev_classification = params_.bev_classification;
    snapshot.param_snapshot.bev_control_model = params_.bev_control_model;
    snapshot.param_snapshot.bev_element = params_.bev_element;
    snapshot.param_snapshot.bev_element_raster = params_.bev_element_raster;
    return snapshot;
}

// 构建转向快照视图 —— 将 DebugSnapshot 转换为媒体协议视图
platform::SteeringMediaSnapshotView SteeringMediaService::BuildSnapshotView(
    const SteeringDebugSnapshot& snapshot) const {
    platform::SteeringMediaSnapshotView view{};
    view.threshold = snapshot.threshold;
    view.perception_health.projector_ok = snapshot.perception_health.projector_ok;
    view.perception_health.reason = snapshot.perception_health.reason;
    view.element_evidence = snapshot.element_evidence;
    view.visual_reference.present = snapshot.visual_reference.present;
    view.visual_reference.source = snapshot.visual_reference.source;
    view.visual_reference.reason = snapshot.visual_reference.reason;
    view.visual_reference.candidate_count = snapshot.visual_reference.candidate_count;
    view.visual_reference.rejected_candidate_reason =
        snapshot.visual_reference.rejected_candidate_reason;
    view.reference.mode = snapshot.reference.mode;
    view.reference.source = snapshot.reference.source;
    view.eligibility.usable = snapshot.eligibility.usable;
    view.eligibility.leading_usable_samples = snapshot.eligibility.leading_usable_samples;
    view.eligibility.leading_min_forward_m = snapshot.eligibility.leading_min_forward_m;
    view.eligibility.leading_max_forward_m = snapshot.eligibility.leading_max_forward_m;
    view.eligibility.reason = snapshot.eligibility.reason;
    view.lateral_error.computed = snapshot.lateral_error.computed;
    view.lateral_error.weighted_lateral_error_m = snapshot.lateral_error.weighted_lateral_error_m;
    view.lateral_error.weighted_sample_count = snapshot.lateral_error.weighted_sample_count;
    view.lateral_error.weight_sum = snapshot.lateral_error.weight_sum;
    view.lateral_error.reason = snapshot.lateral_error.reason;
    view.reference_control.ready = snapshot.reference_control.ready;
    view.reference_control.reason = snapshot.reference_control.reason;
    view.safety_gate.veto_active = snapshot.safety_gate.veto_active;
    view.safety_gate.reason = snapshot.safety_gate.reason;
    view.degraded.active = snapshot.degraded.active;
    view.degraded.reason = snapshot.degraded.reason;
    view.yaw_control.turn_output_target = snapshot.yaw_control.turn_output_target;
    view.actuator.raw_turn_output = snapshot.actuator.raw_turn_output;
    view.actuator.applied_turn_output = snapshot.actuator.applied_turn_output;
    return view;
}

// 媒体 Tick —— 检查连接 → 发布配置快照 → 查找新帧 → 发布图像帧
void SteeringMediaService::Tick(RuntimeState& state, port::DiagnosticSink& diagnostics) {
    LS2K_PERF_SCOPE(port::PerfStage::kSteeringMediaTick);
    if (!configured_ || !enabled_) {
        return;
    }

    const std::uint64_t now_ms = port::NowMs();
    window_stats_.ticks += 1U;
    const platform::SteeringMediaLinkPollResult poll_result = link_.Poll(diagnostics);
    if (poll_result.became_ready || poll_result.connection_lost) {
        config_sent_ = false;
        last_image_frame_id_ = 0;
    }

    if (link_.FlushPendingImage(diagnostics)) {
        window_stats_.pending_flush_sent += 1U;
    }
    if (!poll_result.ready) {
        window_stats_.not_ready += 1U;
        MaybeEmitWindowSummary(now_ms, diagnostics);
        return;
    }

    if (!config_sent_) {
        window_stats_.config_attempts += 1U;
        config_sent_ = link_.PublishConfigSnapshot(BuildConfigSnapshot(now_ms), diagnostics);
        if (config_sent_) {
            window_stats_.config_sent += 1U;
        }
        if (!config_sent_) {
            window_stats_.config_wait += 1U;
            MaybeEmitWindowSummary(now_ms, diagnostics);
            return;
        }
    }

    ControlDebugSnapshot snapshot{};
    port::LegacyCameraFrame capture_frame{};
    CameraFrameHandle capture_handle{};
    bool have_capture = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        snapshot = state.control_debug_snapshot;
        have_capture = ResolveSteeringCapture(state, snapshot, capture_frame, capture_handle);
    }

    if (!have_capture || snapshot.steering.frame_id == 0) {
        if (!have_capture) {
            window_stats_.skip_no_capture += 1U;
        }
        if (snapshot.steering.frame_id == 0) {
            window_stats_.skip_zero_frame += 1U;
        }
        MaybeEmitWindowSummary(now_ms, diagnostics);
        return;
    }
    if (!publish_disarmed_ && snapshot.motion_phase == MotionPhase::kDisarmed) {
        window_stats_.skip_disarmed += 1U;
        MaybeEmitWindowSummary(now_ms, diagnostics);
        return;
    }
    if (capture_handle.frame_id == last_image_frame_id_) {
        window_stats_.skip_duplicate += 1U;
        MaybeEmitWindowSummary(now_ms, diagnostics);
        return;
    }
    if (publish_interval_ms_ > 0 && last_image_publish_ms_ != 0 &&
        now_ms >= last_image_publish_ms_ &&
        now_ms - last_image_publish_ms_ < static_cast<std::uint64_t>(publish_interval_ms_)) {
        window_stats_.skip_interval += 1U;
        MaybeEmitWindowSummary(now_ms, diagnostics);
        return;
    }

    platform::SteeringMediaImageFrame frame{};
    frame.frame_id = capture_handle.frame_id;
    frame.capture_time_ms = capture_handle.capture_time_ms;
    frame.publish_time_ms = now_ms;
    frame.width = capture_frame.width;
    frame.height = capture_frame.height;
    frame.motion_phase = ToString(snapshot.motion_phase);
    frame.steering_snapshot = BuildSnapshotView(snapshot.steering);
    frame.pixel_data = capture_frame.gray.data();
    frame.pixel_size = capture_frame.PixelCount();

    const platform::SteeringMediaPublishResult result = link_.PublishImageFrame(frame, diagnostics);
    if (result == platform::SteeringMediaPublishResult::kSent ||
        result == platform::SteeringMediaPublishResult::kQueued) {
        last_image_publish_ms_ = now_ms;
        last_image_frame_id_ = capture_handle.frame_id;
    }
    if (result == platform::SteeringMediaPublishResult::kSent) {
        window_stats_.image_sent += 1U;
    } else if (result == platform::SteeringMediaPublishResult::kQueued) {
        window_stats_.image_queued += 1U;
    } else {
        window_stats_.image_unavailable += 1U;
    }
    MaybeEmitWindowSummary(now_ms, diagnostics);
}

}  // namespace ls2k::runtime
