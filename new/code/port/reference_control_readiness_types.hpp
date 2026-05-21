/**
 * @file reference_control_readiness_types.hpp
 * @brief 参考路径控制就绪状态类型定义
 *
 * 定义基于参考路径的横向控制系统是否就绪的判定结果。
 * 是安全门（Safety Gate）的重要输入之一。
 */

#ifndef LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP
#define LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP

#include <string>

namespace ls2k::port {

/**
 * @struct ReferenceControlReadiness
 * @brief 参考路径控制就绪状态
 *
 * 综合参考路径可用性和横向误差估计后，判定是否能够进行基于参考路径的横向控制。
 * ready=true 表示可以启用横向控制，degraded 表示降级运行。
 */
struct ReferenceControlReadiness {
    bool ready = false;                        ///< 横向控制是否就绪
    bool degraded = false;                     ///< 是否处于降级模式
    std::string reason = "reference_unusable"; ///< 未就绪的原因描述
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_CONTROL_READINESS_TYPES_HPP
