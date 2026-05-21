/**
 * @file actuator_command_types.hpp
 * @brief 执行器指令类型定义
 *
 * 定义电机PWM指令与急停标志的结构体，是控制层向下发送给电机适配器的标准指令格式。
 */

#ifndef LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
#define LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP

namespace ls2k::port {

/**
 * @struct ActuatorCommand
 * @brief 执行器指令，包含左右轮PWM值和急停标志
 *
 * 由控制决策层生成，经电机适配器转发给硬件桥接层。
 * left_pwm/right_pwm 范围由平台具体实现定义，默认急停为 true 确保上电安全。
 */
struct ActuatorCommand {
    int left_pwm = 0;       ///< 左轮PWM值（有符号，正值为前进）
    int right_pwm = 0;      ///< 右轮PWM值（有符号，正值为前进）
    bool emergency_stop = true;  ///< 急停标志。true=立即停止，false=正常行驶
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
