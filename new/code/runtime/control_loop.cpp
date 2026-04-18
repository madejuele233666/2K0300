#include "runtime/control_loop.hpp"

#include <algorithm>
#include <string>

namespace ls2k::runtime {
namespace {

ControlGateInputs BuildControlGateInputs(const port::PerceptionResult& perception,
                                         const port::ImuSample& imu,
                                         const port::EncoderDelta& encoder,
                                         bool low_voltage_emergency,
                                         uint64_t now_ms,
                                         const port::RuntimeParameters& params) {
    ControlGateInputs inputs{};
    inputs.perception_published = perception.published;
    inputs.perception_fresh = perception.fresh;
    inputs.perception_capture_time_ms = perception.capture_time_ms;
    inputs.perception_emergency_veto = perception.emergency_veto;
    inputs.low_voltage_emergency = low_voltage_emergency;
    inputs.imu_valid = imu.valid;
    inputs.encoder_valid = encoder.valid;
    inputs.now_ms = now_ms;
    inputs.perception_stale_ms = params.perception_stale_ms;
    return inputs;
}

void EmitVetoDiagnostics(port::DiagnosticSink& diagnostics,
                         const ControlGateDecision& decision,
                         uint64_t now_ms) {
    if (!decision.veto_active) {
        return;
    }

    const std::string reason = ToString(decision.veto_reason);
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kFailSafe,
                           "control.veto",
                           "vetoing actuator update due to " + reason,
                           now_ms},
                          1000);
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kFailSafe,
                           ToDiagnosticCode(decision.veto_reason),
                           "dominant control veto reason: " + reason,
                           now_ms},
                          1000);
}

void EmitGateIntervalDiagnostics(port::DiagnosticSink& diagnostics,
                                 bool& have_gate_interval,
                                 bool& last_gate_veto,
                                 ControlVetoReason& last_gate_reason,
                                 uint64_t& gate_interval_start_ms,
                                 bool& gate_interval_reported,
                                 const ControlGateDecision& current,
                                 uint64_t now_ms) {
    if (!have_gate_interval) {
        have_gate_interval = true;
        last_gate_veto = current.veto_active;
        last_gate_reason = current.veto_reason;
        gate_interval_start_ms = now_ms;
        gate_interval_reported = false;
        return;
    }

    const bool state_changed = current.veto_active != last_gate_veto ||
                               (current.veto_active && current.veto_reason != last_gate_reason);
    if (state_changed) {
        const uint64_t duration_ms = now_ms >= gate_interval_start_ms ? now_ms - gate_interval_start_ms : 0;
        const std::string previous_reason = last_gate_veto ? ToString(last_gate_reason) : "none";
        diagnostics.Emit({last_gate_veto ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                          last_gate_veto ? "control.veto.interval.closed" : "control.unveto.interval.closed",
                          std::string("previous ") + (last_gate_veto ? "veto" : "non-veto") +
                              " interval closed after " + std::to_string(duration_ms) +
                              " ms (reason=" + previous_reason + ")",
                          now_ms});
        last_gate_veto = current.veto_active;
        last_gate_reason = current.veto_reason;
        gate_interval_start_ms = now_ms;
        gate_interval_reported = false;
    }

    if (!gate_interval_reported) {
        const uint64_t duration_ms = now_ms >= gate_interval_start_ms ? now_ms - gate_interval_start_ms : 0;
        if (duration_ms >= 200) {
            diagnostics.Emit({current.veto_active ? port::DiagnosticLevel::kWarning
                                                  : port::DiagnosticLevel::kInfo,
                              current.veto_active ? "control.veto.sustained" : "control.unveto.sustained",
                              current.veto_active
                                  ? "control remained vetoed for " + std::to_string(duration_ms) +
                                        " ms with dominant reason=" + ToString(current.veto_reason)
                                  : "control remained non-veto for " + std::to_string(duration_ms) +
                                        " ms; actuator path stayed eligible for drive application",
                              now_ms});
            gate_interval_reported = true;
        }
    }
}

