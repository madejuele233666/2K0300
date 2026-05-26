#ifndef LS2K_RUNTIME_STEERING_BOUNDARY_OVERRIDE_REFERENCE_HPP
#define LS2K_RUNTIME_STEERING_BOUNDARY_OVERRIDE_REFERENCE_HPP

#include <optional>
#include <vector>

#include "legacy/steering_bev_simple_perception.hpp"
#include "port/bev_reference_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::runtime {

enum class BoundaryOverrideSide {
    kLeft,
    kRight,
};

struct BoundaryOverrideRequest {
    BoundaryOverrideSide side = BoundaryOverrideSide::kLeft;
    port::BEVReferencePath boundary_path{};
};

std::optional<port::BEVReferencePath> BuildReferencePathWithBoundaryOverride(
    const std::vector<legacy::BEVSimpleRowScan>& rows,
    const BoundaryOverrideRequest& request,
    const port::RuntimeParameters& params);

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_BOUNDARY_OVERRIDE_REFERENCE_HPP
