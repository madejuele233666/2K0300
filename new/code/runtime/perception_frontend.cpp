#include "runtime/perception_frontend.hpp"

// 感知前端实现 —— 运行时感知管线调度。
// 负责故障注入、诊断发布、感知结果缓存和前端线程生命周期管理。

#include <cstdlib>
#include <string>

#include "legacy/camera_logic.hpp"

namespace ls2k::runtime {
namespace {

// 从环境变量读取正整数值（用于故障注入间隔）
int ReadPositiveIntervalEnv(const char* key, port::DiagnosticSink& diagnostics, uint64_t now_ms) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed > 0) {
            return parsed;
        }
    } catch (...) {
    }
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kWarning,
                           "perception.inject.invalid_env",
                           std::string("ignoring invalid fault-injection interval for ") + key + "=" + value,
                           now_ms},
                          1000);
    return 0;
}

// 构建丢帧回退感知结果（用于故障注入场景）
port::PerceptionResult BuildDroppedFrameFallback(const port::CameraCapture& capture) {
    port::PerceptionResult fallback{};
    fallback.published = true;
    fallback.fresh = false;
    fallback.frame_id = capture.frame_id;
    fallback.capture_time_ms = capture.capture_time_ms;
    fallback.publish_time_ms = capture.capture_time_ms;
    fallback.geometry_veto = true;
    fallback.emergency_veto = true;
    fallback.perception_tag = "injected-drop-frame";
    return fallback;
}

// 缓存最新相机帧到运行时状态
void RememberCameraCapture(RuntimeState& state, const port::CameraCapture& capture) {
    state.latest_camera_capture = capture;
    state.recent_camera_captures.Push(capture);
}

// 将感知分析结果应用到转向运行时状态
void ApplyPerceptionToSteeringState(const legacy::SteeringAnalysisResult& analysis, RuntimeState& state) {
    if (analysis.steering_state_update_valid) {
        state.steering_state = analysis.steering_state_update;
        return;
    }

    state.steering_state.active_module = analysis.perception.active_module;
    state.steering_state.scene_phase = analysis.perception.scene_phase;
    state.steering_state.last_bev_track = analysis.track_estimate;
    state.steering_state.gyro_continuity = analysis.gyro_continuity_state;
}

}  // namespace

PerceptionFrontend::PerceptionFrontend(port::ICameraAdapter& camera,
                                       port::IPowerMonitorAdapter& power,
                                       RuntimeState& state,
                                       port::DiagnosticSink& diagnostics)
    : camera_(camera), power_(power), state_(state), diagnostics_(diagnostics) {}

// 刷新低电压状态 —— 从电源监控采样并更新紧急标志
void PerceptionFrontend::RefreshLowVoltageState() {
    const port::LowVoltageSample sample = power_.SampleLowVoltage(diagnostics_);
    if (!sample.valid) {
        if (!state_.low_voltage_emergency.load()) {
            diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                               "power.low_voltage.invalid",
                               "low-voltage sample unavailable at runtime; forcing emergency veto",
                               port::NowMs()});
        }
        state_.low_voltage_emergency.store(true);
        return;
    }

    const bool previous = state_.low_voltage_emergency.load();
    state_.low_voltage_emergency.store(sample.emergency);
    if (sample.emergency != previous) {
        diagnostics_.Emit({sample.emergency ? port::DiagnosticLevel::kFailSafe
                                            : port::DiagnosticLevel::kInfo,
                           "power.low_voltage.transition",
                           sample.emergency ? "runtime low-voltage emergency asserted"
                                            : "runtime low-voltage emergency cleared",
                           sample.capture_time_ms});
    }
}

// 处理一帧图像：低电压检查 → 故障注入 → 空帧处理 → AnalyzeFrame 感知
void PerceptionFrontend::ProcessOneFrame(const port::RuntimeParameters& params) {
    RefreshLowVoltageState();
    ++processed_frames_;

    // Foreground frame-ready path. Heavy perception stays outside PIT callback.
    const port::CameraCapture capture = camera_.Capture(diagnostics_);
    const int drop_frame_every_n =
        ReadPositiveIntervalEnv("LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N", diagnostics_, port::NowMs());
    if (drop_frame_every_n > 0 && processed_frames_ % static_cast<uint64_t>(drop_frame_every_n) == 0) {
        port::EmitRateLimited(diagnostics_,
                              {port::DiagnosticLevel::kWarning,
                               "perception.inject.drop_frame",
                               "injecting bounded Phase B dropped-frame fault on the accepted runtime entrypoint",
                               port::NowMs()},
                              1000);
        port::PerceptionResult fallback = BuildDroppedFrameFallback(capture);
        fallback.publish_time_ms = port::NowMs();
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        RememberCameraCapture(state_, capture);
        state_.perception = fallback;
        ++state_.perception_publish_count;
        return;
    }

    if (!capture.has_frame) {
        port::PerceptionResult fallback{};
        fallback.published = true;
        fallback.fresh = false;
        fallback.frame_id = capture.frame_id;
        fallback.capture_time_ms = capture.capture_time_ms;
        fallback.publish_time_ms = port::NowMs();
        fallback.geometry_veto = true;
        fallback.emergency_veto = true;

        switch (capture.marker) {
            case port::CameraGeometryMarker::kEmptyFrame:
                fallback.perception_tag = "camera-empty";
                break;
            case port::CameraGeometryMarker::kAdapterNotReady:
                fallback.perception_tag = "camera-not-ready";
                break;
            case port::CameraGeometryMarker::kNonPhase1Geometry:
                fallback.perception_tag = "camera-bad-geometry";
                break;
            case port::CameraGeometryMarker::kAdaptationHookRouted:
                fallback.perception_tag = "camera-hook-routed";
                break;
            default:
                fallback.perception_tag = "camera-marker-unknown";
                break;
        }

        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        RememberCameraCapture(state_, capture);
        state_.perception = fallback;
        ++state_.perception_publish_count;
        return;
    }

    port::LegacySteeringState prior_state{};
    port::ImuSample imu{};
    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        prior_state = state_.steering_state;
        imu = state_.imu;
    }
    const legacy::SteeringAnalysisResult analysis =
        legacy::AnalyzeFrame(capture.frame,
                             params,
                             prior_state,
                             imu,
                             state_.low_voltage_emergency.load(),
                             capture.frame_id,
                             capture.capture_time_ms);
    port::PerceptionResult perception = analysis.perception;
    perception.publish_time_ms = port::NowMs();

    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    RememberCameraCapture(state_, capture);
    state_.perception = perception;
    ApplyPerceptionToSteeringState(analysis, state_);
    ++state_.perception_publish_count;
}

}  // namespace ls2k::runtime
