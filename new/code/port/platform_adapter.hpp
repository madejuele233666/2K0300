#ifndef LS2K_PORT_PLATFORM_ADAPTER_HPP
#define LS2K_PORT_PLATFORM_ADAPTER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "control_types.hpp"
#include "diagnostics.hpp"
#include "hardware_profile.hpp"

namespace ls2k::port {

class ICameraAdapter {
public:
    virtual ~ICameraAdapter() = default;
    virtual bool Initialize(const HardwareProfile& profile,
                            const RuntimeParameters& params,
                            DiagnosticSink& diagnostics) = 0;
    virtual CameraCapture Capture(DiagnosticSink& diagnostics) = 0;
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};

class IImuAdapter {
public:
    virtual ~IImuAdapter() = default;
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    virtual ImuSample Read(DiagnosticSink& diagnostics) = 0;
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};

class IEncoderAdapter {
public:
    virtual ~IEncoderAdapter() = default;
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    virtual EncoderDelta ReadDelta(DiagnosticSink& diagnostics) = 0;
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};

class IMotorAdapter {
public:
    virtual ~IMotorAdapter() = default;
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    virtual bool Apply(const ActuatorCommand& command, DiagnosticSink& diagnostics) = 0;
    virtual void Disable(DiagnosticSink& diagnostics) = 0;
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};

class ITimerAdapter {
public:
    virtual ~ITimerAdapter() = default;
    virtual bool Start(const SubsystemProfile& profile,
                       uint32_t period_ms,
                       std::function<void()> callback,
                       std::function<void()> on_failure,
                       DiagnosticSink& diagnostics) = 0;
    virtual void Stop(DiagnosticSink& diagnostics) = 0;
    virtual bool Running() const = 0;
};

class IPowerMonitorAdapter {
public:
    virtual ~IPowerMonitorAdapter() = default;
    virtual bool Initialize(DiagnosticSink& diagnostics) = 0;
    virtual LowVoltageSample SampleLowVoltage(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};

class IParamStore {
public:
    virtual ~IParamStore() = default;
    virtual bool LoadRuntimeParameters(const std::string& path,
                                       RuntimeParameters& out,
                                       DiagnosticSink& diagnostics) = 0;
    virtual bool LoadHardwareProfile(const std::string& path,
                                     HardwareProfile& out,
                                     DiagnosticSink& diagnostics) = 0;
    virtual void ApplyStartupCritical(RuntimeParameters& params, DiagnosticSink& diagnostics) = 0;
};

struct PlatformBundle {
    std::unique_ptr<ICameraAdapter> camera;
    std::unique_ptr<IImuAdapter> imu;
    std::unique_ptr<IEncoderAdapter> encoder;
    std::unique_ptr<IMotorAdapter> motor;
    std::unique_ptr<ITimerAdapter> timer;
    std::unique_ptr<IPowerMonitorAdapter> power;
    std::unique_ptr<IParamStore> params;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_PLATFORM_ADAPTER_HPP
