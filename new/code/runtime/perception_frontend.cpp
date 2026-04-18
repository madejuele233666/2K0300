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
    fallback.geometry_veto = true;
    fallback.emergency_veto = true;
    fallback.perception_tag = "injected-drop-frame";
    return fallback;
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
        const port::PerceptionResult fallback = BuildDroppedFrameFallback(capture);
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
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
        state_.perception = fallback;
        ++state_.perception_publish_count;
        return;
    }

    port::PerceptionResult perception =
        legacy::AnalyzeFrame(capture.frame,
                             params,
                             state_.low_voltage_emergency.load(),
                             capture.frame_id,
                             capture.capture_time_ms);

    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    state_.perception = perception;
    ++state_.perception_publish_count;
}

}  // namespace ls2k::runtime
