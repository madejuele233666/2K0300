#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
        if (accept_in_flight_) {
            sent_frames.emplace_back(data, data + length);
            detail = "fake transport accepted in-flight";
            return ls2k::platform::SteeringMediaTransportSendResult::kAcceptedInFlight;
        }
        if (busy_) {
            detail = "fake transport busy";
            return ls2k::platform::SteeringMediaTransportSendResult::kBusyRejected;
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

    void set_accept_in_flight(bool accept_in_flight) {
        accept_in_flight_ = accept_in_flight;
    }

    ls2k::platform::SteeringMediaTransportConfig config_{};
    std::vector<std::vector<std::uint8_t>> sent_frames{};

private:
    ls2k::platform::SteeringMediaTransportState state_ =
        ls2k::platform::SteeringMediaTransportState::kDisconnected;
    bool state_dirty_ = true;
    bool busy_ = false;
    bool accept_in_flight_ = false;
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

void FillMatchingCapture(ls2k::runtime::RuntimeState& state,
                         std::uint64_t frame_id,
                         std::uint64_t capture_time_ms,
                         ls2k::port::CameraRawFrameMetadata metadata = {}) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = 320;
    frame.height = 240;
    frame.gray.fill(0x33);
    const ls2k::runtime::CameraFrameHandle handle =
        ls2k::runtime::MaterializeOwnedCameraFrame(state.camera_frame_slots,
                                                  state.next_camera_frame_slot,
                                                  frame.View(frame_id, capture_time_ms),
                                                  metadata);
    state.latest_camera_frame = handle;
    state.recent_camera_captures.Push(handle);
}

ls2k::port::VisualReferenceCandidate MakeCandidatePath(
    ls2k::port::VisualReferenceCandidateKind kind,
    const std::string& source,
    float lateral_base_m) {
    ls2k::port::VisualReferenceCandidate candidate{};
    candidate.present = true;
    candidate.kind = kind;
    candidate.reference_path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    candidate.confidence = 0.86F;
    candidate.source = source;
    candidate.reason = "unit_test_path_candidate";
    for (std::size_t index = 0; index < 3U; ++index) {
        ls2k::port::BEVPathSample& sample = candidate.reference_path.sampled_path[index];
        sample.present = true;
        sample.point.forward_m = 0.05F + 0.05F * static_cast<float>(index);
        sample.point.lateral_m = lateral_base_m + 0.01F * static_cast<float>(index);
        sample.confidence = 0.90F;
        sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    }
    return candidate;
}

void AddCandidatePath(ls2k::runtime::SteeringDebugSnapshot& snapshot,
                      ls2k::port::VisualReferenceCandidateKind kind,
                      const std::string& source,
                      float lateral_base_m) {
    ls2k::port::AppendVisualReferenceCandidatePath(
        snapshot.visual_reference.candidate_paths,
        MakeCandidatePath(kind, source, lateral_base_m));
}

