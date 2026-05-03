#ifndef LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP

#include "port/reference_control_readiness_types.hpp"
#include "port/reference_curvature_types.hpp"
#include "port/reference_usability_types.hpp"

namespace ls2k::legacy {

port::ReferenceControlReadiness EvaluateReferenceControlReadiness(
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceCurvatureEstimate& curvature,
    bool hold_selected);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_CONTROL_READINESS_HPP
