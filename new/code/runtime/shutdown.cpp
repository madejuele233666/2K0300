#include "runtime/shutdown.hpp"

namespace ls2k::runtime {

void RunShutdown(port::PlatformBundle& platform, RuntimeState& state, port::DiagnosticSink& diagnostics) {
    state.stop_requested.store(true);
    state.exit_requested.store(true);
    if (platform.timer) {
        platform.timer->Stop(diagnostics);
    }
    state.timer_started = false;

    if (platform.motor) {
        platform.motor->Disable(diagnostics);
    }
    state.actuators_armed = false;
    state.control_observation = {};

    if (platform.camera) {
        platform.camera->Shutdown(diagnostics);
    }
    if (platform.imu) {
        platform.imu->Shutdown(diagnostics);
    }
    if (platform.encoder) {
        platform.encoder->Shutdown(diagnostics);
    }
    if (platform.motor) {
        platform.motor->Shutdown(diagnostics);
    }

    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "shutdown.complete",
                      "actuators disabled and resources released",
                      port::NowMs()});
}

}  // namespace ls2k::runtime
