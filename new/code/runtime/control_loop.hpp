#ifndef LS2K_RUNTIME_CONTROL_LOOP_HPP
#define LS2K_RUNTIME_CONTROL_LOOP_HPP

#include <atomic>
#include <cstdint>

#include "legacy/actuator_command_builder.hpp"
#include "legacy/steering_yaw_controller.hpp"
#include "legacy/wheel_pid.hpp"
#include "legacy/wheel_target_mixer.hpp"
#include "port/platform_adapter.hpp"
#include "runtime/control_debug_reporter.hpp"
#include "runtime/motion_supervisor.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 主控制循环 —— 管理运行时运动控制的周期性执行。
/// 负责传感器采样、门控评估、运动监督、转向计算和执行器输出。
class ControlLoop {
public:
    /// 构造控制循环
    /// @param platform     平台适配器集合
    /// @param profile      硬件配置文件
    /// @param state        运行时状态
    /// @param diagnostics  诊断输出接口
    ControlLoop(port::PlatformBundle& platform,
                const port::HardwareProfile& profile,
                RuntimeState& state,
                port::DiagnosticSink& diagnostics);

    /// 启动控制循环：校验状态、配置 PID、注册定时器
    /// @param params  运行时参数
    /// @return        是否成功启动
    bool Start(const port::RuntimeParameters& params);
    /// 停止控制循环：停止定时器、禁用执行器、复位状态
    void Stop();

private:
    /// 处理定时器故障事件（进入 FAIL_SAFE_LATCHED 并请求退出）
    void HandleTimerFailure();
    /// 复位到 DISARMED 状态
    void ResetDisarmedControlState();
    /// 锁存定时器故障状态
    void LatchTimerFailureState(uint64_t now_ms);
    /// 控制定时器心跳 —— 主控制循环单次迭代
    void Tick();

    port::PlatformBundle& platform_;                     ///< 平台适配器集合引用
    const port::HardwareProfile& profile_;               ///< 硬件配置引用
    RuntimeState& state_;                                ///< 运行时状态引用
    port::DiagnosticSink& diagnostics_;                  ///< 诊断输出引用
    port::RuntimeParameters params_{};                   ///< 运行时参数副本
    std::atomic<bool> running_{false};                   ///< 循环是否正在运行

    legacy::SteeringYawController yaw_controller_{};     ///< 偏航控制器
    legacy::ActuatorCommandBuilder actuator_command_builder_{};  ///< 执行器命令构造器
    legacy::WheelTargetMixer wheel_target_mixer_{};      ///< 轮速目标混合器
    legacy::WheelPidController left_wheel_pid_{};        ///< 左轮 PID 控制器
    legacy::WheelPidController right_wheel_pid_{};       ///< 右轮 PID 控制器
    ControlDebugReporter debug_reporter_{};              ///< 调试报告器
    MotionSupervisor motion_supervisor_{};               ///< 运动监督器
    port::SteeringControlMemory steering_control_memory_{};  ///< 转向控制记忆
    bool have_gate_interval_ = false;                    ///< 是否已开始门控区间跟踪
    bool last_gate_veto_ = true;                         ///< 上一门控周期是否 veto
    ControlVetoReason last_gate_reason_ = ControlVetoReason::kPerceptionStale;  ///< 上一周期 veto 原因
    uint64_t gate_interval_start_ms_ = 0;                ///< 当前门控区间起始时间
    bool gate_interval_reported_ = false;                ///< 当前区间是否已报告
    bool motion_reset_ready_reported_ = false;           ///< 故障复位就绪是否已报告
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_LOOP_HPP
