#include "platform/assistant_link.hpp"

#include <cstdint>

#include "platform/true_ls2k0300/assistant_bridge.hpp"

namespace ls2k::platform {
namespace {

constexpr std::size_t kMaxInboundLineBytes = 4096;

const char* ToStateMarker(true_ls2k0300::AssistantBridgeState state) {
    switch (state) {
        case true_ls2k0300::AssistantBridgeState::kUnconfigured:
            return "assistant.unconfigured";
        case true_ls2k0300::AssistantBridgeState::kDisconnected:
            return "assistant.disconnected";
        case true_ls2k0300::AssistantBridgeState::kConnecting:
            return "assistant.connecting";
        case true_ls2k0300::AssistantBridgeState::kReady:
            return "assistant.connected";
        case true_ls2k0300::AssistantBridgeState::kBackoff:
            return "assistant.backoff";
    }
    return "assistant.unknown";
}

port::DiagnosticLevel ToStateLevel(true_ls2k0300::AssistantBridgeState state) {
    switch (state) {
        case true_ls2k0300::AssistantBridgeState::kReady:
        case true_ls2k0300::AssistantBridgeState::kConnecting:
            return port::DiagnosticLevel::kInfo;
        case true_ls2k0300::AssistantBridgeState::kUnconfigured:
        case true_ls2k0300::AssistantBridgeState::kDisconnected:
        case true_ls2k0300::AssistantBridgeState::kBackoff:
            return port::DiagnosticLevel::kWarning;
    }
    return port::DiagnosticLevel::kWarning;
}

}  // namespace

bool AssistantLink::Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    ready_ = false;
    disconnect_pending_ = false;
    last_state_code_ = static_cast<int>(true_ls2k0300::AssistantBridgeState::kUnconfigured);
    max_target_speed_ = params.Speed_base;
    inbound_buffer_.clear();
    if (!params.assistant_enabled) {
        return false;
    }

    true_ls2k0300::AssistantBridgeConfig config{};
    config.host = params.assistant_tcp.host;
    config.port = params.assistant_tcp.port;

    std::string detail;
    const bool ok = true_ls2k0300::InitializeAssistantBridge(config, detail);
    diagnostics.Emit({ok ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                      ok ? "assistant.configured" : "assistant.config.failed",
                      ok ? "assistant sidecar configured for TCP endpoint " + config.host + ":" +
                               std::to_string(config.port)
                         : "assistant sidecar unavailable: " + detail,
                      port::NowMs()});
    return ok;
}

AssistantPollResult AssistantLink::Poll(port::DiagnosticSink& diagnostics) {
    AssistantPollResult poll_result{};
    if (!configured_) {
        return poll_result;
    }
    const bool was_ready = ready_;
    const bool disconnect_pending = disconnect_pending_;
    disconnect_pending_ = false;
    const true_ls2k0300::AssistantBridgePollResult result = true_ls2k0300::PollAssistantBridge();
    ready_ = result.state == true_ls2k0300::AssistantBridgeState::kReady;
    poll_result.ready = ready_;
    poll_result.became_ready = !was_ready && ready_;
    poll_result.connection_lost = disconnect_pending || (was_ready && !ready_);
    if (poll_result.became_ready || poll_result.connection_lost) {
        inbound_buffer_.clear();
    }
    if (result.state_changed || last_state_code_ != static_cast<int>(result.state)) {
        last_state_code_ = static_cast<int>(result.state);
        diagnostics.Emit({ToStateLevel(result.state),
                          ToStateMarker(result.state),
                          result.detail,
                          port::NowMs()});
    }
    if (!poll_result.connection_lost && !result.received_bytes.empty()) {
        DecodeReceivedBytes(result.received_bytes, poll_result.inbound_messages);
    }
    return poll_result;
}

bool AssistantLink::PublishJsonLine(const std::string& line,
                                    AssistantJsonSendReliability reliability,
                                    port::DiagnosticSink& diagnostics) {
    if (!ready_) {
        return false;
    }

    const bool was_ready = ready_;
    std::string payload = line;
    payload.push_back('\n');
    std::string detail;
    const bool ok = true_ls2k0300::SendAssistantBytes(
        reinterpret_cast<const std::uint8_t*>(payload.data()),
        payload.size(),
        reliability == AssistantJsonSendReliability::kReliable,
        detail);
    if (!ok) {
        ready_ = true_ls2k0300::AssistantBridgeReady();
        if (was_ready && !ready_) {
            disconnect_pending_ = true;
        }
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "assistant.json.failed",
                          detail,
                          port::NowMs()});
    }
    return ok;
}

bool AssistantLink::PublishWaveform(const AssistantWaveformFrame& frame, port::DiagnosticSink& diagnostics) {
    if (!ready_) {
        return false;
    }
    const bool was_ready = ready_;
    std::string detail;
    const bool ok = true_ls2k0300::SendAssistantOscilloscope(frame.values, frame.channel_count, detail);
    if (!ok) {
        ready_ = true_ls2k0300::AssistantBridgeReady();
        if (was_ready && !ready_) {
            disconnect_pending_ = true;
        }
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "assistant.wave.failed",
                          detail,
                          port::NowMs()});
    }
    return ok;
}

bool AssistantLink::PublishImage(const port::CameraCapture& capture, port::DiagnosticSink& diagnostics) {
    if (!ready_ || !capture.has_frame) {
        return false;
    }
    const bool was_ready = ready_;
    std::string detail;
    const bool ok = true_ls2k0300::SendAssistantImage(
        capture.frame.gray.data(), capture.frame.width, capture.frame.height, detail);
    if (!ok) {
        ready_ = true_ls2k0300::AssistantBridgeReady();
        if (was_ready && !ready_) {
            disconnect_pending_ = true;
        }
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "assistant.image.failed",
                          detail,
                          port::NowMs()});
    }
    return ok;
}

bool AssistantLink::Ready() const {
    return ready_;
}

void AssistantLink::DecodeReceivedBytes(const std::string& bytes,
                                        std::vector<AssistantInboundMessage>& inbound_messages) {
    inbound_buffer_.append(bytes);
    while (true) {
        const std::size_t newline_index = inbound_buffer_.find('\n');
        if (newline_index == std::string::npos) {
            break;
        }

        std::string line = inbound_buffer_.substr(0, newline_index);
        inbound_buffer_.erase(0, newline_index + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        inbound_messages.push_back(DecodeAssistantJsonLine(line, max_target_speed_));
    }

    if (inbound_buffer_.size() > kMaxInboundLineBytes) {
        inbound_messages.push_back(
            AssistantInboundMessage{AssistantInboundMessageType::kInputRejected,
                                    {},
                                    0,
                                    "input line too long"});
        inbound_buffer_.clear();
    }
}

}  // namespace ls2k::platform
