#include "reference/reference_continuity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ls2k::reference {
namespace {

std::size_t ActiveSparseRowCount(const port::RuntimeParameters& params) {
    return static_cast<std::size_t>(
        std::clamp(params.bev_geometry.sparse_row_count,
                   1,
                   static_cast<int>(port::kBevReferenceSampleCount)));
}

bool SameForwardSamples(const std::array<float, port::kBevReferenceSampleCount>& lhs,
                        const std::array<float, port::kBevReferenceSampleCount>& rhs) {
    for (std::size_t index = 0; index < port::kBevReferenceSampleCount; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

port::ReferenceGeometryIdentity MakeReferenceGeometryIdentity(
    const port::RuntimeParameters& params) {
    port::ReferenceGeometryIdentity identity{};
    identity.initialized = true;
    identity.forward_samples_m = params.bev_geometry.forward_samples_m;
    identity.sparse_row_count = static_cast<int>(ActiveSparseRowCount(params));
    identity.search_lateral_limit_m = params.bev_geometry.search_lateral_limit_m;
    identity.lateral_step_m = params.bev_geometry.lateral_step_m;
    return identity;
}

bool SameReferenceGeometryIdentity(const port::ReferenceGeometryIdentity& lhs,
                                   const port::ReferenceGeometryIdentity& rhs) {
    return lhs.initialized && rhs.initialized &&
           lhs.sparse_row_count == rhs.sparse_row_count &&
           lhs.search_lateral_limit_m == rhs.search_lateral_limit_m &&
           lhs.lateral_step_m == rhs.lateral_step_m &&
           SameForwardSamples(lhs.forward_samples_m, rhs.forward_samples_m);
}

void InitializeReferencePath(port::BEVReferencePath& reference,
                             const port::RuntimeParameters& params,
                             port::ReferenceMode mode) {
    reference.mode = mode;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        port::BEVPathSample& sample = reference.sampled_path[index];
        sample.present = false;
        sample.point.forward_m = params.bev_geometry.forward_samples_m[index];
        sample.point.lateral_m = 0.0F;
        sample.confidence = 0.0F;
        sample.source = port::BEVPathPointSource::kNone;
    }
}

}  // namespace

port::ReferenceHoldState MakeReferenceHoldState(
    const port::BEVReferencePath& current_visual_reference,
    uint64_t reference_capture_time_ms,
    const port::RuntimeParameters& params) {
    port::ReferenceHoldState state{};
    state.hold_cycles = 0;
    state.last_reference = current_visual_reference.sampled_path;
    state.geometry_identity = MakeReferenceGeometryIdentity(params);
    state.reference_capture_time_ms = reference_capture_time_ms;
    return state;
}

port::ReferenceContinuityResult BuildReferenceHoldCandidate(
    const port::ReferenceHoldState& prior_hold,
    const port::RuntimeParameters& params) {
    port::ReferenceContinuityResult result{};
    result.next_hold_state = prior_hold;
    const port::ReferenceGeometryIdentity current_identity = MakeReferenceGeometryIdentity(params);
    const bool hold_allowed =
        prior_hold.hold_cycles < params.bev_classification.hold_last_max_cycles &&
        SameReferenceGeometryIdentity(prior_hold.geometry_identity, current_identity);
    if (!hold_allowed) {
        result.next_hold_state = {};
        return result;
    }

    InitializeReferencePath(result.reference_path, params, port::ReferenceMode::kHoldLast);
    std::size_t copied = 0;
    for (std::size_t index = 0; index < result.reference_path.sampled_path.size(); ++index) {
        port::BEVPathSample sample = prior_hold.last_reference[index];
        if (!sample.present ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            break;
        }
        sample.confidence *= 0.75F;
        sample.source = port::BEVPathPointSource::kHold;
        result.reference_path.sampled_path[index] = sample;
        ++copied;
    }
    if (copied == 0) {
        result.reference_path = {};
        result.next_hold_state = {};
        return result;
    }

    result.mode = port::ReferenceMode::kHoldLast;
    result.source = "hold";
    result.hold_selected = true;
    result.reference_capture_time_ms = prior_hold.reference_capture_time_ms;
    result.next_hold_state = prior_hold;
    result.next_hold_state.hold_cycles = prior_hold.hold_cycles + 1;
    return result;
}

}  // namespace ls2k::reference
