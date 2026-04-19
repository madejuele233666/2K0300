#include "platform/assistant_link.hpp"

#include "platform/true_ls2k0300/assistant_bridge.hpp"

namespace ls2k::platform {
namespace {

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
    last_state_code_ = static_cast<int>(true_ls2k0300::AssistantBridgeState::kUnconfigured);
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

void AssistantLink::Poll(port::DiagnosticSink& diagnostics) {
    if (!configured_) {
        return;
    }
    const true_ls2k0300::AssistantBridgePollResult result = true_ls2k0300::PollAssistantBridge();
    ready_ = result.state == true_ls2k0300::AssistantBridgeState::kReady;
    if (result.state_changed || last_state_code_ != static_cast<int>(result.state)) {
        last_state_code_ = static_cast<int>(result.state);
        diagnostics.Emit({ToStateLevel(result.state),
                          ToStateMarker(result.state),
                          result.detail,
                          port::NowMs()});
    }
}

bool AssistantLink::PublishWaveform(const AssistantWaveformFrame& frame, port::DiagnosticSink& diagnostics) {
    if (!ready_) {
        return false;
    }
    std::string detail;
    const bool ok = true_ls2k0300::SendAssistantOscilloscope(frame.values, frame.channel_count, detail);
    if (!ok) {
        ready_ = true_ls2k0300::AssistantBridgeReady();
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
    std::string detail;
    const bool ok = true_ls2k0300::SendAssistantImage(
        capture.frame.gray.data(), port::kLegacyFrameWidth, port::kLegacyFrameHeight, detail);
    if (!ok) {
        ready_ = true_ls2k0300::AssistantBridgeReady();
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

}  // namespace ls2k::platform
