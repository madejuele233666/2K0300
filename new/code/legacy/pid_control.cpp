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

void LegacyPidControl::Reset() {
    camera_error_last_ = 0.0F;
    gyro_error_last_ = 0.0F;
    gyro_i_count_ = 0.0F;
}

float LegacyPidControl::ComputeTurnTarget(const port::PerceptionResult& perception, float& w_target_last) {
    const float err = perception.lateral_error;
    const float proportional_gain = cam_use_fuzzy_
                                        ? cam_p_scale_ * fuzzy_.DuoJiGetP(
                                                             perception.highest_line,
                                                             static_cast<int>(std::round(err)))
                                        : cam_p_;
    const float p_data = proportional_gain * err;
    const float d_data = cam_d_ * (err - camera_error_last_);
    camera_error_last_ = err;
    const float candidate = p_data + d_data;

    // Retain the old W_Target_last smoothing pattern from old/user/isr.c.
    const float smoothed = 0.9F * candidate + 0.1F * w_target_last;
    w_target_last = smoothed;
    return smoothed;
}

float LegacyPidControl::ComputeGyroTurn(float w_target, float gyro_z) {
    const float error = w_target - gyro_z;
    gyro_i_count_ = std::clamp(gyro_i_count_ + error, -1200.0F, 1200.0F);
    const float output =
        gyro_p_ * error + gyro_i_ * gyro_i_count_ + gyro_d_ * (error - gyro_error_last_);
    gyro_error_last_ = error;
    return std::clamp(output, -9000.0F, 9000.0F);
}

}  // namespace ls2k::legacy
