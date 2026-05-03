#ifndef LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
#define LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP

namespace ls2k::port {

struct ActuatorCommand {
    int left_pwm = 0;
    int right_pwm = 0;
    bool emergency_stop = true;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_ACTUATOR_COMMAND_TYPES_HPP
