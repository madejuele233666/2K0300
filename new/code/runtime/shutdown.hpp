#ifndef LS2K_RUNTIME_SHUTDOWN_HPP
#define LS2K_RUNTIME_SHUTDOWN_HPP

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

void RunShutdown(port::PlatformBundle& platform, RuntimeState& state, port::DiagnosticSink& diagnostics);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_SHUTDOWN_HPP
