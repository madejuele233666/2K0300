#include "legacy/steering_reference_control_readiness.hpp"

namespace ls2k::legacy {

port::ReferenceControlReadiness EvaluateReferenceControlReadiness(
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceCurvatureEstimate& curvature,
    bool hold_selected) {
    port::ReferenceControlReadiness readiness{};
    if (!selected_usability.usable) {
        readiness.reason = "reference_unusable";
        return readiness;
    }
    if (!curvature.computed) {
        readiness.reason = "curvature_uncomputed";
        return readiness;
    }

    readiness.ready = true;
    readiness.degraded = hold_selected;
    readiness.reason = hold_selected ? "reference_hold" : "ok";
    return readiness;
}

}  // namespace ls2k::legacy
