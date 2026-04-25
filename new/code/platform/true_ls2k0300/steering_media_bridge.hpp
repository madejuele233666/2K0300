#ifndef LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

struct SteeringMediaBridgeConfig {
    std::string host;
    int port = 0;
};

enum class SteeringMediaBridgeState {
    kUnconfigured = 0,
    kDisconnected,
    kConnecting,
    kReady,
    kBackoff,
};

struct SteeringMediaBridgePollResult {
    SteeringMediaBridgeState state = SteeringMediaBridgeState::kUnconfigured;
    bool state_changed = false;
    std::string detail;
};

enum class SteeringMediaBridgeSendResult {
    kSent = 0,
    kBusy,
    kDisconnected,
    kError,
};

bool InitializeSteeringMediaBridge(const SteeringMediaBridgeConfig& config, std::string& detail);
SteeringMediaBridgePollResult PollSteeringMediaBridge();
bool SteeringMediaBridgeReady();
SteeringMediaBridgeSendResult SendSteeringMediaBytes(const std::uint8_t* data,
                                                     std::size_t length,
                                                     std::string& detail);

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP
