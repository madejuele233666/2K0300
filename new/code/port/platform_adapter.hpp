/**
 * @file platform_adapter.hpp
 * @brief 平台适配器抽象接口定义
 *
 * 定义所有硬件子系统的抽象接口（相机、IMU、编码器、电机、定时器、电源监控、参数存储）。
 * 每个适配器接口提供了初始化、运行操作和关闭等生命周期方法。
 * PlatformBundle 结构体将各适配器的 unique_ptr 聚合在一起，方便统一管理和传递。
 *
 * 适配器模式被用于实现硬件抽象层，允许同一套上层代码适配不同的硬件平台和配置模式
 * （直连模式 vs 适配Hook模式）。
 */

#ifndef LS2K_PORT_PLATFORM_ADAPTER_HPP
#define LS2K_PORT_PLATFORM_ADAPTER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "actuator_command_types.hpp"
#include "camera_frame_types.hpp"
#include "diagnostics.hpp"
#include "hardware_profile.hpp"
#include "runtime_parameter_types.hpp"
#include "sensor_sample_types.hpp"

namespace ls2k::port {

/**
 * @class ICameraAdapter
 * @brief 相机适配器抽象接口
 *
 * 负责图像帧的捕获，提供初始化、捕获、关闭和就绪状态查询。
 */
class ICameraAdapter {
public:
    virtual ~ICameraAdapter() = default;
    /** @brief 初始化相机硬件 */
    virtual bool Initialize(const HardwareProfile& profile,
                            const RuntimeParameters& params,
                            DiagnosticSink& diagnostics) = 0;
    /** @brief 捕获一帧图像 */
    virtual CameraCapture Capture(DiagnosticSink& diagnostics) = 0;
    /** @brief 关闭相机硬件 */
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查相机是否就绪 */
    virtual bool Ready() const = 0;
};

/**
 * @class IImuAdapter
 * @brief IMU适配器抽象接口
 *
 * 负责惯性测量单元的读取，提供角速度和加速度数据。
 */
class IImuAdapter {
public:
    virtual ~IImuAdapter() = default;
    /** @brief 初始化IMU硬件 */
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    /** @brief 读取一组IMU样本（加速度+陀螺仪） */
    virtual ImuSample Read(DiagnosticSink& diagnostics) = 0;
    /** @brief 关闭IMU硬件 */
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查IMU是否就绪 */
    virtual bool Ready() const = 0;
};

/**
 * @class IEncoderAdapter
 * @brief 编码器适配器抽象接口
 *
 * 负责轮速编码器的读取，提供左右轮的转动增量。
 */
class IEncoderAdapter {
public:
    virtual ~IEncoderAdapter() = default;
    /** @brief 初始化编码器硬件 */
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    /** @brief 读取编码器增量值 */
    virtual EncoderDelta ReadDelta(DiagnosticSink& diagnostics) = 0;
    /** @brief 关闭编码器硬件 */
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查编码器是否就绪 */
    virtual bool Ready() const = 0;
};

/**
 * @class IMotorAdapter
 * @brief 电机适配器抽象接口
 *
 * 负责PWM控制命令的发送和电机的安全关闭。
 */
class IMotorAdapter {
public:
    virtual ~IMotorAdapter() = default;
    /** @brief 初始化电机硬件 */
    virtual bool Initialize(const HardwareProfile& profile, DiagnosticSink& diagnostics) = 0;
    /** @brief 执行执行器指令（设置PWM） */
    virtual bool Apply(const ActuatorCommand& command, DiagnosticSink& diagnostics) = 0;
    /** @brief 禁用电机输出 */
    virtual void Disable(DiagnosticSink& diagnostics) = 0;
    /** @brief 关闭电机硬件 */
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查电机是否就绪 */
    virtual bool Ready() const = 0;
};

/**
 * @class ITimerAdapter
 * @brief 定时器适配器抽象接口
 *
 * 负责创建周期性定时器，驱动主循环或控制节拍。
 */
class ITimerAdapter {
public:
    virtual ~ITimerAdapter() = default;
    /**
     * @brief 启动周期性定时器
     * @param profile 定时器子系统配置
     * @param period_ms 周期（毫秒）
     * @param callback 定时回调函数
     * @param on_failure 定时器失败回调
     * @param diagnostics 诊断接收器
     * @return 是否启动成功
     */
    virtual bool Start(const SubsystemProfile& profile,
                       uint32_t period_ms,
                       std::function<void()> callback,
                       std::function<void()> on_failure,
                       DiagnosticSink& diagnostics) = 0;
    /** @brief 停止定时器 */
    virtual void Stop(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查定时器是否在运行 */
    virtual bool Running() const = 0;
};

/**
 * @class IPowerMonitorAdapter
 * @brief 电源监控适配器抽象接口
 *
 * 负责低电压检测和电源状态采样。
 */
class IPowerMonitorAdapter {
public:
    virtual ~IPowerMonitorAdapter() = default;
    /** @brief 初始化电源监控硬件 */
    virtual bool Initialize(DiagnosticSink& diagnostics) = 0;
    /** @brief 配置低电压阈值 */
    virtual void ConfigureLowVoltageThreshold(int raw_threshold, DiagnosticSink& diagnostics) = 0;
    /** @brief 采样当前电压值 */
    virtual LowVoltageSample SampleLowVoltage(DiagnosticSink& diagnostics) = 0;
    /** @brief 检查电源监控是否就绪 */
    virtual bool Ready() const = 0;
};

/**
 * @class IParamStore
 * @brief 参数存储适配器抽象接口
 *
 * 负责从持久化存储加载运行时参数和硬件配置。
 */
class IParamStore {
public:
    virtual ~IParamStore() = default;
    /** @brief 从文件加载运行时参数 */
    virtual bool LoadRuntimeParameters(const std::string& path,
                                       RuntimeParameters& out,
                                       DiagnosticSink& diagnostics) = 0;
    /** @brief 从文件加载硬件配置 */
    virtual bool LoadHardwareProfile(const std::string& path,
                                     HardwareProfile& out,
                                     DiagnosticSink& diagnostics) = 0;
    /** @brief 应用启动阶段的关键运行参数 */
    virtual void ApplyStartupCritical(RuntimeParameters& params, DiagnosticSink& diagnostics) = 0;
};

/**
 * @struct PlatformBundle
 * @brief 平台适配器集合
 *
 * 将各个硬件适配器的 unique_ptr 聚合在一起，
 * 作为依赖注入的载体传递给上层运行时。
 */
struct PlatformBundle {
    std::unique_ptr<ICameraAdapter> camera;          ///< 相机适配器
    std::unique_ptr<IImuAdapter> imu;                ///< IMU适配器
    std::unique_ptr<IEncoderAdapter> encoder;        ///< 编码器适配器
    std::unique_ptr<IMotorAdapter> motor;            ///< 电机适配器
    std::unique_ptr<ITimerAdapter> timer;            ///< 定时器适配器
    std::unique_ptr<IPowerMonitorAdapter> power;     ///< 电源监控适配器
    std::unique_ptr<IParamStore> params;             ///< 参数存储适配器
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_PLATFORM_ADAPTER_HPP
