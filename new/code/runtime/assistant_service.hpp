#ifndef LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
#define LS2K_RUNTIME_ASSISTANT_SERVICE_HPP

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "platform/assistant_protocol.hpp"
#include "platform/assistant_link.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 辅助服务类 —— 管理与外部辅助进程的双向通信。
/// 负责接收远程调参命令、反馈遥测数据，并协调运动状态的生命周期转换。
class AssistantService {
public:
    /// 启动辅助服务：初始化参数、诊断系统和链路层连接
    void Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    /// 周期性 Tick：轮询链路消息、处理命令、发布遥测、处理超时
    void Tick(RuntimeState& state, port::DiagnosticSink& diagnostics);

private:
    /// 延迟运动意图类型枚举
    enum class DeferredMotionIntentType {
        kNone,   ///< 无意图
        kStart,  ///< 请求启动运动
        kStop,   ///< 请求停止运动
    };
    /// 延迟运动意图结构 —— 存储待执行的远程启动/停止命令及就绪时间
    struct DeferredMotionIntent {
        DeferredMotionIntentType type = DeferredMotionIntentType::kNone;  ///< 意图类型
        std::uint64_t seq = 0;                      ///< 关联的命令序列号
        std::uint64_t ready_at_ms = 0;              ///< 意图可执行的时间戳（ms）
    };

    /// 将反馈消息加入待发送队列
    void EnqueueFeedback(std::string line);
    /// 刷新反馈消息队列，将待发送消息通过链路发送
    void FlushFeedback(port::DiagnosticSink& diagnostics);
    /// 重置延迟运动意图（清空）
    void ResetDeferredMotionIntent();
    /// 记录一个延迟运动意图，等待 ACK 排空后执行
    void DeferMotionIntent(DeferredMotionIntentType type, std::uint64_t seq, uint64_t now_ms);
    /// 若延迟意图已就绪则执行之
    void ApplyDeferredMotionIntentIfReady(RuntimeState& state,
                                          port::DiagnosticSink& diagnostics,
                                          uint64_t now_ms);
    /// 发布状态事件（如 override 清除、断开连接）到链路
    void PublishStateEvent(RuntimeState& state,
                           const std::string& event,
                           const std::string& reason,
                           port::DiagnosticSink& diagnostics,
                           uint64_t now_ms);
    /// 处理辅助链路入站消息（命令/拒绝/ACK拒绝）
    void HandleInboundMessages(const std::vector<platform::AssistantInboundMessage>& inbound_messages,
                               RuntimeState& state,
                               port::DiagnosticSink& diagnostics,
                               uint64_t now_ms);
    /// 处理单条辅助命令（调参模式、转向抑制、目标速度、启动/停止）
    void HandleCommand(const platform::AssistantCommand& command,
                       RuntimeState& state,
                       port::DiagnosticSink& diagnostics,
                       uint64_t now_ms);

    bool configured_ = false;                       ///< 是否已完成配置
    bool enabled_ = false;                          ///< 辅助功能是否启用
    uint64_t last_telemetry_publish_ms_ = 0;        ///< 上次遥测发布时间戳
    uint64_t last_telemetry_cycle_ = 0;             ///< 上次遥测对应的控制周期计数
    int telemetry_interval_ms_ = 40;                ///< 遥测发布间隔（ms）
    std::deque<std::string> pending_feedback_{};    ///< 待发送的反馈消息队列
    platform::AssistantLink link_{};                ///< 辅助链路层实例
    DeferredMotionIntent deferred_motion_intent_{}; ///< 当前待执行的延迟运动意图
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
