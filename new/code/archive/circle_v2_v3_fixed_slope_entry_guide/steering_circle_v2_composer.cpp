#include "runtime/detail/steering_circle_v2_internal.hpp"

namespace ls2k::runtime::detail {
std::optional<CircleV2ReferencePlan> ComposeCircleV2Reference(
    const CircleV2ReferenceContext& reference,
    const CircleV2Geometry& geometry,
    const CircleV2Params&) {
    if (reference.role != CircleV2ReferenceRole::kExitTrace ||
        reference.dir == CircleDir::kNone ||
        !geometry.available ||
        geometry.road_half_width_m <= 0.0F) {
        return std::nullopt;
    }

    CircleV2ReferencePlan plan{};
    plan.dir = reference.dir;
    plan.role = reference.role;
    plan.reference_path = geometry.edge_path;
    for (port::BEVPathSample& sample : plan.reference_path.sampled_path) {
        if (!sample.present) {
            continue;
        }
        sample.point.lateral_m += geometry.reference_offset_m;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
    }
    return plan;
}

std::optional<CircleV2BoundaryOverridePlan> ComposeCircleV2BoundaryOverride(
    const CircleV2ReferenceContext& reference,
    const CircleV2Geometry& geometry) {
    if (reference.role != CircleV2ReferenceRole::kInnerTrace ||
        reference.dir == CircleDir::kNone ||
        geometry.boundary_override_side == CircleDir::kNone ||
        !geometry.available) {
        return std::nullopt;
    }

    CircleV2BoundaryOverridePlan plan{};
    plan.dir = reference.dir;
    plan.role = reference.role;
    plan.override_side = geometry.boundary_override_side;
    plan.boundary_path = geometry.edge_path;
    return plan;
}

}  // namespace ls2k::runtime::detail
