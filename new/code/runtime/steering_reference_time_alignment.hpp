#ifndef LS2K_RUNTIME_STEERING_REFERENCE_TIME_ALIGNMENT_HPP
#define LS2K_RUNTIME_STEERING_REFERENCE_TIME_ALIGNMENT_HPP

#include "port/bev_reference_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

struct ReferenceTimeAlignmentResult {
    port::BEVReferencePath reference_path{};
    port::ReferenceTimeAlignmentFacts facts{};
};

/// Align a capture-time reference path into the control-time vehicle frame.
ReferenceTimeAlignmentResult AlignReferencePathToControlTime(
    const port::BEVReferencePath& reference_path,
    uint64_t reference_capture_time_ms,
    uint64_t control_time_ms,
    const MotionHistory& motion_history,
    const port::RuntimeParameters& params);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_REFERENCE_TIME_ALIGNMENT_HPP
