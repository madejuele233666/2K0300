#ifndef LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
#define LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP

#include <cstdint>

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class PerceptionFrontend {
public:
    PerceptionFrontend(port::ICameraAdapter& camera,
                       RuntimeState& state,
                       port::DiagnosticSink& diagnostics);

    bool Configure(const port::RuntimeParameters& params);
    void ProcessOneFrame(const port::RuntimeParameters& params);

private:
    bool ShouldMaterializeFrame(const port::RuntimeParameters& params) const;
    CameraFrameHandle RememberCameraCapture(const port::CameraCapture& capture,
                                            const port::RuntimeParameters& params);
    void ConsumeMemoryResetRequest();

    port::ICameraAdapter& camera_;
    RuntimeState& state_;
    port::DiagnosticSink& diagnostics_;
    legacy::BEVProjector projector_{};
    legacy::BEVSampleProjectionLut sample_lut_{};
    port::SteeringPerceptionMemory perception_memory_{};
    bool projector_configured_ = false;
    uint64_t processed_frames_ = 0;
    uint64_t consumed_perception_memory_reset_generation_ = 0;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
