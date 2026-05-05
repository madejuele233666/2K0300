#ifndef LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP
#define LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP

#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/diagnostics.hpp"
#include "port/perception_result.hpp"
#include "port/platform_adapter.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::runtime {

class SteeringFramePerceptionPipeline {
public:
    bool Configure(const port::RuntimeParameters& params,
                   port::DiagnosticSink& diagnostics);
    void ResetMemory();
    port::PerceptionResult ProcessFrame(const port::CameraCapture& capture,
                                        const port::RuntimeParameters& params);

private:
    legacy::BEVProjector projector_{};
    legacy::BEVSampleProjectionLut sample_lut_{};
    legacy::BEVElementRasterBuilder element_raster_builder_{};
    port::SteeringPerceptionMemory perception_memory_{};
    bool projector_configured_ = false;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP
