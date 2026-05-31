/**
 * @file actuator_command_types.hpp
 * @brief 执行器指令类型定义
 *
 * 定义执行器PWM指令与急停标志的结构体，是控制层向下发送给执行器适配器的标准指令格式。
 */

#ifndef LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
#define LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP

namespace ls2k::port {

/**
 * @struct ActuatorCommand
 * @brief 执行器指令，包含左右驱动PWM值、左右无刷电调PWM值和急停标志
 *
 * 由控制决策层生成，经执行器适配器转发给硬件桥接层。
 * 各 PWM 范围由平台具体实现定义，默认急停为 true 确保上电安全。
 */
struct ActuatorCommand {
    int left_drive_pwm = 0;        ///< 左驱动PWM值（有符号，正值为前进）
    int right_drive_pwm = 0;       ///< 右驱动PWM值（有符号，正值为前进）
    int left_brushless_pwm = 0;    ///< 左无刷电调PWM duty；P828；0=关闭，500~1000=例程油门区间
    int right_brushless_pwm = 0;   ///< 右无刷电调PWM duty；P829；0=关闭，500~1000=例程油门区间
    bool emergency_stop = true;    ///< 急停标志。true=立即停止，false=正常行驶
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
