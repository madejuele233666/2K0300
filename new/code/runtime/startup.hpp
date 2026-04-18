#ifndef LS2K_RUNTIME_STARTUP_HPP
#define LS2K_RUNTIME_STARTUP_HPP

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

bool RunStartup(const port::HardwareProfile& profile,
                port::RuntimeParameters& params,
                port::PlatformBundle& platform,
                RuntimeState& state,
                port::DiagnosticSink& diagnostics);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STARTUP_HPP
