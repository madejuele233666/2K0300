#ifndef LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP
#define LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP

#include <cstdint>

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

class LowVoltageSampler {
public:
    void Configure(const port::RuntimeParameters& params);
    void Tick(port::IPowerMonitorAdapter& power,
              RuntimeState& state,
              port::DiagnosticSink& diagnostics,
              std::uint64_t now_ms);

private:
    int sample_interval_ms_ = 1000;
    std::uint64_t last_sample_attempt_ms_ = 0;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP
