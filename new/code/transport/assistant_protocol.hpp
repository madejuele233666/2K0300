#ifndef LS2K_TRANSPORT_ASSISTANT_PROTOCOL_HPP
#define LS2K_TRANSPORT_ASSISTANT_PROTOCOL_HPP

// 辅助协议定义 —— 外部助手通信的消息类型、状态视图和编解码接口。
// 基于 JSON 行协议，支持命令下发和遥测上传。

#include <cstdint>
#include <string>

#include "port/visual_element_evidence_types.hpp"

namespace ls2k::transport {

/// @brief 助手命令类型枚举
enum class AssistantCommandType {
    kStart,               ///< 启动运动
    kStop,                ///< 停止运动
    kEnableTuningMode,    ///< 启用调优模式
    kDisableTuningMode,   ///< 禁用调优模式
    kSetTurnSuppressed,   ///< 设置转向抑制
    kSetTargetSpeed,      ///< 设置目标速度
};

/// @brief 一条助手命令的完整描述
struct AssistantCommand {
    AssistantCommandType type = AssistantCommandType::kStart;  ///< 命令类型
    std::uint64_t seq = 0;                                     ///< 命令序列号（用于应答匹配）
    bool bool_value = false;                                   ///< 布尔参数（如转向抑制）
    double target_speed_value = 0.0;                           ///< 速度参数（单位：m/s）
    int ttl_ms = 0;                                            ///< 超时时间（毫秒，仅限速度覆盖）
};

/// @brief 助手入站消息类型枚举
enum class AssistantInboundMessageType {
    kCommand,        ///< 有效命令
    kAckRejected,    ///< 命令确认被拒绝
    kInputRejected,  ///< 输入格式错误被拒绝
};

/// @brief 一条已解码的助手入站消息
struct AssistantInboundMessage {
    AssistantInboundMessageType type = AssistantInboundMessageType::kInputRejected;  ///< 消息类型
    AssistantCommand command{};                                                       ///< 命令内容（仅 kCommand 时有效）
    std::uint64_t seq = 0;                                                           ///< 相关序列号
    std::string reason;                                                              ///< 拒绝原因（如适用）
};

/// @brief 助手状态快照视图
struct AssistantStatusView {
    bool tuning_mode_enabled = false;              ///< 调优模式是否启用
    bool turn_suppressed = false;                  ///< 转向是否被抑制
    bool target_speed_override_enabled = false;    ///< 目标速度覆盖是否启用
    double target_speed_override_value = 0.0;      ///< 目标速度覆盖值（m/s）
    double effective_speed_target = 0.0;           ///< 当前生效的速度目标（m/s）
};

/// @brief 参考路径视图
struct AssistantReferenceView {
    std::string mode = "none";    ///< 参考模式（如 path / waypoint）
    std::string source = "none";  ///< 参考来源描述
};

/// @brief 视觉参考视图
struct AssistantVisualReferenceView {
    bool present = false;                              ///< 是否存在视觉参考
    std::string source = "none";                       ///< 视觉信息来源
    std::string reason = "no_visual_reference_candidate";  ///< 状态说明 / 原因
    std::uint64_t candidate_count = 0;                 ///< 候选数量
    std::string rejected_candidate_reason = "none";     ///< 候选被拒原因
};

/// @brief 感知健康状态视图
struct AssistantPerceptionHealthView {
    bool projector_ok = false;                 ///< 投影仪状态是否正常
    std::string reason = "projector_invalid";  ///< 不健康的原因
};

/// @brief 参考合格性视图（判断参考路径是否可用于控制）
struct AssistantEligibilityView {
    bool usable = false;                      ///< 是否可用
    std::uint64_t leading_usable_samples = 0; ///< 领先侧的可用样本数
    double leading_min_forward_m = 0.0;       ///< 领先侧最小前进距离（米）
    double leading_max_forward_m = 0.0;       ///< 领先侧最大前进距离（米）
    std::string reason = "no_reference_facts"; ///< 不可用原因
};

/// @brief 横向误差视图
struct AssistantLateralErrorView {
    bool computed = false;                      ///< 是否已计算横向误差
    double weighted_lateral_error_m = 0.0;      ///< 加权横向误差（米）
    std::uint64_t weighted_sample_count = 0;    ///< 加权样本数量
    double weight_sum = 0.0;                     ///< 权重总和
    std::string reason = "reference_unusable";   ///< 无法计算的原因
};

/// @brief 参考跟踪几何视图
struct AssistantTrackingGeometryView {
    bool computed = false;                    ///< 是否已计算跟踪几何
    double lateral_offset_m = 0.0;            ///< 横向位置项（米）
    double heading_error_rad = 0.0;           ///< 航向误差项（弧度）
    double curvature_m_inv = 0.0;             ///< 曲率前馈项（1/米）
    std::uint64_t sample_count = 0;           ///< 拟合样本数量
    std::string reason = "reference_unusable"; ///< 无法计算的原因
};

/// @brief 参考控制状态视图
struct AssistantReferenceControlView {
    bool ready = false;                          ///< 参考控制是否准备就绪
    std::string reason = "reference_unusable";    ///< 未就绪的原因
};

/// @brief 安全门视图（安全保护逻辑的状态）
struct AssistantSafetyGateView {
    bool veto_active = true;                     ///< 安全否决是否激活
    std::string reason = "perception_stale";      ///< 否决激活的原因
};

/// @brief 降级模式视图
struct AssistantDegradedView {
    bool active = false;                         ///< 降级模式是否激活
    std::string reason = "none";                  ///< 降级原因
};

/// @brief 偏航控制视图
struct AssistantYawControlView {
    double turn_output_target = 0.0;  ///< 偏航（转向）输出目标值
    double lateral_term = 0.0;        ///< 横向位置修正项
    double heading_term = 0.0;        ///< 航向误差修正项
    double curvature_term = 0.0;      ///< 曲率前馈项
};

/// @brief 视觉元素证据视图类型别名
using AssistantElementEvidenceView = port::VisualElementEvidenceFrame;

/// @brief 完整助手遥测数据视图
///
/// 包含运动阶段、感知健康、元素证据、参考路径、合格性、横向误差、
/// 参考控制、安全门、降级、偏航控制、执行器以及速度/PWM 测量值。
struct AssistantTelemetryView {
    std::string motion_phase = "DISARMED";       ///< 运动阶段描述
    AssistantPerceptionHealthView perception_health{};  ///< 感知健康视图
    AssistantElementEvidenceView element_evidence{};    ///< 元素证据帧
    AssistantVisualReferenceView visual_reference{};    ///< 视觉参考视图
    AssistantReferenceView reference{};                 ///< 参考路径视图
    AssistantEligibilityView eligibility{};             ///< 合格性视图
    AssistantLateralErrorView lateral_error{};           ///< 横向误差视图
    AssistantTrackingGeometryView tracking_geometry{};   ///< 跟踪几何视图
    AssistantReferenceControlView reference_control{};   ///< 参考控制视图
    AssistantSafetyGateView safety_gate{};               ///< 安全门视图
    AssistantDegradedView degraded{};                    ///< 降级模式视图
    AssistantYawControlView yaw_control{};               ///< 偏航控制视图
    bool tuning_mode_enabled = false;             ///< 调优模式是否启用
    bool turn_suppressed = false;                 ///< 转向是否抑制
    bool target_speed_override_enabled = false;   ///< 目标速度覆盖是否启用
    double target_speed_override_value = 0.0;     ///< 目标速度覆盖值（m/s）
    double effective_speed_target = 0.0;          ///< 当前生效速度目标（m/s）
    double left_speed_target = 0.0;               ///< 左轮速度目标（m/s）
    double right_speed_target = 0.0;              ///< 右轮速度目标（m/s）
    double left_measured_speed = 0.0;             ///< 左轮实测速度（m/s）
    double right_measured_speed = 0.0;            ///< 右轮实测速度（m/s）
    int raw_turn_output = 0;                      ///< 原始转向输出值
    int applied_turn_output = 0;                  ///< 实际施加的转向输出值
    int left_drive_pwm_command = 0;               ///< 左驱动 PWM 指令值
    int right_drive_pwm_command = 0;              ///< 右驱动 PWM 指令值
    int left_brushless_pwm_command = 0;           ///< 左无刷电调 PWM 指令值
    int right_brushless_pwm_command = 0;          ///< 右无刷电调 PWM 指令值
    std::string actuator_apply_outcome = "not_requested";  ///< 统一执行器施加结果
};

/// @brief 解码一行 JSON 格式的助手入站消息
/// @param line 原始 JSON 行字符串
/// @param max_target_speed 最大允许目标速度，用于校验速度指令
/// @return 解码后的入站消息结构体
AssistantInboundMessage DecodeAssistantJsonLine(const std::string& line, double max_target_speed);

/// @brief 编码助手应答（ACK/NAK）JSON 消息
/// @param seq 对应命令的序列号
/// @param accepted 是否接受
/// @param reason 拒绝原因（仅 accepted=false 时使用）
/// @return 编码后的 JSON 字符串
std::string EncodeAssistantAck(std::uint64_t seq, bool accepted, const std::string& reason = {});

/// @brief 编码助手状态变更 JSON 消息
/// @param event 事件名称
/// @param reason 事件原因
/// @param status 当前状态快照
/// @return 编码后的 JSON 字符串
std::string EncodeAssistantState(const std::string& event,
                                 const std::string& reason,
                                 const AssistantStatusView& status);

/// @brief 编码完整助手遥测 JSON 消息
/// @param telemetry 遥测数据视图
/// @return 编码后的 JSON 字符串
std::string EncodeAssistantTelemetry(const AssistantTelemetryView& telemetry);

/// @brief 将命令类型枚举值转换为可读字符串
/// @param type 命令类型枚举
/// @return 命令名称字符串
const char* ToString(AssistantCommandType type);

}  // namespace ls2k::transport

#endif  // LS2K_TRANSPORT_ASSISTANT_PROTOCOL_HPP
