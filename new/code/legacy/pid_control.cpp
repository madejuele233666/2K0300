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
    speed_base_ = static_cast<float>(std::max(1.0, params.Speed_base));
    cam_use_fuzzy_ = params.pid_turn_camera_use_fuzzy;
    curvature_to_w_target_gain_ = static_cast<float>(params.bev_control_model.curvature_to_w_target_gain);
    fuzzy_.InitMH(params.P_Mode);
}

void LegacyPidControl::Reset() {}

CameraTurnComputation LegacyPidControl::ComputeTurnTarget(const port::PerceptionResult& perception,
                                                          double effective_speed_target,
                                                          port::LegacySteeringControllerMemory& memory) {
    const port::ControlErrorModelOutput& control_model = perception.control_model;
    const float curvature_command = control_model.valid ? control_model.curvature_command : 0.0F;
    const float gain_scale = control_model.valid ? static_cast<float>(control_model.steering_gain_scale) : 1.0F;
    (void)cam_use_fuzzy_;
    (void)cam_p_scale_;
    (void)cam_p_;
    const float speed_scale =
        std::clamp(static_cast<float>(effective_speed_target) / std::max(speed_base_, 1.0F), 0.6F, 1.2F);
    const float candidate_unsuppressed =
        curvature_to_w_target_gain_ * speed_scale * gain_scale * curvature_command;
    float candidate = candidate_unsuppressed;
    if (control_model.steering_suppressed) {
        candidate = 0.0F;
    }
    if (perception.imu_grace_active) {
        candidate = memory.w_target_last + 0.6F * (candidate - memory.w_target_last);
    }
    memory.camera_error_last = curvature_command;
    memory.last_gain_scale = gain_scale;

    // Retain the old W_Target_last smoothing pattern from old/user/isr.c.
    const float smoothed = 0.9F * candidate + 0.1F * memory.w_target_last;
    memory.w_target_last = smoothed;

    CameraTurnComputation computation{};
    computation.resolved_fuzzy_p = curvature_to_w_target_gain_ * gain_scale;
    computation.camera_p_term = candidate_unsuppressed;
    computation.camera_d_term = 0.0F;
    computation.w_target = smoothed;
    return computation;
}

GyroTurnComputation LegacyPidControl::ComputeGyroTurn(float w_target,
                                                      float gyro_z,
                                                      bool imu_valid,
                                                      port::LegacySteeringControllerMemory& memory) {
    const float measurement = imu_valid ? gyro_z : 0.0F;
    const float error = w_target - measurement;
    if (imu_valid) {
        memory.gyro_i_accumulator = std::clamp(memory.gyro_i_accumulator + error, -1200.0F, 1200.0F);
    }
    const float p_term = gyro_p_ * error;
    const float d_term = imu_valid ? gyro_d_ * (error - memory.gyro_error_last) : 0.0F;
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
