#ifndef LS2K_LEGACY_STEERING_REFERENCE_CONNECTIVITY_HPP
#define LS2K_LEGACY_STEERING_REFERENCE_CONNECTIVITY_HPP

#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/bev_reference_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

struct ReferenceConnectivityFrameView {
    const port::LegacyCameraFrameView& gray_frame;
    const BEVProjector& projector;
    int threshold = 0;
    const port::BEVClassificationParameters& classification;
};

bool ReferencePathHasNoBlackSegments(const ReferenceConnectivityFrameView& frame,
                                     const port::BEVReferencePath& path);

void AppendConnectedVisualReferenceCandidate(
    const ReferenceConnectivityFrameView& frame,
    const port::VisualReferenceCandidate& candidate,
    std::vector<port::VisualReferenceCandidate>& accepted_candidates);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_REFERENCE_CONNECTIVITY_HPP
