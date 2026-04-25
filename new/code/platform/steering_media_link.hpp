#ifndef LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/steering_media_protocol.hpp"
#include "port/control_types.hpp"
#include "port/diagnostics.hpp"

namespace ls2k::platform {

struct SteeringMediaTransportConfig {
    std::string host;
    int port = 0;
};

enum class SteeringMediaTransportState {
    kUnconfigured = 0,
    kDisconnected,
    kConnecting,
    kReady,
    kBackoff,
};

struct SteeringMediaTransportPollResult {
    SteeringMediaTransportState state = SteeringMediaTransportState::kUnconfigured;
    bool state_changed = false;
    std::string detail;
};

enum class SteeringMediaTransportSendResult {
    kSent = 0,
    kBusy,
    kDisconnected,
    kError,
};

class ISteeringMediaTransport {
public:
    virtual ~ISteeringMediaTransport() = default;
    virtual bool Initialize(const SteeringMediaTransportConfig& config, std::string& detail) = 0;
    virtual SteeringMediaTransportPollResult Poll() = 0;
    virtual bool Ready() const = 0;
    virtual SteeringMediaTransportSendResult SendBytes(const std::uint8_t* data,
                                                       std::size_t length,
                                                       std::string& detail) = 0;
};

struct SteeringMediaLinkPollResult {
    bool ready = false;
    bool became_ready = false;
    bool connection_lost = false;
};

enum class SteeringMediaPublishResult {
    kUnavailable,
    kSent,
    kQueued,
};

class SteeringMediaLink {
public:
    SteeringMediaLink();
    explicit SteeringMediaLink(std::unique_ptr<ISteeringMediaTransport> transport);

    bool Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    SteeringMediaLinkPollResult Poll(port::DiagnosticSink& diagnostics);
    bool PublishConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                               port::DiagnosticSink& diagnostics);
    SteeringMediaPublishResult PublishImageFrame(const SteeringMediaImageFrame& frame,
                                                 port::DiagnosticSink& diagnostics);
    bool FlushPendingImage(port::DiagnosticSink& diagnostics);
    bool Ready() const;

private:
    SteeringMediaTransportSendResult PublishEncoded(const std::vector<std::uint8_t>& encoded,
                                                    const char* diagnostic_code,
                                                    port::DiagnosticSink& diagnostics);

    bool configured_ = false;
    bool ready_ = false;
    int last_state_code_ = -1;
    std::vector<std::uint8_t> pending_image_{};
    std::unique_ptr<ISteeringMediaTransport> transport_{};
};

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP
