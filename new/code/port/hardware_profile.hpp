/**
 * @file hardware_profile.hpp
 * @brief 硬件配置文件头
 *
 * 定义硬件子系统的工作模式配置结构。
 * 每个子系统（相机、IMU、编码器等）可通过配置选择直连模式、适配Hook模式或禁用。
 */

#ifndef LS2K_PORT_HARDWARE_PROFILE_HPP
#define LS2K_PORT_HARDWARE_PROFILE_HPP

#include <string>

namespace ls2k::port {

/**
 * @enum SubsystemMode
 * @brief 子系统工作模式
 *
 * kDirectMatch 为直接硬件访问，kAdaptationHook 为通过适配层回调访问，
 * kDisabled 表示该子系统不启用。
 */
enum class SubsystemMode {
    kDirectMatch,     ///< 直接匹配（本地硬件直连）
    kAdaptationHook,  ///< 适配Hook模式（通过适配层接口）
    kDisabled         ///< 禁用该子系统
};

/**
 * @struct SubsystemProfile
 * @brief 单个子系统的配置
 *
 * 包含工作模式和适配Hook路径。
 */
struct SubsystemProfile {
    SubsystemMode mode = SubsystemMode::kDisabled;  ///< 子系统工作模式
    std::string hook = "unconfigured";               ///< 适配Hook标识（非直连模式下使用）
};

/**
 * @struct HardwareProfile
 * @brief 完整硬件配置
 *
 * 包含全部硬件的子系统配置。
 */
struct HardwareProfile {
    SubsystemProfile camera{};       ///< 相机子系统配置
    SubsystemProfile imu{};          ///< IMU子系统配置
    SubsystemProfile encoder{};      ///< 编码器子系统配置
    SubsystemProfile actuator{};     ///< 执行器子系统配置
    SubsystemProfile timer{};        ///< 定时器子系统配置
    SubsystemProfile persistence{};  ///< 持久化存储子系统配置
    SubsystemProfile display{SubsystemMode::kDisabled, "phase1-deferred"};  ///< 显示子系统
};

/**
 * @brief 检查子系统是否启用
 * @param profile 子系统配置
 * @return true=已启用，false=已禁用
 */
inline bool IsEnabled(const SubsystemProfile& profile) {
    return profile.mode != SubsystemMode::kDisabled;
}

/**
 * @brief 子系统模式转字符串
 * @param mode 子系统模式枚举
 * @return 字符串表示
 */
const char* ToString(SubsystemMode mode);

}  // namespace ls2k::port

#endif  // LS2K_PORT_HARDWARE_PROFILE_HPP
