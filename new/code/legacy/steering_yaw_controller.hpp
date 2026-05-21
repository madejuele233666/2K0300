#ifndef LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
#define LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP

#include "port/runtime_parameter_types.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::legacy {

/// 转向输出目标计算结果，包含横向误差增益、速度缩放和转向候选/目标值
struct TurnOutputTargetComputation {
    float lateral_error_gain = 0.0F;       ///< 横向误差增益
    float speed_scale = 0.0F;              ///< 速度缩放因子
    float turn_output_candidate = 0.0F;    ///< 转向输出候选值
    float turn_output_target = 0.0F;       ///< 最终转向输出目标值
};

/// 陀螺仪转向计算结果，包含角速度、误差、P/D项和原始输出
struct GyroTurnComputation {
    float gyro_z = 0.0F;          ///< 陀螺仪Z轴角速度测量值
    float gyro_error = 0.0F;      ///< 角速度误差（负的测量值）
    float gyro_p_term = 0.0F;     ///< 比例项
    float gyro_d_term = 0.0F;     ///< 微分项
    float raw_turn_output = 0.0F; ///< 原始转向输出（钳制后）
};

/// 旧版转向偏航控制器，基于横向误差计算转向输出，并用陀螺仪反馈进行补偿
class SteeringYawController {
public:
    /// 从运行时参数配置控制器参数
    void Configure(const port::RuntimeParameters& params);
    /// 重置控制器内部状态
    void Reset();

    /// 从加权横向误差计算转向输出目标
    /// @param weighted_lateral_error_m 加权横向误差（米）
    /// @param effective_speed_target 有效速度目标
    /// @param memory 控制器记忆状态（更新上一帧误差和增益）
    /// @return 转向输出目标计算结果
    TurnOutputTargetComputation ComputeTurnOutputTarget(float weighted_lateral_error_m,
                                                        double effective_speed_target,
                                                        port::BEVControllerMemory& memory);

    /// 使用陀螺仪反馈计算最终转向输出（带PID补偿）
    /// @param turn_output_target 转向输出目标值
    /// @param gyro_z 陀螺仪Z轴角速度
    /// @param memory 控制器记忆状态（更新I累加器和上一帧误差）
    /// @return 陀螺仪补偿后的转向计算结果
    GyroTurnComputation ComputeGyroTurn(float turn_output_target,
                                        float gyro_z,
                                        port::BEVControllerMemory& memory);

private:
    float gyro_p_ = 0.5F;               ///< 陀螺仪PID比例系数
    float gyro_i_ = 0.0F;               ///< 陀螺仪PID积分系数
    float gyro_d_ = 0.0F;               ///< 陀螺仪PID微分系数
    float running_speed_target_ = 100.0F;  ///< 运行速度目标值
    float lateral_error_to_wheel_delta_gain_ = 180.0F; ///< 横向误差到轮距增量的增益
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
