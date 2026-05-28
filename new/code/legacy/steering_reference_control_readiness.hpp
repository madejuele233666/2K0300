#ifndef LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP

#include "port/reference_control_readiness_types.hpp"
#include "port/reference_tracking_geometry_types.hpp"
#include "port/reference_usability_types.hpp"

namespace ls2k::legacy {

/// 评估参考路径控制就绪状态
/// 检查参考路径可用性和横向误差是否已计算，判断车辆是否准备好进行参考跟踪控制
/// @param selected_usability 选中的参考路径可用性信息
/// @param tracking_geometry 跟踪几何事实
/// @param hold_selected 是否选择了保持模式（保持最近的有效参考）
/// @return 控制就绪状态（ready/degraded及原因说明）
port::ReferenceControlReadiness EvaluateReferenceControlReadiness(
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceTrackingGeometry& tracking_geometry,
    bool hold_selected);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP
