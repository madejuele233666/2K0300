#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "runtime/assistant_service.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct RecordingDiagnostics final : ls2k::port::DiagnosticSink {
    std::vector<ls2k::port::DiagnosticEvent> events{};

    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }
};

struct LinkMockState {
    bool ready = true;
    bool initialize_ok = true;
    std::vector<ls2k::platform::AssistantPollResult> poll_results{};
    std::size_t poll_index = 0;
    int json_publish_calls = 0;
    int waveform_publish_calls = 0;
    int image_publish_calls = 0;
    std::vector<std::string> published_json_lines{};
};

LinkMockState g_link_mock{};

void ResetLinkMock() {
    g_link_mock = LinkMockState{};
}

ls2k::platform::AssistantPollResult ConsumePollResult() {
    if (g_link_mock.poll_index >= g_link_mock.poll_results.size()) {
        ls2k::platform::AssistantPollResult result{};
        result.ready = g_link_mock.ready;
        return result;
    }
    ls2k::platform::AssistantPollResult result = g_link_mock.poll_results[g_link_mock.poll_index++];
    g_link_mock.ready = result.ready;
    return result;
}

ls2k::platform::AssistantInboundMessage MakeCommand(ls2k::platform::AssistantCommandType type,
                                                    std::uint64_t seq) {
    ls2k::platform::AssistantInboundMessage message{};
    message.type = ls2k::platform::AssistantInboundMessageType::kCommand;
    message.command.type = type;
    message.command.seq = seq;
    return message;
}

ls2k::platform::AssistantInboundMessage MakeTargetSpeedCommand(std::uint64_t seq,
                                                               double value,
                                                               int ttl_ms) {
    ls2k::platform::AssistantInboundMessage message = MakeCommand(
        ls2k::platform::AssistantCommandType::kSetTargetSpeed, seq);
    message.command.target_speed_value = value;
    message.command.ttl_ms = ttl_ms;
    return message;
}

ls2k::platform::AssistantPollResult MakePollResult(
    std::vector<ls2k::platform::AssistantInboundMessage> messages = {},
    bool ready = true,
    bool became_ready = false,
    bool connection_lost = false) {
    ls2k::platform::AssistantPollResult poll_result{};
    poll_result.ready = ready;
    poll_result.became_ready = became_ready;
    poll_result.connection_lost = connection_lost;
    poll_result.inbound_messages = std::move(messages);
    return poll_result;
}

void PrimeRuntimeState(ls2k::runtime::RuntimeState& state) {
    state.control_debug_snapshot.valid = true;
    state.control_debug_snapshot.cycle_count = 7;
    state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
    state.control_debug_snapshot.tuning_mode_enabled = false;
    state.control_debug_snapshot.left_speed_target = 40.0;
    state.control_debug_snapshot.right_speed_target = 40.0;
    state.latest_camera_capture.has_frame = true;
    state.latest_camera_capture.frame_id = 11;
}

bool HasJsonLine(const std::string& expected) {
    return std::find(g_link_mock.published_json_lines.begin(),
                     g_link_mock.published_json_lines.end(),
                     expected) != g_link_mock.published_json_lines.end();
}

bool HasDiagnosticContaining(const RecordingDiagnostics& diagnostics,
                             const std::string& code,
                             const std::string& expected_fragment) {
    return std::find_if(
               diagnostics.events.begin(),
               diagnostics.events.end(),
               [&](const ls2k::port::DiagnosticEvent& event) {
                   return event.code == code &&
                          event.message.find(expected_fragment) != std::string::npos;
               }) != diagnostics.events.end();
}

void WaitForNextPublishWindow() {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void WaitForDeferredMotionWindow() {
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
}

void TickWithMessages(ls2k::runtime::AssistantService& service,
                      ls2k::runtime::RuntimeState& state,
                      RecordingDiagnostics& diagnostics,
                      std::vector<ls2k::platform::AssistantInboundMessage> messages,
                      std::uint64_t cycle_count) {
    state.control_debug_snapshot.cycle_count = cycle_count;
    state.control_debug_snapshot.tuning_mode_enabled = false;
    state.latest_camera_capture.frame_id = cycle_count;

    ls2k::platform::AssistantPollResult poll_result{};
    poll_result.ready = true;
    poll_result.inbound_messages = std::move(messages);
    g_link_mock.poll_results.push_back(std::move(poll_result));

    service.Tick(state, diagnostics);
}

void TickWithPollResult(ls2k::runtime::AssistantService& service,
                        ls2k::runtime::RuntimeState& state,
                        RecordingDiagnostics& diagnostics,
                        ls2k::platform::AssistantPollResult poll_result,
                        std::uint64_t cycle_count,
                        bool tuning_mode_enabled = false) {
    state.control_debug_snapshot.cycle_count = cycle_count;
    state.control_debug_snapshot.tuning_mode_enabled = tuning_mode_enabled;
    state.latest_camera_capture.frame_id = cycle_count;
    g_link_mock.poll_results.push_back(std::move(poll_result));
    service.Tick(state, diagnostics);
}

void TestSetTargetSpeedSuppressesMediaInSameTick() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);
    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeTargetSpeedCommand(2, 40.0, 2500)},
                     8);

    Expect(g_link_mock.waveform_publish_calls == 0,
           "waveform publish must stay suppressed during the set_target_speed tick");
    Expect(g_link_mock.image_publish_calls == 0,
           "image publish must stay suppressed during the set_target_speed tick");
}

void TestDisableTuningKeepsMediaSuppressedUntilDisconnect() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);
    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeTargetSpeedCommand(2, 40.0, 2500)},
                     8);

    const int waveform_before_disable = g_link_mock.waveform_publish_calls;
    const int image_before_disable = g_link_mock.image_publish_calls;

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kDisableTuningMode, 3)},
                     9);

    Expect(g_link_mock.waveform_publish_calls == 0,
           "waveform publish must remain suppressed after disable_tuning_mode");
    Expect(g_link_mock.image_publish_calls == 0,
           "image publish must remain suppressed after disable_tuning_mode");
    Expect(g_link_mock.waveform_publish_calls == waveform_before_disable,
           "disable_tuning_mode must not introduce a new waveform send");
    Expect(g_link_mock.image_publish_calls == image_before_disable,
           "disable_tuning_mode must not introduce a new image send");
    Expect(HasJsonLine("ack:3"),
           "disable_tuning_mode must still publish the command ack while media is suppressed");
    Expect(HasJsonLine("state:snapshot_cleared:tuning disabled by command"),
           "disable_tuning_mode must still publish the snapshot-cleared state event");
}

void TestControlOnlyStillPublishesTelemetryJson() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);
    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeTargetSpeedCommand(2, 40.0, 2500)},
                     8);

    const int json_before = g_link_mock.json_publish_calls;
    const int waveform_before = g_link_mock.waveform_publish_calls;
    const int image_before = g_link_mock.image_publish_calls;

    WaitForNextPublishWindow();
    TickWithMessages(service, state, diagnostics, {}, 9);

    Expect(HasJsonLine("ack:1"),
           "enable_tuning_mode must publish its ack on the control-only connection");
    Expect(HasJsonLine("ack:2"),
           "set_target_speed must publish its ack on the control-only connection");
    Expect(g_link_mock.json_publish_calls == json_before + 1,
           "a control-only follow-up tick must still publish telemetry");
    Expect(!g_link_mock.published_json_lines.empty() &&
               g_link_mock.published_json_lines.back() == "telemetry",
           "the follow-up control-only publish must be telemetry");
    Expect(g_link_mock.waveform_publish_calls == waveform_before,
           "control-only telemetry must not reopen waveform publishing");
    Expect(g_link_mock.image_publish_calls == image_before,
           "control-only telemetry must not reopen image publishing");
}

void TestConnectionLostResetsControlOnlyPublishPolicy() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);

    Expect(HasDiagnosticContaining(diagnostics,
                                   "assistant.publish_policy",
                                   "control_only"),
           "a command-bearing session must switch the publish policy to control_only");

    // Simulate a stale debug snapshot that still claims tuning mode on the
    // disconnect tick; the session boundary should still reset the lane.
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, false, false, true), 8, true);

    Expect(HasDiagnosticContaining(diagnostics,
                                   "assistant.publish_policy",
                                   "control_and_media"),
           "connection_lost must reset control-only policy even if the debug snapshot lags");
}

void TestBecameReadyResetsControlOnlyPublishPolicy() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);

    Expect(HasDiagnosticContaining(diagnostics,
                                   "assistant.publish_policy",
                                   "control_only"),
           "a command-bearing session must switch the publish policy to control_only");

    // A fresh session must not inherit the prior connection's stale tuning bit.
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, true, true, false), 8, true);

    Expect(HasDiagnosticContaining(diagnostics,
                                   "assistant.publish_policy",
                                   "control_and_media"),
           "became_ready must reset control-only policy even if the debug snapshot lags");
}

void TestReconnectDropsBufferedDisconnectFeedback() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    params.assistant_waveform_publish_interval_ms = 1;
    params.assistant_image_publish_interval_ms = 1;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kEnableTuningMode, 1)},
                     7);

    const int json_before_disconnect = g_link_mock.json_publish_calls;
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, false, false, true), 8, true);
    Expect(g_link_mock.json_publish_calls == json_before_disconnect,
           "connection_lost must not publish buffered feedback on a dead link");

    const int json_before_reconnect = g_link_mock.json_publish_calls;
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, true, true, false), 9, true);

    Expect(g_link_mock.json_publish_calls == json_before_reconnect,
           "became_ready must drop buffered disconnect feedback from the previous session");
    Expect(!HasJsonLine("state:snapshot_cleared:disconnect"),
           "the fresh session must not receive the previous session's disconnect state event");
}

void TestStartIntentWaitsForAckDrain() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kStart, 1)},
                     7);

    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        Expect(!state.motion_intent.start_requested && !state.motion_intent.stop_requested,
               "start must not touch motion_intent in the same tick as the ACK");
    }
    Expect(HasJsonLine("ack:1"),
           "start must still publish its ACK while motion_intent remains deferred");

    WaitForDeferredMotionWindow();
    TickWithMessages(service, state, diagnostics, {}, 8);

    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        Expect(state.motion_intent.start_requested && !state.motion_intent.stop_requested,
               "start must arm motion_intent after the ACK drain window");
    }
    Expect(HasDiagnosticContaining(diagnostics,
                                   "assistant.motion.start.requested",
                                   "after the control ACK drained"),
           "the deferred start application must be visible in diagnostics");
}

void TestDisconnectClearsDeferredMotionIntent() {
    ResetLinkMock();

    ls2k::runtime::AssistantService service;
    RecordingDiagnostics diagnostics;
    ls2k::runtime::RuntimeState state;
    PrimeRuntimeState(state);

    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = true;
    service.Start(params, diagnostics);

    TickWithMessages(service,
                     state,
                     diagnostics,
                     {MakeCommand(ls2k::platform::AssistantCommandType::kStart, 1)},
                     7);
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, false, false, true), 8);

    WaitForDeferredMotionWindow();
    TickWithPollResult(service, state, diagnostics, MakePollResult({}, true, true, false), 9);

    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        Expect(!state.motion_intent.start_requested,
               "connection_lost must discard deferred start updates from the dead session");
    }
}

