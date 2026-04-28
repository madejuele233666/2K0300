#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
    for (int dr = -2; dr <= 2; ++dr) {
        for (int dc = -2; dc <= 2; ++dc) {
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

void DrawVehicleStripe(ls2k::port::LegacyCameraFrame& frame,
                       const ls2k::legacy::BEVProjector& projector,
                       const ls2k::port::RuntimeParameters& params,
                       float lateral_min,
                       float lateral_max,
                       std::uint8_t value) {
    const auto& sampler = params.bev_topology_sampler;
    const float step = std::max(0.02F, sampler.lateral_step_m);
    for (float forward : sampler.forward_samples_m) {
        for (float lateral = lateral_min; lateral <= lateral_max + step * 0.5F; lateral += step) {
            ls2k::port::ImagePoint image{};
            if (projector.ProjectVehicleToImage({forward, lateral}, image)) {
                DrawPatch(frame, image.row_px, image.col_px, value);
            }
        }
    }
}

ls2k::legacy::BEVProjector MakeProjector(const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::BEVProjector projector{};
    Expect(projector.Configure(params.bev_projector), "default projector must configure");
    return projector;
}

const ls2k::port::BEVSample& ClosestSample(const ls2k::legacy::BEVSparseSampleGrid& samples,
                                           std::size_t layer_index,
                                           float lateral_m) {
    const auto& layer = samples.layers[layer_index];
    const ls2k::port::BEVSample* best = nullptr;
    float best_distance = std::numeric_limits<float>::infinity();
    for (const auto& sample : layer.samples) {
        const float distance = std::abs(sample.point.lateral_m - lateral_m);
        if (distance < best_distance) {
            best = &sample;
            best_distance = distance;
        }
    }
    Expect(best != nullptr, "sparse sample layer must not be empty");
    return *best;
}

void TestSparseSamplerClassifiesDrivableAndInvalid() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.18F, 0.18F, 255U);

    const ls2k::legacy::BEVSparseSampleGrid samples =
        ls2k::legacy::SparseMetricSample(frame, 100, params, projector);

    Expect(samples.valid, "sparse sampler must produce valid projected samples");
    int drivable_count = 0;
    int invalid_count = 0;
    for (const auto& layer : samples.layers) {
        for (const auto& sample : layer.samples) {
            if (sample.sample_class == ls2k::port::BEVSampleClass::kDrivable) {
                ++drivable_count;
            }
            if (sample.sample_class == ls2k::port::BEVSampleClass::kInvalidOutsideImage) {
                ++invalid_count;
            }
        }
    }
    Expect(drivable_count > 0, "drawn BEV stripe must classify as drivable");
    Expect(invalid_count > 0, "wide lateral sampling must expose invalid outside-image samples");
    Expect(std::abs(samples.layers.back().forward_m - 0.610F) < 1e-4F,
           "sparse sampler must cover the configured corrected far forward layer");
}

void TestModerateForegroundClassifiesDrivable() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.18F, 0.18F, 160U);

    const ls2k::legacy::BEVSparseSampleGrid samples =
        ls2k::legacy::SparseMetricSample(frame, 107, params, projector);

    int drivable_count = 0;
    for (const auto& layer : samples.layers) {
        for (const auto& sample : layer.samples) {
            if (sample.sample_class == ls2k::port::BEVSampleClass::kDrivable) {
                ++drivable_count;
            }
        }
    }
    Expect(drivable_count > 0, "moderately bright foreground must clear the drivable confidence gate");
}

void TestProjectorLateralSignIsStable() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::ImagePoint left{};
    ls2k::port::ImagePoint right{};
    Expect(projector.ProjectVehicleToImage({0.183F, -0.20F}, left), "left point must project");
    Expect(projector.ProjectVehicleToImage({0.183F, 0.20F}, right), "right point must project");
    Expect(left.col_px < right.col_px, "negative lateral must project left of positive lateral");
}

void TestLowConfidenceBecomesUnknown() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(112U);

    const ls2k::legacy::BEVSparseSampleGrid samples =
        ls2k::legacy::SparseMetricSample(frame, 100, params, projector);

    int unknown_count = 0;
    for (const auto& layer : samples.layers) {
        for (const auto& sample : layer.samples) {
            if (sample.sample_class == ls2k::port::BEVSampleClass::kUnknownLowConfidence) {
                ++unknown_count;
            }
        }
    }
    Expect(unknown_count > 0, "near-threshold valid samples must classify as unknown low confidence");
}

void TestBrightNeighborDoesNotPromoteDarkCenter() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);

    constexpr std::size_t kLayer = 5U;
    constexpr float kLateral = 0.0F;
    ls2k::port::ImagePoint image{};
    Expect(projector.ProjectVehicleToImage({params.bev_topology_sampler.forward_samples_m[kLayer], kLateral},
                                           image),
           "dark-center regression sample must project into the image");
    const int row = static_cast<int>(std::lround(image.row_px));
    const int col = static_cast<int>(std::lround(image.col_px));
    SetPixel(frame, row, col, 30U);
    SetPixel(frame, row, col + 1, 206U);
    SetPixel(frame, row + 1, col, 144U);

    const ls2k::legacy::BEVSparseSampleGrid samples =
        ls2k::legacy::SparseMetricSample(frame, 100, params, projector);
    const ls2k::port::BEVSample& sample = ClosestSample(samples, kLayer, kLateral);

    Expect(std::abs(sample.raw_intensity - 30.0F) < 1e-4F,
           "sparse sample decision intensity must be the center pixel, not the patch maximum");
    Expect(sample.sample_class != ls2k::port::BEVSampleClass::kDrivable,
           "bright neighboring pixels must not promote a dark center sample to drivable");
}

void TestDarkNeighborDoesNotHideBrightCenter() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);

    constexpr std::size_t kLayer = 5U;
    constexpr float kLateral = 0.0F;
    ls2k::port::ImagePoint image{};
    Expect(projector.ProjectVehicleToImage({params.bev_topology_sampler.forward_samples_m[kLayer], kLateral},
                                           image),
           "bright-center regression sample must project into the image");
    const int row = static_cast<int>(std::lround(image.row_px));
    const int col = static_cast<int>(std::lround(image.col_px));
    DrawPatch(frame, image.row_px, image.col_px, 180U);
    SetPixel(frame, row, col + 1, 20U);
    SetPixel(frame, row + 1, col, 33U);

    const ls2k::legacy::BEVSparseSampleGrid samples =
        ls2k::legacy::SparseMetricSample(frame, 100, params, projector);
    const ls2k::port::BEVSample& sample = ClosestSample(samples, kLayer, kLateral);

    Expect(std::abs(sample.raw_intensity - 180.0F) < 1e-4F,
           "bright-center regression must still decide from the center pixel");
    Expect(sample.sample_class == ls2k::port::BEVSampleClass::kDrivable,
           "dark neighboring pixels must not hide a high-confidence bright center sample");
}

}  // namespace

int main() {
    try {
        TestSparseSamplerClassifiesDrivableAndInvalid();
        TestModerateForegroundClassifiesDrivable();
        TestProjectorLateralSignIsStable();
        TestLowConfidenceBecomesUnknown();
        TestBrightNeighborDoesNotPromoteDarkCenter();
        TestDarkNeighborDoesNotHideBrightCenter();
    } catch (const TestFailure& failure) {
        std::cerr << "bev_sparse_sampler_test failed: " << failure.message << "\n";
        return 1;
    }
    return 0;
}
