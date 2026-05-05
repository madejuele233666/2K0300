#ifndef LS2K_PORT_BEV_REFERENCE_TYPES_HPP
#define LS2K_PORT_BEV_REFERENCE_TYPES_HPP

#include <array>
#include <string>

#include "port/bev_geometry_types.hpp"

namespace ls2k::port {

enum class ReferenceMode {
    kNone,
    kIntervalCenter,
    kHoldLast
};

enum class BEVPathPointSource {
    kNone,
    kIntervalCenter,
    kHold
};

struct BEVPathSample {
    bool present = false;
    BEVPoint point{};
    float confidence = 0.0F;
    BEVPathPointSource source = BEVPathPointSource::kNone;
};

struct BEVReferencePath {
    ReferenceMode mode = ReferenceMode::kNone;
    std::array<BEVPathSample, kBevReferenceSampleCount> sampled_path{};
};

struct ReferenceGeometryIdentity {
    bool initialized = false;
    std::array<float, kBevReferenceSampleCount> forward_samples_m{};
    float search_lateral_limit_m = 0.0F;
    float lateral_step_m = 0.0F;
};

struct ReferenceHoldState {
    int hold_cycles = 0;
    std::array<BEVPathSample, kBevReferenceSampleCount> last_reference{};
    ReferenceGeometryIdentity geometry_identity{};
};

struct ReferenceContinuityResult {
    BEVReferencePath reference_path{};
    ReferenceMode mode = ReferenceMode::kNone;
    std::string source = "none";
    bool hold_selected = false;
    ReferenceHoldState next_hold_state{};
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_REFERENCE_TYPES_HPP
