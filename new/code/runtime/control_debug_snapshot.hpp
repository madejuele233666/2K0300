#ifndef LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
#define LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP

/// 控制调试快照结构 —— 记录每帧运动控制、BEV 参考路径和内部转向诊断状态。
/// 用于调试输出、媒体服务协议和离线分析。

#include <cstdint>
#include <cstddef>
#include <string>

#include "runtime/control_decision.hpp"
#include "runtime/motion_types.hpp"
#include "port/visual_element_evidence_types.hpp"

namespace ls2k::runtime {

/// 参考路径调试视图 —— 记录参考路径的来源和模式
struct ReferenceDebugView {
    std::string mode = "none";   ///< 参考路径模式（如 line/curve/none）
    std::string source = "none"; ///< 参考路径来源描述
};

/// 视觉参考调试视图 —— 描述当前帧是否选择了视觉参考及选择原因
struct VisualReferenceDebugView {
    bool present = false;                    ///< 是否存在有效的视觉参考
    std::string source = "none";             ///< 视觉参考来源
    std::string reason = "no_visual_reference_candidate";  ///< 未选择的原因
    std::size_t candidate_count = 0;         ///< 备选参考数量
    std::string rejected_candidate_reason = "none";  ///< 拒绝候选的原因
};

/// 感知健康调试视图 —— 描述 BEV 投影器状态
struct PerceptionHealthDebugView {
    bool projector_ok = false;        ///< 投影器是否正常工作
    std::string reason = "projector_invalid";  ///< 状态原因描述
};

/// 参考可用性调试视图 —— 描述参考路径是否可用于横向误差计算
struct ReferenceEligibilityDebugView {
    bool usable = false;              ///< 参考是否可用
    std::size_t leading_usable_samples = 0;  ///< 前方可用采样点数量
    double leading_min_forward_m = 0.0;      ///< 前方最少可用距离（m）
    double leading_max_forward_m = 0.0;      ///< 前方最大可用距离（m）
    std::string reason = "no_reference_facts";  ///< 不可用原因
};

/// 横向误差调试视图 —— 描述加权横向误差估计结果
struct LateralErrorDebugView {
    bool computed = false;                    ///< 是否已计算横向误差
    double weighted_lateral_error_m = 0.0;    ///< 加权横向误差（m）
    std::size_t weighted_sample_count = 0;     ///< 加权采样点计数
    double weight_sum = 0.0;                   ///< 权重总和
    std::string reason = "reference_unusable"; ///< 未计算原因
};

/// 参考控制就绪调试视图 —— 描述参考控制系统是否就绪
struct ReferenceControlDebugView {
    bool ready = false;                      ///< 参考控制是否就绪
    std::string reason = "reference_unusable";  ///< 未就绪原因
};

/// Reference time alignment 调试视图
struct ReferenceTimeAlignmentDebugView {
    bool enabled = false;
    bool valid = false;
    std::string reason = "disabled";
    std::uint64_t age_ms = 0;
    std::uint64_t reference_capture_time_ms = 0;
    std::uint64_t control_time_ms = 0;
    double delta_s_m = 0.0;
    double delta_yaw_rad = 0.0;
    std::size_t input_sample_count = 0;
    std::size_t aligned_sample_count = 0;
};

/// 安全门调试视图 —— 描述控制是否被安全门 veto
struct SafetyGateDebugView {
    bool veto_active = true;                 ///< veto 是否激活（默认为真，即默认 veto）
    std::string reason = "perception_stale"; ///< veto 原因
};

/// 降级模式调试视图 —— 描述系统是否处于降级运行状态
struct DegradedDebugView {
    bool active = false;          ///< 降级模式是否激活
    std::string reason = "none";  ///< 降级原因
};

/// 偏航控制调试视图 —— 描述偏航控制器的输出目标
struct YawControlDebugView {
    double turn_output_target = 0.0;  ///< 偏航控制输出的转向目标值
};

/// 转向执行器调试视图 —— 描述原始和应用后的转向输出
struct SteeringActuatorDebugView {
    int raw_turn_output = 0;      ///< 原始转向输出（未经过限制）
    int applied_turn_output = 0;  ///< 实际应用的转向输出
};

/// 转向公开快照 —— 只包含 reference/control 最小分层合同，用于媒体服务和遥测
struct SteeringDebugSnapshot {
    bool valid = false;                       ///< 转向快照是否有效
    std::uint64_t frame_id = 0;              ///< 关联的相机帧 ID
    std::uint64_t capture_time_ms = 0;       ///< 帧捕获时间戳（ms）
    int threshold = 0;                        ///< Otsu 二值化阈值
    PerceptionHealthDebugView perception_health{};           ///< 感知健康状态
    port::VisualElementEvidenceFrame element_evidence{};     ///< 视觉元素证据帧
    VisualReferenceDebugView visual_reference{};             ///< 视觉参考选择
    ReferenceDebugView reference{};                          ///< 参考路径信息
    ReferenceEligibilityDebugView eligibility{};             ///< 参考可用性
    LateralErrorDebugView lateral_error{};                   ///< 横向误差估计
    ReferenceTimeAlignmentDebugView reference_time_alignment{}; ///< reference 时间对齐
    ReferenceControlDebugView reference_control{};           ///< 参考控制就绪
    SafetyGateDebugView safety_gate{};                       ///< 安全门状态
    DegradedDebugView degraded{};                            ///< 降级模式
    YawControlDebugView yaw_control{};                       ///< 偏航控制输出
    SteeringActuatorDebugView actuator{};                    ///< 执行器输出
};

/// 转向内部诊断 —— 非权威的内部偏航回路中间量，仅用于调参证据
struct SteeringInternalDebugSnapshot {
    bool valid = false;                       ///< 内部快照是否有效
    std::uint64_t frame_id = 0;              ///< 关联的相机帧 ID
    std::uint64_t capture_time_ms = 0;       ///< 帧捕获时间戳（ms）
    double lateral_error_gain = 0.0;          ///< 横向误差增益
    double speed_scale = 0.0;                 ///< 速度缩放因子
    double turn_output_candidate = 0.0;       ///< 转向输出候选值
    double gyro_z = 0.0;                      ///< 陀螺仪 Z 轴角速度
    double gyro_error = 0.0;                  ///< 陀螺仪误差项
    double gyro_p_term = 0.0;                 ///< 陀螺 PID 比例项
    double gyro_d_term = 0.0;                 ///< 陀螺 PID 微分项
};

/// 控制调试快照 —— 记录每帧的完整运动控制状态，用于诊断、遥测和媒体发布
struct ControlDebugSnapshot {
    bool valid = false;                       ///< 快照是否有效
    uint64_t cycle_count = 0;                 ///< 控制循环周期计数
    uint64_t timestamp_ms = 0;                ///< 快照时间戳（ms）
    MotionPhase motion_phase = MotionPhase::kDisarmed;  ///< 当前运动阶段
    bool veto_active = true;                  ///< 控制门 veto 是否激活
    ControlVetoReason veto_reason = ControlVetoReason::kPerceptionStale;  ///< veto 原因
    bool tuning_mode_enabled = false;          ///< 调参模式是否启用
    bool turn_suppressed = false;              ///< 转向是否被抑制
    bool target_speed_override_enabled = false;  ///< 目标速度覆盖是否启用
    double target_speed_override_value = 0.0;    ///< 目标速度覆盖值
    double effective_speed_target = 0.0;         ///< 有效速度目标
    double left_speed_target = 0.0;              ///< 左轮速度目标
    double right_speed_target = 0.0;             ///< 右轮速度目标
    double left_measured_speed = 0.0;            ///< 左轮实测速度
    double right_measured_speed = 0.0;           ///< 右轮实测速度
    int raw_turn_output = 0;                     ///< 原始转向输出
    int applied_turn_output = 0;                 ///< 应用后的转向输出
    int left_pwm_command = 0;                    ///< 左轮 PWM 命令
    int right_pwm_command = 0;                   ///< 右轮 PWM 命令
    bool emergency_stop = true;                  ///< 紧急停止是否激活
    SteeringDebugSnapshot steering{};            ///< 转向公开快照
    SteeringInternalDebugSnapshot steering_internal{};  ///< 转向内部诊断
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DEBUG_SNAPSHOT_HPP