void TestReporterEmitsMinimalSteeringSnapshot() {
    CollectingDiagnostics diagnostics;
    ls2k::runtime::ControlDebugReporter reporter;
    ls2k::port::RuntimeParameters params{};
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
    snapshot.steering.threshold = 91;
    snapshot.steering.perception_health.projector_ok = true;
    snapshot.steering.perception_health.reason = "ok";
    snapshot.steering.element_evidence.cross_exit.present = true;
    snapshot.steering.element_evidence.cross_exit.confidence = 0.82F;
    snapshot.steering.element_evidence.cross_exit.forward_min_m = 0.20F;
    snapshot.steering.element_evidence.cross_exit.forward_max_m = 0.42F;
    snapshot.steering.element_evidence.cross_exit.lateral_min_m = -0.35F;
    snapshot.steering.element_evidence.cross_exit.lateral_max_m = 0.36F;
    snapshot.steering.element_evidence.cross_exit.sampleable_count = 120;
    snapshot.steering.element_evidence.cross_exit.supporting_white_count = 96;
    snapshot.steering.element_evidence.cross_exit.unknown_count = 3;
    snapshot.steering.element_evidence.cross_exit.reason = "present";
    snapshot.steering.element_evidence.cross_exit.candidate.built = true;
    snapshot.steering.element_evidence.cross_exit.candidate.takeover_enabled = false;
    snapshot.steering.element_evidence.cross_exit.candidate.included_in_arbitration = false;
    snapshot.steering.element_evidence.cross_exit.candidate.reason = "takeover_disabled";
    snapshot.steering.circle_v2.enabled = true;
    snapshot.steering.circle_v2.frame_phase = "exit_trace";
    snapshot.steering.circle_v2.next_phase = "idle";
    snapshot.steering.circle_v2.dir = "left";
    snapshot.steering.circle_v2.reference_role = "exit_trace";
    snapshot.steering.circle_v2.reason = "exit_hold_released";
    snapshot.steering.circle_v2.entry_points.left.available = true;
    snapshot.steering.circle_v2.entry_points.left.point.forward_m = 0.5F;
    snapshot.steering.circle_v2.entry_points.left.point.lateral_m = -0.25F;
    ls2k::port::VisualElementEvidenceRecord record{};
    record.id = "synthetic_marker";
    record.present = true;
    record.confidence = 0.64F;
    record.reason = "synthetic_test_record";
    record.support.supporting_black_count = 7;
    snapshot.steering.element_evidence.records.push_back(record);
    snapshot.steering.visual_reference.present = true;
    snapshot.steering.visual_reference.source = "simple_interval_center";
    snapshot.steering.visual_reference.reason = "line_candidate_selected";
    snapshot.steering.visual_reference.candidate_count = 1;
    snapshot.steering.visual_reference.rejected_candidate_reason = "none";
    AddCandidatePath(snapshot.steering,
                     ls2k::port::VisualReferenceCandidateKind::kLine,
                     "simple_interval_center",
                     -0.02F);
    snapshot.steering.reference.mode = "interval_center";
    snapshot.steering.reference.source = "simple_interval_center";
    snapshot.steering.eligibility.usable = true;
    snapshot.steering.eligibility.leading_usable_samples = 4;
    snapshot.steering.eligibility.leading_min_forward_m = 0.061;
    snapshot.steering.eligibility.leading_max_forward_m = 0.25;
    snapshot.steering.eligibility.reason = "ok";
    snapshot.steering.lateral_error.computed = true;
    snapshot.steering.lateral_error.weighted_lateral_error_m = -0.09;
    snapshot.steering.lateral_error.weighted_sample_count = 4;
    snapshot.steering.lateral_error.weight_sum = 3.75;
    snapshot.steering.lateral_error.reason = "ok";
    snapshot.steering.reference_control.ready = true;
    snapshot.steering.reference_control.reason = "reference_hold";
    snapshot.steering.safety_gate.veto_active = false;
    snapshot.steering.safety_gate.reason = "none";
    snapshot.steering.degraded.active = true;
    snapshot.steering.degraded.reason = "reference_hold";
    snapshot.steering.yaw_control.turn_output_target = -0.18;
    snapshot.steering.actuator.raw_turn_output = -17;
    snapshot.steering.actuator.applied_turn_output = -15;
    snapshot.steering_internal.valid = true;
    snapshot.steering_internal.frame_id = 7;
    snapshot.steering_internal.capture_time_ms = 88;
    snapshot.steering_internal.lateral_error_gain = 18.0;
    snapshot.steering_internal.speed_scale = 1.2;
    snapshot.steering_internal.turn_output_candidate = -2.0;
    snapshot.steering_internal.gyro_z = 0.2;
    snapshot.steering_internal.gyro_error = -0.1;
    snapshot.steering_internal.gyro_p_term = -0.3;
    snapshot.steering_internal.gyro_d_term = -0.4;

    reporter.MaybeEmit(snapshot, diagnostics);

    Require(diagnostics.events.size() == 3,
            "expected control.snapshot, public steering snapshot, and internal steering diagnostics");
    const std::string& message = diagnostics.events[1].message;
    Require(diagnostics.events[1].code == "control.steering_snapshot",
            "second diagnostic must be control.steering_snapshot");
    Require(Contains(message, "eligibility.leading_min_forward_m=0.061"),
            "steering snapshot must expose leading minimum forward distance");
    Require(Contains(message, "eligibility.leading_max_forward_m=0.25"),
            "steering snapshot must expose leading maximum forward distance");
    Require(Contains(message, "lateral_error.weighted_lateral_error_m=-0.09"),
            "steering snapshot must expose weighted lateral error");
    Require(Contains(message, "lateral_error.weighted_sample_count=4"),
            "steering snapshot must expose weighted lateral sample count");
    Require(Contains(message, "lateral_error.weight_sum=3.75"),
            "steering snapshot must expose lateral-error weight sum");
    Require(Contains(message, "yaw_control.turn_output_target=-0.18"),
            "steering snapshot must expose turn-output target");
    Require(Contains(message, "perception_health.projector_ok=true"),
            "steering snapshot must expose perception health");
    Require(Contains(message, "element_evidence.cross_exit.present=true"),
            "steering snapshot must expose cross-exit evidence presence");
    Require(Contains(message, "element_evidence.cross_exit.reason=present"),
            "steering snapshot must expose cross-exit evidence reason");
    Require(Contains(message, "element_evidence.cross_exit.candidate.included_in_arbitration=false"),
            "steering snapshot must expose cross-exit arbitration inclusion");
    Require(Contains(message, "circle_v2.enabled=true"),
            "steering snapshot must expose CircleV2 enablement");
    Require(Contains(message, "circle_v2.frame_phase=exit_trace"),
            "steering snapshot must expose CircleV2 current-frame phase");
    Require(Contains(message, "circle_v2.next_phase=idle"),
            "steering snapshot must expose CircleV2 next memory phase");
    Require(Contains(message, "circle_v2.reference_role=exit_trace"),
            "steering snapshot must expose CircleV2 reference role");
    Require(Contains(message, "circle_v2.reason=exit_hold_released"),
            "steering snapshot must expose CircleV2 reason");
    Require(Contains(message, "circle_v2.entry_points.left.available=true"),
            "steering snapshot must expose CircleV2 left P availability");
    Require(Contains(message, "circle_v2.entry_points.left.forward_m=0.5"),
            "steering snapshot must expose CircleV2 left P forward coordinate");
    Require(Contains(message, "circle_v2.entry_points.left.lateral_m=-0.25"),
            "steering snapshot must expose CircleV2 left P lateral coordinate");
    Require(Contains(message, "element_evidence.records[0].id=synthetic_marker"),
            "steering snapshot must expose generic evidence record id");
    Require(Contains(message, "element_evidence.records[0].support.supporting_black_count=7"),
            "steering snapshot must expose generic evidence record support");
    Require(Contains(message, "visual_reference.reason=line_candidate_selected"),
            "steering snapshot must expose visual reference orchestration reason");
    Require(Contains(message, "visual_reference.candidate_count=1"),
            "steering snapshot must expose visual reference candidate count");
    Require(Contains(message, "visual_reference.path_candidates.count=1"),
            "steering snapshot must expose candidate path count");
    Require(Contains(message, "visual_reference.path_candidates[0].source=simple_interval_center"),
            "steering snapshot must expose candidate path source");
    Require(Contains(message, "reference_control.ready=true"),
            "steering snapshot must expose reference-control readiness");
    Require(Contains(message, "safety_gate.veto_active=false"),
            "steering snapshot must expose safety-gate state");
    Require(Contains(message, "degraded.reason=reference_hold"),
            "steering snapshot must expose degrade reason");
    Require(Contains(message, "reference.mode=interval_center"),
            "steering snapshot must expose factual interval-center reference mode");
    Require(!Contains(message, std::string("w_") + "target"),
            "steering snapshot must not expose removed legacy angular target field");
    Require(Contains(message, "actuator.raw_turn_output=-17"),
            "steering snapshot must expose raw turn command");
    Require(Contains(message, "actuator.applied_turn_output=-15"),
            "steering snapshot must expose applied turn command");
    Require(!Contains(message, "near_lateral_error"),
            "steering snapshot must not expose removed near/far control fields");
    Require(!Contains(message, std::string("cross_") + "band_present"),
            "steering snapshot must not expose unfinished element fields");
    Require(!Contains(message, "scene_evidence."),
            "steering snapshot must not expose removed evidence fields");
    Require(!Contains(message, std::string("trusted_") + "error"),
            "steering snapshot must not expose removed blend fields");
    Require(!Contains(message, "topology_"),
            "steering snapshot must not expose removed map fields");
    Require(!Contains(message, std::string("active") + "_module"),
            "steering snapshot must not expose removed module field");
    Require(!Contains(message, std::string("scene") + "_phase"),
            "steering snapshot must not expose removed phase field");
    Require(!Contains(message, std::string("scene") + "_override_source"),
            "steering snapshot must not expose removed override field");
    Require(!Contains(message, std::string("track") + "_valid"),
            "steering snapshot must not expose removed path-valid alias");
    Require(!Contains(message, std::string("threshold") + "_veto"),
            "public steering snapshot must not expose threshold veto internals");
    Require(!Contains(message, std::string("roadblock_") + "interface_state"),
            "public steering snapshot must not expose roadblock internals");
    Require(!Contains(message, "lateral_error_gain"),
            "public steering snapshot must not expose PID internals");

    const std::string& internal_message = diagnostics.events[2].message;
    Require(diagnostics.events[2].code == "control.steering_internal",
            "third diagnostic must be internal steering diagnostics");
    Require(Contains(internal_message, "authority=internal_debug_only"),
            "internal steering diagnostics must identify non-authority scope");
    Require(!Contains(internal_message, std::string("roadblock_") + "interface_state"),
            "internal steering diagnostics must not expose removed roadblock state");
    Require(Contains(internal_message, "lateral_error_gain=18"),
            "internal steering diagnostics must expose PID internals");
    Require(Contains(internal_message, "speed_scale=1.2"),
            "internal steering diagnostics must expose speed scaling");
}

