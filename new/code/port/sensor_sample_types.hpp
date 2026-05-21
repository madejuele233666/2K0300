/**
 * @file sensor_sample_types.hpp
 * @brief 传感器样本类型定义
 *
 * 定义所有传感器采样数据的结构化类型，包含IMU（加速度计+陀螺仪）、
 * 编码器（左右轮增量）和低电压监测样本。
 * 每个样本类型均包含 valid 标记和时间戳。
 */

#ifndef LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP
#define LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP

#include <cstdint>
#include <string>

namespace ls2k::port {

/**
 * @struct ImuSample
 * @brief IMU传感器样本
 *
 * 包含三轴加速度（acc_x/y/z）和三轴角速度（gyro_x/y/z）。
 * 加速度单位通常为 m/s²，角速度单位为 rad/s。
 */
struct ImuSample {
    bool valid = false;           ///< 样本是否有效
    float acc_x = 0.0F;          ///< X轴加速度
    float acc_y = 0.0F;          ///< Y轴加速度
    float acc_z = 0.0F;          ///< Z轴加速度
    float gyro_x = 0.0F;         ///< X轴角速度（横滚角速率）
    float gyro_y = 0.0F;         ///< Y轴角速度（俯仰角速率）
    float gyro_z = 0.0F;         ///< Z轴角速度（偏航角速率）
    uint64_t capture_time_ms = 0;  ///< 采样时间戳（毫秒）
};

/**
 * @struct EncoderDelta
 * @brief 编码器增量样本
 *
 * 记录左右轮编码器在最近一个采样周期内的脉冲增量。
 */
struct EncoderDelta {
    bool valid = false;           ///< 样本是否有效
    int left = 0;                 ///< 左轮编码器增量
    int right = 0;                ///< 右轮编码器增量
    uint64_t capture_time_ms = 0;  ///< 采样时间戳（毫秒）
};

/**
 * @struct LowVoltageSample
 * @brief 低电压检测样本
 *
 * 包含电压原始值、阈值和紧急状态标记。
 * emergency=true 表示电压低于安全阈值，需触发保护动作。
 */
struct LowVoltageSample {
    bool valid = false;           ///< 样本是否有效
    bool emergency = true;        ///< 是否处于低电压紧急状态
    int raw_value = -1;           ///< 电压原始AD值
    int threshold = 0;            ///< 当前使用的低电压阈值
    uint64_t capture_time_ms = 0;  ///< 采样时间戳（毫秒）
    std::string source = "unavailable";  ///< 数据来源描述
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP
