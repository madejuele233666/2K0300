#include "runtime/assistant_service.hpp"

#include <utility>

#include "port/perf_counter.hpp"
#include "runtime/assistant_telemetry_view.hpp"

namespace ls2k::runtime {
namespace {

constexpr uint64_t kLifecycleCommandAckGuardMs = 75;

platform::AssistantStatusView BuildStatusView(const RuntimeTuningSnapshot& tuning_snapshot,
                                              const ControlDebugSnapshot& control_snapshot,
                                              uint64_t now_ms) {
    platform::AssistantStatusView status{};
    status.tuning_mode_enabled = tuning_snapshot.tuning_mode_enabled;
    status.turn_suppressed = tuning_snapshot.tuning_mode_enabled && tuning_snapshot.turn_suppressed;
    status.target_speed_override_enabled = RuntimeTuningOverrideActiveAt(tuning_snapshot, now_ms);
    status.target_speed_override_value =
        status.target_speed_override_enabled ? tuning_snapshot.target_speed_override_value : 0.0;
    status.effective_speed_target = control_snapshot.valid ? control_snapshot.effective_speed_target : 0.0;
    return status;
}

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

const char* ToCommandName(platform::AssistantCommandType type) {
    switch (type) {
        case platform::AssistantCommandType::kEnableTuningMode:
            return "enable_tuning_mode";
        case platform::AssistantCommandType::kDisableTuningMode:
            return "disable_tuning_mode";
        case platform::AssistantCommandType::kSetTurnSuppressed:
            return "set_turn_suppressed";
        case platform::AssistantCommandType::kSetTargetSpeed:
            return "set_target_speed";
        case platform::AssistantCommandType::kStart:
            return "start";
        case platform::AssistantCommandType::kStop:
            return "stop";
    }
    return "unknown";
}

std::string DescribeCommand(const platform::AssistantCommand& command) {
    std::string detail = std::string("seq=") + std::to_string(command.seq) +
                         " cmd=" + ToCommandName(command.type);
    switch (command.type) {
        case platform::AssistantCommandType::kSetTurnSuppressed:
            detail += std::string(" value=") + (command.bool_value ? "true" : "false");
            break;
        case platform::AssistantCommandType::kSetTargetSpeed:
            detail += " value=" + std::to_string(command.target_speed_value) +
                      " ttl_ms=" + std::to_string(command.ttl_ms);
            break;
        case platform::AssistantCommandType::kEnableTuningMode:
        case platform::AssistantCommandType::kDisableTuningMode:
        case platform::AssistantCommandType::kStart:
        case platform::AssistantCommandType::kStop:
            break;
    }
    return detail;
}

}  // namespace

void AssistantService::Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    enabled_ = params.assistant_enabled;
    periodic_publish_armed_ = false;
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

void AssistantService::ResetDeferredMotionIntent() {
    deferred_motion_intent_ = {};
}

void AssistantService::DeferMotionIntent(DeferredMotionIntentType type,
                                         std::uint64_t seq,
                                         uint64_t now_ms) {
    deferred_motion_intent_.type = type;
    deferred_motion_intent_.seq = seq;
    deferred_motion_intent_.ready_at_ms = now_ms + kLifecycleCommandAckGuardMs;
}

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

void AssistantService::Tick(RuntimeState& state, port::DiagnosticSink& diagnostics) {
    LS2K_PERF_SCOPE(port::PerfStage::kAssistantTick);
    if (!configured_ || !enabled_) {
        return;
    }

    const uint64_t now_ms = port::NowMs();
    const platform::AssistantPollResult poll_result = link_.Poll(diagnostics);
    if (poll_result.became_ready) {
        // Delay periodic telemetry on a fresh connection until the host sends
        // a command. This shrinks the unstable connect-then-command window.
        periodic_publish_armed_ = false;
        pending_feedback_.clear();
        ResetDeferredMotionIntent();
    }
    if (poll_result.connection_lost) {
        periodic_publish_armed_ = false;
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
    if (periodic_publish_armed_ && telemetry_phase_allowed && snapshot.valid &&
        snapshot.cycle_count != last_telemetry_cycle_ &&
        (last_telemetry_publish_ms_ == 0 ||
         now_ms - last_telemetry_publish_ms_ >= static_cast<uint64_t>(telemetry_interval_ms_))) {
        if (link_.PublishJsonLine(platform::EncodeAssistantTelemetry(
                                      BuildAssistantTelemetryView(snapshot)),
                                  platform::AssistantJsonSendReliability::kBestEffort,
                                  diagnostics)) {
            last_telemetry_publish_ms_ = now_ms;
            last_telemetry_cycle_ = snapshot.cycle_count;
        }
    }

}

void AssistantService::EnqueueFeedback(std::string line) {
    pending_feedback_.push_back(std::move(line));
}

void AssistantService::FlushFeedback(port::DiagnosticSink& diagnostics) {
    while (!pending_feedback_.empty() && link_.Ready()) {
        if (!link_.PublishJsonLine(pending_feedback_.front(),
                                   platform::AssistantJsonSendReliability::kReliable,
                                   diagnostics)) {
            return;
        }
        pending_feedback_.pop_front();
    }
}

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
    const platform::AssistantStatusView status =
        BuildStatusView(tuning_snapshot, control_snapshot, now_ms);
    EnqueueFeedback(platform::EncodeAssistantState(event, reason, status));
    FlushFeedback(diagnostics);
}

void AssistantService::HandleInboundMessages(
    const std::vector<platform::AssistantInboundMessage>& inbound_messages,
    RuntimeState& state,
    port::DiagnosticSink& diagnostics,
    uint64_t now_ms) {
    for (const platform::AssistantInboundMessage& inbound_message : inbound_messages) {
        switch (inbound_message.type) {
            case platform::AssistantInboundMessageType::kInputRejected:
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "assistant.input_rejected",
                                  inbound_message.reason,
                                  now_ms});
                PublishStateEvent(state, "input_rejected", inbound_message.reason, diagnostics, now_ms);
                break;
            case platform::AssistantInboundMessageType::kAckRejected:
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "assistant.command.rejected",
                                  inbound_message.reason,
                                  now_ms});
                EnqueueFeedback(platform::EncodeAssistantAck(inbound_message.seq, false, inbound_message.reason));
                FlushFeedback(diagnostics);
                break;
            case platform::AssistantInboundMessageType::kCommand:
                HandleCommand(inbound_message.command, state, diagnostics, now_ms);
                break;
        }
    }
}

