#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/steering_reference_tracking_geometry.hpp"
#include "legacy/steering_reference_usability.hpp"
#include "port/runtime_parameter_types.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void ExpectNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw TestFailure{message};
    }
}

ls2k::port::BEVPathSample Sample(float forward_m, float lateral_m) {
    ls2k::port::BEVPathSample sample{};
    sample.present = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = 1.0F;
    sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    return sample;
}

ls2k::port::BEVReferencePath MakePolynomialPath(
    const ls2k::port::RuntimeParameters& params,
    int count,
    float a,
    float b,
    float c) {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    for (int index = 0; index < count && index < static_cast<int>(ls2k::port::kBevReferenceSampleCount); ++index) {
        const float x = params.bev_geometry.forward_samples_m[static_cast<std::size_t>(index)];
        path.sampled_path[static_cast<std::size_t>(index)] = Sample(x, a * x * x + b * x + c);
    }
    return path;
}

ls2k::port::ReferenceTrackingGeometry Compute(
    const ls2k::port::BEVReferencePath& path,
    const ls2k::port::RuntimeParameters& params) {
    const ls2k::port::ReferenceUsability usability =
        ls2k::legacy::EvaluateReferenceUsability(path, params);
    return ls2k::legacy::ComputeReferenceTrackingGeometry(path,
                                                          usability,
                                                          params.bev_control_model);
}

void TestStraightReferenceProducesZeroCurvature() {
    const ls2k::port::RuntimeParameters params{};
    const auto output = Compute(MakePolynomialPath(params, 8, 0.0F, 0.0F, 0.0F), params);
    Expect(output.computed, "straight path must produce tracking geometry");
    ExpectNear(output.lateral_offset_m, 0.0F, 1.0e-4F, "straight path lateral offset");
    ExpectNear(output.heading_error_rad, 0.0F, 1.0e-4F, "straight path heading");
    ExpectNear(output.curvature_m_inv, 0.0F, 1.0e-3F, "straight path curvature");
    Expect(output.sample_count == 8, "tracking geometry must report used sample count");
}

void TestOffsetStraightReferencePreservesOffsetAndHeading() {
    const ls2k::port::RuntimeParameters params{};
    constexpr float kOffset = 0.18F;
    constexpr float kSlope = 0.20F;
    const auto output = Compute(MakePolynomialPath(params, 8, 0.0F, kSlope, kOffset), params);
    Expect(output.computed, "offset line must produce tracking geometry");
    ExpectNear(output.lateral_offset_m, kOffset, 1.0e-4F, "line intercept becomes lateral offset");
    ExpectNear(output.heading_error_rad, std::atan(kSlope), 1.0e-4F, "line slope becomes heading error");
    ExpectNear(output.curvature_m_inv, 0.0F, 1.0e-3F, "line curvature remains near zero");
}

void TestCurvedReferenceSeparatesCurvature() {
    const ls2k::port::RuntimeParameters params{};
    constexpr float kA = 0.50F;
    constexpr float kB = 0.10F;
    constexpr float kC = -0.04F;
    const auto output = Compute(MakePolynomialPath(params, 10, kA, kB, kC), params);
    const float expected_curvature = (2.0F * kA) / std::pow(1.0F + kB * kB, 1.5F);
    Expect(output.computed, "quadratic path must produce tracking geometry");
    ExpectNear(output.lateral_offset_m, kC, 1.0e-4F, "quadratic intercept becomes lateral offset");
    ExpectNear(output.heading_error_rad, std::atan(kB), 1.0e-4F, "quadratic slope at origin becomes heading");
    ExpectNear(output.curvature_m_inv, expected_curvature, 1.0e-3F, "quadratic coefficient becomes curvature");
}

void TestInsufficientSamplesFailClosed() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.min_leading_reference_samples = 3;
    params.bev_control_model.tracking_fit_min_samples = 4;
    const ls2k::port::BEVReferencePath path = MakePolynomialPath(params, 3, 0.0F, 0.0F, 0.0F);
    const auto output = Compute(path, params);
    Expect(!output.computed, "tracking geometry must reject too few fit samples");
    Expect(output.reason == "insufficient_tracking_geometry_samples",
           "tracking geometry insufficient-sample reason must be explicit");
}

void TestDegenerateFitFailsClosed() {
    const ls2k::port::RuntimeParameters params{};
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    path.sampled_path[0] = Sample(0.2F, 0.0F);
    path.sampled_path[1] = Sample(0.2F, 0.1F);
    path.sampled_path[2] = Sample(0.2F, 0.2F);
    const auto output = Compute(path, params);
    Expect(!output.computed, "degenerate fit must fail closed");
    Expect(output.reason == "tracking_geometry_fit_degenerate",
           "degenerate fit reason must be explicit");
}

}  // namespace

int main() {
    try {
        TestStraightReferenceProducesZeroCurvature();
        TestOffsetStraightReferencePreservesOffsetAndHeading();
        TestCurvedReferenceSeparatesCurvature();
        TestInsufficientSamplesFailClosed();
        TestDegenerateFitFailsClosed();
    } catch (const TestFailure& failure) {
        std::cerr << "reference_tracking_geometry_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "reference_tracking_geometry_test passed\n";
    return EXIT_SUCCESS;
}
