#ifndef LS2K_RUNTIME_CONTROL_DECISION_HPP
#define LS2K_RUNTIME_CONTROL_DECISION_HPP

#include <cstdint>

#include "port/actuator_command_types.hpp"
#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

/// 控制门 veto 原因枚举 —— 描述控制循环被否决的具体原因
enum class ControlVetoReason {
    kNone,                      ///< 无 veto
    kPerceptionStale,           ///< 感知数据过时
    kPerceptionInvalid,         ///< 感知数据无效
    kReferenceControlNotReady,  ///< 参考控制未就绪
    kLowVoltage,                ///< 低电压
    kImuInvalid,                ///< IMU 数据无效
    kEncoderInvalid             ///< 编码器数据无效
};

/// 控制施加结果枚举 —— 描述控制命令的施加状态
enum class ControlApplyOutcome {
    kNotRequested,              ///< 未请求施加
    kSuppressedByProfile,       ///< 被电机配置抑制（诊断模式）
    kHeldDisarmedApplied,       ///< 保持未就绪状态施加
    kEmergencyStopApplied,      ///< 紧急停止已施加
    kZeroCommandApplied,        ///< 零命令已施加
    kDriveCommandApplied,       ///< 驱动命令已施加
    kApplyFailed                ///< 施加失败（执行器拒绝）
};

/// 控制门输入结构 —— 描述用于门控评估的全部输入信号
struct ControlGateInputs {
    bool perception_published = false;         ///< 感知结果是否已发布
    bool perception_fresh = false;             ///< 感知结果是否新鲜
    uint64_t perception_capture_time_ms = 0;   ///< 感知帧捕获时间戳
    uint64_t perception_publish_time_ms = 0;   ///< 感知结果发布时间戳
    bool perception_projector_ok = false;      ///< BEV 投影器是否正常
    bool reference_control_ready = false;      ///< 参考控制是否就绪
    bool low_voltage_emergency = false;        ///< 低电压紧急标志
    bool imu_valid = false;                    ///< IMU 数据是否有效
    bool encoder_valid = false;                ///< 编码器数据是否有效
    uint64_t now_ms = 0;                       ///< 当前时间戳（ms）
    int perception_stale_ms = 0;               ///< 感知过期阈值（ms）
};

/// 控制门决策结构 —— 门控评估结果
struct ControlGateDecision {
    bool veto_active = true;                                     ///< 是否否决（默认否决）
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;  ///< 否决原因
};

/// 控制周期输入结构 —— 描述控制周期评估的全部输入
struct ControlCycleInputs {
    ControlGateDecision gate{};                ///< 门控决策
    port::ActuatorCommand command{};           ///< 执行器命令
    MotionPhase motion_phase = MotionPhase::kDisarmed;  ///< 当前运动阶段
    bool apply_ok = false;                     ///< 命令施加是否成功
    bool apply_suppressed_by_profile = false;  ///< 是否被配置抑制
    bool hold_disarmed = false;                ///< 是否保持未就绪
    bool previously_armed = false;             ///< 上一周期是否已就绪
};

/// 控制周期观察结构 —— 控制周期执行后的观察结果
struct ControlCycleObservation {
    bool veto_active = true;                                     ///< veto 是否激活
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;  ///< veto 原因
    MotionPhase motion_phase = MotionPhase::kDisarmed;          ///< 当前运动阶段
    bool hold_disarmed = false;                 ///< 是否保持未就绪
    bool motion_reset_ready = false;            ///< 故障恢复是否就绪
    bool requested_nonzero_output = false;      ///< 是否请求了非零输出
    ControlApplyOutcome apply_outcome = ControlApplyOutcome::kNotRequested;  ///< 施加结果
    int applied_left_pwm = 0;                   ///< 应用后的左轮 PWM
    int applied_right_pwm = 0;                  ///< 应用后的右轮 PWM
    bool actuators_armed = false;               ///< 执行器是否已就绪
    bool arming_transition = false;             ///< 是否发生了就绪状态转换
};

/// 评估控制门：检查各输入条件，确定是否否决当前控制周期
ControlGateDecision EvaluateControlGate(const ControlGateInputs& inputs);
/// 观察控制周期执行结果：分析命令施加状态并输出观察结构
ControlCycleObservation ObserveControlCycle(const ControlCycleInputs& inputs);
/// 判断是否为非零驱动命令
bool IsNonZeroDriveCommand(const port::ActuatorCommand& command);
/// 将控制 veto 原因转换为字符串
const char* ToString(ControlVetoReason reason);
/// 将控制 veto 原因转换为诊断代码字符串
const char* ToDiagnosticCode(ControlVetoReason reason);
/// 将控制施加结果转换为字符串
const char* ToString(ControlApplyOutcome outcome);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DECISION_HPP
