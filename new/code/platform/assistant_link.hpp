#ifndef LS2K_PLATFORM_ASSISTANT_LINK_HPP
#define LS2K_PLATFORM_ASSISTANT_LINK_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "platform/assistant_protocol.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::platform {

struct AssistantPollResult {
    bool ready = false;
    bool became_ready = false;
    bool connection_lost = false;
    std::vector<AssistantInboundMessage> inbound_messages{};
};

enum class AssistantJsonSendReliability {
    kBestEffort = 0,
    kReliable,
};

class AssistantLink {
public:
    bool Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    AssistantPollResult Poll(port::DiagnosticSink& diagnostics);
    bool PublishJsonLine(const std::string& line,
                         AssistantJsonSendReliability reliability,
                         port::DiagnosticSink& diagnostics);
    bool Ready() const;

private:
    void DecodeReceivedBytes(const std::string& bytes, std::vector<AssistantInboundMessage>& inbound_messages);

    bool configured_ = false;
    bool ready_ = false;
    bool disconnect_pending_ = false;
    int last_state_code_ = -1;
    double max_target_speed_ = 0.0;
    std::string inbound_buffer_{};
};

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_ASSISTANT_LINK_HPP
