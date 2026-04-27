#include "legacy/steering_topology_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ls2k::legacy {
namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

struct IntervalEvidenceSummary {
    bool valid = false;
    float near_left = 0.0F;
    float near_right = 0.0F;
    float near_width = 0.0F;
    float max_width = 0.0F;
    float left_opening = 0.0F;
    float right_opening = 0.0F;
    float invalid_edge_penalty = 0.0F;
    float confidence = 0.0F;
    int valid_layers = 0;
};

IntervalEvidenceSummary SummarizeIntervals(const CorridorIntervalSet& intervals) {
    IntervalEvidenceSummary summary{};
    float confidence_sum = 0.0F;
    int edge_count = 0;
    int invalid_edges = 0;
    for (const CorridorIntervalLayer& layer : intervals.layers) {
        if (layer.intervals.empty()) {
            continue;
        }
        const port::CorridorInterval* best = nullptr;
        for (const port::CorridorInterval& interval : layer.intervals) {
            if (best == nullptr || interval.confidence > best->confidence) {
                best = &interval;
            }
        }
        if (best == nullptr) {
            continue;
        }
        if (!summary.valid) {
            summary.valid = true;
            summary.near_left = best->lateral_min_m;
            summary.near_right = best->lateral_max_m;
            summary.near_width = best->width_m;
        }
        summary.max_width = std::max(summary.max_width, best->width_m);
        summary.left_opening = std::max(summary.left_opening, summary.near_left - best->lateral_min_m);
        summary.right_opening = std::max(summary.right_opening, best->lateral_max_m - summary.near_right);
        if (!best->left_edge_valid) {
            ++invalid_edges;
        }
        if (!best->right_edge_valid) {
            ++invalid_edges;
        }
        edge_count += 2;
        confidence_sum += best->confidence;
        ++summary.valid_layers;
    }
    if (summary.valid_layers > 0) {
        summary.confidence = Clamp01(confidence_sum / static_cast<float>(summary.valid_layers));
    }
    if (edge_count > 0) {
        summary.invalid_edge_penalty = Clamp01(static_cast<float>(invalid_edges) / static_cast<float>(edge_count));
    }
    return summary;
}

float WidthBulgeScore(const IntervalEvidenceSummary& summary,
                      const port::BEVCorridorGraphParameters& params) {
    const float reference = summary.near_width > 1e-4F ? summary.near_width : params.nominal_lane_width_m;
    const float bulge = summary.max_width - reference;
    return Clamp01(bulge / std::max(1e-4F, params.nominal_lane_width_m));
}

float ZebraStripeScore(const CorridorIntervalSet& intervals,
                       const port::RuntimeParameters& params,
                       float invalid_edge_penalty) {
    constexpr std::size_t kMaxZebraLayers = 8U;
    bool previous_wide_layer = false;
    bool have_previous = false;
    int transitions = 0;
    int wide_layers = 0;
    int inspected_layers = 0;
    const float wide_threshold = params.bev_corridor_graph.nominal_lane_width_m * 0.75F;
    for (std::size_t layer = 0; layer < intervals.layers.size() && layer < kMaxZebraLayers; ++layer) {
        bool wide_layer = false;
        for (const port::CorridorInterval& interval : intervals.layers[layer].intervals) {
            if (interval.width_m >= wide_threshold && interval.confidence >= 0.45F &&
                interval.valid_sample_ratio >= 0.45F) {
                wide_layer = true;
                break;
            }
        }
        if (wide_layer) {
            ++wide_layers;
        }
        if (have_previous && wide_layer != previous_wide_layer) {
            ++transitions;
        }
        previous_wide_layer = wide_layer;
        have_previous = true;
        ++inspected_layers;
    }
    if (inspected_layers < 4) {
        return 0.0F;
    }
    const float transition_score = Clamp01(static_cast<float>(transitions) / 3.0F);
    const float density_score = Clamp01(static_cast<float>(wide_layers) / 4.0F);
    return Clamp01(transition_score * density_score * (1.0F - 0.25F * invalid_edge_penalty));
}

}  // namespace

port::TopologyEvidence ScoreTopologyEvidence(const port::RoadHypotheses& hypotheses,
                                             const CorridorIntervalSet& intervals,
                                             const port::VehicleContext& vehicle,
                                             const port::RuntimeParameters& params,
                                             const port::TopologyEvidenceAccumulator& prior_accumulator) {
    (void)vehicle;
    (void)prior_accumulator;
    port::TopologyEvidence evidence{};
    const IntervalEvidenceSummary summary = SummarizeIntervals(intervals);

    evidence.invalid_edge_penalty = summary.invalid_edge_penalty;
    evidence.left_opening_score = summary.left_opening;
    evidence.right_opening_score = summary.right_opening;
    evidence.bilateral_opening_sync =
        Clamp01(std::min(summary.left_opening, summary.right_opening) /
                std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    evidence.forward_reacquire_score = hypotheses.forward_exit.valid ? 1.0F : 0.0F;
    evidence.bend_curvature_abs = std::abs(hypotheses.ordinary.curvature);
    evidence.bend_veto_score =
        Clamp01(evidence.bend_curvature_abs / std::max(1e-4F, params.bev_corridor_graph.max_curvature_abs));

    if (hypotheses.ordinary.valid) {
        evidence.ordinary_score =
            Clamp01(hypotheses.ordinary.confidence * (0.5F + 0.5F * hypotheses.ordinary.width_stability) *
                    (1.0F - 0.5F * evidence.invalid_edge_penalty));
    }

    const float width_bulge = WidthBulgeScore(summary, params.bev_corridor_graph);
    evidence.cross_score = Clamp01(std::min(width_bulge, evidence.bilateral_opening_sync) *
                                   std::max(evidence.forward_reacquire_score, evidence.ordinary_score) *
                                   (1.0F - 0.5F * evidence.bend_veto_score));

    const float left_open_score =
        Clamp01(summary.left_opening / std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    const float right_open_score =
        Clamp01(summary.right_opening / std::max(1e-4F, params.bev_corridor_graph.nominal_lane_width_m * 0.5F));
    evidence.left_circle_score =
        Clamp01(std::max(left_open_score, hypotheses.left_branch.confidence) *
                (hypotheses.left_arc.valid ? 1.0F : 0.5F) *
                (1.0F - evidence.bilateral_opening_sync) *
                (1.0F - evidence.bend_veto_score) *
                (1.0F - 0.5F * evidence.invalid_edge_penalty));
    evidence.right_circle_score =
        Clamp01(std::max(right_open_score, hypotheses.right_branch.confidence) *
                (hypotheses.right_arc.valid ? 1.0F : 0.5F) *
                (1.0F - evidence.bilateral_opening_sync) *
                (1.0F - evidence.bend_veto_score) *
                (1.0F - 0.5F * evidence.invalid_edge_penalty));

    evidence.zebra_score = intervals.valid ? ZebraStripeScore(intervals, params, evidence.invalid_edge_penalty) : 0.0F;
    const float best_known = std::max({evidence.ordinary_score,
                                       evidence.cross_score,
                                       evidence.left_circle_score,
                                       evidence.right_circle_score,
                                       evidence.zebra_score});
    evidence.lost_score = Clamp01(1.0F - best_known);
    return evidence;
}

port::TopologyEvidenceAccumulator UpdateTopologyEvidenceAccumulator(
    const port::TopologyEvidence& evidence,
    const port::RuntimeParameters& params,
    const port::TopologyEvidenceAccumulator& prior_accumulator) {
    const float keep = Clamp01(params.bev_topology_evidence.evidence_decay);
    const float add = 1.0F - keep;
    port::TopologyEvidenceAccumulator next{};
    next.value.ordinary_score = prior_accumulator.value.ordinary_score * keep + evidence.ordinary_score * add;
    next.value.bend_curvature_abs =
        prior_accumulator.value.bend_curvature_abs * keep + evidence.bend_curvature_abs * add;
    next.value.bend_veto_score =
        prior_accumulator.value.bend_veto_score * keep + evidence.bend_veto_score * add;
    next.value.cross_score = prior_accumulator.value.cross_score * keep + evidence.cross_score * add;
    next.value.left_circle_score =
        prior_accumulator.value.left_circle_score * keep + evidence.left_circle_score * add;
    next.value.right_circle_score =
        prior_accumulator.value.right_circle_score * keep + evidence.right_circle_score * add;
    next.value.zebra_score = prior_accumulator.value.zebra_score * keep + evidence.zebra_score * add;
    next.value.lost_score = prior_accumulator.value.lost_score * keep + evidence.lost_score * add;
    next.value.bilateral_opening_sync =
        prior_accumulator.value.bilateral_opening_sync * keep + evidence.bilateral_opening_sync * add;
    next.value.forward_reacquire_score =
        prior_accumulator.value.forward_reacquire_score * keep + evidence.forward_reacquire_score * add;
    next.value.left_opening_score =
        prior_accumulator.value.left_opening_score * keep + evidence.left_opening_score * add;
    next.value.right_opening_score =
        prior_accumulator.value.right_opening_score * keep + evidence.right_opening_score * add;
    next.value.invalid_edge_penalty =
        prior_accumulator.value.invalid_edge_penalty * keep + evidence.invalid_edge_penalty * add;
    next.update_cycles = prior_accumulator.update_cycles + 1;
    return next;
}

}  // namespace ls2k::legacy
