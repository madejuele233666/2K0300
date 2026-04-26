#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "platform/steering_media_link.hpp"
#include "platform/steering_media_protocol.hpp"
#include "port/diagnostics.hpp"
#include "runtime/control_debug_reporter.hpp"
#include "runtime/steering_media_service.hpp"

namespace {

class CollectingDiagnostics final : public ls2k::port::DiagnosticSink {
public:
    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }

    std::vector<ls2k::port::DiagnosticEvent> events{};
};

class FakeSteeringMediaTransport final : public ls2k::platform::ISteeringMediaTransport {
public:
    bool Initialize(const ls2k::platform::SteeringMediaTransportConfig& config,
                    std::string& detail) override {
        config_ = config;
        detail = "fake transport configured";
        return !config.host.empty() && config.port > 0;
    }

    ls2k::platform::SteeringMediaTransportPollResult Poll() override {
        ls2k::platform::SteeringMediaTransportPollResult result{};
        result.state = state_;
        result.state_changed = state_dirty_;
        result.detail = detail_;
        state_dirty_ = false;
        return result;
    }

    bool Ready() const override {
        return state_ == ls2k::platform::SteeringMediaTransportState::kReady;
    }

    ls2k::platform::SteeringMediaTransportSendResult SendBytes(const std::uint8_t* data,
                                                               std::size_t length,
                                                               std::string& detail) override {
        if (!Ready()) {
            detail = "fake transport not ready";
            return ls2k::platform::SteeringMediaTransportSendResult::kDisconnected;
        }
        if (busy_) {
            detail = "fake transport busy";
            return ls2k::platform::SteeringMediaTransportSendResult::kBusy;
        }
        sent_frames.emplace_back(data, data + length);
        detail.clear();
        return ls2k::platform::SteeringMediaTransportSendResult::kSent;
    }

    void SetState(ls2k::platform::SteeringMediaTransportState state, std::string detail) {
        state_ = state;
        detail_ = std::move(detail);
        state_dirty_ = true;
    }

    void set_busy(bool busy) {
        busy_ = busy;
    }

    ls2k::platform::SteeringMediaTransportConfig config_{};
    std::vector<std::vector<std::uint8_t>> sent_frames{};

private:
    ls2k::platform::SteeringMediaTransportState state_ =
        ls2k::platform::SteeringMediaTransportState::kDisconnected;
    bool state_dirty_ = true;
    bool busy_ = false;
    std::string detail_ = "fake transport disconnected";
};

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestReporterEmitsSteeringSnapshotWithoutAssistantOrMedia() {
    CollectingDiagnostics diagnostics;
    ls2k::runtime::ControlDebugReporter reporter;
    ls2k::port::RuntimeParameters params{};
    params.assistant_enabled = false;
    params.steering_media_enabled = false;
    params.control_snapshot_emit_interval_ms = 1;
    reporter.Configure(params);

    ls2k::runtime::ControlDebugSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.timestamp_ms = 123;
    snapshot.motion_phase = ls2k::runtime::MotionPhase::kDisarmed;
    snapshot.veto_active = true;
    snapshot.raw_turn_output = 0;
    snapshot.applied_turn_output = 0;
    snapshot.steering.valid = true;
    snapshot.steering.frame_id = 7;
    snapshot.steering.capture_time_ms = 88;
    snapshot.steering.lateral_error = -1.25;
    snapshot.steering.highest_line = 42;
    snapshot.steering.farthest_line = 38;
    snapshot.steering.steering_reference_col = 157;
    snapshot.steering.threshold = 91;
    snapshot.steering.threshold_veto = true;
    snapshot.steering.active_module = "bend";
    snapshot.steering.scene_phase = "tracking";
    snapshot.steering.scene_override_source = "lane_geometry";
    snapshot.steering.last_special_scene_correction = "bend_bias";
    snapshot.steering.circle_direction = "left";
    snapshot.steering.circle_reference_mode = "inner_offset";
    snapshot.steering.circle_heading_delta_deg = 48.5;
    snapshot.steering.circle_fallback_reason = "none";
    snapshot.steering.circle_entry_signal_active = false;
    snapshot.steering.circle_entry_release_reason = "entry_signal_lost";

    reporter.MaybeEmit(snapshot, diagnostics);

    Require(diagnostics.events.size() == 2, "expected control.snapshot and control.steering_snapshot");
    Require(diagnostics.events[0].code == "control.snapshot", "first diagnostic must be control.snapshot");
    Require(diagnostics.events[1].code == "control.steering_snapshot",
            "second diagnostic must be control.steering_snapshot");
    Require(Contains(diagnostics.events[1].message, "threshold_veto=true"),
            "steering snapshot must contain threshold_veto");
    Require(Contains(diagnostics.events[1].message, "raw_turn_output=0"),
            "vetoed steering snapshot must still be exportable");
    Require(Contains(diagnostics.events[1].message, "active_module=bend"),
            "steering snapshot must expose active_module");
    Require(Contains(diagnostics.events[1].message, "farthest_line=38"),
            "steering snapshot must expose farthest_line");
    Require(Contains(diagnostics.events[1].message, "circle_reference_mode=inner_offset"),
            "steering snapshot must expose circle reference mode");
    Require(Contains(diagnostics.events[1].message, "circle_entry_release_reason=entry_signal_lost"),
            "steering snapshot must expose circle entry release reason");
}

void TestEnvelopeValidation() {
    ls2k::platform::SteeringMediaConfigSnapshot config{};
    config.publish_time_ms = 101;
    config.media_publish_interval_ms = 80;
    config.param_snapshot.pid_turn_camera_p = 14.75;
    config.param_snapshot.pid_turn_camera_p_scale = 1.25;
    config.param_snapshot.pid_turn_camera_d = 0.5;
    config.param_snapshot.pid_turn_camera_use_fuzzy = true;
    config.param_snapshot.pid_turn_gyro_camera_p = 20.0;
    config.param_snapshot.pid_turn_gyro_camera_i = 0.75;
    config.param_snapshot.pid_turn_gyro_camera_d = 0.25;
    config.param_snapshot.p_mode = 3;
    config.param_snapshot.speed_base = 77.0;
    config.param_snapshot.control_period_ms = 5;
    config.param_snapshot.circle_exit.exit_complete_deg = 300.0;
    config.param_snapshot.circle_fallback.fixsteer_bias_scale = 0.55;

    std::vector<std::uint8_t> encoded;
    std::string error;
    Require(ls2k::platform::EncodeSteeringMediaConfigSnapshot(config, encoded, error),
            "config envelope should encode");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(
                encoded.data(), encoded.size(), header_json, payload, error),
            "encoded envelope should decode");
    Require(payload.empty(), "config snapshot payload must be empty");
    Require(Contains(header_json, "\"type\":\"config_snapshot\""),
            "config snapshot header must carry config_snapshot type");
    Require(Contains(header_json, "\"pid_turn_camera_p\":14.75"),
            "config snapshot must include camera P");
    Require(Contains(header_json, "\"pid_turn_camera_p_scale\":1.25"),
            "config snapshot must include camera P scale");
    Require(Contains(header_json, "\"pid_turn_camera_use_fuzzy\":true"),
            "config snapshot must include camera fuzzy flag");
    Require(Contains(header_json, "\"pid_turn_gyro_camera_p\":20"),
            "config snapshot must include gyro P");
    Require(Contains(header_json, "\"pid_turn_gyro_camera_i\":0.75"),
            "config snapshot must include gyro I");
    Require(Contains(header_json, "\"CIRCLE_EXIT\""),
            "config snapshot must include decoupled circle exit group");
    Require(Contains(header_json, "\"FIXSTEER_BIAS_SCALE\":0.55"),
            "config snapshot must include circle fallback settings");

    Require(!ls2k::platform::DecodeSteeringMediaEnvelope(
                encoded.data(), encoded.size() - 1, header_json, payload, error),
            "truncated envelope must fail length validation");
}

void TestImagePayloadValidation() {
    std::vector<std::uint8_t> payload(10, 0x11);
    ls2k::platform::SteeringMediaImageFrame frame{};
    frame.frame_id = 1;
    frame.capture_time_ms = 11;
    frame.publish_time_ms = 12;
    frame.width = 320;
    frame.height = 240;
    frame.motion_phase = "RUNNING";
    frame.pixel_data = payload.data();
    frame.pixel_size = payload.size();

    std::vector<std::uint8_t> encoded;
    std::string error;
    Require(!ls2k::platform::EncodeSteeringMediaImageFrame(frame, encoded, error),
            "invalid image payload size must be rejected");
    Require(Contains(error, "exactly"), "payload validation error should mention exact size");
}

void TestLinkQueuesLatestFrameOnBusySocket() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::platform::SteeringMediaLink link{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    Require(link.Initialize(params, diagnostics), "link should initialize");

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    const ls2k::platform::SteeringMediaLinkPollResult ready_poll = link.Poll(diagnostics);
    Require(ready_poll.became_ready && ready_poll.ready, "link should observe ready transition");

    ls2k::platform::SteeringMediaConfigSnapshot config{};
    config.publish_time_ms = 1;
    config.media_publish_interval_ms = 80;
    Require(link.PublishConfigSnapshot(config, diagnostics), "config snapshot should publish on ready link");
    Require(fake_transport->sent_frames.size() == 1, "config snapshot should be the first sent frame");

    std::vector<std::uint8_t> image_payload(
        ls2k::platform::SteeringMediaImagePayloadBytes(320, 240), 0x33);
    ls2k::platform::SteeringMediaImageFrame first_frame{};
    first_frame.frame_id = 1;
    first_frame.capture_time_ms = 10;
    first_frame.publish_time_ms = 11;
    first_frame.width = 320;
    first_frame.height = 240;
    first_frame.motion_phase = "RUNNING";
    first_frame.pixel_data = image_payload.data();
    first_frame.pixel_size = image_payload.size();

    fake_transport->set_busy(true);
    Require(link.PublishImageFrame(first_frame, diagnostics) ==
                ls2k::platform::SteeringMediaPublishResult::kQueued,
            "busy socket should queue the newest image frame");

    ls2k::platform::SteeringMediaImageFrame second_frame = first_frame;
    second_frame.frame_id = 2;
    Require(link.PublishImageFrame(second_frame, diagnostics) ==
                ls2k::platform::SteeringMediaPublishResult::kQueued,
            "second busy publish should replace the stale queued frame");

    fake_transport->set_busy(false);
    Require(link.FlushPendingImage(diagnostics), "queued image frame should flush when transport recovers");
    Require(fake_transport->sent_frames.size() == 2,
            "only config snapshot and latest queued image should be sent");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames.back().data(),
                                                        fake_transport->sent_frames.back().size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "queued image frame should decode");
    Require(Contains(header_json, "\"frame_id\":2"), "latest queued frame must win");
    Require(payload.size() == ls2k::platform::SteeringMediaImagePayloadBytes(320, 240),
            "flushed image payload must remain intact");
}

void TestServicePublishesConfigSnapshotOnReadyTransition() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::runtime::SteeringMediaService service{ls2k::platform::SteeringMediaLink{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)}};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    params.steering_media_publish_interval_ms = 80;
    params.pid_turn_camera_p = 14.75;
    params.pid_turn_camera_p_scale = 1.5;
    params.pid_turn_camera_d = 0.5;
    params.pid_turn_camera_use_fuzzy = true;
    params.pid_turn_gyro_camera_p = 20.0;
    params.pid_turn_gyro_camera_i = 0.5;
    params.pid_turn_gyro_camera_d = 0.25;
    params.P_Mode = 3;
    params.Speed_base = 77.0;
    params.control_period_ms = 5;
    params.circle_exit.exit_complete_deg = 300.0;
    params.circle_fallback.fixsteer_bias_scale = 0.55;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 41;
        state.control_debug_snapshot.steering.capture_time_ms = 1234;
        state.control_debug_snapshot.steering.active_module = "circle_entry";
        state.control_debug_snapshot.steering.scene_phase = "repairing";
        state.control_debug_snapshot.steering.farthest_line = 55;
        state.control_debug_snapshot.steering.steering_reference_col = 148;
        state.control_debug_snapshot.steering.circle_direction = "left";
        state.control_debug_snapshot.steering.circle_reference_mode = "inner_offset";
        state.control_debug_snapshot.steering.circle_heading_delta_deg = 52.0;
        state.control_debug_snapshot.steering.circle_fallback_reason = "none";
        state.control_debug_snapshot.steering.circle_entry_signal_active = true;
        state.control_debug_snapshot.steering.circle_entry_release_reason = "none";
        state.latest_camera_capture.has_frame = true;
        state.latest_camera_capture.frame_id = 41;
        state.latest_camera_capture.capture_time_ms = 1234;
        state.latest_camera_capture.frame.width = 320;
        state.latest_camera_capture.frame.height = 240;
        state.recent_camera_captures.Push(state.latest_camera_capture);
    }

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    service.Tick(state, diagnostics);

    Require(fake_transport->sent_frames.size() >= 2,
            "ready transition must emit config_snapshot before image publication");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames.front().data(),
                                                        fake_transport->sent_frames.front().size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "config snapshot frame should decode");
    Require(Contains(header_json, "\"type\":\"config_snapshot\""),
            "first emitted frame must be config_snapshot");
    Require(Contains(header_json, "\"pid_turn_camera_p\":14.75"),
            "service config snapshot must export camera P");
    Require(Contains(header_json, "\"pid_turn_camera_p_scale\":1.5"),
            "service config snapshot must export camera P scale");
    Require(Contains(header_json, "\"pid_turn_camera_use_fuzzy\":true"),
            "service config snapshot must export camera fuzzy flag");
    Require(Contains(header_json, "\"pid_turn_gyro_camera_p\":20"),
            "service config snapshot must export gyro P");
    Require(Contains(header_json, "\"pid_turn_gyro_camera_i\":0.5"),
            "service config snapshot must export gyro I");
    Require(Contains(header_json, "\"CIRCLE_ENTRY\""),
            "service config snapshot must expose decoupled circle entry group");

    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames[1].data(),
                                                        fake_transport->sent_frames[1].size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "image frame should decode");
    Require(Contains(header_json, "\"type\":\"image_frame\""),
            "second emitted frame must be image_frame");
    Require(Contains(header_json, "\"active_module\":\"circle_entry\""),
            "image frame must include active_module in steering snapshot");
    Require(Contains(header_json, "\"scene_phase\":\"repairing\""),
            "image frame must include scene_phase in steering snapshot");
    Require(Contains(header_json, "\"farthest_line\":55"),
            "image frame must include farthest_line in steering snapshot");
    Require(Contains(header_json, "\"circle_direction\":\"left\""),
            "image frame must include circle direction");
    Require(Contains(header_json, "\"circle_reference_mode\":\"inner_offset\""),
            "image frame must include circle reference mode");
    Require(Contains(header_json, "\"circle_entry_signal_active\":true"),
            "image frame must include circle entry signal activity");
}

void TestServicePublishesFromRecentMatchingCapture() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::runtime::SteeringMediaService service{ls2k::platform::SteeringMediaLink{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)}};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    params.steering_media_publish_interval_ms = 80;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 41;
        state.control_debug_snapshot.steering.capture_time_ms = 1234;
        state.control_debug_snapshot.steering.active_module = "straight";
        state.control_debug_snapshot.steering.scene_phase = "idle";

        ls2k::port::CameraCapture matched_capture{};
        matched_capture.has_frame = true;
        matched_capture.frame_id = 41;
        matched_capture.capture_time_ms = 1234;
        matched_capture.frame.width = 320;
        matched_capture.frame.height = 240;
        state.recent_camera_captures.Push(matched_capture);

        state.latest_camera_capture.has_frame = true;
        state.latest_camera_capture.frame_id = 42;
        state.latest_camera_capture.capture_time_ms = 1249;
        state.latest_camera_capture.frame.width = 320;
        state.latest_camera_capture.frame.height = 240;
        state.recent_camera_captures.Push(state.latest_camera_capture);
    }

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    service.Tick(state, diagnostics);

    Require(fake_transport->sent_frames.size() >= 2,
            "service should publish image using the most recent matching capture");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames[1].data(),
                                                        fake_transport->sent_frames[1].size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "recent-history image frame should decode");
    Require(Contains(header_json, "\"frame_id\":41"),
            "service must publish the capture that exactly matches steering snapshot metadata");
}

}  // namespace

int main() {
    try {
        TestReporterEmitsSteeringSnapshotWithoutAssistantOrMedia();
        TestEnvelopeValidation();
        TestImagePayloadValidation();
        TestLinkQueuesLatestFrameOnBusySocket();
        TestServicePublishesConfigSnapshotOnReadyTransition();
        TestServicePublishesFromRecentMatchingCapture();
    } catch (const std::exception& error) {
        std::cerr << "steering_media_selftest failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "steering_media_selftest passed\n";
    return EXIT_SUCCESS;
}
