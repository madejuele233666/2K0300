#ifndef LS2K_PLATFORM_ASSISTANT_LINK_HPP
#define LS2K_PLATFORM_ASSISTANT_LINK_HPP

#include <array>
#include <cstddef>

#include "port/control_types.hpp"
#include "port/diagnostics.hpp"

namespace ls2k::platform {

struct AssistantWaveformFrame {
    std::array<float, 8> values{};
    std::size_t channel_count = 0;
};

class AssistantLink {
public:
    bool Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    void Poll(port::DiagnosticSink& diagnostics);
    bool PublishWaveform(const AssistantWaveformFrame& frame, port::DiagnosticSink& diagnostics);
    bool PublishImage(const port::CameraCapture& capture, port::DiagnosticSink& diagnostics);
    bool Ready() const;

private:
    bool configured_ = false;
    bool ready_ = false;
    int last_state_code_ = -1;
};

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_ASSISTANT_LINK_HPP
