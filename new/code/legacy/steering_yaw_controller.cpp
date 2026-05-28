#include "legacy/steering_yaw_controller.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

/// SteeringYawController::Configure 实现
/// 从运行时参数读取PID系数、速度目标和增益值
void SteeringYawController::Configure(const port::RuntimeParameters& params) {
    gyro_p_ = static_cast<float>(params.yaw_rate_pid_p);
    gyro_i_ = static_cast<float>(params.yaw_rate_pid_i);
    gyro_d_ = static_cast<float>(params.yaw_rate_pid_d);
    running_speed_target_ = static_cast<float>(std::max(1.0, params.running_speed_target));
    raw_turn_output_limit_ = static_cast<float>(std::max(0, params.raw_turn_output_limit));
    lateral_offset_to_wheel_delta_gain_ =
        static_cast<float>(params.bev_control_model.lateral_offset_to_wheel_delta_gain);
    heading_error_to_wheel_delta_gain_ =
        static_cast<float>(params.bev_control_model.heading_error_to_wheel_delta_gain);
    curvature_to_wheel_delta_gain_ =
        static_cast<float>(params.bev_control_model.curvature_to_wheel_delta_gain);
}

/// SteeringYawController::Reset 实现
/// 重置控制器状态（当前为空操作）
void SteeringYawController::Reset() {}

/// SteeringYawController::ComputeTurnOutputTarget 实现
/// 根据参考跟踪几何和速度目标计算转向输出目标
TurnOutputTargetComputation SteeringYawController::ComputeTurnOutputTarget(
    const port::ReferenceTrackingGeometry& tracking_geometry,
    double effective_speed_target,
    port::BEVControllerMemory& memory) {
    const float speed_scale =
        static_cast<float>(effective_speed_target) / std::max(running_speed_target_, 1.0F);
    const float lateral_term =
        lateral_offset_to_wheel_delta_gain_ * speed_scale * tracking_geometry.lateral_offset_m;
    const float heading_term =
        heading_error_to_wheel_delta_gain_ * speed_scale * tracking_geometry.heading_error_rad;
    const float curvature_term =
        curvature_to_wheel_delta_gain_ * speed_scale *
        tracking_geometry.curvature_m_inv;
    const float turn_output_candidate =
        lateral_term + heading_term + curvature_term;
    const float turn_output_target =
        std::clamp(turn_output_candidate, -raw_turn_output_limit_, raw_turn_output_limit_);
    memory.weighted_lateral_error_last = tracking_geometry.lateral_offset_m;
    memory.last_gain_scale = speed_scale;
    memory.turn_output_target_last = turn_output_target;

    TurnOutputTargetComputation computation{};
    computation.lateral_offset_gain = lateral_offset_to_wheel_delta_gain_;
    computation.heading_error_gain = heading_error_to_wheel_delta_gain_;
    computation.curvature_gain = curvature_to_wheel_delta_gain_;
    computation.speed_scale = speed_scale;
    computation.lateral_term = lateral_term;
    computation.heading_term = heading_term;
    computation.curvature_term = curvature_term;
    computation.turn_output_candidate = turn_output_candidate;
    computation.turn_output_target = turn_output_target;
    return computation;
}

/// SteeringYawController::ComputeGyroTurn 实现
/// 使用陀螺仪角速度反馈对转向输出进行PID补偿
/// 输出 = 转向目标 + P项 + I项 + D项
/// 积分项累加器被限制在[-1200, 1200]范围内
GyroTurnComputation SteeringYawController::ComputeGyroTurn(float turn_output_target,
                                                           float gyro_z,
                                                           port::BEVControllerMemory& memory) {
    const float measurement = gyro_z;
    const float error = -measurement;
    memory.gyro_i_accumulator = std::clamp(memory.gyro_i_accumulator + error, -1200.0F, 1200.0F);
    const float p_term = gyro_p_ * error;
    const float d_term = gyro_d_ * (error - memory.gyro_error_last);
    const float output = turn_output_target + p_term + gyro_i_ * memory.gyro_i_accumulator + d_term;
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
