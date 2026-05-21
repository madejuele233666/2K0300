#include <cmath>
#include <iostream>
#include <stdexcept>

#include "runtime/steering_reference_time_alignment.hpp"

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void ExpectNear(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

ls2k::port::BEVReferencePath MakeStraightPath() {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    for (std::size_t index = 0; index < 4; ++index) {
        auto& sample = path.sampled_path[index];
        sample.present = true;
        sample.point.forward_m = 0.2F + static_cast<float>(index) * 0.2F;
        sample.point.lateral_m = 0.0F;
        sample.confidence = 1.0F;
        sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    }
    return path;
}

ls2k::runtime::MotionHistory MakeMotionHistory(float gyro_z, std::uint64_t step_ms = 10) {
    ls2k::runtime::MotionHistory history{};
    for (std::uint64_t time_ms = 100; time_ms <= 130; time_ms += step_ms) {
        history.Push({time_ms, true, gyro_z, true, 0, 0});
    }
    return history;
}

ls2k::port::RuntimeParameters EnabledParams() {
    ls2k::port::RuntimeParameters params{};
    params.reference_time_alignment.enabled = true;
    params.reference_time_alignment.max_age_ms = 120;
    params.reference_time_alignment.max_integration_gap_ms = 30;
    params.reference_time_alignment.max_delta_yaw_rad = 0.8;
    params.reference_time_alignment.min_aligned_samples = 3;
    return params;
}

void TestDisabledKeepsPath() {
    const auto path = MakeStraightPath();
    const auto result = ls2k::runtime::AlignReferencePathToControlTime(
        path, 100, 130, MakeMotionHistory(0.0F), ls2k::port::RuntimeParameters{});
    Expect(result.facts.valid, "disabled alignment must be valid");
    Expect(result.facts.reason == "disabled", "disabled reason mismatch");
    Expect(result.reference_path.sampled_path[0].present, "disabled path sample missing");
    ExpectNear(result.reference_path.sampled_path[0].point.forward_m, 0.2, 1.0e-6, "disabled forward changed");
}

void TestIdentityAndYawSign() {
    const auto path = MakeStraightPath();
    auto params = EnabledParams();
    auto zero = ls2k::runtime::AlignReferencePathToControlTime(
        path, 100, 130, MakeMotionHistory(0.0F), params);
    Expect(zero.facts.valid, "zero yaw alignment invalid");
    Expect(zero.facts.reason == "aligned_yaw_only", "zero yaw reason mismatch");
    ExpectNear(zero.reference_path.sampled_path[2].point.forward_m,
               path.sampled_path[2].point.forward_m,
               1.0e-6,
               "identity forward mismatch");
    ExpectNear(zero.reference_path.sampled_path[2].point.lateral_m, 0.0, 1.0e-6, "identity lateral mismatch");

    auto yaw = ls2k::runtime::AlignReferencePathToControlTime(
        path, 100, 130, MakeMotionHistory(1.0F), params);
    Expect(yaw.facts.valid, "yaw alignment invalid");
    ExpectNear(yaw.facts.delta_yaw_rad, 0.03, 1.0e-6, "yaw integral mismatch");
    Expect(yaw.reference_path.sampled_path[2].point.lateral_m < 0.0F,
           "positive yaw should rotate old forward point to negative lateral in current frame");
}

void TestFailClosed() {
    const auto path = MakeStraightPath();
    auto params = EnabledParams();
    params.reference_time_alignment.max_age_ms = 20;
    auto aged = ls2k::runtime::AlignReferencePathToControlTime(
        path, 100, 130, MakeMotionHistory(0.0F), params);
    Expect(!aged.facts.valid && aged.facts.reason == "age_exceeded", "age fail reason mismatch");

    params = EnabledParams();
    params.reference_time_alignment.max_integration_gap_ms = 5;
    auto gap = ls2k::runtime::AlignReferencePathToControlTime(
        path, 100, 130, MakeMotionHistory(0.0F, 10), params);
    Expect(!gap.facts.valid && gap.facts.reason == "motion_history_unavailable",
           "gap fail reason mismatch");
}

}  // namespace

int main() {
    try {
        TestDisabledKeepsPath();
        TestIdentityAndYawSign();
        TestFailClosed();
    } catch (const std::exception& error) {
        std::cerr << "reference_time_alignment_test failed: " << error.what() << "\n";
        return 1;
    }
    std::cout << "reference_time_alignment_test passed\n";
    return 0;
}