void TestResolveRuntimeSpeedTargetKeepsTuningIdleWithoutOverride() {
    ls2k::runtime::RuntimeTuningSnapshot snapshot{};
    snapshot.tuning_mode_enabled = true;

    Expect(ls2k::runtime::ResolveRuntimeSpeedTarget(snapshot, 42.0, 1000) == 0.0,
           "tuning mode without an active override must hold the runtime speed target at zero");

    snapshot.target_speed_override_enabled = true;
    snapshot.target_speed_override_value = 18.5;
    snapshot.target_speed_override_expire_at_ms = 2000;
    Expect(ls2k::runtime::ResolveRuntimeSpeedTarget(snapshot, 42.0, 1000) == 18.5,
           "an active override must still win over the tuning-mode idle target");

    snapshot.tuning_mode_enabled = false;
    snapshot.target_speed_override_enabled = false;
    snapshot.target_speed_override_expire_at_ms = 0;
    Expect(ls2k::runtime::ResolveRuntimeSpeedTarget(snapshot, 42.0, 1000) == 42.0,
           "outside tuning mode the runtime speed target must fall back to Speed_base");
}

}  // namespace

namespace ls2k::platform {

bool AssistantLink::Initialize(const port::RuntimeParameters&, port::DiagnosticSink&) {
    configured_ = true;
    ready_ = g_link_mock.ready;
    return g_link_mock.initialize_ok;
}

AssistantPollResult AssistantLink::Poll(port::DiagnosticSink&) {
    AssistantPollResult result = ConsumePollResult();
    ready_ = result.ready;
    return result;
}

bool AssistantLink::PublishJsonLine(const std::string& line, port::DiagnosticSink&) {
    g_link_mock.published_json_lines.push_back(line);
    ++g_link_mock.json_publish_calls;
    return true;
}

bool AssistantLink::PublishWaveform(const AssistantWaveformFrame&, port::DiagnosticSink&) {
    ++g_link_mock.waveform_publish_calls;
    return true;
}

bool AssistantLink::PublishImage(const port::CameraCapture&, port::DiagnosticSink&) {
    ++g_link_mock.image_publish_calls;
    return true;
}

bool AssistantLink::Ready() const {
    return g_link_mock.ready;
}

AssistantInboundMessage DecodeAssistantJsonLine(const std::string&, double) {
    return {};
}

std::string EncodeAssistantAck(std::uint64_t seq, bool accepted, const std::string&) {
    return accepted ? "ack:" + std::to_string(seq) : "reject:" + std::to_string(seq);
}

std::string EncodeAssistantState(const std::string& event,
                                 const std::string& reason,
                                 const AssistantStatusView&) {
    return "state:" + event + ":" + reason;
}

std::string EncodeAssistantTelemetry(const AssistantTelemetryView&) {
    return "telemetry";
}

const char* ToString(AssistantCommandType) {
    return "command";
}

}  // namespace ls2k::platform

namespace ls2k::runtime {

bool IsDrivePhase(MotionPhase phase) {
    return phase == MotionPhase::kSpinup || phase == MotionPhase::kRunning || phase == MotionPhase::kStopping;
}

const char* ToString(MotionPhase phase) {
    switch (phase) {
        case MotionPhase::kDisarmed:
            return "DISARMED";
        case MotionPhase::kStartRequested:
            return "START_REQUESTED";
        case MotionPhase::kSpinup:
            return "SPINUP";
        case MotionPhase::kRunning:
            return "RUNNING";
        case MotionPhase::kStopping:
            return "STOPPING";
        case MotionPhase::kFailSafeLatched:
            return "FAIL_SAFE_LATCHED";
    }
    return "UNKNOWN";
}

}  // namespace ls2k::runtime

int main() {
    try {
        TestSetTargetSpeedSuppressesMediaInSameTick();
        TestDisableTuningKeepsMediaSuppressedUntilDisconnect();
        TestControlOnlyStillPublishesTelemetryJson();
        TestConnectionLostResetsControlOnlyPublishPolicy();
        TestBecameReadyResetsControlOnlyPublishPolicy();
        TestReconnectDropsBufferedDisconnectFeedback();
        TestStartIntentWaitsForAckDrain();
        TestDisconnectClearsDeferredMotionIntent();
        TestResolveRuntimeSpeedTargetKeepsTuningIdleWithoutOverride();
    } catch (const TestFailure& failure) {
        std::cerr << "assistant_service_publish_policy_test: " << failure.message << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "assistant_service_publish_policy_test: ok\n";
    return EXIT_SUCCESS;
}
