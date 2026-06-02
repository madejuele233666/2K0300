#ifndef LS2K_LEGACY_WHEEL_PID_HPP
#define LS2K_LEGACY_WHEEL_PID_HPP

#include "port/runtime_parameter_types.hpp"

namespace ls2k::control {

/// 旧版车轮PID控制器，用于控制电机转速跟踪目标速度
class WheelPidController {
public:
    /// 从参数结构配置PID系数
    void Configure(const port::WheelPidParameters& params);
    /// 重置控制器内部状态（误差、积分、滤波）
    void Reset();
    /// 计算PID输出
    /// @param target_speed 目标速度
    /// @param measured_speed 测量速度（先经过一阶低通滤波）
    /// @param pwm_limit PWM输出限幅
    /// @return 计算得到的PWM值
    int Compute(double target_speed, double measured_speed, int pwm_limit);

private:
    double p_ = 84.0;                     ///< 比例系数
    double i_ = 2.4;                      ///< 积分系数
    double d_ = 0.75;                     ///< 微分系数
    double integral_limit_ = 5000.0;      ///< 积分限幅
    double measurement_filter_alpha_ = 0.4;  ///< 测量值低通滤波系数（0=完全信任历史，1=完全信任新值）
    double last_error_ = 0.0;             ///< 上一帧误差（用于微分项）
    double integral_ = 0.0;               ///< 积分累加器
    double filtered_measured_speed_ = 0.0;  ///< 滤波后的测量速度
    bool filtered_measured_ready_ = false;  ///< 滤波是否已初始化
};

}  // namespace ls2k::control

#endif  // LS2K_LEGACY_WHEEL_PID_HPP
