#include "runtime/detail/steering_circle_v2_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ls2k::runtime::detail {
namespace {

constexpr std::size_t kMinCircleV2LeadingSamples = 3U;
constexpr float kExitTraceMaxLateralSpanM = 0.12F;

bool FindOuterRowEdge(const legacy::BEVSimpleRowScan& row,
                      float center_lateral_m,
                      CircleDir side,
                      float& edge_lateral_m) {
    float best_distance = std::numeric_limits<float>::infinity();
    bool found = false;
    for (const legacy::BEVSimpleWhiteInterval& interval : row.intervals) {
        float near_edge = 0.0F;
        float outer_edge = 0.0F;
        bool on_requested_side = false;
        if (side == CircleDir::kLeft) {
            near_edge = interval.right_m;
            outer_edge = interval.left_m;
            on_requested_side = near_edge <= center_lateral_m;
        } else if (side == CircleDir::kRight) {
            near_edge = interval.left_m;
            outer_edge = interval.right_m;
            on_requested_side = near_edge >= center_lateral_m;
        }
        if (!on_requested_side) {
            continue;
        }
        const float distance = std::fabs(near_edge - center_lateral_m);
        if (distance < best_distance) {
            best_distance = distance;
            edge_lateral_m = outer_edge;
            found = true;
        }
    }
    return found;
}

CircleDir Opposite(CircleDir dir) {
    if (dir == CircleDir::kLeft) {
        return CircleDir::kRight;
    }
    if (dir == CircleDir::kRight) {
        return CircleDir::kLeft;
    }
    return CircleDir::kNone;
}

const legacy::BEVSimpleRowScan* ConsumeNextRow(const SceneFrameView& frame,
                                               float forward_m,
                                               std::size_t& row_index) {
    while (row_index < frame.rows.rows.size()) {
        const legacy::BEVSimpleRowScan& row = frame.rows.rows[row_index];
        ++row_index;
        if (!row.valid || row.intervals.empty() || row.forward_m < forward_m) {
            continue;
        }
        return &row;
    }
    return nullptr;
}

CircleDir ExitEdgeSideForRole(const CircleV2ReferenceContext& reference) {
    if (reference.role == CircleV2ReferenceRole::kExitTrace) {
        return Opposite(reference.dir);
    }
    return CircleDir::kNone;
}

CircleDir InnerEdgeSideForRole(const CircleV2ReferenceContext& reference) {
    if (reference.role == CircleV2ReferenceRole::kInnerTrace) {
        return reference.dir;
    }
    return CircleDir::kNone;
}

bool IsFiniteSample(const port::BEVPathSample& sample) {
    return sample.present &&
           std::isfinite(sample.point.forward_m) &&
           std::isfinite(sample.point.lateral_m);
}

bool HasMinimumLeadingSegment(std::size_t leading_count) {
    return leading_count >= kMinCircleV2LeadingSamples;
}

bool IsLeadingSegmentStraightEnough(const port::BEVReferencePath& edge_path,
                                    std::size_t leading_count) {
    if (!HasMinimumLeadingSegment(leading_count)) {
        return false;
    }
    float min_lateral = edge_path.sampled_path[0].point.lateral_m;
    float max_lateral = edge_path.sampled_path[0].point.lateral_m;
    for (std::size_t index = 0; index < leading_count; ++index) {
        const port::BEVPathSample& sample = edge_path.sampled_path[index];
        if (!IsFiniteSample(sample)) {
            return false;
        }
        min_lateral = std::min(min_lateral, sample.point.lateral_m);
        max_lateral = std::max(max_lateral, sample.point.lateral_m);
    }
    return max_lateral - min_lateral <= kExitTraceMaxLateralSpanM;
}

bool FindInnerRowEdge(const legacy::BEVSimpleRowScan& row,
                      float center_lateral_m,
                      CircleDir side,
                      float& edge_lateral_m) {
    bool found = false;
    float best_distance = std::numeric_limits<float>::infinity();
    for (const legacy::BEVSimpleWhiteInterval& interval : row.intervals) {
        const float candidates[2] = {interval.left_m, interval.right_m};
        for (float candidate : candidates) {
            const bool on_requested_side =
                (side == CircleDir::kLeft && candidate <= center_lateral_m) ||
                (side == CircleDir::kRight && candidate >= center_lateral_m);
            if (!on_requested_side || !std::isfinite(candidate)) {
                continue;
            }
            const float distance = std::fabs(candidate - center_lateral_m);
            if (!found || distance < best_distance) {
                best_distance = distance;
                edge_lateral_m = candidate;
                found = true;
            }
        }
    }
    return found;
}

float ExitTraceOffset(CircleDir dir, float road_half_width_m) {
    if (dir == CircleDir::kLeft) {
        return -road_half_width_m;
    }
    if (dir == CircleDir::kRight) {
        return road_half_width_m;
    }
    return 0.0F;
}

struct InnerTraceTargetSample {
    float forward_m = 0.0F;
    float center_lateral_m = 0.0F;
    float confidence = 0.8F;
};

std::vector<InnerTraceTargetSample> BuildInnerTraceTargets(
    const SceneFrameView& frame,
    const OrdinaryRoadModel* ordinary_road) {
    std::vector<InnerTraceTargetSample> targets;
    targets.reserve(port::kBevReferenceSampleCount);
    if (ordinary_road != nullptr) {
        for (const port::BEVPathSample& sample : ordinary_road->center_path.sampled_path) {
            if (!IsFiniteSample(sample)) {
                break;
            }
            targets.push_back(InnerTraceTargetSample{sample.point.forward_m,
                                                     sample.point.lateral_m,
                                                     sample.confidence});
        }
        return targets;
    }

    for (std::size_t index = 0; index < frame.rows.rows.size() &&
                                index < port::kBevReferenceSampleCount;
         ++index) {
        const legacy::BEVSimpleRowScan& row = frame.rows.rows[index];
        if (!row.valid || row.intervals.empty() || !std::isfinite(row.forward_m)) {
            break;
        }
        targets.push_back(InnerTraceTargetSample{row.forward_m, 0.0F, 0.8F});
    }
    return targets;
}

CircleV2Geometry ObserveInnerTraceGeometry(const SceneFrameView& frame,
                                           const CircleV2ReferenceContext& reference,
                                           const OrdinaryRoadModel* ordinary_road) {
    CircleV2Geometry geometry{};
    const CircleDir edge_side = InnerEdgeSideForRole(reference);
    if (edge_side == CircleDir::kNone) {
        return geometry;
    }

    port::BEVReferencePath edge_path{};
    edge_path.mode = port::ReferenceMode::kIntervalCenter;
    std::size_t leading_count = 0;
    std::size_t row_index = 0;
    const std::vector<InnerTraceTargetSample> targets =
        BuildInnerTraceTargets(frame, ordinary_road);
    for (std::size_t index = 0; index < targets.size() &&
                                index < edge_path.sampled_path.size();
         ++index) {
        const InnerTraceTargetSample& target = targets[index];
        const legacy::BEVSimpleRowScan* row =
            ConsumeNextRow(frame, target.forward_m, row_index);
        if (row == nullptr) {
            break;
        }
        float edge_lateral = 0.0F;
        const bool edge_found =
            FindInnerRowEdge(*row, target.center_lateral_m, edge_side, edge_lateral);
        if (!edge_found) {
            break;
        }
        port::BEVPathSample& edge_sample = edge_path.sampled_path[index];
        edge_sample.present = true;
        edge_sample.point.forward_m = target.forward_m;
        edge_sample.point.lateral_m = edge_lateral;
        edge_sample.confidence = target.confidence > 0.0F ? target.confidence : 0.8F;
        edge_sample.source = port::BEVPathPointSource::kIntervalCenter;
        ++leading_count;
    }

    geometry.available = HasMinimumLeadingSegment(leading_count);
    geometry.edge_path = edge_path;
    return geometry;
}

}  // namespace

CircleV2Geometry ObserveCircleV2Geometry(const SceneFrameView& frame,
                                         const CircleV2ReferenceContext& reference,
                                         const CircleSideExpansionObservation& expansion,
                                         const CircleV2Params& params) {
    (void)expansion;
    (void)params;
    CircleV2Geometry geometry{};
    const OrdinaryRoadModel* ordinary_road =
        frame.ordinary_road.has_value() ? &*frame.ordinary_road : nullptr;
    if (ordinary_road != nullptr) {
        geometry.road_half_width_m = ordinary_road->half_width.value_m;
    }

    if (reference.role == CircleV2ReferenceRole::kInnerTrace) {
        return ObserveInnerTraceGeometry(frame, reference, ordinary_road);
    }

    const CircleDir edge_side = ExitEdgeSideForRole(reference);
    if (ordinary_road == nullptr ||
        edge_side == CircleDir::kNone ||
        geometry.road_half_width_m <= 0.0F) {
        return geometry;
    }

    port::BEVReferencePath edge_path{};
    edge_path.mode = port::ReferenceMode::kIntervalCenter;
    std::size_t leading_count = 0;
    std::size_t row_index = 0;
    for (std::size_t index = 0; index < ordinary_road->center_path.sampled_path.size(); ++index) {
        const port::BEVPathSample& center_sample =
            ordinary_road->center_path.sampled_path[index];
        if (!IsFiniteSample(center_sample)) {
            break;
        }
        const legacy::BEVSimpleRowScan* row =
            ConsumeNextRow(frame, center_sample.point.forward_m, row_index);
        if (row == nullptr) {
            break;
        }
        float edge_lateral = 0.0F;
        const bool edge_found =
            FindOuterRowEdge(*row, center_sample.point.lateral_m, edge_side, edge_lateral);
        if (!edge_found) {
            break;
        }
        port::BEVPathSample& edge_sample = edge_path.sampled_path[index];
        edge_sample.present = true;
        edge_sample.point.forward_m = center_sample.point.forward_m;
        edge_sample.point.lateral_m = edge_lateral;
        edge_sample.confidence = center_sample.confidence > 0.0F
                                     ? center_sample.confidence
                                     : 0.8F;
        edge_sample.source = port::BEVPathPointSource::kIntervalCenter;
        ++leading_count;
    }

    geometry.available =
        IsLeadingSegmentStraightEnough(edge_path, leading_count);
    geometry.edge_path = edge_path;
    geometry.reference_offset_m = ExitTraceOffset(reference.dir, geometry.road_half_width_m);
    return geometry;
}

}  // namespace ls2k::runtime::detail
