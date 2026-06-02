#include "runtime/services/assistant_service.hpp"

#include <utility>

#include "port/perf_counter.hpp"
#include "observability/assistant_telemetry_view.hpp"

namespace ls2k::runtime {

using control::ClearExpiredRuntimeTuningOverride;
using control::ClearRuntimeTuningSnapshot;
using control::DisableRuntimeTuningMode;
using control::EnableRuntimeTuningMode;
using control::MotionPhase;
using control::NoteRuntimeTuningSeq;
using control::RuntimeTuningEvent;
using control::RuntimeTuningEventType;
using control::RuntimeTuningOverrideActiveAt;
using control::RuntimeTuningSnapshot;
using control::SetRuntimeTargetSpeedOverride;
using control::SetRuntimeTurnSuppressed;
using control::SnapshotRuntimeTuningState;
using observability::BuildAssistantTelemetryView;
using observability::ControlDebugSnapshot;

namespace {

/// ACK 确认后的安全等待时间（ms），确保控制层 ACK 排空后再提交运动意图
constexpr uint64_t kLifecycleCommandAckGuardMs = 75;

/// 构建辅助状态视图 —— 从调参快照和控制快照组装发送给辅助进程的状态信息
/// @param tuning_snapshot    当前运行时调参状态快照
/// @param control_snapshot   当前控制调试快照
/// @param now_ms             当前时间戳（ms）
/// @return                   组装好的 AssistantStatusView
transport::AssistantStatusView BuildStatusView(const RuntimeTuningSnapshot& tuning_snapshot,
                                              const ControlDebugSnapshot& control_snapshot,
                                              uint64_t now_ms) {
    transport::AssistantStatusView status{};
    status.tuning_mode_enabled = tuning_snapshot.tuning_mode_enabled;
    status.turn_suppressed = tuning_snapshot.tuning_mode_enabled && tuning_snapshot.turn_suppressed;
    status.target_speed_override_enabled = RuntimeTuningOverrideActiveAt(tuning_snapshot, now_ms);
    status.target_speed_override_value =
        status.target_speed_override_enabled ? tuning_snapshot.target_speed_override_value : 0.0;
    status.effective_speed_target = control_snapshot.valid ? control_snapshot.effective_speed_target : 0.0;
    return status;
}

/// 将运行时调参事件类型转换为字符串名称
/// @param type  调参事件类型
/// @return      对应的事件名称字符串
const char* ToEventName(RuntimeTuningEventType type) {
    switch (type) {
        case RuntimeTuningEventType::kOverrideCleared:
            return "override_cleared";
        case RuntimeTuningEventType::kSnapshotCleared:
            return "snapshot_cleared";
        case RuntimeTuningEventType::kNone:
            break;
    }
    return "";
}

/// 将辅助命令类型转换为字符串名称（用于诊断输出）
/// @param type  辅助命令类型
/// @return      对应的命令名称字符串
const char* ToCommandName(transport::AssistantCommandType type) {
    switch (type) {
        case transport::AssistantCommandType::kEnableTuningMode:
            return "enable_tuning_mode";
        case transport::AssistantCommandType::kDisableTuningMode:
            return "disable_tuning_mode";
        case transport::AssistantCommandType::kSetTurnSuppressed:
            return "set_turn_suppressed";
        case transport::AssistantCommandType::kSetTargetSpeed:
            return "set_target_speed";
        case transport::AssistantCommandType::kStart:
            return "start";
        case transport::AssistantCommandType::kStop:
            return "stop";
    }
    return "unknown";
}

/// 构造辅助命令的描述字符串（序列号 + 类型 + 参数，用于诊断日志）
/// @param command  辅助命令
/// @return         格式化的命令描述
std::string DescribeCommand(const transport::AssistantCommand& command) {
    std::string detail = std::string("seq=") + std::to_string(command.seq) +
                         " cmd=" + ToCommandName(command.type);
    switch (command.type) {
        case transport::AssistantCommandType::kSetTurnSuppressed:
            detail += std::string(" value=") + (command.bool_value ? "true" : "false");
            break;
        case transport::AssistantCommandType::kSetTargetSpeed:
            detail += " value=" + std::to_string(command.target_speed_value) +
                      " ttl_ms=" + std::to_string(command.ttl_ms);
            break;
        case transport::AssistantCommandType::kEnableTuningMode:
        case transport::AssistantCommandType::kDisableTuningMode:
        case transport::AssistantCommandType::kStart:
        case transport::AssistantCommandType::kStop:
            break;
    }
    return detail;
}

}  // namespace

/// 启动辅助服务：初始化参数、链路连接，并设置遥测间隔。若辅助功能禁用则直接返回。
/// @param params       运行时参数（含 assistant_enabled 标志）
/// @param diagnostics  诊断输出接口
void AssistantService::Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    enabled_ = params.assistant_enabled;
    ResetDeferredMotionIntent();
    // Keep telemetry slower than the control loop. Images and media diagnostics use steering media.
    telemetry_interval_ms_ = 200;
    last_telemetry_publish_ms_ = 0;
    last_telemetry_cycle_ = 0;
    pending_feedback_.clear();
    if (!enabled_) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "assistant.disabled",
                          "assistant sidecar disabled by runtime parameters",
                          port::NowMs()});
        return;
    }
    (void)link_.Initialize(params, diagnostics);
}

/// 重置延迟运动意图（清空结构体）
void AssistantService::ResetDeferredMotionIntent() {
    deferred_motion_intent_ = {};
}

/// 延迟运动意图：记录意图类型和序列号，设置就绪时间为当前时间 + ACK 防护间隔
/// @param type    运动意图类型（启动/停止）
/// @param seq     命令序列号
/// @param now_ms  当前时间戳（ms）
void AssistantService::DeferMotionIntent(DeferredMotionIntentType type,
                                         std::uint64_t seq,
                                         uint64_t now_ms) {
    deferred_motion_intent_.type = type;
    deferred_motion_intent_.seq = seq;
    deferred_motion_intent_.ready_at_ms = now_ms + kLifecycleCommandAckGuardMs;
}

/// 若延迟运动意图就绪（已过防护时间、无待发送反馈、链路就绪），则提交到 state.motion_intent
/// @param state       运行时状态
/// @param diagnostics 诊断输出接口
/// @param now_ms      当前时间戳（ms）
void AssistantService::ApplyDeferredMotionIntentIfReady(RuntimeState& state,
                                                        port::DiagnosticSink& diagnostics,
                                                        uint64_t now_ms) {
    if (deferred_motion_intent_.type == DeferredMotionIntentType::kNone ||
        now_ms < deferred_motion_intent_.ready_at_ms ||
        !pending_feedback_.empty() ||
        !link_.Ready()) {
        return;
    }

    const DeferredMotionIntent pending = deferred_motion_intent_;
    ResetDeferredMotionIntent();

    std::string code;
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        NoteRuntimeTuningSeq(state.tuning_state, pending.seq);
        switch (pending.type) {
            case DeferredMotionIntentType::kStart:
                state.motion_intent.start_requested = true;
                state.motion_intent.stop_requested = false;
                code = "assistant.motion.start.requested";
                detail =
                    "assistant command committed remote start into motion_intent after the control ACK drained";
                break;
            case DeferredMotionIntentType::kStop:
                state.motion_intent.stop_requested = true;
                state.motion_intent.start_requested = false;
                code = "assistant.motion.stop.requested";
                detail =
                    "assistant command committed remote stop into motion_intent after the control ACK drained";
                break;
            case DeferredMotionIntentType::kNone:
                return;
        }
    }

    diagnostics.Emit({port::DiagnosticLevel::kInfo, code, detail, now_ms});
}

/// 辅助服务 Tick：轮询链路消息、处理入站命令、执行延迟意图、处理超时事件、发布遥测
/// @param state       运行时状态
/// @param diagnostics 诊断输出接口
void AssistantService::Tick(RuntimeState& state, port::DiagnosticSink& diagnostics) {
    LS2K_PERF_SCOPE(port::PerfStage::kAssistantTick);
    if (!configured_ || !enabled_) {
        return;
    }

    const uint64_t now_ms = port::NowMs();
    const transport::AssistantPollResult poll_result = link_.Poll(diagnostics);
    if (poll_result.became_ready) {
        pending_feedback_.clear();
        ResetDeferredMotionIntent();
    }
    if (poll_result.connection_lost) {
        pending_feedback_.clear();
        ResetDeferredMotionIntent();
        RuntimeTuningEvent disconnect_event{};
        {
            std::lock_guard<std::mutex> lock(state.shared_mutex);
            disconnect_event =
                ClearRuntimeTuningSnapshot(state.tuning_state, "disconnect", false);
            state.motion_intent.stop_requested = true;
            state.motion_intent.start_requested = false;
        }
        if (disconnect_event.type != RuntimeTuningEventType::kNone) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "assistant.snapshot.cleared",
                              "assistant disconnect cleared the volatile tuning snapshot",
                              now_ms});
            PublishStateEvent(state, ToEventName(disconnect_event.type), disconnect_event.reason, diagnostics, now_ms);
        }
    }

    if (!poll_result.connection_lost) {
        HandleInboundMessages(poll_result.inbound_messages, state, diagnostics, now_ms);
    }

    ApplyDeferredMotionIntentIfReady(state, diagnostics, now_ms);

    RuntimeTuningEvent expiry_event{};
    ControlDebugSnapshot snapshot{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        expiry_event = ClearExpiredRuntimeTuningOverride(state.tuning_state, now_ms);
        snapshot = state.control_debug_snapshot;
    }
    if (expiry_event.type != RuntimeTuningEventType::kNone) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "assistant.override.cleared",
                          "assistant target-speed override cleared after TTL expiry",
                          now_ms});
        PublishStateEvent(state, ToEventName(expiry_event.type), expiry_event.reason, diagnostics, now_ms);
    }

    FlushFeedback(diagnostics);
    if (!poll_result.ready) {
        return;
    }

    const bool telemetry_phase_allowed =
        snapshot.motion_phase == MotionPhase::kRunning ||
        snapshot.motion_phase == MotionPhase::kStopping;
    if (telemetry_phase_allowed && snapshot.valid &&
        snapshot.cycle_count != last_telemetry_cycle_ &&
        (last_telemetry_publish_ms_ == 0 ||
         now_ms - last_telemetry_publish_ms_ >= static_cast<uint64_t>(telemetry_interval_ms_))) {
        if (link_.PublishJsonLine(transport::EncodeAssistantTelemetry(
                                      BuildAssistantTelemetryView(snapshot)),
                                  transport::AssistantJsonSendReliability::kBestEffort,
                                  diagnostics)) {
            last_telemetry_publish_ms_ = now_ms;
            last_telemetry_cycle_ = snapshot.cycle_count;
        }
    }

}

/// 将反馈行加入待发送队列
/// @param line 反馈消息字符串（已编码的 JSON 行）
void AssistantService::EnqueueFeedback(std::string line) {
    pending_feedback_.push_back(std::move(line));
}

/// 刷新待发送反馈队列：当链路就绪时逐条发送，直到发送失败或队列清空
/// @param diagnostics 诊断输出接口
void AssistantService::FlushFeedback(port::DiagnosticSink& diagnostics) {
    while (!pending_feedback_.empty() && link_.Ready()) {
        if (!link_.PublishJsonLine(pending_feedback_.front(),
                                   transport::AssistantJsonSendReliability::kReliable,
                                   diagnostics)) {
            return;
        }
        pending_feedback_.pop_front();
    }
}

/// 发布状态事件：采集调参快照和控制快照，编码为状态事件消息并发送
/// @param state       运行时状态
/// @param event       事件名称
/// @param reason      事件原因描述
/// @param diagnostics 诊断输出接口
/// @param now_ms      当前时间戳（ms）
void AssistantService::PublishStateEvent(RuntimeState& state,
                                         const std::string& event,
                                         const std::string& reason,
                                         port::DiagnosticSink& diagnostics,
                                         uint64_t now_ms) {
    RuntimeTuningSnapshot tuning_snapshot{};
    ControlDebugSnapshot control_snapshot{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        tuning_snapshot = SnapshotRuntimeTuningState(state.tuning_state);
        control_snapshot = state.control_debug_snapshot;
    }
    const transport::AssistantStatusView status =
        BuildStatusView(tuning_snapshot, control_snapshot, now_ms);
    EnqueueFeedback(transport::EncodeAssistantState(event, reason, status));
    FlushFeedback(diagnostics);
}

