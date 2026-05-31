#ifndef LS2K_PLATFORM_BOOTSTRAP_HPP
#define LS2K_PLATFORM_BOOTSTRAP_HPP

#include "port/hardware_profile.hpp"
#include "port/platform_adapter.hpp"

namespace ls2k::platform {

/// @brief 创建相机适配器实例
std::unique_ptr<port::ICameraAdapter> MakeCameraAdapter();

/// @brief 创建 IMU 适配器实例
std::unique_ptr<port::IImuAdapter> MakeImuAdapter();

/// @brief 创建编码器适配器实例
std::unique_ptr<port::IEncoderAdapter> MakeEncoderAdapter();

/// @brief 创建执行器适配器实例
std::unique_ptr<port::IActuatorAdapter> MakeActuatorAdapter();

/// @brief 创建电源监控适配器实例
std::unique_ptr<port::IPowerMonitorAdapter> MakePowerMonitorAdapter();

/// @brief 创建参数存储实例
std::unique_ptr<port::IParamStore> MakeParamStore();

/// @brief 创建平台适配器组合包
///
/// 根据硬件描述文件和诊断输出构造所有平台适配器实例（相机、IMU、
/// 编码器、执行器、电源、参数存储、定时器），打包为 PlatformBundle 返回。
/// @param profile 硬件描述文件
/// @param diagnostics 诊断输出接口
/// @return 平台适配器组合包
port::PlatformBundle CreatePlatformBundle(const port::HardwareProfile& profile,
                                          port::DiagnosticSink& diagnostics);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_BOOTSTRAP_HPP
