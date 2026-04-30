#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/steering_bev_element_evidence.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_sparse_sampler.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void SetPixel(ls2k::port::LegacyCameraFrame& frame, int row, int col, std::uint8_t value) {
    if (row < 0 || col < 0 || row >= frame.height || col >= frame.width) {
        return;
    }
    frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
               static_cast<std::size_t>(col)] = value;
}

void DrawPatch(ls2k::port::LegacyCameraFrame& frame, float row_px, float col_px, std::uint8_t value) {
    const int row = static_cast<int>(std::lround(row_px));
    const int col = static_cast<int>(std::lround(col_px));
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            SetPixel(frame, row + dr, col + dc, value);
        }
    }
}

ls2k::port::LegacyCameraFrame MakeFrame(std::uint8_t fill = 0U) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(fill);
    return frame;
}

ls2k::legacy::BEVProjector MakeProjector(const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::BEVProjector projector{};
    Expect(projector.Configure(params.bev_projector), "default BEV projector must configure");
    return projector;
}

void DrawBevPoint(ls2k::port::LegacyCameraFrame& frame,
                  const ls2k::legacy::BEVProjector& projector,
                  float forward_m,
                  float lateral_m,
                  std::uint8_t value) {
    ls2k::port::ImagePoint image{};
    if (projector.ProjectVehicleToImage({forward_m, lateral_m}, image)) {
        DrawPatch(frame, image.row_px, image.col_px, value);
    }
}

void DrawBevRect(ls2k::port::LegacyCameraFrame& frame,
                 const ls2k::legacy::BEVProjector& projector,
                 float forward_min_m,
                 float forward_max_m,
                 float lateral_min_m,
                 float lateral_max_m,
                 std::uint8_t value) {
    for (float forward = forward_min_m; forward <= forward_max_m + 0.0025F; forward += 0.005F) {
        for (float lateral = lateral_min_m; lateral <= lateral_max_m + 0.005F; lateral += 0.010F) {
            DrawBevPoint(frame, projector, forward, lateral, value);
        }
    }
}

void DrawCornerRegion(ls2k::port::LegacyCameraFrame& frame,
                      const ls2k::legacy::BEVProjector& projector,
                      bool sharp_corner) {
    for (float forward = 0.42F; forward <= 0.78F; forward += 0.005F) {
        float left_edge = -0.42F;
        if (sharp_corner) {
            const float distance = std::abs(forward - 0.60F);
            left_edge = -0.16F - distance * 1.45F;
        } else {
            const float t = (forward - 0.60F) / 0.18F;
            left_edge = -0.34F + 0.11F * (1.0F - t * t);
        }
        for (float lateral = left_edge; lateral <= 0.34F; lateral += 0.010F) {
            DrawBevPoint(frame, projector, forward, lateral, 220U);
        }
    }
}

ls2k::port::BEVElementEvidence Extract(ls2k::port::LegacyCameraFrame frame,
                                       const ls2k::port::RuntimeParameters& params,
                                       const ls2k::legacy::BEVProjector& projector) {
    const ls2k::legacy::BEVSparseSampleGrid sparse =
        ls2k::legacy::SparseMetricSample(frame, 100, params, projector);
    return ls2k::legacy::ExtractBEVElementEvidence(frame, 100, params, projector, sparse);
}

void TestCrossBandPassesOnlyForContinuousWhiteSpan() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawBevRect(frame, projector, 0.48F, 0.56F, -0.64F, 0.64F, 220U);

    const ls2k::port::BEVElementEvidence evidence = Extract(frame, params, projector);

    Expect(evidence.cross_band.present, "continuous valid-span white band must produce cross evidence");
    Expect(evidence.cross_band.score >= 0.70F, "cross band score must clear enter evidence");
}

void TestCrossBandRejectsGapAndBlankFrame() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVProjector projector = MakeProjector(params);

    Expect(!Extract(MakeFrame(0U), params, projector).cross_band.present,
           "blank frame must not produce cross evidence");

    ls2k::port::LegacyCameraFrame gap_frame = MakeFrame(0U);
    DrawBevRect(gap_frame, projector, 0.48F, 0.56F, -0.64F, -0.18F, 220U);
    DrawBevRect(gap_frame, projector, 0.48F, 0.56F, 0.18F, 0.64F, 220U);
    Expect(!Extract(gap_frame, params, projector).cross_band.present,
           "large dark gap must break cross-band evidence");
}

void TestCircleCornerRequiresSharpPoint() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVProjector projector = MakeProjector(params);

    ls2k::port::LegacyCameraFrame sharp = MakeFrame(0U);
    DrawCornerRegion(sharp, projector, true);
    const ls2k::port::BEVElementEvidence corner = Extract(sharp, params, projector);
    Expect(corner.left_circle_corner.present || corner.right_circle_corner.present,
           "clear BEV corner must produce circle-corner evidence");

    ls2k::port::LegacyCameraFrame smooth = MakeFrame(0U);
    DrawCornerRegion(smooth, projector, false);
    const ls2k::port::BEVElementEvidence smooth_evidence = Extract(smooth, params, projector);
    Expect(!smooth_evidence.left_circle_corner.present && !smooth_evidence.right_circle_corner.present,
           "smooth inner-curve support must not be treated as a circle corner");
}

void TestCircleInnerIslandRequiresWhiteBlackWhite() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVProjector projector = MakeProjector(params);

    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawCornerRegion(frame, projector, true);
    DrawBevRect(frame, projector, 0.11F, 0.22F, -0.14F, -0.06F, 220U);
    DrawBevRect(frame, projector, 0.28F, 0.39F, -0.14F, -0.06F, 0U);
    DrawBevRect(frame, projector, 0.45F, 0.62F, -0.14F, -0.06F, 220U);

    const ls2k::port::BEVElementEvidence evidence = Extract(frame, params, projector);

    Expect(evidence.left_inner_island.present || evidence.right_inner_island.present,
           "corner-side white-black-white vertical scan must establish inner-island evidence");
    const ls2k::port::BEVCircleInnerIslandEvidence& island =
        evidence.left_inner_island.present ? evidence.left_inner_island : evidence.right_inner_island;
    Expect(island.trace_present && island.edge_present,
           "white-black-white inner island must expose a continuous traced road-facing edge");
    Expect(island.trace_support_layers >= std::max(1, params.bev_path_policy.circle_inner_min_layers),
           "inner-island trace must have enough support layers for path authority");
    Expect(island.trace_end_forward_m > island.trace_start_forward_m,
           "inner-island trace must cover a forward span, not a single row");

    ls2k::port::LegacyCameraFrame incomplete = MakeFrame(0U);
    DrawBevRect(incomplete, projector, 0.11F, 0.22F, -0.14F, -0.06F, 220U);
    DrawBevRect(incomplete, projector, 0.28F, 0.70F, -0.14F, -0.06F, 0U);
    const ls2k::port::BEVElementEvidence incomplete_evidence =
        Extract(incomplete, params, projector);
    Expect(!incomplete_evidence.left_inner_island.present &&
               !incomplete_evidence.right_inner_island.present,
           "white-black without the second white run must not calibrate inner-island memory");
}

void TestCrossSuppressesCircleCornerEvidence() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawCornerRegion(frame, projector, true);
    DrawBevRect(frame, projector, 0.48F, 0.56F, -0.64F, 0.64F, 220U);

    const ls2k::port::BEVElementEvidence evidence = Extract(frame, params, projector);

    Expect(evidence.cross_band.present, "fixture must contain cross-band evidence");
    Expect(!evidence.left_circle_corner.present && !evidence.right_circle_corner.present,
           "cross band must suppress simultaneous circle-corner evidence");
    Expect(!evidence.left_inner_island.present && !evidence.right_inner_island.present,
           "cross band must suppress simultaneous inner-island evidence");
    Expect(!evidence.left_inner_island.trace_present && !evidence.right_inner_island.trace_present,
           "cross band must suppress simultaneous inner-island trace");
}

}  // namespace

int main() {
    try {
        TestCrossBandPassesOnlyForContinuousWhiteSpan();
        TestCrossBandRejectsGapAndBlankFrame();
        TestCircleCornerRequiresSharpPoint();
        TestCircleInnerIslandRequiresWhiteBlackWhite();
        TestCrossSuppressesCircleCornerEvidence();
    } catch (const TestFailure& failure) {
        std::cerr << "bev_element_evidence_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "bev_element_evidence_test passed\n";
    return EXIT_SUCCESS;
}