/// 处理入站消息集合：根据消息类型分发到拒绝处理、ACK拒绝处理或命令处理
/// @param inbound_messages  入站消息列表
/// @param state             运行时状态
/// @param diagnostics       诊断输出接口
/// @param now_ms            当前时间戳（ms）
void AssistantService::HandleInboundMessages(
    const std::vector<transport::AssistantInboundMessage>& inbound_messages,
    RuntimeState& state,
    port::DiagnosticSink& diagnostics,
    uint64_t now_ms) {
    for (const transport::AssistantInboundMessage& inbound_message : inbound_messages) {
        switch (inbound_message.type) {
            case transport::AssistantInboundMessageType::kInputRejected:
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "assistant.input_rejected",
                                  inbound_message.reason,
                                  now_ms});
                PublishStateEvent(state, "input_rejected", inbound_message.reason, diagnostics, now_ms);
                break;
            case transport::AssistantInboundMessageType::kAckRejected:
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "assistant.command.rejected",
                                  inbound_message.reason,
                                  now_ms});
                EnqueueFeedback(transport::EncodeAssistantAck(inbound_message.seq, false, inbound_message.reason));
                FlushFeedback(diagnostics);
                break;
            case transport::AssistantInboundMessageType::kCommand:
                HandleCommand(inbound_message.command, state, diagnostics, now_ms);
                break;
        }
    }
}

/// 处理单条辅助命令：执行调参模式切换、转向抑制、目标速度覆盖、运动启动/停止
/// @param command     辅助命令
/// @param state       运行时状态
/// @param diagnostics 诊断输出接口
/// @param now_ms      当前时间戳（ms）
void AssistantService::HandleCommand(const transport::AssistantCommand& command,
                                     RuntimeState& state,
                                     port::DiagnosticSink& diagnostics,
                                     uint64_t now_ms) {
    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "assistant.command.rx",
                      DescribeCommand(command),
                      now_ms});
    bool accepted = true;
    std::string reject_reason;
    RuntimeTuningEvent tuning_event{};

    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        switch (command.type) {
            case transport::AssistantCommandType::kEnableTuningMode:
                EnableRuntimeTuningMode(state.tuning_state, command.seq);
                break;
            case transport::AssistantCommandType::kDisableTuningMode:
                tuning_event = DisableRuntimeTuningMode(state.tuning_state, command.seq);
                break;
            case transport::AssistantCommandType::kSetTurnSuppressed:
                if (!state.tuning_state.tuning_mode_enabled) {
                    accepted = false;
                    reject_reason = "tuning mode is disabled";
                } else {
                    SetRuntimeTurnSuppressed(state.tuning_state, command.bool_value, command.seq);
                }
                break;
            case transport::AssistantCommandType::kSetTargetSpeed:
                if (!state.tuning_state.tuning_mode_enabled) {
                    accepted = false;
                    reject_reason = "tuning mode is disabled";
                } else {
                    SetRuntimeTargetSpeedOverride(state.tuning_state,
                                                  command.target_speed_value,
                                                  now_ms + static_cast<uint64_t>(command.ttl_ms),
                                                  command.seq);
                }
                break;
            case transport::AssistantCommandType::kStart:
                if (state.motion_state.phase == MotionPhase::kFailSafeLatched) {
                    accepted = false;
                    reject_reason = "motion fault latch remains active";
                } else {
                    DeferMotionIntent(DeferredMotionIntentType::kStart, command.seq, now_ms);
                }
                break;
            case transport::AssistantCommandType::kStop:
                if (state.motion_state.phase == MotionPhase::kFailSafeLatched) {
                    accepted = false;
                    reject_reason = "motion fault latch remains active";
                } else {
                    DeferMotionIntent(DeferredMotionIntentType::kStop, command.seq, now_ms);
                }
                break;
        }
    }

    const bool enqueue_state_event = accepted && tuning_event.type != RuntimeTuningEventType::kNone;
    EnqueueFeedback(transport::EncodeAssistantAck(command.seq, accepted, reject_reason));
    FlushFeedback(diagnostics);

    if (!accepted) {
        PublishStateEvent(state, "input_rejected", reject_reason, diagnostics, now_ms);
    }

    if (enqueue_state_event) {
        RuntimeTuningSnapshot tuning_snapshot{};
        ControlDebugSnapshot control_snapshot{};
        {
            std::lock_guard<std::mutex> lock(state.shared_mutex);
            tuning_snapshot = SnapshotRuntimeTuningState(state.tuning_state);
            control_snapshot = state.control_debug_snapshot;
        }
        EnqueueFeedback(transport::EncodeAssistantState(
            ToEventName(tuning_event.type),
            tuning_event.reason,
            BuildStatusView(tuning_snapshot, control_snapshot, now_ms)));
    }
    diagnostics.Emit({accepted ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                      accepted ? "assistant.command.accepted" : "assistant.command.rejected",
                      DescribeCommand(command) +
                          (accepted ? std::string() : " reason=" + reject_reason),
                      now_ms});
}

}  // namespace ls2k::runtime