void TestConfigEnvelopeIsMinimalBevContract() {
    ls2k::platform::SteeringMediaConfigSnapshot config{};
    config.publish_time_ms = 101;
    config.media_publish_interval_ms = 80;
    config.param_snapshot.running_speed_target = 100.0;
    config.param_snapshot.yaw_rate_pid_p = 0.5;
    config.param_snapshot.yaw_rate_pid_i = 0.0;
    config.param_snapshot.yaw_rate_pid_d = 0.0;
    config.param_snapshot.control_period_ms = 5;
    config.param_snapshot.low_voltage_raw_threshold = 400;
    config.param_snapshot.raw_turn_output_limit = 8000;
    config.param_snapshot.bev_control_model.lateral_error_to_wheel_delta_gain = 180.0;
    config.param_snapshot.bev_control_model.lateral_error_far_weight = 0.25;
    config.param_snapshot.bev_element.cross_exit_takeover_enabled = false;
    config.param_snapshot.bev_element_raster.enabled = true;
    config.param_snapshot.bev_element_raster.width = 320;
    config.param_snapshot.bev_projector.projector_hash = "unit-test-projector-hash";
    config.param_snapshot.bev_geometry.search_lateral_limit_m = 0.72F;
    config.param_snapshot.bev_classification.white_confidence_min = 0.60F;

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
    Require(Contains(header_json, "\"running_speed_target\":100"),
            "config snapshot must include running speed target");
    Require(Contains(header_json, "\"yaw_rate_pid\":{\"p\":0.5,\"i\":0,\"d\":0}"),
            "config snapshot must include yaw-rate PID group");
    Require(Contains(header_json, "\"low_voltage_raw_threshold\":400"),
            "config snapshot must include low-voltage raw threshold");
    Require(Contains(header_json, "\"raw_turn_output_limit\":8000"),
            "config snapshot must include raw turn output limit");
    Require(Contains(header_json, "\"LATERAL_ERROR_TO_WHEEL_DELTA_GAIN\":180"),
            "config snapshot must include lateral-error-to-wheel-delta gain");
    Require(Contains(header_json, "\"LATERAL_ERROR_FAR_WEIGHT\":0.25"),
            "config snapshot must include lateral-error far weight");
    Require(!Contains(header_json, "\"turn_output_to_wheel_delta_gain\""),
            "config snapshot must not include removed mixer gain");
    Require(!Contains(header_json, std::string("pid_turn_") + "camera"),
            "config snapshot must not include removed camera PID parameters");
    Require(Contains(header_json, "\"BEV_PROJECTOR\""),
            "config snapshot must include BEV projector group");
    Require(Contains(header_json, "\"PROJECTOR_HASH\":\"unit-test-projector-hash\""),
            "config snapshot must include projector hash");
    Require(Contains(header_json, "\"BEV_GEOMETRY\""),
            "config snapshot must include BEV geometry group");
    Require(Contains(header_json, "\"SEARCH_LATERAL_LIMIT_M\""),
            "config snapshot must include BEV image scan lateral range");
    Require(Contains(header_json, "\"BEV_CLASSIFICATION\""),
            "config snapshot must include BEV classification group");
    Require(Contains(header_json, "\"WHITE_CONFIDENCE_MIN\":0.600000023842"),
            "config snapshot must include white classification confidence");
    Require(Contains(header_json, "\"BEV_CONTROL_MODEL\""),
            "config snapshot must include BEV control model group");
    Require(!Contains(header_json, "\"CURVATURE_COMMAND_LIMIT\""),
            "config snapshot must not include removed curvature command limit");
    Require(!Contains(header_json, "\"CURVATURE_TO_TURN_OUTPUT_GAIN\""),
            "config snapshot must not include removed curvature-to-turn-output gain");
    Require(!Contains(header_json, "\"CURVATURE_TO_YAW_RATE_TARGET_GAIN\""),
            "config snapshot must not include removed yaw-rate target gain");
    Require(Contains(header_json, "\"MIN_LEADING_REFERENCE_SAMPLES\""),
            "config snapshot must include configured leading reference minimum");
    Require(Contains(header_json, "\"BEV_ELEMENT\""),
            "config snapshot must include BEV element group");
    Require(Contains(header_json, "\"CROSS_EXIT_TAKEOVER_ENABLED\":false"),
            "config snapshot must include default-off cross-exit takeover");
    Require(Contains(header_json, "\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\":0.949999988079"),
            "config snapshot must include cross wide-row white-ratio threshold");
    Require(Contains(header_json, "\"CIRCLE_V2_ENABLED\":true"),
            "config snapshot must include CircleV2 enablement");
    Require(Contains(header_json, "\"CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG\":330"),
            "config snapshot must include CircleV2 exit yaw threshold");
    Require(Contains(header_json, "\"CIRCLE_V2_EXIT_HOLD_FRAMES\":60"),
            "config snapshot must include CircleV2 exit hold frames");
    Require(Contains(header_json, "\"CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M\":0"),
            "config snapshot must include CircleV2 inner path offset");
    Require(Contains(header_json, "\"CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN\":0.5"),
            "config snapshot must include CircleV2 opposite-straight confidence threshold");
    Require(!Contains(header_json, "\"CIRCLE_ENTRY_"),
            "config snapshot must not include legacy circle entry parameters");
    Require(!Contains(header_json, "\"CIRCLE_EVIDENCE_"),
            "config snapshot must not include legacy circle evidence parameters");
    Require(!Contains(header_json, "\"CIRCLE_OPEN"),
            "config snapshot must not include legacy circle opening parameters");
    Require(!Contains(header_json, "\"CIRCLE_OPPOSITE_"),
            "config snapshot must not include legacy circle opposite-side parameters");
    Require(!Contains(header_json, "\"CIRCLE_PRESENT_"),
            "config snapshot must not include legacy circle confidence parameters");
    Require(Contains(header_json, "\"BEV_ELEMENT_RASTER\""),
            "config snapshot must include BEV element raster group");
    Require(Contains(header_json, "\"WIDTH\":320"),
            "config snapshot must include BEV element raster width");
    Require(!Contains(header_json, std::string("CURVATURE_TO_") + "W_" + "TARGET_GAIN"),
            "config snapshot must not include removed legacy angular target gain key");
    Require(!Contains(header_json, std::string("\"BEV_") + "TOPOLOGY"),
            "config snapshot must not expose removed map parameters");
    Require(!Contains(header_json, std::string("\"BEV_") + "PATH_POLICY\""),
            "config snapshot must not expose removed reference parameters");
    Require(!Contains(header_json, std::string("\"BEV_") + "SCENE_FSM\""),
            "config snapshot must not expose removed scene FSM parameters");
    Require(!Contains(header_json, std::string("\"NOMINAL_") + "LANE_WIDTH_M\""),
            "config snapshot must not expose removed lane width parameter");
    Require(!Contains(header_json, std::string("\"CONTINUITY_") + "BREAK_THRESHOLD_M\""),
            "config snapshot must not expose removed continuity parameter");
    Require(!Contains(header_json, std::string("\"SAMPLE_") + "ROW_STEP_PX\""),
            "config snapshot must not expose removed row-step parameter");

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

void TestGray4ImagePayloadEncoding() {
    std::vector<std::uint8_t> payload(320U * 240U, 0);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<std::uint8_t>(index % 256U);
    }

    std::vector<std::uint8_t> packed((payload.size() + 1U) / 2U, 0);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        const std::uint8_t nibble =
            static_cast<std::uint8_t>(std::min(15, (static_cast<int>(payload[index]) + 8) >> 4));
        if ((index & 1U) == 0U) {
            packed[index / 2U] = static_cast<std::uint8_t>(nibble << 4U);
        } else {
            packed[index / 2U] = static_cast<std::uint8_t>(packed[index / 2U] | nibble);
        }
    }

    ls2k::platform::SteeringMediaImageFrame frame{};
    frame.frame_id = 9;
    frame.capture_time_ms = 90;
    frame.publish_time_ms = 91;
    frame.width = 320;
    frame.height = 240;
    frame.pixel_format = "gray4";
    frame.motion_phase = "DISARMED";
    frame.pixel_data = packed.data();
    frame.pixel_size = packed.size();

    std::vector<std::uint8_t> encoded;
    std::string error;
    Require(ls2k::platform::EncodeSteeringMediaImageFrame(frame, encoded, error),
            "gray4 image frame should encode");

    std::string header_json;
    std::vector<std::uint8_t> decoded_payload;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(encoded.data(),
                                                        encoded.size(),
                                                        header_json,
                                                        decoded_payload,
                                                        error),
            "gray4 image frame should decode");
    Require(Contains(header_json, "\"pixel_format\":\"gray4\""),
            "gray4 image frame must declare pixel format");
    Require(Contains(header_json, "\"payload_encoding\":\"gray4_packed\""),
            "gray4 image frame must declare packed payload encoding");
    Require(decoded_payload.size() == (320U * 240U) / 2U,
            "gray4 payload should pack two pixels per byte");
}