void EmitObservationDiagnostics(port::DiagnosticSink& diagnostics,
                                const ControlCycleObservation& previous,
                                const ControlCycleObservation& current,
                                uint64_t now_ms) {
    if (current.requested_nonzero_output) {
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kInfo,
                               "control.command.requested_nonzero",
                               "non-zero differential-drive command requested",
                               now_ms},
                              1000);
    }

    switch (current.apply_outcome) {
        case ControlApplyOutcome::kSuppressedByProfile:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "control.apply.suppressed",
                                   "drive command suppressed because the active motor profile is diagnostics-only",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kDriveCommandApplied:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "control.apply.drive",
                                   "drive command applied to motor adapter",
                                   now_ms},
                                  1000);
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "control.apply.command",
                                   "logical drive command left_pwm=" + std::to_string(current.applied_left_pwm) +
                                       " right_pwm=" + std::to_string(current.applied_right_pwm),
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kEmergencyStopApplied:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "control.apply.emergency_stop",
                                   "emergency-stop command applied to keep actuators fail-safe",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kApplyFailed:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "control.apply.failed",
                                   "motor adapter rejected the current control command",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kZeroCommandApplied:
        case ControlApplyOutcome::kNotRequested:
            break;
    }

    if (current.arming_transition) {
        const std::string message =
            std::string("actuator armed state transitioned from ") +
            (previous.actuators_armed ? "armed" : "disarmed") + " to " +
            (current.actuators_armed ? "armed" : "disarmed") +
            " (apply_outcome=" + ToString(current.apply_outcome) + ")";
        diagnostics.Emit({current.actuators_armed ? port::DiagnosticLevel::kInfo
                                                  : port::DiagnosticLevel::kFailSafe,
                          "control.arm.transition",
                          message,
                          now_ms});
    }
}

}  // namespace

ControlLoop::ControlLoop(port::PlatformBundle& platform,
                         const port::HardwareProfile& profile,
                         RuntimeState& state,
                         port::DiagnosticSink& diagnostics)
    : platform_(platform), profile_(profile), state_(state), diagnostics_(diagnostics) {}

bool ControlLoop::Start(const port::RuntimeParameters& params) {
    if (running_) {
        return true;
    }
    if (!state_.startup_complete) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.startup.incomplete",
                           "control loop refused to start before startup completion",
                           port::NowMs()});
        return false;
    }
    if (!platform_.motor) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.motor.not_ready",
                           "control loop refused to start because motor adapter is missing",
                           port::NowMs()});
        return false;
    }

    const bool degraded_motor_disabled = state_.degraded_startup &&
                                         profile_.motor.mode == port::SubsystemMode::kDisabled;
    if (!platform_.motor->Ready() && !degraded_motor_disabled) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.motor.not_ready",
                           "control loop refused to arm because motor adapter is not ready",
                           port::NowMs()});
        return false;
    }
    if (degraded_motor_disabled) {
        diagnostics_.Emit({port::DiagnosticLevel::kWarning,
                           "control.start.degraded.motor_disabled",
                           "degraded startup keeps control loop running with motor disabled; actuators stay disarmed",
                           port::NowMs()});
    }

    params_ = params;
    pid_.Configure(params_);

    state_.timer_started = false;
    state_.actuators_armed = false;
    state_.control_observation = {};

    running_ = true;
    const bool timer_ok = platform_.timer->Start(
        profile_.timer,
        static_cast<uint32_t>(std::max(1, params_.control_period_ms)),
        [this]() { Tick(); },
        diagnostics_);
    if (!timer_ok) {
        running_ = false;
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.timer.start",
                           "control timer failed to start; actuators remain disarmed",
                           port::NowMs()});
        return false;
    }

    state_.timer_started = true;
    const bool low_voltage_block = state_.low_voltage_emergency.load();
    const bool motor_hook_block = profile_.motor.mode == port::SubsystemMode::kAdaptationHook;
    const bool motor_disabled_block = profile_.motor.mode == port::SubsystemMode::kDisabled;
    if (low_voltage_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.low_voltage",
                           "low-voltage emergency active; actuators stay disarmed until cleared",
                           port::NowMs()});
    }
    if (motor_hook_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.motor_hook",
                           "motor adaptation hook has no phase-1 implementation; actuators stay disarmed",
                           port::NowMs()});
    }
    if (motor_disabled_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.motor_disabled",
                           "motor profile is disabled; control loop stays diagnostics-only with actuators disarmed",
                           port::NowMs()});
    }
    diagnostics_.Emit({port::DiagnosticLevel::kInfo,
                       "control.start",
                       "control loop started with PIT-equivalent periodic callback",
                       port::NowMs()});
    return true;
}

