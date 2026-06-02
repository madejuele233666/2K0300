#ifndef LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP
#define LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP

#include <cstdint>

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 低电压采样器 —— 周期性采样电源电压并更新低电压紧急状态。
class LowVoltageSampler {
public:
    /// 配置采样器：从运行时参数读取采样间隔
    void Configure(const port::RuntimeParameters& params);
    /// 周期性 Tick：按间隔采样电压，更新紧急标志，输出诊断
    /// @param power       电源监控适配器
    /// @param state       运行时状态
    /// @param diagnostics 诊断输出接口
    /// @param now_ms      当前时间戳（ms）
    void Tick(port::IPowerMonitorAdapter& power,
              RuntimeState& state,
              port::DiagnosticSink& diagnostics,
              std::uint64_t now_ms);

private:
    int sample_interval_ms_ = 1000;           ///< 采样间隔（ms）
    std::uint64_t last_sample_attempt_ms_ = 0;  ///< 上次采样尝试的时间戳
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_LOW_VOLTAGE_SAMPLER_HPP
