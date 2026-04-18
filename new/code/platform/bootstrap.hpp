#ifndef LS2K_PLATFORM_BOOTSTRAP_HPP
#define LS2K_PLATFORM_BOOTSTRAP_HPP

#include "port/hardware_profile.hpp"
#include "port/platform_adapter.hpp"

namespace ls2k::platform {

std::unique_ptr<port::ICameraAdapter> MakeCameraAdapter();
std::unique_ptr<port::IImuAdapter> MakeImuAdapter();
std::unique_ptr<port::IEncoderAdapter> MakeEncoderAdapter();
std::unique_ptr<port::IMotorAdapter> MakeMotorAdapter();
std::unique_ptr<port::IPowerMonitorAdapter> MakePowerMonitorAdapter();
std::unique_ptr<port::IParamStore> MakeParamStore();

port::PlatformBundle CreatePlatformBundle(const port::HardwareProfile& profile,
                                          port::DiagnosticSink& diagnostics);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_BOOTSTRAP_HPP
