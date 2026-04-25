#include "runtime/assistant_service.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::runtime {
namespace {

constexpr uint64_t kLifecycleCommandAckGuardMs = 75;

platform::AssistantWaveformFrame BuildWaveformFrame(const ControlDebugSnapshot& snapshot) {
    platform::AssistantWaveformFrame frame{};
    frame.channel_count = 8;
    frame.values[0] = static_cast<float>(snapshot.left_speed_target);
    frame.values[1] = static_cast<float>(snapshot.right_speed_target);
    frame.values[2] = static_cast<float>(snapshot.left_measured_speed);
    frame.values[3] = static_cast<float>(snapshot.right_measured_speed);
    frame.values[4] = static_cast<float>(snapshot.left_pwm_command);
    frame.values[5] = static_cast<float>(snapshot.right_pwm_command);
    frame.values[6] = static_cast<float>(snapshot.raw_turn_output);
    frame.values[7] = static_cast<float>(snapshot.applied_turn_output);
    return frame;
}

uint64_t SafeElapsedMs(uint64_t now_ms, uint64_t start_ms) {
    return now_ms >= start_ms ? now_ms - start_ms : 0;
}

double ClampRatio(uint64_t elapsed_ms, int window_ms) {
    if (window_ms <= 0) {
        return 1.0;
    }
    return std::clamp(static_cast<double>(elapsed_ms) / static_cast<double>(window_ms), 0.0, 1.0);
}

double StopDecayTarget(double entry_speed_target, uint64_t elapsed_ms, int stop_ms) {
    if (stop_ms <= 0) {
        return 0.0;
    }
    const double stop_ratio = 1.0 - ClampRatio(elapsed_ms, stop_ms);
    return std::max(0.0, entry_speed_target * std::max(0.0, stop_ratio));
}

double ComputeEffectiveSpeedTargetForState(const MotionSupervisorState& motion_state,
                                           const RuntimeTuningSnapshot& tuning_snapshot,
                                           const port::RuntimeParameters& params,
                                           uint64_t now_ms) {
    const double running_speed_target = ResolveRuntimeSpeedTarget(tuning_snapshot, params.Speed_base, now_ms);
    switch (motion_state.phase) {
        case MotionPhase::kDisarmed:
        case MotionPhase::kStartRequested:
        case MotionPhase::kFailSafeLatched:
            return 0.0;
        case MotionPhase::kSpinup: {
            const uint64_t elapsed_ms = SafeElapsedMs(now_ms, motion_state.phase_entry_ms);
            return running_speed_target * ClampRatio(elapsed_ms, params.motion_spinup_ms);
        }
        case MotionPhase::kRunning:
            return running_speed_target;
        case MotionPhase::kStopping: {
            const uint64_t elapsed_ms = SafeElapsedMs(now_ms, motion_state.phase_entry_ms);
            return StopDecayTarget(motion_state.stop_entry_speed_target, elapsed_ms, params.motion_stop_ms);
        }
    }
    return 0.0;
}

platform::AssistantStatusView BuildStatusView(const MotionSupervisorState& motion_state,
                                              const RuntimeTuningSnapshot& tuning_snapshot,
                                              const port::RuntimeParameters& params,
                                              uint64_t now_ms) {
    platform::AssistantStatusView status{};
    status.tuning_mode_enabled = tuning_snapshot.tuning_mode_enabled;
    status.turn_suppressed = tuning_snapshot.tuning_mode_enabled && tuning_snapshot.turn_suppressed;
    status.target_speed_override_enabled = RuntimeTuningOverrideActiveAt(tuning_snapshot, now_ms);
    status.target_speed_override_value =
        status.target_speed_override_enabled ? tuning_snapshot.target_speed_override_value : 0.0;
    status.effective_speed_target =
        ComputeEffectiveSpeedTargetForState(motion_state, tuning_snapshot, params, now_ms);
    return status;
}

platform::AssistantTelemetryView BuildTelemetryView(const ControlDebugSnapshot& snapshot) {
    platform::AssistantTelemetryView telemetry{};
    telemetry.motion_phase = ToString(snapshot.motion_phase);
    telemetry.active_module = snapshot.steering.active_module;
    telemetry.scene_phase = snapshot.steering.scene_phase;
    telemetry.scene_override_source = snapshot.steering.scene_override_source;
    telemetry.tuning_mode_enabled = snapshot.tuning_mode_enabled;
    telemetry.turn_suppressed = snapshot.turn_suppressed;
    telemetry.target_speed_override_enabled = snapshot.target_speed_override_enabled;
    telemetry.target_speed_override_value = snapshot.target_speed_override_value;
    telemetry.effective_speed_target = snapshot.effective_speed_target;
    telemetry.left_speed_target = snapshot.left_speed_target;
    telemetry.right_speed_target = snapshot.right_speed_target;
    telemetry.left_measured_speed = snapshot.left_measured_speed;
    telemetry.right_measured_speed = snapshot.right_measured_speed;
    telemetry.left_pwm_command = snapshot.left_pwm_command;
    telemetry.right_pwm_command = snapshot.right_pwm_command;
    telemetry.raw_turn_output = snapshot.raw_turn_output;
    telemetry.applied_turn_output = snapshot.applied_turn_output;
    telemetry.farthest_line = snapshot.steering.farthest_line;
    telemetry.steering_reference_col = snapshot.steering.steering_reference_col;
    return telemetry;
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
    publish_policy_ = PublishPolicy::kControlAndMedia;
    control_priority_connection_ = false;
    ResetDeferredMotionIntent();
    params_ = params;
    waveform_interval_ms_ = std::max(0, params.assistant_waveform_publish_interval_ms);
    // The TCP assistant path becomes unstable under sustained small-frame bursts.
    // Keep telemetry slower than the control loop when waveform streaming is off.
    telemetry_interval_ms_ = waveform_interval_ms_ > 0 ? waveform_interval_ms_ : 200;
    image_interval_ms_ = std::max(0, params.assistant_image_publish_interval_ms);
    last_wave_publish_ms_ = 0;
    last_telemetry_publish_ms_ = 0;
    last_image_publish_ms_ = 0;
    last_wave_cycle_ = 0;
    last_telemetry_cycle_ = 0;
    last_image_frame_id_ = 0;
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

AssistantService::PublishPolicy AssistantService::DeterminePublishPolicy(
    const ControlDebugSnapshot& snapshot,
    bool session_boundary_reset) const {
    // Runtime control and optional media share one TCP stream. Once a client
    // starts issuing commands on a connection, keep that whole session on a
    // control-only lane until disconnect so trailing stop/disable/close steps
    // cannot be disrupted by a late media burst reclaiming the socket.
    if (control_priority_connection_) {
        return PublishPolicy::kControlOnly;
    }
    // A fresh or torn-down session should not inherit a stale debug snapshot
    // from the previous connection. Let the new session start from the default
    // mixed publish lane until it actually sends a command.
    if (session_boundary_reset) {
        return PublishPolicy::kControlAndMedia;
    }
    if (snapshot.tuning_mode_enabled) {
        return PublishPolicy::kControlOnly;
    }
    return PublishPolicy::kControlAndMedia;
}

void AssistantService::UpdatePublishPolicy(PublishPolicy next_policy,
                                           port::DiagnosticSink& diagnostics,
                                           uint64_t now_ms) {
    if (publish_policy_ == next_policy) {
        return;
    }

    publish_policy_ = next_policy;
    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "assistant.publish_policy",
                      std::string("assistant publish policy switched to ") +
                          (next_policy == PublishPolicy::kControlOnly ? "control_only"
                                                                      : "control_and_media"),
                      now_ms});
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
    if (!configured_ || !enabled_) {
        return;
    }

    const uint64_t now_ms = port::NowMs();
    const platform::AssistantPollResult poll_result = link_.Poll(diagnostics);
    const bool session_boundary_reset = poll_result.became_ready || poll_result.connection_lost;
    if (poll_result.became_ready) {
        // Delay periodic telemetry on a fresh connection until the host sends
        // a command. This shrinks the unstable connect-then-command window.
        periodic_publish_armed_ = false;
        control_priority_connection_ = false;
        pending_feedback_.clear();
        ResetDeferredMotionIntent();
    }
    if (poll_result.connection_lost) {
        periodic_publish_armed_ = false;
        control_priority_connection_ = false;
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
    port::CameraCapture capture{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        expiry_event = ClearExpiredRuntimeTuningOverride(state.tuning_state, now_ms);
        snapshot = state.control_debug_snapshot;
        capture = state.latest_camera_capture;
    }
    if (expiry_event.type != RuntimeTuningEventType::kNone) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "assistant.override.cleared",
                          "assistant target-speed override cleared after TTL expiry",
                          now_ms});
        PublishStateEvent(state, ToEventName(expiry_event.type), expiry_event.reason, diagnostics, now_ms);
    }

    UpdatePublishPolicy(DeterminePublishPolicy(snapshot, session_boundary_reset), diagnostics, now_ms);

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
        if (link_.PublishJsonLine(platform::EncodeAssistantTelemetry(BuildTelemetryView(snapshot)),
                                  platform::AssistantJsonSendReliability::kBestEffort,
                                  diagnostics)) {
            last_telemetry_publish_ms_ = now_ms;
            last_telemetry_cycle_ = snapshot.cycle_count;
        }
    }

    const bool media_publish_allowed = publish_policy_ == PublishPolicy::kControlAndMedia;

    if (periodic_publish_armed_ && media_publish_allowed && waveform_interval_ms_ > 0 && snapshot.valid &&
        snapshot.cycle_count != last_wave_cycle_ &&
        (last_wave_publish_ms_ == 0 || now_ms - last_wave_publish_ms_ >= static_cast<uint64_t>(waveform_interval_ms_))) {
        if (link_.PublishWaveform(BuildWaveformFrame(snapshot), diagnostics)) {
            last_wave_publish_ms_ = now_ms;
            last_wave_cycle_ = snapshot.cycle_count;
        }
    }

    if (periodic_publish_armed_ && media_publish_allowed && image_interval_ms_ > 0 && capture.has_frame &&
        capture.frame_id != last_image_frame_id_ &&
        (last_image_publish_ms_ == 0 || now_ms - last_image_publish_ms_ >= static_cast<uint64_t>(image_interval_ms_))) {
        if (link_.PublishImage(capture, diagnostics)) {
            last_image_publish_ms_ = now_ms;
            last_image_frame_id_ = capture.frame_id;
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
    MotionSupervisorState motion_state{};
    RuntimeTuningSnapshot tuning_snapshot{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        motion_state = state.motion_state;
        tuning_snapshot = SnapshotRuntimeTuningState(state.tuning_state);
    }
    const platform::AssistantStatusView status =
        BuildStatusView(motion_state, tuning_snapshot, params_, now_ms);
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
    control_priority_connection_ = true;
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
        MotionSupervisorState motion_state{};
        RuntimeTuningSnapshot tuning_snapshot{};
        {
            std::lock_guard<std::mutex> lock(state.shared_mutex);
            motion_state = state.motion_state;
            tuning_snapshot = SnapshotRuntimeTuningState(state.tuning_state);
        }
        EnqueueFeedback(platform::EncodeAssistantState(
            ToEventName(tuning_event.type),
            tuning_event.reason,
            BuildStatusView(motion_state, tuning_snapshot, params_, now_ms)));
    }
    diagnostics.Emit({accepted ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                      accepted ? "assistant.command.accepted" : "assistant.command.rejected",
                      DescribeCommand(command) +
                          (accepted ? std::string() : " reason=" + reject_reason),
                      now_ms});
}

}  // namespace ls2k::runtime
