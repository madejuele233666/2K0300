#ifndef LS2K_PORT_STEERING_STATE_TYPES_HPP
#define LS2K_PORT_STEERING_STATE_TYPES_HPP

#include <cstdint>
#include <string>

#include "port/bev_reference_types.hpp"

namespace ls2k::port {

struct BEVControllerMemory {
    float yaw_rate_target_last = 0.0F;
    float curvature_command_last = 0.0F;
    float gyro_error_last = 0.0F;
    float gyro_i_accumulator = 0.0F;
    float last_gain_scale = 1.0F;
};

struct SteeringPerceptionMemory {
    ReferenceHoldState reference_hold{};
};

struct SteeringControlMemory {
    BEVControllerMemory controller_memory{};
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_STEERING_STATE_TYPES_HPP
