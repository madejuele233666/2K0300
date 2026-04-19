#ifndef LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
#define LS2K_RUNTIME_ASSISTANT_SERVICE_HPP

#include <cstdint>

#include "platform/assistant_link.hpp"
#include "port/control_types.hpp"
#include "port/diagnostics.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class AssistantService {
public:
    void Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    void Tick(RuntimeState& state, port::DiagnosticSink& diagnostics);

private:
    bool configured_ = false;
    bool enabled_ = false;
    uint64_t last_wave_publish_ms_ = 0;
    uint64_t last_image_publish_ms_ = 0;
    uint64_t last_wave_cycle_ = 0;
    uint64_t last_image_frame_id_ = 0;
    int waveform_interval_ms_ = 40;
    int image_interval_ms_ = 80;
    platform::AssistantLink link_{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
