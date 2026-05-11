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
    float cross_wide_row_white_ratio_min = 0.95F;
    bool circle_evidence_enabled = true;
    int circle_min_support_rows = 4;
    int circle_min_sampleable_per_row = 16;
    float circle_open_expansion_min_m = 0.05F;
    float circle_opening_expansion_ratio_min = 0.10F;
    float circle_opposite_straight_drift_max_m = 0.06F;
    float circle_opposite_shrink_ratio_min = 0.10F;
    float circle_present_confidence_min = 0.65F;
    bool circle_entry_takeover_enabled = false;
    int circle_entry_min_frontier_points = 4;
    float circle_entry_direction_min_lateral_m = 0.08F;
    float circle_entry_max_interpolation_gap_m = 0.12F;
    float circle_entry_max_join_jump_m = 0.12F;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP
