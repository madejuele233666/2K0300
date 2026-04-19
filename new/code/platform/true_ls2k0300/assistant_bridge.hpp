#ifndef LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP

#include <array>
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
};

bool InitializeAssistantBridge(const AssistantBridgeConfig& config, std::string& detail);
AssistantBridgePollResult PollAssistantBridge();
bool AssistantBridgeReady();
bool SendAssistantOscilloscope(const std::array<float, 8>& values,
                               std::size_t channel_count,
                               std::string& detail);
bool SendAssistantImage(const uint8_t* image_gray,
                        int width,
                        int height,
                        std::string& detail);

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
