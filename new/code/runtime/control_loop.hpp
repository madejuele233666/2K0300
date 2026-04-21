#ifndef LS2K_RUNTIME_CONTROL_LOOP_HPP
#define LS2K_RUNTIME_CONTROL_LOOP_HPP

#include <atomic>
#include <cstdint>

#include "legacy/attitude_logic.hpp"
#include "legacy/motor_logic.hpp"
#include "legacy/pid_control.hpp"
#include "legacy/wheel_pid.hpp"
#include "legacy/wheel_target_mixer.hpp"
#include "port/platform_adapter.hpp"
#include "runtime/control_debug_reporter.hpp"
#include "runtime/motion_supervisor.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class ControlLoop {
public:
    ControlLoop(port::PlatformBundle& platform,
                const port::HardwareProfile& profile,
                RuntimeState& state,
                port::DiagnosticSink& diagnostics);

    bool Start(const port::RuntimeParameters& params);
    void Stop();

private:
    void HandleTimerFailure();
    void ResetDisarmedControlState();
    void LatchTimerFailureState(uint64_t now_ms);
    void Tick();

    port::PlatformBundle& platform_;
    const port::HardwareProfile& profile_;
    RuntimeState& state_;
    port::DiagnosticSink& diagnostics_;
    port::RuntimeParameters params_{};
    std::atomic<bool> running_{false};

    legacy::LegacyPidControl pid_{};
    legacy::LegacyMotorLogic motor_logic_{};
    legacy::LegacyAttitudeLogic attitude_{};
    legacy::WheelTargetMixer wheel_target_mixer_{};
    legacy::WheelPidController left_wheel_pid_{};
    legacy::WheelPidController right_wheel_pid_{};
    ControlDebugReporter debug_reporter_{};
    MotionSupervisor motion_supervisor_{};
    bool have_gate_interval_ = false;
    bool last_gate_veto_ = true;
    ControlVetoReason last_gate_reason_ = ControlVetoReason::kPerceptionStale;
    uint64_t gate_interval_start_ms_ = 0;
    bool gate_interval_reported_ = false;
    bool motion_reset_ready_reported_ = false;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_LOOP_HPP
