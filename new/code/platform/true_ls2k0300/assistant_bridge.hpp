#ifndef LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

struct AssistantBridgeConfig {
    std::string host;
    int port = 0;
};

enum class AssistantBridgeState {
    kUnconfigured = 0,
    kDisconnected,
    kConnecting,
    kReady,
    kBackoff,
};

struct AssistantBridgePollResult {
    AssistantBridgeState state = AssistantBridgeState::kUnconfigured;
    bool state_changed = false;
    std::string detail;
    std::string received_bytes;
};

bool InitializeAssistantBridge(const AssistantBridgeConfig& config, std::string& detail);
AssistantBridgePollResult PollAssistantBridge();
bool AssistantBridgeReady();
bool SendAssistantBytes(const std::uint8_t* data, std::size_t length, bool reliable, std::string& detail);
}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
