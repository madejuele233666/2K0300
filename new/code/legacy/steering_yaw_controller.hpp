#ifndef LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
#define LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP

#include "port/runtime_parameter_types.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::legacy {

struct YawRateTargetComputation {
    float yaw_rate_gain = 0.0F;
    float yaw_rate_candidate = 0.0F;
    float yaw_rate_target = 0.0F;
};

struct GyroTurnComputation {
    float gyro_z = 0.0F;
    float gyro_error = 0.0F;
    float gyro_p_term = 0.0F;
    float gyro_d_term = 0.0F;
    float raw_turn_output = 0.0F;
};

class SteeringYawController {
public:
    void Configure(const port::RuntimeParameters& params);
    void Reset();

    YawRateTargetComputation ComputeYawRateTarget(float curvature_command,
                                                  double effective_speed_target,
                                                  port::BEVControllerMemory& memory);
    GyroTurnComputation ComputeGyroTurn(float yaw_rate_target,
                                        float gyro_z,
                                        port::BEVControllerMemory& memory);

private:
    float gyro_p_ = 0.5F;
    float gyro_i_ = 0.0F;
    float gyro_d_ = 0.0F;
    float running_speed_target_ = 100.0F;
    float curvature_to_yaw_rate_target_gain_ = 12000.0F;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
