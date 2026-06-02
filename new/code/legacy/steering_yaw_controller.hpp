#ifndef LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
#define LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP

#include "port/reference_tracking_geometry_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::legacy {

/// 转向输出目标计算结果，包含跟踪几何三项、速度缩放和转向候选/目标值
struct TurnOutputTargetComputation {
    float lateral_offset_gain = 0.0F;      ///< 横向位置项增益
    float heading_error_gain = 0.0F;       ///< 航向误差项增益
    float curvature_gain = 0.0F;           ///< 曲率前馈项增益
    float speed_scale = 0.0F;              ///< 速度缩放因子
    float lateral_term = 0.0F;             ///< 横向位置项输出
    float heading_term = 0.0F;             ///< 航向误差项输出
    float curvature_term = 0.0F;           ///< 曲率前馈项输出
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

    /// 从参考跟踪几何计算转向输出目标
    /// @param tracking_geometry selected/aligned reference 的跟踪几何事实
    /// @param effective_speed_target 有效速度目标
    /// @param memory 控制器记忆状态（更新上一帧误差和增益）
    /// @return 转向输出目标计算结果
    TurnOutputTargetComputation ComputeTurnOutputTarget(const port::ReferenceTrackingGeometry& tracking_geometry,
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
    float raw_turn_output_limit_ = 20000.0F; ///< 转向目标限幅
    float lateral_offset_to_wheel_delta_gain_ = 180.0F; ///< 横向位置项到轮距增量的增益
    float heading_error_to_wheel_delta_gain_ = 0.0F;    ///< 航向误差项到轮距增量的增益
    float curvature_to_wheel_delta_gain_ = 0.0F;        ///< 曲率前馈项到轮距增量的增益
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_YAW_CONTROLLER_HPP
