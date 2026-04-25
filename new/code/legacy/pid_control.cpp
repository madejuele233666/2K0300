#include "legacy/pid_control.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

void LegacyPidControl::Configure(const port::RuntimeParameters& params) {
    cam_p_ = static_cast<float>(params.pid_turn_camera_p);
    cam_p_scale_ = static_cast<float>(params.pid_turn_camera_p_scale);
    cam_d_ = static_cast<float>(params.pid_turn_camera_d);
    gyro_p_ = static_cast<float>(params.pid_turn_gyro_camera_p);
    gyro_i_ = static_cast<float>(params.pid_turn_gyro_camera_i);
    gyro_d_ = static_cast<float>(params.pid_turn_gyro_camera_d);
    cam_use_fuzzy_ = params.pid_turn_camera_use_fuzzy;
    fuzzy_.InitMH(params.P_Mode);
}

void LegacyPidControl::Reset() {}

CameraTurnComputation LegacyPidControl::ComputeTurnTarget(const port::PerceptionResult& perception,
                                                          port::LegacySteeringControllerMemory& memory) {
    const float err = perception.lateral_error;
    const float proportional_gain = cam_use_fuzzy_
                                        ? cam_p_scale_ * fuzzy_.DuoJiGetP(
                                                             perception.highest_line,
                                                             static_cast<int>(std::round(err)))
                                        : cam_p_;
    const float p_data = proportional_gain * err;
    const float d_data = cam_d_ * (err - memory.camera_error_last);
    memory.camera_error_last = err;
    const float candidate = p_data + d_data;

    // Retain the old W_Target_last smoothing pattern from old/user/isr.c.
    const float smoothed = 0.9F * candidate + 0.1F * memory.w_target_last;
    memory.w_target_last = smoothed;

    CameraTurnComputation computation{};
    computation.resolved_fuzzy_p = proportional_gain;
    computation.camera_p_term = p_data;
    computation.camera_d_term = d_data;
    computation.w_target = smoothed;
    return computation;
}

GyroTurnComputation LegacyPidControl::ComputeGyroTurn(float w_target,
                                                      float gyro_z,
                                                      port::LegacySteeringControllerMemory& memory) {
    const float error = w_target - gyro_z;
    memory.gyro_i_accumulator = std::clamp(memory.gyro_i_accumulator + error, -1200.0F, 1200.0F);
    const float p_term = gyro_p_ * error;
    const float d_term = gyro_d_ * (error - memory.gyro_error_last);
    const float output = p_term + gyro_i_ * memory.gyro_i_accumulator + d_term;
    memory.gyro_error_last = error;

    GyroTurnComputation computation{};
    computation.gyro_z = gyro_z;
    computation.gyro_error = error;
    computation.gyro_p_term = p_term;
    computation.gyro_d_term = d_term;
    computation.raw_turn_output = std::clamp(output, -9000.0F, 9000.0F);
    return computation;
}

}  // namespace ls2k::legacy
