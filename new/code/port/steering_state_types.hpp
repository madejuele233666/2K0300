/**
 * @file steering_state_types.hpp
 * @brief 转向系统状态记忆类型定义
 *
 * 定义跨帧的状态记忆结构，用于在连续的控制周期之间传递历史信息。
 * 分为感知记忆（参考路径保持）和控制记忆（PID控制器状态）。
 */

#ifndef LS2K_PORT_STEERING_STATE_TYPES_HPP
#define LS2K_PORT_STEERING_STATE_TYPES_HPP

#include <cstdint>
#include <string>

#include "port/bev_reference_types.hpp"
#include "port/circle_v2_types.hpp"

namespace ls2k::port {

/**
 * @struct BEVControllerMemory
 * @brief BEV控制器跨帧记忆
 *
 * 保存上一帧的控制输出值、横向误差、角速率误差、积分累积值和增益缩放系数。
 * 用于PID控制器的连续计算和状态保持。
 */
struct BEVControllerMemory {
    float turn_output_target_last = 0.0F;   ///< 上一帧的转向输出目标值
    float weighted_lateral_error_last = 0.0F;  ///< 上一帧的加权横向误差
    float gyro_error_last = 0.0F;            ///< 上一帧的角速率误差
    float gyro_i_accumulator = 0.0F;         ///< 偏航角速率PID积分累积值
    float last_gain_scale = 1.0F;            ///< 上一帧的增益缩放系数
};

/**
 * @struct SteeringPerceptionMemory
 * @brief 转向感知系统跨帧记忆
 *
 * 携带参考路径的保持状态，使感知流水线能够在短暂丢失跟踪时维持上一帧的参考路径。
 */
struct SteeringPerceptionMemory {
    ReferenceHoldState reference_hold{};  ///< 参考路径保持状态
    CircleV2Memory circle_v2{};           ///< CircleV2 场景记忆
};

/**
 * @struct SteeringControlMemory
 * @brief 转向控制系统跨帧记忆
 *
 * 包含BEV控制器的跨帧状态记忆。
 */
struct SteeringControlMemory {
    BEVControllerMemory controller_memory{};  ///< BEV控制器记忆
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_STEERING_STATE_TYPES_HPP
