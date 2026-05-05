#ifndef LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP
#define LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ls2k::port {

struct VisualElementCandidateSummary {
    bool built = false;
    bool takeover_enabled = false;
    bool included_in_arbitration = false;
    std::string reason = "not_built";
};

struct CrossExitElementEvidence {
    bool present = false;
    float confidence = 0.0F;
    float forward_min_m = 0.0F;
    float forward_max_m = 0.0F;
    float lateral_min_m = 0.0F;
    float lateral_max_m = 0.0F;
    std::size_t sampleable_count = 0;
    std::size_t supporting_white_count = 0;
    std::size_t unknown_count = 0;
    std::string reason = "not_evaluated";
    VisualElementCandidateSummary candidate{};
};

struct VisualElementEvidenceBounds {
    float forward_min_m = 0.0F;
    float forward_max_m = 0.0F;
    float lateral_min_m = 0.0F;
    float lateral_max_m = 0.0F;
};

struct VisualElementEvidenceSupport {
    std::size_t sampleable_count = 0;
    std::size_t supporting_white_count = 0;
    std::size_t supporting_black_count = 0;
    std::size_t unknown_count = 0;
};

struct VisualElementEvidenceRecord {
    std::string id{};
    bool present = false;
    float confidence = 0.0F;
    std::string reason = "not_evaluated";
    VisualElementEvidenceBounds bounds{};
    VisualElementEvidenceSupport support{};
    VisualElementCandidateSummary candidate{};
};

struct VisualElementEvidenceFrame {
    CrossExitElementEvidence cross_exit{};
    std::vector<VisualElementEvidenceRecord> records{};
};

struct BEVElementParameters {
    bool cross_exit_takeover_enabled = false;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP
