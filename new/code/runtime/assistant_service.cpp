#include "runtime/assistant_service.hpp"

#include <algorithm>

namespace ls2k::runtime {
namespace {

platform::AssistantWaveformFrame BuildWaveformFrame(const ControlDebugSnapshot& snapshot) {
    platform::AssistantWaveformFrame frame{};
    frame.channel_count = 8;
    frame.values[0] = static_cast<float>(snapshot.effective_speed_target);
    frame.values[1] = static_cast<float>(snapshot.left_speed_target);
    frame.values[2] = static_cast<float>(snapshot.right_speed_target);
    frame.values[3] = static_cast<float>(snapshot.left_measured_speed);
    frame.values[4] = static_cast<float>(snapshot.right_measured_speed);
    frame.values[5] = static_cast<float>(snapshot.turn_pwm_command);
    frame.values[6] = static_cast<float>(snapshot.left_pwm_command);
    frame.values[7] = static_cast<float>(snapshot.right_pwm_command);
    return frame;
}

}  // namespace

void AssistantService::Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    enabled_ = params.assistant_enabled;
    waveform_interval_ms_ = std::max(1, params.assistant_waveform_publish_interval_ms);
    image_interval_ms_ = std::max(1, params.assistant_image_publish_interval_ms);
    last_wave_publish_ms_ = 0;
    last_image_publish_ms_ = 0;
    last_wave_cycle_ = 0;
    last_image_frame_id_ = 0;
    if (!enabled_) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "assistant.disabled",
                          "assistant sidecar disabled by runtime parameters",
                          port::NowMs()});
        return;
    }
    (void)link_.Initialize(params, diagnostics);
}

void AssistantService::Tick(RuntimeState& state, port::DiagnosticSink& diagnostics) {
    if (!configured_ || !enabled_) {
        return;
    }

    link_.Poll(diagnostics);
    if (!link_.Ready()) {
        return;
    }

    ControlDebugSnapshot snapshot{};
    port::CameraCapture capture{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        snapshot = state.control_debug_snapshot;
        capture = state.latest_camera_capture;
    }

    const uint64_t now_ms = port::NowMs();
    if (snapshot.valid && snapshot.cycle_count != last_wave_cycle_ &&
        (last_wave_publish_ms_ == 0 || now_ms - last_wave_publish_ms_ >= static_cast<uint64_t>(waveform_interval_ms_))) {
        if (link_.PublishWaveform(BuildWaveformFrame(snapshot), diagnostics)) {
            last_wave_publish_ms_ = now_ms;
            last_wave_cycle_ = snapshot.cycle_count;
        }
    }

    if (capture.has_frame && capture.frame_id != last_image_frame_id_ &&
        (last_image_publish_ms_ == 0 || now_ms - last_image_publish_ms_ >= static_cast<uint64_t>(image_interval_ms_))) {
        if (link_.PublishImage(capture, diagnostics)) {
            last_image_publish_ms_ = now_ms;
            last_image_frame_id_ = capture.frame_id;
        }
    }
}

}  // namespace ls2k::runtime
