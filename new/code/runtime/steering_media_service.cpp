#include "runtime/steering_media_service.hpp"

// 转向媒体服务实现 —— 将调试快照与相机帧打包为媒体帧并发布。
// 通过 SteeringMediaLink 发送到外部媒体接收端（如远程监控）。

#include <algorithm>
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
    return snapshot;
}

// 构建转向快照视图 —— 将 DebugSnapshot 转换为媒体协议视图
platform::SteeringMediaSnapshotView SteeringMediaService::BuildSnapshotView(
    const SteeringDebugSnapshot& snapshot) const {
    platform::SteeringMediaSnapshotView view{};
    view.threshold = snapshot.threshold;
    view.perception_health.projector_ok = snapshot.perception_health.projector_ok;
    view.perception_health.reason = snapshot.perception_health.reason;
    view.reference.mode = snapshot.reference.mode;
    view.reference.source = snapshot.reference.source;
    view.eligibility.usable = snapshot.eligibility.usable;
    view.eligibility.leading_usable_samples = snapshot.eligibility.leading_usable_samples;
    view.eligibility.leading_min_forward_m = snapshot.eligibility.leading_min_forward_m;
    view.eligibility.leading_max_forward_m = snapshot.eligibility.leading_max_forward_m;
    view.eligibility.lookahead_distance_m = snapshot.eligibility.lookahead_distance_m;
    view.eligibility.reason = snapshot.eligibility.reason;
    view.curvature.computed = snapshot.curvature.computed;
    view.curvature.lookahead_distance_m = snapshot.curvature.lookahead_distance_m;
    view.curvature.curvature_command = snapshot.curvature.curvature_command;
    view.curvature.reason = snapshot.curvature.reason;
    view.reference_control.ready = snapshot.reference_control.ready;
    view.reference_control.reason = snapshot.reference_control.reason;
    view.safety_gate.veto_active = snapshot.safety_gate.veto_active;
    view.safety_gate.reason = snapshot.safety_gate.reason;
    view.degraded.active = snapshot.degraded.active;
    view.degraded.reason = snapshot.degraded.reason;
    view.yaw_control.yaw_rate_target = snapshot.yaw_control.yaw_rate_target;
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
    port::LegacyCameraFrame capture_frame{};
    CameraFrameHandle capture_handle{};
    bool have_capture = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        snapshot = state.control_debug_snapshot;
        have_capture = ResolveSteeringCapture(state, snapshot, capture_frame, capture_handle);
    }

    if (!have_capture || snapshot.steering.frame_id == 0) {
        return;
    }
    if (capture_handle.frame_id == last_image_frame_id_) {
        return;
    }
    if (publish_interval_ms_ > 0 && last_image_publish_ms_ != 0 &&
        now_ms >= last_image_publish_ms_ &&
        now_ms - last_image_publish_ms_ < static_cast<std::uint64_t>(publish_interval_ms_)) {
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
}

}  // namespace ls2k::runtime
