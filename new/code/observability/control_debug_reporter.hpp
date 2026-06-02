#ifndef LS2K_OBSERVABILITY_CONTROL_DEBUG_REPORTER_HPP
#define LS2K_OBSERVABILITY_CONTROL_DEBUG_REPORTER_HPP

#include <cstdint>

#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"
#include "observability/control_debug_snapshot.hpp"

namespace ls2k::observability {

/// 控制调试报告器 —— 周期性将调试快照格式化为诊断消息输出。
class ControlDebugReporter {
public:
    /// 配置报告器：从运行时参数读取发射间隔
    void Configure(const port::RuntimeParameters& params);
    /// 重置发射时间戳（强制下一次 Tick 立即发射）
    void Reset();
    /// 按间隔策略发射调试快照到诊断系统
    void MaybeEmit(const ControlDebugSnapshot& snapshot, port::DiagnosticSink& diagnostics);

private:
    uint64_t last_emit_ms_ = 0;  ///< 上次发射的时间戳（ms）
    int interval_ms_ = 100;      ///< 发射间隔（ms）
};

}  // namespace ls2k::observability

#endif  // LS2K_OBSERVABILITY_CONTROL_DEBUG_REPORTER_HPP
