#ifndef LS2K_PORT_PERCEPTION_RESULT_HPP
#define LS2K_PORT_PERCEPTION_RESULT_HPP

#include <cstdint>
#include <string>

#include "port/reference_control_readiness_types.hpp"
#include "port/reference_lateral_error_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::port {

struct PerceptionHealth {
    bool projector_ok = false;
    std::string reason = "projector_invalid";
};

// Runtime steering pipeline snapshot.
// This is a transport aggregate only. It does not own layer decisions.
struct PerceptionResult {
    bool published = false;
    bool fresh = false;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
    uint64_t publish_time_ms = 0;

    int threshold = 0;
    std::string perception_tag = "none";

    std::string reference_source = "none";
    std::string reference_mode = "none";

    PerceptionHealth perception_health{};
    VisualElementEvidenceFrame element_evidence{};
    VisualReferenceSelection visual_reference_selection{};
    ReferenceUsability reference_usability{};
    ReferenceLateralErrorEstimate reference_lateral_error{};
    ReferenceControlReadiness reference_control{};
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_PERCEPTION_RESULT_HPP
