#ifndef LS2K_RUNTIME_STARTUP_HPP
#define LS2K_RUNTIME_STARTUP_HPP

#include "port/platform_adapter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 执行运行时启动流程：校验硬件配置 → 初始化各适配器 → 检查低电压 → 设置启动完成标志
/// @param profile     硬件配置文件
/// @param params      运行时参数（启动时应用关键参数）
/// @param platform    平台适配器集合
/// @param state       运行时状态（启动后初始化）
/// @param diagnostics 诊断输出接口
/// @return            启动是否成功
bool RunStartup(const port::HardwareProfile& profile,
                port::RuntimeParameters& params,
                port::PlatformBundle& platform,
                RuntimeState& state,
                port::DiagnosticSink& diagnostics);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STARTUP_HPP
