#include "runtime/detail/steering_circle_v2_internal.hpp"

#include <vector>

#include "legacy/steering_single_boundary_offset.hpp"

namespace ls2k::runtime::detail {
namespace {

constexpr std::size_t kMinCircleV2ReferenceSamples = 3U;

float InnerTraceOffset(CircleDir dir, float offset_m) {
    if (dir == CircleDir::kLeft) {
        return offset_m;
    }
    if (dir == CircleDir::kRight) {
        return -offset_m;
    }
    return 0.0F;
}

std::optional<port::BEVReferencePath> ComposeOffsetPath(
    const port::BEVReferencePath& edge_path,
    float signed_offset_m) {
    std::vector<port::BEVPoint> boundary_trace;
    std::vector<float> target_forward_samples;
    boundary_trace.reserve(edge_path.sampled_path.size());
    target_forward_samples.reserve(edge_path.sampled_path.size());

    for (const port::BEVPathSample& sample : edge_path.sampled_path) {
        if (!sample.present) {
            break;
        }
        boundary_trace.push_back(sample.point);
        target_forward_samples.push_back(sample.point.forward_m);
    }

    const std::vector<port::BEVPoint> offset_points =
        legacy::BuildSingleBoundaryOffsetReference(boundary_trace,
                                                   target_forward_samples,
                                                   signed_offset_m);
    if (offset_points.size() < kMinCircleV2ReferenceSamples) {
        return std::nullopt;
    }

    port::BEVReferencePath reference_path{};
    reference_path.mode = edge_path.mode;
    for (std::size_t index = 0; index < offset_points.size() &&
                                index < reference_path.sampled_path.size();
         ++index) {
        port::BEVPathSample& sample = reference_path.sampled_path[index];
        sample.present = true;
        sample.point = offset_points[index];
        sample.confidence = edge_path.sampled_path[index].confidence > 0.0F
                                ? edge_path.sampled_path[index].confidence
                                : 0.8F;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
    }
    return reference_path;
}

}  // namespace

std::optional<CircleV2ReferencePlan> ComposeCircleV2Reference(
    const CircleV2ReferenceContext& reference,
    const CircleV2Geometry& geometry,
    const CircleV2Params& params) {
    if ((reference.role != CircleV2ReferenceRole::kInnerTrace &&
         reference.role != CircleV2ReferenceRole::kExitTrace) ||
        reference.dir == CircleDir::kNone ||
        !geometry.available) {
        return std::nullopt;
    }
    if (reference.role == CircleV2ReferenceRole::kExitTrace &&
        geometry.road_half_width_m <= 0.0F) {
        return std::nullopt;
    }

    CircleV2ReferencePlan plan{};
    plan.dir = reference.dir;
    plan.role = reference.role;
    if (reference.role == CircleV2ReferenceRole::kInnerTrace) {
        const float offset_m =
            InnerTraceOffset(reference.dir, params.inner_trace_path_offset_m);
        const std::optional<port::BEVReferencePath> reference_path =
            ComposeOffsetPath(geometry.edge_path, offset_m);
        if (!reference_path.has_value()) {
            return std::nullopt;
        }
        plan.reference_path = *reference_path;
    } else if (reference.role == CircleV2ReferenceRole::kExitTrace) {
        const std::optional<port::BEVReferencePath> reference_path =
            ComposeOffsetPath(geometry.edge_path, geometry.reference_offset_m);
        if (!reference_path.has_value()) {
            return std::nullopt;
        }
        plan.reference_path = *reference_path;
    }
    return plan;
}

}  // namespace ls2k::runtime::detail
