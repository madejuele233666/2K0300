#ifndef LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
#define LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class PerceptionFrontend {
public:
    PerceptionFrontend(port::ICameraAdapter& camera,
                       port::IPowerMonitorAdapter& power,
                       RuntimeState& state,
                       port::DiagnosticSink& diagnostics);

    void ProcessOneFrame(const port::RuntimeParameters& params);

private:
    void RefreshLowVoltageState();

    port::ICameraAdapter& camera_;
    port::IPowerMonitorAdapter& power_;
    RuntimeState& state_;
    port::DiagnosticSink& diagnostics_;
    uint64_t processed_frames_ = 0;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
