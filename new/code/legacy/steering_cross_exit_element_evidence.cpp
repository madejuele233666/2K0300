#include "legacy/steering_cross_exit_element_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ls2k::legacy {
namespace {

constexpr std::size_t kCrossMinContiguousWideRows = 3U;
constexpr std::size_t kCrossMinSampleablePerRow = 8U;
constexpr float kCrossMinWideWidthM = 0.52F;
constexpr float kCrossMinSampleableWidthRatio = 0.65F;
constexpr float kCrossMinBilateralReachM = 0.35F;
constexpr float kCrossMinBilateralBalance = 0.50F;
constexpr float kCrossUnknownRatioMax = 0.25F;
constexpr float kCrossPresentConfidenceMin = 0.70F;
constexpr float kRatioDenominatorFloor = 1.0e-4F;
constexpr std::size_t kOpeningSustainRows = 2U;

float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

struct CrossRunAccumulator {
    std::size_t row_count = 0;
    float score_sum = 0.0F;
    float forward_min_m = 0.0F;
    float forward_max_m = 0.0F;
    float lateral_min_m = 0.0F;
    float lateral_max_m = 0.0F;
    std::size_t sampleable_count = 0;
    std::size_t supporting_white_count = 0;
    std::size_t unknown_count = 0;
};

struct BoundaryOpeningFacts {
    bool left_open = false;
    bool right_open = false;
};

struct CrossBoundaryObservation {
    float forward_m = 0.0F;
    float left_reach_m = 0.0F;
    float right_reach_m = 0.0F;
};

const BEVSimpleWhiteInterval* WidestInterval(const BEVSimpleRowScan& row) {
    const BEVSimpleWhiteInterval* best = nullptr;
    for (const BEVSimpleWhiteInterval& interval : row.intervals) {
        if (best == nullptr || interval.width_m > best->width_m) {
            best = &interval;
        }
    }
    return best;
}

void AddWideRow(CrossRunAccumulator& run,
                const BEVSimpleRowScan& row,
                const BEVSimpleWhiteInterval& interval,
                float score) {
    if (run.row_count == 0U) {
        run.forward_min_m = row.forward_m;
        run.forward_max_m = row.forward_m;
        run.lateral_min_m = interval.left_m;
        run.lateral_max_m = interval.right_m;
    } else {
        run.forward_min_m = std::min(run.forward_min_m, row.forward_m);
        run.forward_max_m = std::max(run.forward_max_m, row.forward_m);
        run.lateral_min_m = std::min(run.lateral_min_m, interval.left_m);
        run.lateral_max_m = std::max(run.lateral_max_m, interval.right_m);
    }
    ++run.row_count;
    run.score_sum += score;
    run.sampleable_count += row.sampleable_count;
    run.supporting_white_count += row.white_count;
    run.unknown_count += row.unknown_count;
}

bool BetterRun(const CrossRunAccumulator& candidate, const CrossRunAccumulator& best) {
    if (candidate.row_count != best.row_count) {
        return candidate.row_count > best.row_count;
    }
    return candidate.score_sum > best.score_sum;
}

float GrowthRatio(float near_reach, float far_reach) {
    return (far_reach - near_reach) / std::max(kRatioDenominatorFloor, near_reach);
}

float Reach(const CrossBoundaryObservation& observation, bool use_left) {
    return use_left ? observation.left_reach_m : observation.right_reach_m;
}

float SustainedGrowthRatio(const std::vector<CrossBoundaryObservation>& observations,
                           bool use_left) {
    if (observations.size() <= kOpeningSustainRows) {
        return 0.0F;
    }
    float best = 0.0F;
    for (std::size_t split = 1U;
         split + kOpeningSustainRows <= observations.size();
         ++split) {
        float sustained_reach = Reach(observations[split], use_left);
        for (std::size_t offset = 1U; offset < kOpeningSustainRows; ++offset) {
            sustained_reach =
                std::min(sustained_reach, Reach(observations[split + offset], use_left));
        }
        best = std::max(best,
                        GrowthRatio(Reach(observations[split - 1U], use_left),
                                    sustained_reach));
    }
    return best;
}

BoundaryOpeningFacts AssessCrossOpenings(const std::vector<BEVSimpleRowScan>& rows,
                                         const port::RuntimeParameters& params) {
    std::vector<CrossBoundaryObservation> observations;
    for (const BEVSimpleRowScan& row : rows) {
        if (!row.valid || row.sampleable_count < kCrossMinSampleablePerRow) {
            continue;
        }
        const BEVSimpleWhiteInterval* interval = WidestInterval(row);
        if (interval == nullptr) {
            continue;
        }
        CrossBoundaryObservation observation{};
        observation.forward_m = row.forward_m;
        observation.left_reach_m = std::max(0.0F, -interval->left_m);
        observation.right_reach_m = std::max(0.0F, interval->right_m);
        observations.push_back(observation);
    }
    std::sort(observations.begin(),
              observations.end(),
              [](const CrossBoundaryObservation& lhs, const CrossBoundaryObservation& rhs) {
                  return lhs.forward_m < rhs.forward_m;
              });
    if (observations.size() < kCrossMinContiguousWideRows) {
        return {};
    }

    const float opening_ratio_min =
        std::max(kRatioDenominatorFloor, params.bev_element.circle_opening_expansion_ratio_min);

    BoundaryOpeningFacts facts{};
    facts.left_open = SustainedGrowthRatio(observations, true) >= opening_ratio_min;
    facts.right_open = SustainedGrowthRatio(observations, false) >= opening_ratio_min;
    return facts;
}

bool HasLeadingVisualReference(const port::BEVReferencePath& reference) {
    if (reference.mode != port::ReferenceMode::kIntervalCenter ||
        !reference.sampled_path[0].present) {
        return false;
    }
    bool gap_seen = false;
    for (const port::BEVPathSample& sample : reference.sampled_path) {
        if (!sample.present) {
            gap_seen = true;
            continue;
        }
        if (gap_seen ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            return false;
        }
    }
    return true;
}

}  // namespace

port::CrossExitElementEvidence DetectCrossExitEvidence(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params) {
    port::CrossExitElementEvidence evidence{};
    evidence.reason = "no_sparse_rows";
    if (rows.empty()) {
        return evidence;
    }

    bool saw_supported_row = false;
    bool saw_wide_row = false;
    CrossRunAccumulator current{};
    CrossRunAccumulator best{};
    const float expected_sampleable_width =
        std::max(0.0F, 2.0F * params.bev_geometry.search_lateral_limit_m);
    const BoundaryOpeningFacts opening = AssessCrossOpenings(rows, params);
    const float white_ratio_min =
        std::clamp(params.bev_element.cross_wide_row_white_ratio_min, 0.0F, 1.0F);

    const auto finish_run = [&current, &best]() {
        if (current.row_count > 0U && BetterRun(current, best)) {
            best = current;
        }
        current = {};
    };

    for (const BEVSimpleRowScan& row : rows) {
        evidence.sampleable_count += row.sampleable_count;
        evidence.supporting_white_count += row.white_count;
        evidence.unknown_count += row.unknown_count;

        const bool supported = row.valid && row.sampleable_count >= kCrossMinSampleablePerRow;
        saw_supported_row = saw_supported_row || supported;
        if (!supported) {
            finish_run();
            continue;
        }

        const float unknown_ratio =
            static_cast<float>(row.unknown_count) /
            std::max(1.0F, static_cast<float>(row.sampleable_count));
        const float white_ratio =
            static_cast<float>(row.white_count) /
            std::max(1.0F, static_cast<float>(row.sampleable_count));
        const BEVSimpleWhiteInterval* interval = WidestInterval(row);
        if (interval == nullptr || unknown_ratio > kCrossUnknownRatioMax) {
            finish_run();
            continue;
        }
        if (white_ratio < white_ratio_min) {
            finish_run();
            continue;
        }

        const float row_sampleable_width =
            row.sampleable_width_m > 0.0F ? row.sampleable_width_m : expected_sampleable_width;
        const float width_threshold =
            std::max(kCrossMinWideWidthM, row_sampleable_width * kCrossMinSampleableWidthRatio);
        if (interval->width_m < width_threshold) {
            finish_run();
            continue;
        }

        const float left_reach_m = std::max(0.0F, -interval->left_m);
        const float right_reach_m = std::max(0.0F, interval->right_m);
        const float bilateral_balance =
            std::min(left_reach_m, right_reach_m) /
            std::max(1.0e-4F, std::max(left_reach_m, right_reach_m));
        if (left_reach_m < kCrossMinBilateralReachM ||
            right_reach_m < kCrossMinBilateralReachM ||
            bilateral_balance < kCrossMinBilateralBalance) {
            finish_run();
            continue;
        }

        saw_wide_row = true;
        const float width_score = Clamp01(interval->width_m / std::max(1.0e-4F, width_threshold));
        const float white_score =
            Clamp01(white_ratio / std::max(kRatioDenominatorFloor, white_ratio_min));
        const float unknown_score = Clamp01(1.0F - unknown_ratio / kCrossUnknownRatioMax);
        const float balance_score = Clamp01(bilateral_balance / kCrossMinBilateralBalance);
        const float score = Clamp01(0.40F * width_score + 0.30F * white_score +
                                    0.15F * unknown_score + 0.15F * balance_score);
        AddWideRow(current, row, *interval, score);
    }
    finish_run();

    if (!saw_supported_row) {
        evidence.reason = "insufficient_sampleable_support";
        return evidence;
    }
    if (!saw_wide_row || best.row_count < kCrossMinContiguousWideRows) {
        evidence.reason = "wide_white_rows_absent";
        return evidence;
    }
    if (!opening.left_open || !opening.right_open) {
        evidence.reason = "wide_white_rows_absent";
        return evidence;
    }

    evidence.confidence = best.score_sum / static_cast<float>(best.row_count);
    evidence.forward_min_m = best.forward_min_m;
    evidence.forward_max_m = best.forward_max_m;
    evidence.lateral_min_m = best.lateral_min_m;
    evidence.lateral_max_m = best.lateral_max_m;
    evidence.sampleable_count = best.sampleable_count;
    evidence.supporting_white_count = best.supporting_white_count;
    evidence.unknown_count = best.unknown_count;
    if (evidence.confidence < kCrossPresentConfidenceMin) {
        evidence.reason = "low_confidence";
        return evidence;
    }

    evidence.present = true;
    evidence.reason = "present";
    return evidence;
}

port::VisualReferenceCandidate BuildCrossExitVisualReferenceCandidate(
    const port::CrossExitElementEvidence& evidence,
    const port::VisualReferenceCandidate& line_candidate,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary) {
    port::VisualReferenceCandidate candidate{};
    summary = {};
    summary.takeover_enabled = params.bev_element.cross_exit_takeover_enabled;
    if (!evidence.present) {
        summary.reason = evidence.reason.empty() ? "evidence_absent" : evidence.reason;
        return candidate;
    }
    if (!line_candidate.present || !HasLeadingVisualReference(line_candidate.reference_path)) {
        summary.reason = "line_candidate_absent";
        return candidate;
    }

    candidate.present = true;
    candidate.kind = port::VisualReferenceCandidateKind::kCrossExit;
    candidate.reference_path = line_candidate.reference_path;
    candidate.confidence = evidence.confidence;
    candidate.source = "cross_exit";
    candidate.reason = "cross_exit_evidence_candidate";

    summary.built = true;
    summary.included_in_arbitration = summary.takeover_enabled;
    summary.reason = summary.included_in_arbitration ? "included_in_arbitration"
                                                     : "takeover_disabled";
    return candidate;
}

}  // namespace ls2k::legacy
