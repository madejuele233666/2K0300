#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <string>

namespace ls2k::platform {
namespace {

/**
 * 电机适配器 —— 实现 port::IMotorAdapter 接口。
 * 负责控制左右两个直流电机的 PWM 和 GPIO 输出。
 * 支持硬件直连模式和钩子（adaptation hook）模式。
 */
class MotorAdapter final : public port::IMotorAdapter {
public:
    /**
     * 初始化电机适配器。
     * 根据硬件配置文件的 motor 子系统信息决定是否启用电机。
     * 如果选择 adaptation hook 模式则绕过底层桥接，否则通过 true_ls2k0300 桥接初始化电机。
     * @param profile 硬件配置文件，包含 motor 子系统的模式和钩子名称
     * @param diagnostics 诊断输出接收器，用于输出初始化状态日志
     * @return true 表示初始化成功（即使被禁用也算成功），false 表示初始化失败
     */
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

    /**
     * 应用执行器命令，控制左右电机 PWM 输出。
     * 如果电机未就绪或处于钩子模式，则抑制输出并返回 false。
     * 支持紧急停止（emergency_stop）特殊处理 —— 立即关闭电机输出。
     * 正常模式下计算并写入左右电机的 PWM 占空比。
     * @param command 执行器指令，含左右轮 PWM 值和急停标志
     * @param diagnostics 诊断输出接收器，用于输出执行结果日志
     * @return true 表示指令执行成功，false 表示执行失败
     */
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

    /**
     * 禁用电机输出。
     * 仅在电机已启用且非 adaptation hook 模式时执行禁用操作。
     * 禁用失败时会设置 ready_ = false 并发出诊断警告。
     * @param diagnostics 诊断输出接收器
     */
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

    /**
     * 关闭电机适配器 —— 先禁用电机输出，再标记为未就绪。
     * @param diagnostics 诊断输出接收器
     */
    void Shutdown(port::DiagnosticSink& diagnostics) override {
        Disable(diagnostics);
        ready_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "motor.shutdown",
                          "motor adapter shutdown complete",
                          port::NowMs()});
    }

    /**
     * 查询电机适配器是否已就绪（已初始化且无故障）。
     * @return true 表示电机已就绪可用
     */
    bool Ready() const override { return ready_; }

private:
    /** 电机子系统在硬件配置中是否被启用 */
    bool enabled_ = false;
    /** 电机适配器是否已初始化就绪 */
    bool ready_ = false;
    /** 是否使用 adaptation hook 模式（绕过直连硬件） */
    bool adaptation_hook_ = false;
    /** 钩子名称，标识具体的 adaptation hook 实现 */
    std::string hook_name_ = "direct-match";
};

}  // namespace

/**
 * 创建电机适配器实例（工厂函数）。
 * @return 指向 IMotorAdapter 接口的唯一指针
 */
std::unique_ptr<port::IMotorAdapter> MakeMotorAdapter() {
    return std::make_unique<MotorAdapter>();
}

}  // namespace ls2k::platform
