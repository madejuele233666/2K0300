#include "legacy/steering_cross_exit_element_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

constexpr std::size_t kCrossMinContiguousWideRows = 3U;
constexpr std::size_t kCrossMinSampleablePerRow = 8U;
constexpr float kCrossMinWideWidthM = 0.52F;
constexpr float kCrossMinSampleableWidthRatio = 0.65F;
constexpr float kCrossUnknownRatioMax = 0.25F;
constexpr float kCrossPresentConfidenceMin = 0.70F;

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
        const BEVSimpleWhiteInterval* interval = WidestInterval(row);
        if (interval == nullptr || unknown_ratio > kCrossUnknownRatioMax) {
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

        saw_wide_row = true;
        const float width_score = Clamp01(interval->width_m / std::max(1.0e-4F, width_threshold));
        const float unknown_score = Clamp01(1.0F - unknown_ratio / kCrossUnknownRatioMax);
        const float support_score =
            Clamp01(row_sampleable_width / std::max(1.0e-4F, expected_sampleable_width));
        const float score = Clamp01(0.55F * width_score + 0.30F * unknown_score + 0.15F * support_score);
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
