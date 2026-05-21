#include "legacy/steering_reference_control_readiness.hpp"

namespace ls2k::legacy {

/// EvaluateReferenceControlReadiness 实现
/// 只有当参考路径可用且横向误差已计算时，才认为控制就绪
/// 若处于保持模式，则标记为降级状态
port::ReferenceControlReadiness EvaluateReferenceControlReadiness(
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceLateralErrorEstimate& lateral_error,
    bool hold_selected) {
    port::ReferenceControlReadiness readiness{};
    if (!selected_usability.usable) {
        readiness.reason = "reference_unusable";
        return readiness;
    }
    if (!lateral_error.computed) {
        readiness.reason = "lateral_error_uncomputed";
        return readiness;
    }

    readiness.ready = true;
    readiness.degraded = hold_selected;
    readiness.reason = hold_selected ? "reference_hold" : "ok";
    return readiness;
}

}  // namespace ls2k::legacy
