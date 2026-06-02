#ifndef LS2K_CONTROL_MOTION_TYPES_HPP
#define LS2K_CONTROL_MOTION_TYPES_HPP

#include <cstdint>

namespace ls2k::control {

/// 运动阶段枚举 —— 表示运动控制生命周期的各个阶段
enum class MotionPhase {
    kDisarmed,          ///< 未就绪（初始状态）
    kStartRequested,    ///< 已请求启动（等待门控确认）
    kSpinup,            ///< 启动加速阶段
    kRunning,           ///< 正常运行阶段
    kStopping,          ///< 减速停止阶段
    kFailSafeLatched   ///< 故障锁存（需要显式复位）
};

/// 运动意图结构 —— 表示外部对运动的请求（启动/停止/复位故障）
struct MotionIntent {
    bool start_requested = false;        ///< 是否请求启动
    bool stop_requested = false;         ///< 是否请求停止
    bool reset_fault_requested = false;  ///< 是否请求复位故障
};

/// 运动监督器状态结构 —— 记录运动阶段机当前状态
struct MotionSupervisorState {
    MotionPhase phase = MotionPhase::kDisarmed;  ///< 当前运动阶段
    uint64_t phase_entry_ms = 0;                 ///< 进入当前阶段的时间戳（ms）
    uint64_t fail_safe_latched_at_ms = 0;        ///< 进入故障锁存的时间戳（ms）
    int clean_gate_cycles = 0;                   ///< 连续的门控确认周期数
    bool last_shaped_command_zero = true;        ///< 上一周期修正后的命令是否为零
    double last_effective_speed_target = 0.0;    ///< 上一周期的有效速度目标
    double stop_entry_speed_target = 0.0;        ///< 进入停止阶段时的速度目标
};

/// 运动监督器输入结构 —— 用于运动决策评估的全部输入
struct MotionSupervisorInputs {
    MotionSupervisorState state{};   ///< 当前运动监督器状态
    MotionIntent intent{};           ///< 运动意图
    bool startup_complete = false;   ///< 启动是否完成
    bool gate_clear = false;         ///< 控制门是否通过（无 veto）
    uint64_t now_ms = 0;             ///< 当前时间戳（ms）
    double running_speed_target = 0.0;  ///< 正常运行速度目标
    int encoder_mean_abs = 0;           ///< 编码器均值绝对值（用于检测停止）
    int motion_unveto_confirm_cycles = 0;    ///< 取消 veto 的确认周期数
    int motion_spinup_ms = 0;               ///< 启动加速持续时间（ms）
    double motion_turn_limit_spinup = 1.0;   ///< 启动阶段的转向限制
    int motion_pwm_step_limit = 0;           ///< PWM 每周期变化限制
    int motion_stop_ms = 0;                  ///< 停止持续时间（ms）
    int motion_stop_encoder_threshold = 0;   ///< 停止判定编码器阈值
    int motion_fault_rearm_hold_ms = 0;      ///< 故障复位的保持时间（ms）
    bool shaped_command_zero = false;        ///< 修正后的命令是否为零
};

/// 运动决策结构 —— 运动监督器评估后输出的完整决策
struct MotionDecision {
    MotionSupervisorState state{};                  ///< 决策后的运动状态
    MotionPhase previous_phase = MotionPhase::kDisarmed;  ///< 决策前的运动阶段
    bool phase_changed = false;                     ///< 阶段是否发生变化
    bool hold_disarmed = true;                      ///< 是否保持未就绪
    bool allow_drive = false;                       ///< 是否允许驱动
    bool require_emergency_stop = false;             ///< 是否需要紧急停止
    bool reset_controllers = false;                  ///< 是否重置控制器
    bool consume_reset_request = false;              ///< 是否消耗复位请求
    bool blocked_start = false;                      ///< 启动是否被阻塞
    bool reset_ready = false;                        ///< 故障恢复是否就绪
    double effective_speed_target = 0.0;             ///< 有效速度目标
    double turn_limit_scale = 1.0;                   ///< 转向限制缩放因子
    int pwm_step_limit = 0;                          ///< PWM 每周期变化限制
};

/// 判断是否为驱动阶段（SPINUP / RUNNING / STOPPING）
bool IsDrivePhase(MotionPhase phase);
/// 将 MotionPhase 枚举转换为字符串
const char* ToString(MotionPhase phase);

}  // namespace ls2k::control

#endif  // LS2K_CONTROL_MOTION_TYPES_HPP