void ControlLoop::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    platform_.timer->Stop(diagnostics_);
    state_.timer_started = false;
    if (platform_.motor) {
        platform_.motor->Disable(diagnostics_);
    }
    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.actuators_armed = false;
        state_.last_command = {};
        state_.control_observation = {};
    }
}

void ControlLoop::Tick() {
    if (!running_) {
        return;
    }

    const port::LowVoltageSample low_voltage = platform_.power->SampleLowVoltage(diagnostics_);
    const bool low_voltage_emergency = !low_voltage.valid || low_voltage.emergency;
    const bool previous_low_voltage = state_.low_voltage_emergency.load();
    if (low_voltage_emergency != previous_low_voltage) {
        diagnostics_.Emit({low_voltage_emergency ? port::DiagnosticLevel::kFailSafe
                                                 : port::DiagnosticLevel::kInfo,
                           "power.low_voltage.transition",
                           low_voltage_emergency ? "runtime low-voltage state transitioned to emergency"
                                                 : "runtime low-voltage state cleared",
                           port::NowMs()});
    }
    state_.low_voltage_emergency.store(low_voltage_emergency);

    const port::ImuSample imu = platform_.imu->Read(diagnostics_);
    const port::EncoderDelta encoder = platform_.encoder->ReadDelta(diagnostics_);

    port::PerceptionResult perception{};
    ControlCycleObservation previous_observation{};
    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.imu = imu;
        state_.encoder = encoder;
        perception = state_.perception;
        previous_observation = state_.control_observation;
    }

    const uint64_t now_ms = port::NowMs();
    const ControlGateDecision gate =
        EvaluateControlGate(BuildControlGateInputs(perception, imu, encoder, low_voltage_emergency, now_ms, params_));
    EmitVetoDiagnostics(diagnostics_, gate, now_ms);
    EmitGateIntervalDiagnostics(diagnostics_,
                                have_gate_interval_,
                                last_gate_veto_,
                                last_gate_reason_,
                                gate_interval_start_ms_,
                                gate_interval_reported_,
                                gate,
                                now_ms);

    port::ActuatorCommand command{};
    if (!gate.veto_active) {
        attitude_.UpdateFromImu(imu, static_cast<float>(params_.control_period_ms) / 1000.0F);
        const float w_target = pid_.ComputeTurnTarget(perception, state_.W_Target_last);
        const float turn_output = pid_.ComputeGyroTurn(w_target, imu.gyro_z);
        const double measured_speed = static_cast<double>(encoder.left + encoder.right) * 0.5;
        const int mean_pwm =
            pid_.ComputeMeanSpeedPwm(params_.Speed_base, measured_speed, params_.pwm_limit);
        command = motor_logic_.Mix(mean_pwm, turn_output, false, params_.pwm_limit);
    } else {
        // Keep controller internals frozen while fail-safe veto is active.
        command = port::ActuatorCommand{};
    }

    const bool diagnostics_only_motor =
        profile_.motor.mode == port::SubsystemMode::kAdaptationHook ||
        profile_.motor.mode == port::SubsystemMode::kDisabled;
    bool apply_ok = true;
    if (diagnostics_only_motor) {
        platform_.motor->Disable(diagnostics_);
    } else {
        apply_ok = platform_.motor->Apply(command, diagnostics_);
    }
    const ControlCycleObservation observation =
        ObserveControlCycle({gate, command, apply_ok, diagnostics_only_motor, previous_observation.actuators_armed});
    EmitObservationDiagnostics(diagnostics_, previous_observation, observation, now_ms);

    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.last_command = (apply_ok && !diagnostics_only_motor) ? command : port::ActuatorCommand{};
        state_.control_observation = observation;
        state_.actuators_armed = observation.actuators_armed;
        if (observation.apply_outcome == ControlApplyOutcome::kDriveCommandApplied) {
            state_.bcount = std::min(state_.bcount + 1, 200);
        }
    }
    ++state_.control_cycle_count;
}

}  // namespace ls2k::runtime