void TestGray2ImagePayloadEncoding() {
    std::vector<std::uint8_t> payload(320U * 240U, 0);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<std::uint8_t>(index % 256U);
    }

    std::vector<std::uint8_t> packed((payload.size() + 3U) / 4U, 0);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        const std::uint8_t level =
            static_cast<std::uint8_t>(std::min(3, (static_cast<int>(payload[index]) * 3 + 127) / 255));
        const std::size_t bit_index = index * 2U;
        const int shift = 6 - static_cast<int>(bit_index % 8U);
        packed[bit_index / 8U] = static_cast<std::uint8_t>(packed[bit_index / 8U] | (level << shift));
    }

    ls2k::platform::SteeringMediaImageFrame frame{};
    frame.frame_id = 10;
    frame.capture_time_ms = 100;
    frame.publish_time_ms = 101;
    frame.width = 320;
    frame.height = 240;
    frame.pixel_format = "gray2";
    frame.motion_phase = "DISARMED";
    frame.pixel_data = packed.data();
    frame.pixel_size = packed.size();

    std::vector<std::uint8_t> encoded;
    std::string error;
    Require(ls2k::platform::EncodeSteeringMediaImageFrame(frame, encoded, error),
            "gray2 image frame should encode");

    std::string header_json;
    std::vector<std::uint8_t> decoded_payload;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(encoded.data(),
                                                        encoded.size(),
                                                        header_json,
                                                        decoded_payload,
                                                        error),
            "gray2 image frame should decode");
    Require(Contains(header_json, "\"pixel_format\":\"gray2\""),
            "gray2 image frame must declare pixel format");
    Require(Contains(header_json, "\"payload_encoding\":\"gray2_packed\""),
            "gray2 image frame must declare packed payload encoding");
    Require(decoded_payload.size() == (320U * 240U) / 4U,
            "gray2 payload should pack four pixels per byte");
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
    first_frame.steering_snapshot.reference_control.ready = true;
    first_frame.steering_snapshot.safety_gate.veto_active = false;
    first_frame.pixel_data = image_payload.data();
    first_frame.pixel_size = image_payload.size();

    fake_transport->set_busy(true);
    Require(link.PublishImageFrame(first_frame, diagnostics) ==
                ls2k::platform::SteeringMediaPublishResult::kQueued,
            "busy socket should queue the newest image frame");

    ls2k::platform::SteeringMediaImageFrame second_frame = first_frame;
    second_frame.frame_id = 2;
    second_frame.camera_metadata.source = "unit_v4l2";
    second_frame.camera_metadata.frame_id = 2;
    second_frame.camera_metadata.capture_time_ms = 10;
    second_frame.camera_metadata.dequeue_time_ms = 12;
    second_frame.camera_metadata.v4l2_sequence = 44;
    second_frame.camera_metadata.v4l2_timestamp_valid = true;
    second_frame.camera_metadata.drained_buffer_count = 3;
    second_frame.camera_metadata.poll_wait_us = 101;
    second_frame.camera_metadata.dequeue_us = 202;
    second_frame.camera_metadata.yuyv_to_gray_us = 303;
    second_frame.camera_metadata.store_submit_us = 404;
    second_frame.camera_store_health.submitted_frame_count = 9;
    second_frame.camera_store_health.overwritten_frame_count = 1;
    second_frame.camera_store_health.dropped_frame_count = 2;
    second_frame.camera_store_health.lookup_miss_count = 3;
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
    Require(Contains(header_json, "\"camera_frame\":{\"source\":\"unit_v4l2\""),
            "image frame must expose camera metadata group");
    Require(Contains(header_json, "\"width\":320"),
            "image frame camera metadata must expose source width");
    Require(Contains(header_json, "\"height\":240"),
            "image frame camera metadata must expose source height");
    Require(Contains(header_json, "\"stride\":320"),
            "image frame camera metadata must expose source stride");
    Require(Contains(header_json, "\"v4l2_sequence\":44"),
            "image frame must expose v4l2 sequence");
    Require(Contains(header_json, "\"v4l2_timestamp_valid\":true"),
            "image frame must expose v4l2 timestamp validity");
    Require(Contains(header_json, "\"poll_wait_us\":101"),
            "image frame must expose camera poll timing");
    Require(Contains(header_json, "\"dequeue_us\":202"),
            "image frame must expose camera dequeue timing");
    Require(Contains(header_json, "\"yuyv_to_gray_us\":303"),
            "image frame must expose camera conversion timing");
    Require(Contains(header_json, "\"store_submit_us\":404"),
            "image frame must expose camera frame-store timing");
    Require(Contains(header_json, "\"overwritten_frame_count\":1"),
            "image frame must expose frame-store overwrite count");
    Require(Contains(header_json, "\"dropped_frame_count\":2"),
            "image frame must expose frame-store drop count");
    Require(Contains(header_json, "\"lookup_miss_count\":3"),
            "image frame must expose frame-store lookup misses");
    Require(Contains(header_json, "\"reference_control\":{\"ready\":true"),
            "image frame snapshot must nest reference-control readiness");
    Require(Contains(header_json, "\"safety_gate\":{\"veto_active\":false"),
            "image frame snapshot must nest safety-gate state");
    Require(!Contains(header_json, std::string("\"w_") + "target\""),
            "image frame snapshot must not include removed legacy angular target field");
    Require(payload.size() == ls2k::platform::SteeringMediaImagePayloadBytes(320, 240),
            "flushed image payload must remain intact");
}

void TestLinkDoesNotCacheFrameAcceptedInFlight() {
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
    (void)link.Poll(diagnostics);

    std::vector<std::uint8_t> image_payload(
        ls2k::platform::SteeringMediaImagePayloadBytes(320, 240), 0x55);
    ls2k::platform::SteeringMediaImageFrame frame{};
    frame.frame_id = 3;
    frame.capture_time_ms = 30;
    frame.publish_time_ms = 31;
    frame.width = 320;
    frame.height = 240;
    frame.motion_phase = "RUNNING";
    frame.pixel_data = image_payload.data();
    frame.pixel_size = image_payload.size();

    fake_transport->set_accept_in_flight(true);
    Require(link.PublishImageFrame(frame, diagnostics) ==
                ls2k::platform::SteeringMediaPublishResult::kQueued,
            "accepted in-flight image should be reported as queued by the lower layer");
    fake_transport->set_accept_in_flight(false);
    Require(!link.FlushPendingImage(diagnostics),
            "upper link must not retain a duplicate pending image after lower layer accepts ownership");
    Require(fake_transport->sent_frames.size() == 1,
            "accepted in-flight image must be sent exactly once");
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
    params.yaw_rate_pid_p = 0.5;
    params.yaw_rate_pid_i = 0.0;
    params.yaw_rate_pid_d = 0.0;
    params.running_speed_target = 100.0;
    params.control_period_ms = 5;
    params.bev_control_model.lateral_error_to_wheel_delta_gain = 180.0;
    params.bev_control_model.lateral_error_far_weight = 0.25;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 41;
        state.control_debug_snapshot.steering.capture_time_ms = 1234;
        state.control_debug_snapshot.steering.perception_health.projector_ok = true;
        state.control_debug_snapshot.steering.perception_health.reason = "ok";
        state.control_debug_snapshot.steering.element_evidence.cross_exit.present = true;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.confidence = 0.81F;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.forward_min_m = 0.20F;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.forward_max_m = 0.42F;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.lateral_min_m = -0.35F;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.lateral_max_m = 0.36F;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.sampleable_count = 120;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.supporting_white_count = 96;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.unknown_count = 3;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.reason = "present";
        state.control_debug_snapshot.steering.element_evidence.cross_exit.candidate.built = true;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.candidate.takeover_enabled = false;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.candidate.included_in_arbitration = false;
        state.control_debug_snapshot.steering.element_evidence.cross_exit.candidate.reason = "takeover_disabled";
        state.control_debug_snapshot.steering.circle_v2.enabled = true;
        state.control_debug_snapshot.steering.circle_v2.frame_phase = "inner_trace";
        state.control_debug_snapshot.steering.circle_v2.next_phase = "inner_trace";
        state.control_debug_snapshot.steering.circle_v2.dir = "left";
        state.control_debug_snapshot.steering.circle_v2.reference_role = "inner_trace";
        state.control_debug_snapshot.steering.circle_v2.reason = "none";
        state.control_debug_snapshot.steering.circle_v2.entry_points.left.available = true;
        state.control_debug_snapshot.steering.circle_v2.entry_points.left.point.forward_m = 0.5F;
        state.control_debug_snapshot.steering.circle_v2.entry_points.left.point.lateral_m = -0.25F;
        ls2k::port::VisualElementEvidenceRecord record{};
        record.id = "synthetic_marker";
        record.present = true;
        record.confidence = 0.64F;
        record.reason = "synthetic_test_record";
        record.support.supporting_black_count = 7;
        state.control_debug_snapshot.steering.element_evidence.records.push_back(record);
        state.control_debug_snapshot.steering.visual_reference.present = true;
        state.control_debug_snapshot.steering.visual_reference.source = "simple_interval_center";
        state.control_debug_snapshot.steering.visual_reference.reason = "line_candidate_selected";
        state.control_debug_snapshot.steering.visual_reference.candidate_count = 1;
        state.control_debug_snapshot.steering.visual_reference.rejected_candidate_reason = "none";
        AddCandidatePath(state.control_debug_snapshot.steering,
                         ls2k::port::VisualReferenceCandidateKind::kLine,
                         "simple_interval_center",
                         -0.02F);
        state.control_debug_snapshot.steering.reference.mode = "interval_center";
        state.control_debug_snapshot.steering.reference.source = "simple_interval_center";
        state.control_debug_snapshot.steering.eligibility.usable = true;
        state.control_debug_snapshot.steering.eligibility.leading_usable_samples = 4;
        state.control_debug_snapshot.steering.eligibility.leading_min_forward_m = 0.061;
        state.control_debug_snapshot.steering.eligibility.leading_max_forward_m = 0.25;
        state.control_debug_snapshot.steering.eligibility.reason = "ok";
        state.control_debug_snapshot.steering.lateral_error.computed = true;
        state.control_debug_snapshot.steering.lateral_error.weighted_lateral_error_m = -0.10;
        state.control_debug_snapshot.steering.lateral_error.weighted_sample_count = 4;
        state.control_debug_snapshot.steering.lateral_error.weight_sum = 3.75;
        state.control_debug_snapshot.steering.lateral_error.reason = "ok";
        state.control_debug_snapshot.steering.reference_control.ready = true;
        state.control_debug_snapshot.steering.reference_control.reason = "ok";
        state.control_debug_snapshot.steering.safety_gate.veto_active = false;
        state.control_debug_snapshot.steering.safety_gate.reason = "none";
        state.control_debug_snapshot.steering.degraded.active = false;
        state.control_debug_snapshot.steering.degraded.reason = "none";
        state.control_debug_snapshot.steering.yaw_control.turn_output_target = -0.20;
        ls2k::port::CameraRawFrameMetadata metadata{};
        metadata.source = "service_v4l2";
        metadata.v4l2_sequence = 88;
        metadata.v4l2_timestamp_valid = true;
        metadata.poll_wait_us = 11;
        metadata.dequeue_us = 22;
        metadata.yuyv_to_gray_us = 33;
        metadata.store_submit_us = 44;
        FillMatchingCapture(state, 41, 1234, metadata);
        state.camera_frame_store_health.submitted_frame_count = 12;
        state.camera_frame_store_health.overwritten_frame_count = 2;
        state.camera_frame_store_health.dropped_frame_count = 1;
        state.camera_frame_store_health.lookup_miss_count = 4;
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
    Require(!Contains(header_json, std::string("pid_turn_") + "camera"),
            "service config snapshot must not export removed camera PID parameters");
    Require(Contains(header_json, "\"BEV_PROJECTOR\""),
            "service config snapshot must expose BEV projector settings");
    Require(Contains(header_json, "\"BEV_CONTROL_MODEL\""),
            "service config snapshot must expose BEV control settings");
    Require(Contains(header_json, "\"BEV_ELEMENT\""),
            "service config snapshot must expose BEV element settings");
    Require(Contains(header_json, "\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\":0.949999988079"),
            "service config snapshot must expose cross white-ratio settings");
    Require(Contains(header_json, "\"CIRCLE_V2_ENABLED\":true"),
            "service config snapshot must expose CircleV2 enablement");
    Require(Contains(header_json, "\"CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG\":330"),
            "service config snapshot must expose CircleV2 yaw threshold");
    Require(Contains(header_json, "\"CIRCLE_V2_EXIT_HOLD_FRAMES\":60"),
            "service config snapshot must expose CircleV2 hold frames");
    Require(Contains(header_json, "\"CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M\":0"),
            "service config snapshot must expose CircleV2 inner path offset");
    Require(Contains(header_json, "\"CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN\":0.5"),
            "service config snapshot must expose CircleV2 opposite-straight confidence threshold");
    Require(!Contains(header_json, "\"CIRCLE_ENTRY_"),
            "service config snapshot must not expose legacy circle entry settings");
    Require(!Contains(header_json, "\"CIRCLE_EVIDENCE_"),
            "service config snapshot must not expose legacy circle evidence settings");
    Require(!Contains(header_json, "\"CIRCLE_OPEN"),
            "service config snapshot must not expose legacy circle opening settings");
    Require(Contains(header_json, "\"BEV_ELEMENT_RASTER\""),
            "service config snapshot must expose BEV element raster settings");
    Require(Contains(header_json, "\"LATERAL_ERROR_TO_WHEEL_DELTA_GAIN\":180"),
            "service config snapshot must expose lateral-error-to-wheel-delta gain");
    Require(Contains(header_json, "\"LATERAL_ERROR_FAR_WEIGHT\":0.25"),
            "service config snapshot must expose lateral-error far weight");
    Require(!Contains(header_json, "\"turn_output_to_wheel_delta_gain\""),
            "service config snapshot must not expose removed mixer gain");
    Require(!Contains(header_json, std::string("\"BEV_") + "PATH_POLICY\""),
            "service config snapshot must not expose removed BEV path policy");

    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames[1].data(),
                                                        fake_transport->sent_frames[1].size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "image frame should decode");
    Require(Contains(header_json, "\"type\":\"image_frame\""),
            "second emitted frame must be image_frame");
    Require(Contains(header_json, "\"camera_frame\":{\"source\":\"service_v4l2\""),
            "service image frame must include camera metadata");
    Require(Contains(header_json, "\"v4l2_sequence\":88"),
            "service image frame must publish camera v4l2 sequence");
    Require(Contains(header_json, "\"store_submit_us\":44"),
            "service image frame must publish frame-store timing");
    Require(Contains(header_json, "\"submitted_frame_count\":12"),
            "service image frame must publish frame-store submitted count");
    Require(Contains(header_json, "\"overwritten_frame_count\":2"),
            "service image frame must publish frame-store overwritten count");
    Require(Contains(header_json, "\"dropped_frame_count\":1"),
            "service image frame must publish frame-store drop count");
    Require(Contains(header_json, "\"lookup_miss_count\":4"),
            "service image frame must publish frame-store lookup misses");
    Require(Contains(header_json, "\"perception_health\":{\"projector_ok\":true"),
            "image frame must include perception health");
    Require(Contains(header_json, "\"element_evidence\":{\"cross_exit\":{\"present\":true"),
            "image frame must include cross-exit element evidence");
    Require(Contains(header_json, "\"candidate\":{\"built\":true"),
            "image frame must include cross-exit candidate summary");
    Require(Contains(header_json, "\"included_in_arbitration\":false"),
            "image frame must expose cross-exit arbitration inclusion");
    Require(Contains(header_json, "\"circle_v2\":{\"enabled\":true"),
            "image frame must include CircleV2 telemetry");
    Require(Contains(header_json, "\"reference_role\":\"inner_trace\""),
            "image frame must expose CircleV2 reference role");
    Require(Contains(header_json, "\"entry_points\":{\"left\":{\"available\":true,\"forward_m\":0.5,\"lateral_m\":-0.25}"),
            "image frame must expose CircleV2 left P coordinate");
    Require(Contains(header_json, "\"right\":{\"available\":false,\"forward_m\":null,\"lateral_m\":null}"),
            "image frame must expose absent CircleV2 right P as null coordinates");
    Require(!Contains(header_json, "\"circle_entry"),
            "image frame must not expose legacy circle entry diagnostics");
    Require(Contains(header_json, "\"records\":[{\"id\":\"synthetic_marker\""),
            "image frame must include generic element evidence records");
    Require(Contains(header_json, "\"visual_reference\":{\"present\":true"),
            "image frame must include visual reference orchestration summary");
    Require(Contains(header_json, "\"candidate_count\":1"),
            "image frame must include visual reference candidate count");
    Require(Contains(header_json, "\"path_candidates\":{\"count\":1"),
            "image frame must include visual reference path candidate set");
    Require(Contains(header_json, "\"samples\":[{\"index\":0,\"forward_m\":"),
            "image frame must include path candidate BEV samples");
    Require(Contains(header_json, "\"reference_control\":{\"ready\":true"),
            "image frame must include reference-control readiness");
    Require(Contains(header_json, "\"safety_gate\":{\"veto_active\":false"),
            "image frame must include safety gate");
    Require(Contains(header_json, "\"lateral_error\":{\"computed\":true"),
            "image frame must include lateral-error group");
    Require(Contains(header_json, "\"weighted_lateral_error_m\":-0.1"),
            "image frame must include weighted lateral error");
    Require(Contains(header_json, "\"weighted_sample_count\":4"),
            "image frame must include lateral-error sample count");
    Require(Contains(header_json, "\"weight_sum\":3.75"),
            "image frame must include lateral-error weight sum");
    Require(Contains(header_json, "\"turn_output_target\":-0.2"),
            "image frame must include turn-output target");
    Require(Contains(header_json, "\"reference\":{\"mode\":\"interval_center\",\"source\":\"simple_interval_center\"}"),
            "image frame must include nested reference facts");
    Require(!Contains(header_json, "\"reference_mode\""),
            "image frame must not include old flat reference mode");
    Require(!Contains(header_json, "\"reference_source\""),
            "image frame must not include old flat reference source");
    Require(!Contains(header_json, "\"near_lateral_error\""),
            "image frame must not include removed near/far fields");
    Require(!Contains(header_json, std::string("cross_") + "band_present"),
            "image frame must not include unfinished element fields");
    Require(!Contains(header_json, std::string("\"trusted_") + "error\""),
            "image frame must not include removed blend fields");
    Require(!Contains(header_json, "\"topology_"),
            "image frame must not include removed map fields");
    Require(!Contains(header_json, std::string("\"active") + "_module\""),
            "image frame must not include removed module field");
    Require(!Contains(header_json, std::string("\"scene") + "_phase\""),
            "image frame must not include removed phase field");
    Require(!Contains(header_json, std::string("\"scene") + "_override_source\""),
            "image frame must not include removed override field");
    Require(!Contains(header_json, std::string("\"track") + "_valid\""),
            "image frame must not include removed path-valid alias");
    Require(!Contains(header_json, std::string("\"threshold") + "_veto\""),
            "image frame must not include threshold veto internals");
    Require(!Contains(header_json, std::string("\"roadblock_") + "interface_state\""),
            "image frame must not include roadblock internals");
    Require(!Contains(header_json, "\"lateral_error_gain\""),
            "image frame must not include PID internals");
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
        FillMatchingCapture(state, 41, 1234);

        FillMatchingCapture(state, 42, 1249);
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
    Require(Contains(header_json, "\"frame_source\":\"snapshot_aligned\""),
            "default media mode must keep snapshot-aligned image publication");
    Require(Contains(header_json, "\"snapshot_alignment\":{\"aligned\":true"),
            "default media mode must expose exact snapshot/image alignment");
}

