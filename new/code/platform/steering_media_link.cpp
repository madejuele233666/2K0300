#include "platform/steering_media_link.hpp"

#include <utility>

#include "platform/true_ls2k0300/steering_media_bridge.hpp"
#include "port/perf_counter.hpp"

namespace ls2k::platform {
namespace {

class BridgeSteeringMediaTransport final : public ISteeringMediaTransport {
public:
    bool Initialize(const SteeringMediaTransportConfig& config, std::string& detail) override {
        true_ls2k0300::SteeringMediaBridgeConfig bridge_config{};
        bridge_config.host = config.host;
        bridge_config.port = config.port;
        return true_ls2k0300::InitializeSteeringMediaBridge(bridge_config, detail);
    }

    SteeringMediaTransportPollResult Poll() override {
        const true_ls2k0300::SteeringMediaBridgePollResult result =
            true_ls2k0300::PollSteeringMediaBridge();
        SteeringMediaTransportPollResult view{};
        switch (result.state) {
            case true_ls2k0300::SteeringMediaBridgeState::kUnconfigured:
                view.state = SteeringMediaTransportState::kUnconfigured;
                break;
            case true_ls2k0300::SteeringMediaBridgeState::kDisconnected:
                view.state = SteeringMediaTransportState::kDisconnected;
                break;
            case true_ls2k0300::SteeringMediaBridgeState::kConnecting:
                view.state = SteeringMediaTransportState::kConnecting;
                break;
            case true_ls2k0300::SteeringMediaBridgeState::kReady:
                view.state = SteeringMediaTransportState::kReady;
                break;
            case true_ls2k0300::SteeringMediaBridgeState::kBackoff:
                view.state = SteeringMediaTransportState::kBackoff;
                break;
        }
        view.state_changed = result.state_changed;
        view.detail = result.detail;
        return view;
    }

    bool Ready() const override {
        return true_ls2k0300::SteeringMediaBridgeReady();
    }

    SteeringMediaTransportSendResult SendBytes(const std::uint8_t* data,
                                               std::size_t length,
                                               std::string& detail) override {
        switch (true_ls2k0300::SendSteeringMediaBytes(data, length, detail)) {
            case true_ls2k0300::SteeringMediaBridgeSendResult::kSent:
                return SteeringMediaTransportSendResult::kSent;
            case true_ls2k0300::SteeringMediaBridgeSendResult::kAcceptedInFlight:
                return SteeringMediaTransportSendResult::kAcceptedInFlight;
            case true_ls2k0300::SteeringMediaBridgeSendResult::kBusyRejected:
                return SteeringMediaTransportSendResult::kBusyRejected;
            case true_ls2k0300::SteeringMediaBridgeSendResult::kDisconnected:
                return SteeringMediaTransportSendResult::kDisconnected;
            case true_ls2k0300::SteeringMediaBridgeSendResult::kError:
                return SteeringMediaTransportSendResult::kError;
        }
        return SteeringMediaTransportSendResult::kError;
    }
};

const char* ToStateMarker(SteeringMediaTransportState state) {
    switch (state) {
        case SteeringMediaTransportState::kUnconfigured:
            return "steering_media.unconfigured";
        case SteeringMediaTransportState::kDisconnected:
            return "steering_media.disconnected";
        case SteeringMediaTransportState::kConnecting:
            return "steering_media.connecting";
        case SteeringMediaTransportState::kReady:
            return "steering_media.connected";
        case SteeringMediaTransportState::kBackoff:
            return "steering_media.backoff";
    }
    return "steering_media.unknown";
}

port::DiagnosticLevel ToStateLevel(SteeringMediaTransportState state) {
    switch (state) {
        case SteeringMediaTransportState::kReady:
        case SteeringMediaTransportState::kConnecting:
            return port::DiagnosticLevel::kInfo;
        case SteeringMediaTransportState::kUnconfigured:
        case SteeringMediaTransportState::kDisconnected:
        case SteeringMediaTransportState::kBackoff:
            return port::DiagnosticLevel::kWarning;
    }
    return port::DiagnosticLevel::kWarning;
}

}  // namespace

SteeringMediaLink::SteeringMediaLink()
    : SteeringMediaLink(std::make_unique<BridgeSteeringMediaTransport>()) {}

SteeringMediaLink::SteeringMediaLink(std::unique_ptr<ISteeringMediaTransport> transport)
    : transport_(std::move(transport)) {}

bool SteeringMediaLink::Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    ready_ = false;
    last_state_code_ = static_cast<int>(SteeringMediaTransportState::kUnconfigured);
    pending_image_.clear();
    if (!params.steering_media_enabled) {
        return false;
    }

    SteeringMediaTransportConfig config{};
    config.host = params.assistant_tcp.host;
    config.port = params.steering_media_port;

    std::string detail;
    const bool ok = transport_ != nullptr && transport_->Initialize(config, detail);
    diagnostics.Emit({ok ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                      ok ? "steering_media.configured" : "steering_media.config.failed",
                      ok ? "steering media configured for TCP endpoint " + config.host + ":" +
                               std::to_string(config.port)
                         : "steering media unavailable: " + detail,
                      port::NowMs()});
    return ok;
}

SteeringMediaLinkPollResult SteeringMediaLink::Poll(port::DiagnosticSink& diagnostics) {
    SteeringMediaLinkPollResult poll_result{};
    if (!configured_ || transport_ == nullptr) {
        return poll_result;
    }
    const bool was_ready = ready_;
    const SteeringMediaTransportPollResult result = transport_->Poll();
    ready_ = result.state == SteeringMediaTransportState::kReady;
    poll_result.ready = ready_;
    poll_result.became_ready = !was_ready && ready_;
    poll_result.connection_lost = was_ready && !ready_;
    if (poll_result.connection_lost) {
        pending_image_.clear();
    }
    if (result.state_changed || last_state_code_ != static_cast<int>(result.state)) {
        last_state_code_ = static_cast<int>(result.state);
        diagnostics.Emit({ToStateLevel(result.state),
                          ToStateMarker(result.state),
                          result.detail,
                          port::NowMs()});
    }
    return poll_result;
}

SteeringMediaTransportSendResult SteeringMediaLink::PublishEncoded(
    const std::vector<std::uint8_t>& encoded,
    const char* diagnostic_code,
    port::DiagnosticSink& diagnostics) {
    if (!ready_ || transport_ == nullptr) {
        return SteeringMediaTransportSendResult::kDisconnected;
    }
    std::string detail;
    SteeringMediaTransportSendResult send_result = SteeringMediaTransportSendResult::kError;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaSend);
        send_result = transport_->SendBytes(encoded.data(), encoded.size(), detail);
    }
    if (send_result != SteeringMediaTransportSendResult::kSent &&
        send_result != SteeringMediaTransportSendResult::kAcceptedInFlight &&
        send_result != SteeringMediaTransportSendResult::kBusyRejected) {
        ready_ = transport_->Ready();
    }
    if (send_result == SteeringMediaTransportSendResult::kDisconnected ||
        send_result == SteeringMediaTransportSendResult::kError) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          diagnostic_code,
                          detail,
                          port::NowMs()});
    }
    return send_result;
}

bool SteeringMediaLink::PublishConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                              port::DiagnosticSink& diagnostics) {
    std::vector<std::uint8_t> encoded;
    std::string error;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaEncode);
        if (!EncodeSteeringMediaConfigSnapshot(snapshot, encoded, error)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "steering_media.config_snapshot.invalid",
                              error,
                              port::NowMs()});
            return false;
        }
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(encoded, "steering_media.config_snapshot.failed", diagnostics);
    return send_result == SteeringMediaTransportSendResult::kSent ||
           send_result == SteeringMediaTransportSendResult::kAcceptedInFlight;
}

SteeringMediaPublishResult SteeringMediaLink::PublishImageFrame(const SteeringMediaImageFrame& frame,
                                                                port::DiagnosticSink& diagnostics) {
    std::vector<std::uint8_t> encoded;
    std::string error;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaEncode);
        if (!EncodeSteeringMediaImageFrame(frame, encoded, error)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "steering_media.image_frame.invalid",
                              error,
                              port::NowMs()});
            return SteeringMediaPublishResult::kUnavailable;
        }
    }
    if (!ready_) {
        return SteeringMediaPublishResult::kUnavailable;
    }
    if (!pending_image_.empty()) {
        pending_image_ = std::move(encoded);
        return SteeringMediaPublishResult::kQueued;
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(encoded, "steering_media.image_frame.failed", diagnostics);
    if (send_result == SteeringMediaTransportSendResult::kSent) {
        return SteeringMediaPublishResult::kSent;
    }
    if (send_result == SteeringMediaTransportSendResult::kAcceptedInFlight) {
        return SteeringMediaPublishResult::kQueued;
    }
    if (send_result == SteeringMediaTransportSendResult::kBusyRejected) {
        pending_image_ = std::move(encoded);
        return SteeringMediaPublishResult::kQueued;
    }
    return SteeringMediaPublishResult::kUnavailable;
}

bool SteeringMediaLink::FlushPendingImage(port::DiagnosticSink& diagnostics) {
    if (pending_image_.empty()) {
        return false;
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(pending_image_, "steering_media.image_frame.failed", diagnostics);
    if (send_result == SteeringMediaTransportSendResult::kSent ||
        send_result == SteeringMediaTransportSendResult::kAcceptedInFlight) {
        pending_image_.clear();
        return true;
    }
    if (send_result != SteeringMediaTransportSendResult::kBusyRejected) {
        if (!ready_) {
            pending_image_.clear();
        }
        return false;
    }
    return false;
}

bool SteeringMediaLink::Ready() const {
    return ready_;
}

}  // namespace ls2k::platform
