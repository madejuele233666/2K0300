#include <cmath>
#include <cstddef>
#include <iostream>
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
    Expect(std::abs(samples.layers.back().forward_m - 4.50F) < 1e-4F,
           "sparse sampler must cover the configured 4.5m forward layer");
}

void TestProjectorLateralSignIsStable() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::ImagePoint left{};
    ls2k::port::ImagePoint right{};
    Expect(projector.ProjectVehicleToImage({1.0F, -0.20F}, left), "left point must project");
    Expect(projector.ProjectVehicleToImage({1.0F, 0.20F}, right), "right point must project");
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

}  // namespace

int main() {
    try {
        TestSparseSamplerClassifiesDrivableAndInvalid();
        TestProjectorLateralSignIsStable();
        TestLowConfidenceBecomesUnknown();
    } catch (const TestFailure& failure) {
        std::cerr << "bev_sparse_sampler_test failed: " << failure.message << "\n";
        return 1;
    }
    return 0;
}
