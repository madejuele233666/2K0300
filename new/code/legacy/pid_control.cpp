#include "legacy/pid_control.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

void LegacyPidControl::Configure(const port::RuntimeParameters& params) {
    cam_d_ = static_cast<float>(params.pid_turn_camera_d);
    gyro_d_ = static_cast<float>(params.pid_turn_gyro_camera_d);
    fuzzy_.InitMH(params.P_Mode);
}

float LegacyPidControl::ComputeTurnTarget(const port::PerceptionResult& perception, float& w_target_last) {
    const float err = perception.lateral_error;
    const float fuzzy_p = fuzzy_.DuoJiGetP(perception.highest_line, static_cast<int>(std::round(err)));
    const float p_data = fuzzy_p * err;
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

int LegacyPidControl::ComputeMeanSpeedPwm(double speed_target, double speed_measured, int pwm_limit) {
    const float error = static_cast<float>(speed_target - speed_measured);
    speed_i_count_ = std::clamp(speed_i_count_ + error, -2200.0F, 2200.0F);
    const float output =
        speed_p_ * error + speed_i_ * speed_i_count_ + speed_d_ * (error - speed_error_last_);
    speed_error_last_ = error;
    return static_cast<int>(std::round(std::clamp(output, -static_cast<float>(pwm_limit), static_cast<float>(pwm_limit))));
}

}  // namespace ls2k::legacy
