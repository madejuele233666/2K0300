#include "runtime/control_debug_reporter.hpp"

#include <algorithm>
#include <sstream>

namespace ls2k::runtime {

void ControlDebugReporter::Configure(const port::RuntimeParameters& params) {
    interval_ms_ = std::max(1, params.control_snapshot_emit_interval_ms);
}

void ControlDebugReporter::Reset() {
    last_emit_ms_ = 0;
}

void ControlDebugReporter::MaybeEmit(const ControlDebugSnapshot& snapshot, port::DiagnosticSink& diagnostics) {
    if (!snapshot.valid) {
        return;
    }
    const uint64_t now_ms = snapshot.timestamp_ms == 0 ? port::NowMs() : snapshot.timestamp_ms;
    if (last_emit_ms_ != 0 && now_ms >= last_emit_ms_ &&
        now_ms - last_emit_ms_ < static_cast<uint64_t>(interval_ms_)) {
        return;
    }
    last_emit_ms_ = now_ms;

    std::ostringstream message;
    message << "phase=" << ToString(snapshot.motion_phase)
            << " veto=" << (snapshot.veto_active ? "true" : "false")
            << " reason=" << ToString(snapshot.veto_reason)
            << " effective_speed_target=" << snapshot.effective_speed_target
            << " left_target=" << snapshot.left_speed_target
            << " right_target=" << snapshot.right_speed_target
            << " left_measured=" << snapshot.left_measured_speed
            << " right_measured=" << snapshot.right_measured_speed
            << " turn_pwm=" << snapshot.turn_pwm_command
            << " left_pwm=" << snapshot.left_pwm_command
            << " right_pwm=" << snapshot.right_pwm_command
            << " emergency_stop=" << (snapshot.emergency_stop ? "true" : "false");
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.snapshot",
                      message.str(),
                      now_ms});
}

}  // namespace ls2k::runtime
