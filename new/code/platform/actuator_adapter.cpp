#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <string>

namespace ls2k::platform {
namespace {

class ActuatorAdapter final : public port::IActuatorAdapter {
public:
    bool Initialize(const port::HardwareProfile& profile, port::DiagnosticSink& diagnostics) override {
        if (!port::IsEnabled(profile.actuator)) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "actuator.disabled",
                              "actuator subsystem disabled by hardware profile",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return true;
        }

        enabled_ = true;
        adaptation_hook_ = profile.actuator.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.actuator.hook;

        if (adaptation_hook_) {
            ready_ = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "actuator.init.hook",
                              "actuator direct path bypassed; adaptation hook selected: " + hook_name_,
                              port::NowMs()});
            return true;
        }

        if (hook_name_ != "differential-motor-plus-brushless-esc") {
            ready_ = false;
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "actuator.init.unsupported_hook",
                              "unsupported actuator direct-match hook: " + hook_name_,
                              port::NowMs()});
            return false;
        }

        const true_ls2k0300::BridgeStatus motor_init = true_ls2k0300::InitializeMotor();
        if (!motor_init.ok) {
            ready_ = false;
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "actuator.init.motor",
                              "differential motor backend unavailable: " + motor_init.detail,
                              port::NowMs()});
            return false;
        }

        const true_ls2k0300::BridgeStatus esc_init = true_ls2k0300::InitializeBrushlessEsc();
        if (!esc_init.ok) {
            (void)true_ls2k0300::DisableMotorOutput();
            ready_ = false;
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "actuator.init.brushless_esc",
                              "brushless ESC backend unavailable: " + esc_init.detail,
                              port::NowMs()});
            return false;
        }

        ready_ = true;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "actuator.init",
                          "actuator initialized: logical_left maps to pwm=" +
                              std::string(true_ls2k0300::kRightMotorPwmPath) +
                              ", gpio=" + std::string(true_ls2k0300::kRightMotorGpioPath) +
                              "; logical_right maps to pwm=" +
                              std::string(true_ls2k0300::kLeftMotorPwmPath) +
                              ", gpio=" + std::string(true_ls2k0300::kLeftMotorGpioPath) +
                              "; brushless_esc=" + std::string(true_ls2k0300::kBrushlessEsc1PwmPath) +
                              "," + std::string(true_ls2k0300::kBrushlessEsc2PwmPath),
                          port::NowMs()});
        return true;
    }

    bool Apply(const port::ActuatorCommand& command, port::DiagnosticSink& diagnostics) override {
        if (!enabled_ || !ready_) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "actuator.apply.unavailable",
                                   "actuator apply requested while actuator adapter not ready",
                                   port::NowMs()},
                                  1000);
            return false;
        }

        if (adaptation_hook_) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "actuator.hook.apply",
                                   "actuator adaptation hook selected with no concrete implementation: " +
                                       hook_name_ + "; suppressing actuator output",
                                   port::NowMs()},
                                  1000);
            return false;
        }

        if (command.emergency_stop) {
            return DisableAllForApply(diagnostics, "actuator.emergency_stop.failed");
        }

        const true_ls2k0300::BridgeStatus motor_result =
            true_ls2k0300::ApplyMotorCommand(command.left_drive_pwm, command.right_drive_pwm);
        if (!motor_result.ok) {
            DisableAfterFailure();
            ready_ = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "actuator.apply.motor_failed",
                                   motor_result.detail,
                                   port::NowMs()},
                                  1000);
            return false;
        }

        const true_ls2k0300::BridgeStatus esc_result =
            true_ls2k0300::ApplyBrushlessEscCommand(command.left_brushless_pwm,
                                                    command.right_brushless_pwm);
        if (!esc_result.ok) {
            DisableAfterFailure();
            ready_ = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "actuator.apply.brushless_esc_failed",
                                   esc_result.detail,
                                   port::NowMs()},
                                  1000);
            return false;
        }

        return true;
    }

    void Disable(port::DiagnosticSink& diagnostics) override {
        if (!enabled_ || adaptation_hook_) {
            return;
        }
        const true_ls2k0300::BridgeStatus motor_result = true_ls2k0300::DisableMotorOutput();
        if (!motor_result.ok) {
            ready_ = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "actuator.disable.motor_failed",
                                   motor_result.detail,
                                   port::NowMs()},
                                  1000);
        }

        const true_ls2k0300::BridgeStatus esc_result = true_ls2k0300::DisableBrushlessEscOutput();
        if (!esc_result.ok) {
            ready_ = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "actuator.disable.brushless_esc_failed",
                                   esc_result.detail,
                                   port::NowMs()},
                                  1000);
        }
    }

    void Shutdown(port::DiagnosticSink& diagnostics) override {
        Disable(diagnostics);
        ready_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "actuator.shutdown",
                          "actuator adapter shutdown complete",
                          port::NowMs()});
    }

    bool Ready() const override { return ready_; }

private:
    bool DisableAllForApply(port::DiagnosticSink& diagnostics, const char* diagnostic_code) {
        const true_ls2k0300::BridgeStatus motor_result = true_ls2k0300::DisableMotorOutput();
        const true_ls2k0300::BridgeStatus esc_result = true_ls2k0300::DisableBrushlessEscOutput();
        const bool ok = motor_result.ok && esc_result.ok;
        if (!ok) {
            ready_ = false;
            const std::string detail =
                std::string("motor=") + motor_result.detail + "; brushless_esc=" + esc_result.detail;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   diagnostic_code,
                                   detail,
                                   port::NowMs()},
                                  1000);
        }
        return ok;
    }

    void DisableAfterFailure() {
        (void)true_ls2k0300::DisableMotorOutput();
        (void)true_ls2k0300::DisableBrushlessEscOutput();
    }

    bool enabled_ = false;
    bool ready_ = false;
    bool adaptation_hook_ = false;
    std::string hook_name_ = "direct-match";
};

}  // namespace

std::unique_ptr<port::IActuatorAdapter> MakeActuatorAdapter() {
    return std::make_unique<ActuatorAdapter>();
}

}  // namespace ls2k::platform
