#ifndef LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
#define LS2K_RUNTIME_ASSISTANT_SERVICE_HPP

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "platform/assistant_protocol.hpp"
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
    enum class PublishPolicy {
        kControlOnly,
        kControlAndMedia,
    };
    enum class DeferredMotionIntentType {
        kNone,
        kStart,
        kStop,
    };
    struct DeferredMotionIntent {
        DeferredMotionIntentType type = DeferredMotionIntentType::kNone;
        std::uint64_t seq = 0;
        std::uint64_t ready_at_ms = 0;
    };

    void EnqueueFeedback(std::string line);
    void FlushFeedback(port::DiagnosticSink& diagnostics);
    PublishPolicy DeterminePublishPolicy(const ControlDebugSnapshot& snapshot,
                                        bool session_boundary_reset) const;
    void UpdatePublishPolicy(PublishPolicy next_policy,
                             port::DiagnosticSink& diagnostics,
                             uint64_t now_ms);
    void ResetDeferredMotionIntent();
    void DeferMotionIntent(DeferredMotionIntentType type, std::uint64_t seq, uint64_t now_ms);
    void ApplyDeferredMotionIntentIfReady(RuntimeState& state,
                                          port::DiagnosticSink& diagnostics,
                                          uint64_t now_ms);
    void PublishStateEvent(RuntimeState& state,
                           const std::string& event,
                           const std::string& reason,
                           port::DiagnosticSink& diagnostics,
                           uint64_t now_ms);
    void HandleInboundMessages(const std::vector<platform::AssistantInboundMessage>& inbound_messages,
                               RuntimeState& state,
                               port::DiagnosticSink& diagnostics,
                               uint64_t now_ms);
    void HandleCommand(const platform::AssistantCommand& command,
                       RuntimeState& state,
                       port::DiagnosticSink& diagnostics,
                       uint64_t now_ms);

    bool configured_ = false;
    bool enabled_ = false;
    bool periodic_publish_armed_ = false;
    uint64_t last_wave_publish_ms_ = 0;
    uint64_t last_telemetry_publish_ms_ = 0;
    uint64_t last_image_publish_ms_ = 0;
    uint64_t last_wave_cycle_ = 0;
    uint64_t last_telemetry_cycle_ = 0;
    uint64_t last_image_frame_id_ = 0;
    int telemetry_interval_ms_ = 40;
    int waveform_interval_ms_ = 40;
    int image_interval_ms_ = 80;
    port::RuntimeParameters params_{};
    std::deque<std::string> pending_feedback_{};
    platform::AssistantLink link_{};
    PublishPolicy publish_policy_ = PublishPolicy::kControlAndMedia;
    bool control_priority_connection_ = false;
    DeferredMotionIntent deferred_motion_intent_{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_ASSISTANT_SERVICE_HPP
