#ifndef LS2K_RUNTIME_SHUTDOWN_HPP
#define LS2K_RUNTIME_SHUTDOWN_HPP

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 执行运行时关闭流程：停止定时器、禁用执行器、关闭各硬件适配器、发布关闭完成诊断
/// @param platform     平台适配器集合
/// @param state        运行时状态
/// @param diagnostics  诊断输出接口
void RunShutdown(port::PlatformBundle& platform, RuntimeState& state, port::DiagnosticSink& diagnostics);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_SHUTDOWN_HPP