void AssistantService::HandleCommand(const platform::AssistantCommand& command,
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
            case platform::AssistantCommandType::kEnableTuningMode:
                EnableRuntimeTuningMode(state.tuning_state, command.seq);
                break;
            case platform::AssistantCommandType::kDisableTuningMode:
                tuning_event = DisableRuntimeTuningMode(state.tuning_state, command.seq);
                break;
            case platform::AssistantCommandType::kSetTurnSuppressed:
                if (!state.tuning_state.tuning_mode_enabled) {
                    accepted = false;
                    reject_reason = "tuning mode is disabled";
                } else {
                    SetRuntimeTurnSuppressed(state.tuning_state, command.bool_value, command.seq);
                }
                break;
            case platform::AssistantCommandType::kSetTargetSpeed:
                if (!state.tuning_state.tuning_mode_enabled) {
                    accepted = false;
                    reject_reason = "tuning mode is disabled";
                } else {
                    SetRuntimeTargetSpeedOverride(state.tuning_state,
                                                  command.target_speed_value,
                                                  now_ms + static_cast<uint64_t>(command.ttl_ms),
                                                  command.seq);
                    periodic_publish_armed_ = true;
                }
                break;
            case platform::AssistantCommandType::kStart:
                if (state.motion_state.phase == MotionPhase::kFailSafeLatched) {
                    accepted = false;
                    reject_reason = "motion fault latch remains active";
                } else {
                    DeferMotionIntent(DeferredMotionIntentType::kStart, command.seq, now_ms);
                }
                break;
            case platform::AssistantCommandType::kStop:
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
    EnqueueFeedback(platform::EncodeAssistantAck(command.seq, accepted, reject_reason));
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
        EnqueueFeedback(platform::EncodeAssistantState(
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
