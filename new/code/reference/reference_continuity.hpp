#ifndef LS2K_REFERENCE_REFERENCE_CONTINUITY_HPP
#define LS2K_REFERENCE_REFERENCE_CONTINUITY_HPP

#include <cstdint>

#include "port/bev_reference_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::reference {

port::ReferenceHoldState MakeReferenceHoldState(
    const port::BEVReferencePath& current_visual_reference,
    uint64_t reference_capture_time_ms,
    const port::RuntimeParameters& params);

inline port::ReferenceHoldState MakeReferenceHoldState(
    const port::BEVReferencePath& current_visual_reference,
    const port::RuntimeParameters& params) {
    return MakeReferenceHoldState(current_visual_reference, 0, params);
}

port::ReferenceContinuityResult BuildReferenceHoldCandidate(
    const port::ReferenceHoldState& prior_hold,
    const port::RuntimeParameters& params);

}  // namespace ls2k::reference

#endif  // LS2K_REFERENCE_REFERENCE_CONTINUITY_HPP
