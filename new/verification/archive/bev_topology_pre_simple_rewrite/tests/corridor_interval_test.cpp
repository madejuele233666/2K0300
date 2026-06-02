#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/steering_corridor_intervals.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

ls2k::port::BEVSample Sample(float lateral,
                             ls2k::port::BEVSampleClass sample_class,
                             bool near_image_border = false) {
    ls2k::port::BEVSample sample{};
    sample.point.forward_m = 1.0F;
    sample.point.lateral_m = lateral;
    sample.valid_image_projection = sample_class != ls2k::port::BEVSampleClass::kInvalidOutsideImage;
    sample.sample_class = sample_class;
    sample.raw_intensity = sample_class == ls2k::port::BEVSampleClass::kDrivable ? 255.0F : 0.0F;
    sample.confidence = sample_class == ls2k::port::BEVSampleClass::kUnknownLowConfidence ? 0.1F : 0.9F;
    sample.near_image_border = near_image_border;
    return sample;
}

ls2k::legacy::BEVSparseSampleGrid Grid(std::initializer_list<ls2k::port::BEVSampleClass> classes) {
    ls2k::legacy::BEVSparseSampleGrid grid{};
    grid.valid = true;
    grid.layers[0].forward_m = 1.0F;
    float lateral = -0.06F;
    for (const auto sample_class : classes) {
        grid.layers[0].samples.push_back(Sample(lateral, sample_class));
        lateral += 0.02F;
    }
    return grid;
}

ls2k::legacy::BEVSparseSampleGrid GridWithBorderAdjacent() {
    ls2k::legacy::BEVSparseSampleGrid grid{};
    grid.valid = true;
    grid.layers[0].forward_m = 1.0F;
    grid.layers[0].samples.push_back(Sample(-0.06F, ls2k::port::BEVSampleClass::kBackground));
    grid.layers[0].samples.push_back(Sample(-0.04F, ls2k::port::BEVSampleClass::kDrivable));
    grid.layers[0].samples.push_back(Sample(-0.02F, ls2k::port::BEVSampleClass::kDrivable));
    grid.layers[0].samples.push_back(Sample(0.0F, ls2k::port::BEVSampleClass::kBackground, true));
    return grid;
}

void TestSingleCorridor() {
    ls2k::port::RuntimeParameters params{};
    const auto intervals =
        ls2k::legacy::ExtractCorridorIntervals(Grid({ls2k::port::BEVSampleClass::kBackground,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kBackground}),
                                                params);
    Expect(intervals.valid, "single corridor interval set must be valid");
    Expect(intervals.layers[0].intervals.size() == 1U, "single corridor must create one interval");
    const auto& interval = intervals.layers[0].intervals[0];
    Expect(interval.left_edge_valid && interval.right_edge_valid, "background-bounded corridor edges must be valid");
    Expect(interval.left_boundary_evidence ==
               ls2k::port::BEVBoundaryEvidenceKind::kObservedDrivableBackground,
           "left boundary evidence must record a drivable/background observation");
    Expect(interval.right_boundary_evidence ==
               ls2k::port::BEVBoundaryEvidenceKind::kObservedDrivableBackground,
           "right boundary evidence must record a drivable/background observation");
    Expect(interval.confidence > 0.8F, "drivable corridor confidence must remain high");
}

void TestMultiIntervalAndOpenings() {
    ls2k::port::RuntimeParameters params{};
    const auto intervals =
        ls2k::legacy::ExtractCorridorIntervals(Grid({ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kBackground,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kDrivable}),
                                                params);
    Expect(intervals.layers[0].intervals.size() == 2U, "two drivable runs must create two intervals");
    Expect(!intervals.layers[0].intervals[0].left_edge_valid,
           "run reaching valid lateral search edge is not an observed lane boundary");
    Expect(intervals.layers[0].intervals[0].left_opening_score > 0.0F,
           "run reaching valid lateral search edge must record opening");
    Expect(intervals.layers[0].intervals[0].left_boundary_evidence ==
               ls2k::port::BEVBoundaryEvidenceKind::kSearchWindowEdge,
           "run reaching lateral search edge must be typed as search-window support, not observed boundary");
    Expect(!intervals.layers[0].intervals[1].right_edge_valid,
           "right run reaching valid lateral search edge is not an observed lane boundary");
    Expect(intervals.layers[0].intervals[1].right_opening_score > 0.0F,
           "right run reaching valid lateral search edge must record opening");
}

void TestInvalidEdgeDoesNotOpen() {
    ls2k::port::RuntimeParameters params{};
    const auto intervals =
        ls2k::legacy::ExtractCorridorIntervals(Grid({ls2k::port::BEVSampleClass::kInvalidOutsideImage,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kBackground}),
                                                params);
    Expect(intervals.layers[0].intervals.size() == 1U, "invalid-edge fixture must still find corridor");
    const auto& interval = intervals.layers[0].intervals[0];
    Expect(!interval.left_edge_valid, "invalid adjacent edge is not a valid boundary");
    Expect(interval.left_boundary_evidence ==
               ls2k::port::BEVBoundaryEvidenceKind::kInvalidOutsideImage,
           "invalid adjacent edge must be typed as invalid outside-image evidence");
    Expect(interval.left_opening_score == 0.0F, "invalid outside-image must not count as opening");
}

void TestImageBorderDoesNotValidateBoundary() {
    ls2k::port::RuntimeParameters params{};
    const auto intervals = ls2k::legacy::ExtractCorridorIntervals(GridWithBorderAdjacent(), params);
    Expect(intervals.layers[0].intervals.size() == 1U, "image-border fixture must find corridor support");
    const auto& interval = intervals.layers[0].intervals[0];
    Expect(interval.left_edge_valid, "non-border side remains an observed boundary");
    Expect(!interval.right_edge_valid, "image-border adjacent background must not validate a boundary");
    Expect(interval.right_boundary_evidence == ls2k::port::BEVBoundaryEvidenceKind::kImageBorder,
           "image-border adjacent background must be typed as image-border evidence");
}

void TestUnknownDoesNotValidateBoundary() {
    ls2k::port::RuntimeParameters params{};
    const auto intervals =
        ls2k::legacy::ExtractCorridorIntervals(Grid({ls2k::port::BEVSampleClass::kUnknownLowConfidence,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kDrivable,
                                                     ls2k::port::BEVSampleClass::kBackground}),
                                                params);
    Expect(intervals.layers[0].intervals.size() == 1U, "unknown-adjacent fixture must find corridor");
    Expect(!intervals.layers[0].intervals[0].left_edge_valid,
           "unknown low-confidence support must not validate an observed boundary");
    Expect(intervals.layers[0].intervals[0].left_boundary_evidence ==
               ls2k::port::BEVBoundaryEvidenceKind::kUnknownLowConfidence,
           "unknown adjacent support must be typed separately from observed boundary");
    Expect(intervals.layers[0].intervals[0].confidence < 0.8F,
           "unknown adjacent samples must lower interval confidence");
}

}  // namespace

int main() {
    try {
        TestSingleCorridor();
        TestMultiIntervalAndOpenings();
        TestInvalidEdgeDoesNotOpen();
        TestImageBorderDoesNotValidateBoundary();
        TestUnknownDoesNotValidateBoundary();
    } catch (const TestFailure& failure) {
        std::cerr << "corridor_interval_test failed: " << failure.message << "\n";
        return 1;
    }
    return 0;
}
