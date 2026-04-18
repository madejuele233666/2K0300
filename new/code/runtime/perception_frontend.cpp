#include "runtime/perception_frontend.hpp"

#include "legacy/camera_logic.hpp"

namespace ls2k::runtime {

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

    // Foreground frame-ready path. Heavy perception stays outside PIT callback.
    const port::CameraCapture capture = camera_.Capture(diagnostics_);
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
