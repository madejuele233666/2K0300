#include "legacy/steering_yaw_controller.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

void SteeringYawController::Configure(const port::RuntimeParameters& params) {
    gyro_p_ = static_cast<float>(params.yaw_rate_pid_p);
    gyro_i_ = static_cast<float>(params.yaw_rate_pid_i);
    gyro_d_ = static_cast<float>(params.yaw_rate_pid_d);
    running_speed_target_ = static_cast<float>(std::max(1.0, params.running_speed_target));
    curvature_to_yaw_rate_target_gain_ =
        static_cast<float>(params.bev_control_model.curvature_to_yaw_rate_target_gain);
}

void SteeringYawController::Reset() {}

YawRateTargetComputation SteeringYawController::ComputeYawRateTarget(float curvature_command,
                                                                     double effective_speed_target,
                                                                     port::BEVControllerMemory& memory) {
    const float gain_scale = 1.0F;
    const float speed_scale =
        std::clamp(static_cast<float>(effective_speed_target) / std::max(running_speed_target_, 1.0F),
                   0.6F,
                   1.2F);
    const float yaw_rate_candidate =
        curvature_to_yaw_rate_target_gain_ * speed_scale * gain_scale * curvature_command;
    memory.curvature_command_last = curvature_command;
    memory.last_gain_scale = gain_scale;
    memory.yaw_rate_target_last = yaw_rate_candidate;

    YawRateTargetComputation computation{};
    computation.yaw_rate_gain = curvature_to_yaw_rate_target_gain_ * gain_scale;
    computation.yaw_rate_candidate = yaw_rate_candidate;
    computation.yaw_rate_target = yaw_rate_candidate;
    return computation;
}

GyroTurnComputation SteeringYawController::ComputeGyroTurn(float yaw_rate_target,
                                                           float gyro_z,
                                                           port::BEVControllerMemory& memory) {
    const float measurement = gyro_z;
    const float error = yaw_rate_target - measurement;
    memory.gyro_i_accumulator = std::clamp(memory.gyro_i_accumulator + error, -1200.0F, 1200.0F);
    const float p_term = gyro_p_ * error;
    const float d_term = gyro_d_ * (error - memory.gyro_error_last);
    const float output = p_term + gyro_i_ * memory.gyro_i_accumulator + d_term;
    memory.gyro_error_last = error;

    GyroTurnComputation computation{};
    computation.gyro_z = measurement;
    computation.gyro_error = error;
    computation.gyro_p_term = p_term;
    computation.gyro_d_term = d_term;
    computation.raw_turn_output = std::clamp(output, -9000.0F, 9000.0F);
    return computation;
}

}  // namespace ls2k::legacy
