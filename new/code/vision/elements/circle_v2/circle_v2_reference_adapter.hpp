#ifndef LS2K_RUNTIME_STEERING_CIRCLE_V2_REFERENCE_ADAPTER_HPP
#define LS2K_RUNTIME_STEERING_CIRCLE_V2_REFERENCE_ADAPTER_HPP

#include <optional>

#include "port/visual_reference_orchestration_types.hpp"
#include "vision/elements/circle_v2/circle_v2_scene.hpp"

namespace ls2k::vision {

std::optional<port::VisualReferenceCandidate> AdaptCircleV2ReferencePlan(
    const std::optional<CircleV2ReferencePlan>& plan);

}  // namespace ls2k::vision

#endif  // LS2K_RUNTIME_STEERING_CIRCLE_V2_REFERENCE_ADAPTER_HPP