void TestServiceCanPublishLatestCameraFrameForLiveView() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::runtime::SteeringMediaService service{ls2k::platform::SteeringMediaLink{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)}};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    params.steering_media_publish_interval_ms = 0;
    params.steering_media_downsample = 1;
    params.steering_media_publish_disarmed = true;
    params.steering_media_publish_latest_frame = true;
    params.steering_media_gray_bits = 4;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kDisarmed;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 41;
        state.control_debug_snapshot.steering.capture_time_ms = 1234;
        FillMatchingCapture(state, 41, 1234);
        FillMatchingCapture(state, 99, 2222);
    }

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    service.Tick(state, diagnostics);

    Require(fake_transport->sent_frames.size() >= 2,
            "latest-frame mode should publish image even when latest capture is newer than steering snapshot");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames[1].data(),
                                                        fake_transport->sent_frames[1].size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "latest-frame image should decode");
    Require(Contains(header_json, "\"frame_id\":99"),
            "latest-frame mode should publish newest camera frame");
    Require(Contains(header_json, "\"frame_source\":\"latest_camera_frame\""),
            "latest-frame mode must declare frame source");
    Require(Contains(header_json, "\"snapshot_alignment\":{\"aligned\":false"),
            "latest-frame mode must expose non-exact snapshot alignment");
    Require(Contains(header_json, "\"payload_encoding\":\"gray4_packed\""),
            "latest-frame high-fps mode should use gray4 payload");
    Require(payload.size() == (320U * 240U) / 2U,
            "latest-frame gray4 payload should be half-size");
}

