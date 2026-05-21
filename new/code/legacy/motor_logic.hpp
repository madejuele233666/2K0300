#ifndef LS2K_LEGACY_MOTOR_LOGIC_HPP
#define LS2K_LEGACY_MOTOR_LOGIC_HPP

#include "port/actuator_command_types.hpp"

namespace ls2k::legacy {

/// 旧版电机逻辑类，负责将左右PWM值与急停信号组合成最终的执行器指令
class LegacyMotorLogic {
public:
    /// 组合左右PWM值与急停信号，生成执行器指令
    /// @param left_pwm 左轮PWM值（将被钳制到±pwm_limit范围内）
    /// @param right_pwm 右轮PWM值（将被钳制到±pwm_limit范围内）
    /// @param emergency_stop 是否紧急停止（为true时返回空指令）
    /// @param pwm_limit PWM限制值（绝对值上限）
    /// @return 组合后的执行器指令
    port::ActuatorCommand Compose(int left_pwm, int right_pwm, bool emergency_stop, int pwm_limit) const;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_MOTOR_LOGIC_HPP
