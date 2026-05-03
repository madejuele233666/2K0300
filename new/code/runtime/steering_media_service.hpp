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
    platform::SteeringMediaConfigSnapshot BuildConfigSnapshot(std::uint64_t now_ms) const;
    platform::SteeringMediaSnapshotView BuildSnapshotView(const SteeringDebugSnapshot& snapshot) const;

    bool configured_ = false;
    bool enabled_ = false;
    bool config_sent_ = false;
    int publish_interval_ms_ = 80;
    std::uint64_t last_image_publish_ms_ = 0;
    std::uint64_t last_image_frame_id_ = 0;
    port::RuntimeParameters params_{};
    platform::SteeringMediaLink link_{};
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP
