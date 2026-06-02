#include "reference/reference_control_readiness.hpp"

#include <cmath>

namespace ls2k::reference {
namespace {

bool TrackingGeometryFinite(const port::ReferenceTrackingGeometry& tracking_geometry) {
    return std::isfinite(tracking_geometry.lateral_offset_m) &&
           std::isfinite(tracking_geometry.heading_error_rad) &&
           std::isfinite(tracking_geometry.curvature_m_inv);
}

}  // namespace

/// EvaluateReferenceControlReadiness 实现
/// 只有当参考路径可用且跟踪几何已计算时，才认为控制就绪
/// 若处于保持模式，则标记为降级状态
port::ReferenceControlReadiness EvaluateReferenceControlReadiness(
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceTrackingGeometry& tracking_geometry,
    bool hold_selected) {
    port::ReferenceControlReadiness readiness{};
    if (!selected_usability.usable) {
        readiness.reason = "reference_unusable";
        return readiness;
    }
    if (!tracking_geometry.computed || !TrackingGeometryFinite(tracking_geometry)) {
        readiness.reason = "tracking_geometry_uncomputed";
        return readiness;
    }

    readiness.ready = true;
    readiness.degraded = hold_selected;
    readiness.reason = hold_selected ? "reference_hold" : "ok";
    return readiness;
}

}  // namespace ls2k::reference
