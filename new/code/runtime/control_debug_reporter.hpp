#ifndef LS2K_RUNTIME_CONTROL_DEBUG_REPORTER_HPP
#define LS2K_RUNTIME_CONTROL_DEBUG_REPORTER_HPP

#include <cstdint>

#include "port/control_types.hpp"
#include "port/diagnostics.hpp"
#include "runtime/control_debug_snapshot.hpp"

namespace ls2k::runtime {

class ControlDebugReporter {
public:
    void Configure(const port::RuntimeParameters& params);
    void Reset();
    void MaybeEmit(const ControlDebugSnapshot& snapshot, port::DiagnosticSink& diagnostics);

private:
    uint64_t last_emit_ms_ = 0;
    int interval_ms_ = 100;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_CONTROL_DEBUG_REPORTER_HPP
