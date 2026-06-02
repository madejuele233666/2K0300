#include "safety/low_voltage_sampler.hpp"

#include <algorithm>
namespace ls2k::safety {

/// 配置低电压采样器：设置采样间隔，重置上次采样时间
/// @param params  运行时参数（含 low_voltage_sample_interval_ms）
void LowVoltageSampler::Configure(const port::RuntimeParameters& params) {
    sample_interval_ms_ = std::max(1, params.low_voltage_sample_interval_ms);
    last_sample_attempt_ms_ = 0;
}

/// 低电压采样 Tick：按间隔采样电源电压，返回由 runtime 写回的状态更新。
/// @param power       电源监控适配器
/// @param snapshot    runtime 提供的低电压状态快照
/// @param diagnostics 诊断输出接口
/// @param now_ms      当前时间戳（ms）
LowVoltageSamplerUpdate LowVoltageSampler::Tick(port::IPowerMonitorAdapter& power,
                                                const LowVoltageSamplerSnapshot& snapshot,
                                                port::DiagnosticSink& diagnostics,
                                                std::uint64_t now_ms) {
    if (last_sample_attempt_ms_ == 0) {
        if (snapshot.last_sample.valid &&
            snapshot.last_sample.capture_time_ms != 0 &&
            now_ms >= snapshot.last_sample.capture_time_ms &&
            now_ms - snapshot.last_sample.capture_time_ms < static_cast<std::uint64_t>(sample_interval_ms_)) {
            last_sample_attempt_ms_ = snapshot.last_sample.capture_time_ms;
            return {};
        }
    }
    if (last_sample_attempt_ms_ != 0 &&
        now_ms >= last_sample_attempt_ms_ &&
        now_ms - last_sample_attempt_ms_ < static_cast<std::uint64_t>(sample_interval_ms_)) {
        return {};
    }
    last_sample_attempt_ms_ = now_ms;

    const port::LowVoltageSample sample = power.SampleLowVoltage(diagnostics);
    const bool emergency = !sample.valid || sample.emergency;
    const bool previous = snapshot.low_voltage_emergency;

    if (!sample.valid) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "power.low_voltage.invalid",
                          "low-voltage sample unavailable at runtime sampler; forcing emergency veto",
                          now_ms});
        return {true, sample, emergency};
    }
    if (emergency != previous) {
        diagnostics.Emit({emergency ? port::DiagnosticLevel::kFailSafe : port::DiagnosticLevel::kInfo,
                          "power.low_voltage.transition",
                          emergency ? "runtime low-voltage emergency asserted"
                                    : "runtime low-voltage emergency cleared",
                          sample.capture_time_ms});
    }
    return {true, sample, emergency};
}

}  // namespace ls2k::safety
