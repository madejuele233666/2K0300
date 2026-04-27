#ifndef LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

struct ReferencePolicyResult {
    port::ReferencePolicyState state{};
    port::BEVReferencePath reference_path{};
    std::string reference_mode = "centerline";
};

ReferencePolicyResult ResolveReferencePolicy(const port::BEVTrackEstimate& track,
                                             const port::BEVSceneObservation& observation,
                                             const port::SpecialSceneFsmState& scene_state,
                                             const port::ReferencePolicyState& prior_state,
                                             const port::RuntimeParameters& params);
const char* ToString(port::ReferenceMode mode);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_POLICY_HPP
