#ifndef LS2K_LEGACY_ACTUATOR_COMMAND_BUILDER_HPP
#define LS2K_LEGACY_ACTUATOR_COMMAND_BUILDER_HPP

#include "port/actuator_command_types.hpp"

namespace ls2k::legacy {

/// 执行器命令构造器，负责将各输出通道 PWM 与急停信号组合成统一执行器指令
class ActuatorCommandBuilder {
public:
    /// 组合左右驱动PWM、左右无刷电调PWM与急停信号，生成统一执行器指令。
    port::ActuatorCommand Compose(int left_drive_pwm,
                                  int right_drive_pwm,
                                  int left_brushless_pwm,
                                  int right_brushless_pwm,
                                  bool emergency_stop,
                                  int drive_pwm_limit,
                                  int brushless_pwm_limit) const;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_ACTUATOR_COMMAND_BUILDER_HPP
