#include "runtime/perception_frontend.hpp"

#include <cstdlib>
#include <string>

#include "legacy/camera_logic.hpp"

namespace ls2k::runtime {
namespace {

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

void RememberCameraCapture(RuntimeState& state, const port::CameraCapture& capture) {
    state.latest_camera_capture = capture;
    state.recent_camera_captures.Push(capture);
}

void ApplyPerceptionToSteeringState(const legacy::SteeringAnalysisResult& analysis, RuntimeState& state) {
    const port::PerceptionResult& perception = analysis.perception;
    state.steering_state.highest_line = perception.highest_line;
    state.steering_state.farthest_line = perception.farthest_line;
    state.steering_state.steering_reference_col = perception.steering_reference_col;
    state.steering_state.active_module = perception.active_module;
    state.steering_state.scene_phase = perception.scene_phase;
    state.steering_state.scene_override_source = perception.scene_override_source;
    state.steering_state.roadblock_interface_state = perception.roadblock_interface_state;
    state.steering_state.last_special_scene_correction = perception.last_special_scene_correction;
    state.steering_state.roadblock_active = perception.roadblock_active;
    state.steering_state.special_wide_candidate = analysis.special_wide_candidate;
    state.steering_state.special_wide_candidate_streak = analysis.special_wide_candidate_streak;
    state.steering_state.special_wide_cross_score_last = analysis.special_wide_cross_score_last;
    state.steering_state.special_wide_circle_left_score_last =
        analysis.special_wide_circle_left_score_last;
    state.steering_state.special_wide_circle_right_score_last =
        analysis.special_wide_circle_right_score_last;
    state.steering_state.lane_geometry_previous = state.steering_state.lane_geometry_recent;
    state.steering_state.lane_geometry_recent = analysis.lane_geometry_snapshot;
    state.steering_state.track_history = analysis.track_history_snapshot;
    state.steering_state.gyro_continuity = analysis.gyro_continuity_state;
}

}  // namespace

PerceptionFrontend::PerceptionFrontend(port::ICameraAdapter& camera,
                                       port::IPowerMonitorAdapter& power,
                                       RuntimeState& state,
                                       port::DiagnosticSink& diagnostics)
    : camera_(camera), power_(power), state_(state), diagnostics_(diagnostics) {}

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
