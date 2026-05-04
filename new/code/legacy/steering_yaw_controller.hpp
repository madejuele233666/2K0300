#ifndef LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
#define LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP

#include "port/runtime_parameter_types.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::legacy {

struct TurnOutputTargetComputation {
    float lateral_error_gain = 0.0F;
    float speed_scale = 0.0F;
    float turn_output_candidate = 0.0F;
    float turn_output_target = 0.0F;
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

    TurnOutputTargetComputation ComputeTurnOutputTarget(float weighted_lateral_error_m,
                                                        double effective_speed_target,
                                                        port::BEVControllerMemory& memory);
    GyroTurnComputation ComputeGyroTurn(float turn_output_target,
                                        float gyro_z,
                                        port::BEVControllerMemory& memory);

private:
    float gyro_p_ = 0.5F;
    float gyro_i_ = 0.0F;
    float gyro_d_ = 0.0F;
    float running_speed_target_ = 100.0F;
    float lateral_error_to_wheel_delta_gain_ = 180.0F;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
