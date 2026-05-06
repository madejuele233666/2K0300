#ifndef LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP
#define LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP

#include <cstdint>

#include "platform/steering_media_link.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class SteeringMediaService {
public:
    SteeringMediaService() = default;
    explicit SteeringMediaService(platform::SteeringMediaLink link);

    void Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    void Tick(RuntimeState& state, port::DiagnosticSink& diagnostics);

private:
    struct WindowStats {
        std::uint64_t ticks = 0;
        std::uint64_t not_ready = 0;
        std::uint64_t pending_flush_sent = 0;
        std::uint64_t config_attempts = 0;
        std::uint64_t config_sent = 0;
        std::uint64_t config_wait = 0;
        std::uint64_t skip_no_capture = 0;
        std::uint64_t skip_zero_frame = 0;
        std::uint64_t skip_disarmed = 0;
        std::uint64_t skip_duplicate = 0;
        std::uint64_t skip_interval = 0;
        std::uint64_t image_sent = 0;
        std::uint64_t image_queued = 0;
        std::uint64_t image_unavailable = 0;
    };

    platform::SteeringMediaConfigSnapshot BuildConfigSnapshot(std::uint64_t now_ms) const;
    platform::SteeringMediaSnapshotView BuildSnapshotView(const SteeringDebugSnapshot& snapshot) const;
    void ResetWindowStats();
    void MaybeEmitWindowSummary(std::uint64_t now_ms, port::DiagnosticSink& diagnostics);

    bool configured_ = false;
    bool enabled_ = false;
    bool config_sent_ = false;
    bool publish_disarmed_ = false;
    int publish_interval_ms_ = 80;
    std::uint64_t last_image_publish_ms_ = 0;
    std::uint64_t last_image_frame_id_ = 0;
    std::uint64_t last_summary_ms_ = 0;
    WindowStats window_stats_{};
    port::RuntimeParameters params_{};
    platform::SteeringMediaLink link_{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP
