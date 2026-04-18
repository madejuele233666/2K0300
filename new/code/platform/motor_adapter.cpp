#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <string>

namespace ls2k::platform {
namespace {

class MotorAdapter final : public port::IMotorAdapter {
public:
    bool Initialize(const port::HardwareProfile& profile, port::DiagnosticSink& diagnostics) override {
        if (!port::IsEnabled(profile.motor)) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "motor.disabled",
                              "motor subsystem disabled by hardware profile",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return true;
        }

        enabled_ = true;
        adaptation_hook_ = profile.motor.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.motor.hook;

        if (adaptation_hook_) {
            ready_ = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "motor.init.hook",
                              "motor direct path bypassed; adaptation hook selected: " + hook_name_,
                              port::NowMs()});
            return true;
        }

        const true_ls2k0300::BridgeStatus init = true_ls2k0300::InitializeMotor();
        ready_ = init.ok;

        diagnostics.Emit({ready_ ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kFailSafe,
                          "motor.init",
                          ready_ ? "motor initialized through true_ls2k0300 bridge: logical_left maps to pwm=" +
                                       std::string(true_ls2k0300::kRightMotorPwmPath) +
                                       ", gpio=" + std::string(true_ls2k0300::kRightMotorGpioPath) +
                                       "; logical_right maps to pwm=" +
                                       std::string(true_ls2k0300::kLeftMotorPwmPath) +
                                       ", gpio=" + std::string(true_ls2k0300::kLeftMotorGpioPath)
                                 : "motor backend unavailable: " + init.detail,
                          port::NowMs()});
        return ready_;
    }

    bool Apply(const port::ActuatorCommand& command, port::DiagnosticSink& diagnostics) override {
        if (!enabled_ || !ready_) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "motor.apply.unavailable",
                                   "motor apply requested while motor adapter not ready",
                                   port::NowMs()},
                                  1000);
            return false;
        }

        if (adaptation_hook_) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "motor.hook.apply",
                                   "motor adaptation hook selected with no concrete phase-1 implementation: " +
                                       hook_name_ + "; suppressing actuator output",
                                   port::NowMs()},
                                  1000);
            return false;
        }

        if (command.emergency_stop) {
            const true_ls2k0300::BridgeStatus result = true_ls2k0300::DisableMotorOutput();
            if (!result.ok) {
                ready_ = false;
                port::EmitRateLimited(diagnostics,
                                      {port::DiagnosticLevel::kFailSafe,
                                       "motor.emergency_stop.failed",
                                       result.detail,
                                       port::NowMs()},
                                      1000);
                return false;
            }
            return true;
        }

        const true_ls2k0300::BridgeStatus result =
            true_ls2k0300::ApplyMotorCommand(command.left_pwm, command.right_pwm);
        if (!result.ok) {
            const true_ls2k0300::BridgeStatus disable_result = true_ls2k0300::DisableMotorOutput();
            ready_ = false;
            const std::string detail = disable_result.ok ? result.detail
                                                         : (result.detail + "; disable failed: " + disable_result.detail);
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "motor.apply.failed",
                                   detail,
                                   port::NowMs()},
                                  1000);
            return false;
        }
        return true;
    }

    void Disable(port::DiagnosticSink& diagnostics) override {
        if (enabled_ && !adaptation_hook_) {
            const true_ls2k0300::BridgeStatus result = true_ls2k0300::DisableMotorOutput();
            if (!result.ok) {
                ready_ = false;
                port::EmitRateLimited(diagnostics,
                                      {port::DiagnosticLevel::kWarning,
                                       "motor.disable.failed",
                                       result.detail,
                                       port::NowMs()},
                                      1000);
            }
        }
    }

    void Shutdown(port::DiagnosticSink& diagnostics) override {
        Disable(diagnostics);
        ready_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "motor.shutdown",
                          "motor adapter shutdown complete",
                          port::NowMs()});
    }

    bool Ready() const override { return ready_; }

private:
    bool enabled_ = false;
    bool ready_ = false;
    bool adaptation_hook_ = false;
    std::string hook_name_ = "direct-match";
};

}  // namespace

std::unique_ptr<port::IMotorAdapter> MakeMotorAdapter() {
    return std::make_unique<MotorAdapter>();
}

}  // namespace ls2k::platform
