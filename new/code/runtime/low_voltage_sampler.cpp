#include "runtime/low_voltage_sampler.hpp"

#include <algorithm>
#include <mutex>

namespace ls2k::runtime {

void LowVoltageSampler::Configure(const port::RuntimeParameters& params) {
    sample_interval_ms_ = std::max(1, params.low_voltage_sample_interval_ms);
    last_sample_attempt_ms_ = 0;
}

void LowVoltageSampler::Tick(port::IPowerMonitorAdapter& power,
                             RuntimeState& state,
                             port::DiagnosticSink& diagnostics,
                             std::uint64_t now_ms) {
    if (last_sample_attempt_ms_ == 0) {
        port::LowVoltageSample startup_sample{};
        {
            std::lock_guard<std::mutex> lock(state.shared_mutex);
            startup_sample = state.low_voltage_last_sample;
        }
        if (startup_sample.valid &&
            startup_sample.capture_time_ms != 0 &&
            now_ms >= startup_sample.capture_time_ms &&
            now_ms - startup_sample.capture_time_ms < static_cast<std::uint64_t>(sample_interval_ms_)) {
            last_sample_attempt_ms_ = startup_sample.capture_time_ms;
            return;
        }
    }
    if (last_sample_attempt_ms_ != 0 &&
        now_ms >= last_sample_attempt_ms_ &&
        now_ms - last_sample_attempt_ms_ < static_cast<std::uint64_t>(sample_interval_ms_)) {
        return;
    }
    last_sample_attempt_ms_ = now_ms;

    const port::LowVoltageSample sample = power.SampleLowVoltage(diagnostics);
    const bool emergency = !sample.valid || sample.emergency;
    const bool previous = state.low_voltage_emergency.load();
    state.low_voltage_emergency.store(emergency);
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        state.low_voltage_last_sample = sample;
    }

    if (!sample.valid) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "power.low_voltage.invalid",
                          "low-voltage sample unavailable at runtime sampler; forcing emergency veto",
                          now_ms});
        return;
    }
    if (emergency != previous) {
        diagnostics.Emit({emergency ? port::DiagnosticLevel::kFailSafe : port::DiagnosticLevel::kInfo,
                          "power.low_voltage.transition",
                          emergency ? "runtime low-voltage emergency asserted"
                                    : "runtime low-voltage emergency cleared",
                          sample.capture_time_ms});
    }
}

}  // namespace ls2k::runtime