void TestServiceSkipsDisarmedImagesAndPublishesRunningImage() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::runtime::SteeringMediaService service{ls2k::platform::SteeringMediaLink{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)}};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    params.steering_media_publish_interval_ms = 0;
    params.steering_media_publish_disarmed = false;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kDisarmed;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 1;
        state.control_debug_snapshot.steering.capture_time_ms = 10;
        FillMatchingCapture(state, 1, 10);
    }

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    service.Tick(state, diagnostics);
    Require(fake_transport->sent_frames.size() == 1,
            "DISARMED tick should publish only config_snapshot and skip image_frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.steering.frame_id = 2;
        state.control_debug_snapshot.steering.capture_time_ms = 20;
        FillMatchingCapture(state, 2, 20);
    }
    service.Tick(state, diagnostics);
    Require(fake_transport->sent_frames.size() == 1,
            "second DISARMED tick should still skip image_frame");

    bool saw_disarmed_skip_summary = false;
    for (const auto& event : diagnostics.events) {
        if (event.code == "steering_media.summary" && Contains(event.message, "skip_disarmed=")) {
            saw_disarmed_skip_summary = true;
        }
    }
    Require(saw_disarmed_skip_summary,
            "steering_media.summary must report skip_disarmed for skipped non-running images");

    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kRunning;
        state.control_debug_snapshot.steering.frame_id = 3;
        state.control_debug_snapshot.steering.capture_time_ms = 30;
        FillMatchingCapture(state, 3, 30);
    }
    service.Tick(state, diagnostics);
    Require(fake_transport->sent_frames.size() == 2,
            "RUNNING tick should publish an image_frame after DISARMED images were skipped");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames.back().data(),
                                                        fake_transport->sent_frames.back().size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "RUNNING image frame should decode");
    Require(Contains(header_json, "\"type\":\"image_frame\""),
            "RUNNING publish must be an image_frame");
    Require(Contains(header_json, "\"motion_phase\":\"RUNNING\""),
            "RUNNING image frame must preserve motion phase");
}

void TestServiceCanPublishDisarmedImagesForCalibration() {
    auto* fake_transport = new FakeSteeringMediaTransport();
    ls2k::runtime::SteeringMediaService service{ls2k::platform::SteeringMediaLink{
        std::unique_ptr<ls2k::platform::ISteeringMediaTransport>(fake_transport)}};
    CollectingDiagnostics diagnostics;

    ls2k::port::RuntimeParameters params{};
    params.assistant_tcp.host = "127.0.0.1";
    params.steering_media_enabled = true;
    params.steering_media_port = 8890;
    params.steering_media_publish_interval_ms = 0;
    params.steering_media_downsample = 4;
    params.steering_media_gray_bits = 8;
    params.steering_media_publish_disarmed = true;
    service.Start(params, diagnostics);

    ls2k::runtime::RuntimeState state{};
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.control_debug_snapshot.valid = true;
        state.control_debug_snapshot.motion_phase = ls2k::runtime::MotionPhase::kDisarmed;
        state.control_debug_snapshot.steering.valid = true;
        state.control_debug_snapshot.steering.frame_id = 8;
        state.control_debug_snapshot.steering.capture_time_ms = 80;
        FillMatchingCapture(state, 8, 80);
    }

    fake_transport->SetState(ls2k::platform::SteeringMediaTransportState::kReady, "fake ready");
    service.Tick(state, diagnostics);
    Require(fake_transport->sent_frames.size() >= 2,
            "enabled calibration mode should publish a DISARMED image_frame");

    std::string header_json;
    std::vector<std::uint8_t> payload;
    std::string error;
    Require(ls2k::platform::DecodeSteeringMediaEnvelope(fake_transport->sent_frames[1].data(),
                                                        fake_transport->sent_frames[1].size(),
                                                        header_json,
                                                        payload,
                                                        error),
            "DISARMED calibration image frame should decode");
    Require(Contains(header_json, "\"type\":\"image_frame\""),
            "calibration publish must be an image_frame");
    Require(Contains(header_json, "\"motion_phase\":\"DISARMED\""),
            "calibration image frame must preserve DISARMED motion phase");
    Require(Contains(header_json, "\"width\":80"),
            "downsampled calibration image should report transmitted width");
    Require(Contains(header_json, "\"height\":60"),
            "downsampled calibration image should report transmitted height");
    Require(Contains(header_json, "\"source_width\":320"),
            "downsampled calibration image should preserve source width");
    Require(Contains(header_json, "\"source_height\":240"),
            "downsampled calibration image should preserve source height");
    Require(Contains(header_json, "\"downsample\":4"),
            "downsampled calibration image should report downsample factor");
    Require(payload.size() == 80U * 60U,
            "downsampled calibration image payload should match transmitted dimensions");
}

}  // namespace

int main() {
    try {
        TestReporterEmitsMinimalSteeringSnapshot();
        TestConfigEnvelopeIsMinimalBevContract();
        TestImagePayloadValidation();
        TestGray4ImagePayloadEncoding();
        TestGray2ImagePayloadEncoding();
        TestLinkQueuesLatestFrameOnBusySocket();
        TestLinkDoesNotCacheFrameAcceptedInFlight();
        TestServicePublishesConfigSnapshotOnReadyTransition();
        TestServicePublishesFromRecentMatchingCapture();
        TestServiceCanPublishLatestCameraFrameForLiveView();
        TestServiceSkipsDisarmedImagesAndPublishesRunningImage();
        TestServiceCanPublishDisarmedImagesForCalibration();
    } catch (const std::exception& error) {
        std::cerr << "steering_media_selftest failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "steering_media_selftest passed\n";
    return EXIT_SUCCESS;
}
