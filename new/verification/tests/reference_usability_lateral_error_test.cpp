#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/steering_reference_control_readiness.hpp"
#include "legacy/steering_reference_lateral_error.hpp"
#include "legacy/steering_reference_usability.hpp"
#include "legacy/steering_yaw_controller.hpp"
#include "runtime/control_decision.hpp"

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

ls2k::port::BEVPathSample Sample(
    float forward_m,
    float lateral_m,
    ls2k::port::BEVPathPointSource source = ls2k::port::BEVPathPointSource::kIntervalCenter) {
    ls2k::port::BEVPathSample sample{};
    sample.present = true;
    sample.point.forward_m = forward_m;
    sample.point.lateral_m = lateral_m;
    sample.confidence = 1.0F;
    sample.source = source;
    return sample;
}

ls2k::port::BEVReferencePath MakePath(
    const ls2k::port::RuntimeParameters& params,
    int count,
    float lateral_m,
    ls2k::port::BEVPathPointSource source = ls2k::port::BEVPathPointSource::kIntervalCenter) {
    ls2k::port::BEVReferencePath path{};
    path.mode = source == ls2k::port::BEVPathPointSource::kHold
                    ? ls2k::port::ReferenceMode::kHoldLast
                    : ls2k::port::ReferenceMode::kIntervalCenter;
    for (int index = 0; index < count && index < static_cast<int>(ls2k::port::kBevReferenceSampleCount); ++index) {
        path.sampled_path[static_cast<std::size_t>(index)] =
            Sample(params.bev_geometry.forward_samples_m[static_cast<std::size_t>(index)],
                   lateral_m,
                   source);
    }
    return path;
}

ls2k::port::ReferenceLateralErrorEstimate ComputeLateralError(
    const ls2k::port::BEVReferencePath& path,
    const ls2k::port::RuntimeParameters& params) {
    const ls2k::port::ReferenceUsability usability =
        ls2k::legacy::EvaluateReferenceUsability(path, params);
    return ls2k::legacy::ComputeReferenceLateralError(path, usability, params);
}

void TestUsabilityRequiresConfiguredMinimumLeadingReferenceSamples() {
    const ls2k::port::RuntimeParameters params{};

    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 0, 0.0F), params).usable,
           "empty reference facts must not be usable");
    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 2, 0.0F), params).usable,
           "fewer than configured minimum leading samples must not be usable");
    Expect(ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 6, 0.0F), params).usable,
           "continuous leading reference facts with enough points must be usable");
}

void TestConfiguredMinimumLeadingReferenceSamplesCanChange() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.min_leading_reference_samples = 4;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 3, 0.0F), params).usable,
           "configured minimum of four must reject three leading points");
    Expect(ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 4, 0.0F), params).usable,
           "configured minimum of four must accept four leading points");

    params.bev_control_model.min_leading_reference_samples = 1;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 1, 0.0F), params).usable,
           "configured minimum below interpolation floor must clamp to two samples");
    Expect(ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 2, 0.0F), params).usable,
           "two leading samples satisfy the interpolation floor");

    params.bev_control_model.min_leading_reference_samples = -1;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 1, 0.0F), params).usable,
           "negative configured minimum must clamp to two samples");
    params.bev_control_model.min_leading_reference_samples = 0;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(MakePath(params, 1, 0.0F), params).usable,
           "zero configured minimum must clamp to two samples");
    params.bev_control_model.min_leading_reference_samples =
        static_cast<int>(ls2k::port::kBevReferenceSampleCount) + 10;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(
               MakePath(params, static_cast<int>(ls2k::port::kBevReferenceSampleCount) - 1, 0.0F),
               params)
                .usable,
           "configured minimum above sample capacity must clamp to the capacity");
}

void TestLeadingContinuityIsRequired() {
    const ls2k::port::RuntimeParameters params{};
    ls2k::port::BEVReferencePath path = MakePath(params, 6, 0.05F);
    path.sampled_path[0].present = false;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(path, params).usable,
           "path with index zero missing must not be usable even if far points exist");

    path = MakePath(params, 6, 0.05F);
    path.sampled_path[1].present = false;
    Expect(!ls2k::legacy::EvaluateReferenceUsability(path, params).usable,
           "usability must not scan past a gap in the leading segment");
}

void TestSourceDoesNotAffectUsabilityOrLateralError() {
    const ls2k::port::RuntimeParameters params{};
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    for (int index = 0; index < 6; ++index) {
        path.sampled_path[static_cast<std::size_t>(index)] =
            Sample(params.bev_geometry.forward_samples_m[static_cast<std::size_t>(index)],
                   0.1F,
                   ls2k::port::BEVPathPointSource::kNone);
    }

    const auto usability = ls2k::legacy::EvaluateReferenceUsability(path, params);
    const auto output = ls2k::legacy::ComputeReferenceLateralError(path, usability, params);
    Expect(usability.usable, "usability must depend on present geometry, not source");
    Expect(output.computed, "lateral error must depend on usable geometry, not source");
}

void TestStraightReferenceProducesZeroLateralError() {
    const ls2k::port::RuntimeParameters params{};
    const auto output = ComputeLateralError(MakePath(params, 8, 0.0F), params);

    Expect(output.computed, "straight path must produce computed lateral error");
    ExpectNear(output.weighted_lateral_error_m, 0.0F, 1.0e-6F, "straight path must produce zero lateral error");
}

void TestConstantOffsetReferencePreservesOffset() {
    const ls2k::port::RuntimeParameters params{};
    const auto output = ComputeLateralError(MakePath(params, 8, 0.2F), params);

    Expect(output.computed, "offset path must produce computed lateral error");
    ExpectNear(output.weighted_lateral_error_m, 0.2F, 1.0e-6F, "constant offset must survive weighting");
}

void TestNearSamplesHaveMoreWeightThanFarSamples() {
    const ls2k::port::RuntimeParameters params{};
    ls2k::port::BEVReferencePath path = MakePath(params, 24, 0.0F);
    for (int index = 0; index < 6; ++index) {
        path.sampled_path[static_cast<std::size_t>(index)].point.lateral_m = 0.30F;
    }

    const auto output = ComputeLateralError(path, params);
    Expect(output.computed, "mixed path must produce computed lateral error");
    Expect(output.weighted_lateral_error_m > 0.075F,
           "near-heavy weights must pull the result above the unweighted average");
    Expect(output.weighted_lateral_error_m < 0.30F,
           "weighted result must remain inside the observed lateral range");
}

void TestGapStopsWeightedSamples() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.min_leading_reference_samples = 4;
    ls2k::port::BEVReferencePath path = MakePath(params, 8, 0.10F);
    path.sampled_path[4].present = false;
    for (int index = 5; index < 8; ++index) {
        path.sampled_path[static_cast<std::size_t>(index)].point.lateral_m = 0.60F;
    }

    const auto output = ComputeLateralError(path, params);
    Expect(output.computed, "leading segment before gap must be usable");
    Expect(output.weighted_sample_count == 4, "lateral error must not use samples beyond the first gap");
    ExpectNear(output.weighted_lateral_error_m, 0.10F, 1.0e-6F, "far samples after a gap must not affect output");
}

void TestMaxWeightedSampleIndexStopsFarSamples() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.min_leading_reference_samples = 3;
    params.bev_control_model.lateral_error_max_weighted_sample_index = 3;
    ls2k::port::BEVReferencePath path = MakePath(params, 8, 0.10F);
    for (int index = 4; index < 8; ++index) {
        path.sampled_path[static_cast<std::size_t>(index)].point.lateral_m = 0.60F;
    }

    const auto output = ComputeLateralError(path, params);
    Expect(output.computed, "leading segment must produce lateral error");
    Expect(output.weighted_sample_count == 4,
           "lateral error must stop at the configured max weighted sample index");
    ExpectNear(output.weighted_lateral_error_m,
               0.10F,
               1.0e-6F,
               "samples beyond the configured weighted horizon must not affect output");
}

void TestFarWeightUsesConfiguredExponentialEndpoint() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.min_leading_reference_samples = 4;
    params.bev_control_model.lateral_error_far_weight = 0.25;
    params.bev_control_model.lateral_error_max_weighted_sample_index = 3;
    ls2k::port::BEVReferencePath path = MakePath(params, 4, 0.0F);
    path.sampled_path[3].point.lateral_m = 1.0F;

    const auto output = ComputeLateralError(path, params);
    const float expected = 0.25F / (1.0F + std::pow(0.25F, 1.0F / 3.0F) +
                                    std::pow(0.25F, 2.0F / 3.0F) + 0.25F);
    Expect(output.computed, "four-point leading segment must produce lateral error");
    ExpectNear(output.weighted_lateral_error_m,
               expected,
               1.0e-5F,
               "exponential weighting must reach far weight at the configured endpoint");
}

void TestTurnOutputTargetUsesLateralErrorGainAndUnclampedSpeedScale() {
    ls2k::port::RuntimeParameters params{};
    params.running_speed_target = 100.0;
    params.bev_control_model.lateral_error_to_wheel_delta_gain = 180.0;

    ls2k::legacy::SteeringYawController controller;
    controller.Configure(params);
    ls2k::port::BEVControllerMemory memory{};
    const auto output = controller.ComputeTurnOutputTarget(0.2F, 150.0, memory);

    ExpectNear(output.turn_output_target,
               54.0F,
               1.0e-4F,
               "turn-output target must apply lateral error gain and unclamped speed scale directly");
    ExpectNear(output.speed_scale, 1.5F, 1.0e-6F, "speed scale must not be clamped");
}

void TestGyroTurnUsesGateApprovedGyroValueOnly() {
    ls2k::port::RuntimeParameters params{};
    params.yaw_rate_pid_p = 0.5;
    params.yaw_rate_pid_i = 0.0;
    params.yaw_rate_pid_d = 0.0;

    ls2k::legacy::SteeringYawController controller;
    controller.Configure(params);
    ls2k::port::BEVControllerMemory memory{};
    const auto output = controller.ComputeGyroTurn(10.0F, 2.0F, memory);

    ExpectNear(output.gyro_z, 2.0F, 1.0e-6F, "gyro turn must use the gate-approved gyro value directly");
    ExpectNear(output.gyro_error, -2.0F, 1.0e-6F, "gyro turn feedback must oppose the direct gyro value");
    ExpectNear(output.raw_turn_output,
               9.0F,
               1.0e-6F,
               "gyro turn must add feedback correction without scaling the turn-output target");
}

void TestReferenceControlReadinessUsesHoldSelectedNotReferenceSource() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::port::BEVReferencePath path = MakePath(params, 6, 0.0F, ls2k::port::BEVPathPointSource::kHold);
    const ls2k::port::ReferenceUsability usability = ls2k::legacy::EvaluateReferenceUsability(path, params);
    const ls2k::port::ReferenceLateralErrorEstimate lateral_error =
        ls2k::legacy::ComputeReferenceLateralError(path, usability, params);

    const ls2k::port::ReferenceControlReadiness current =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, lateral_error, false);
    const ls2k::port::ReferenceControlReadiness held =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, lateral_error, true);

    Expect(current.ready && !current.degraded && current.reason == "ok",
           "reference-control readiness must not infer hold from point source");
    Expect(held.ready && held.degraded && held.reason == "reference_hold",
           "reference-control readiness must use the explicit hold_selected input");
}

void TestReferenceControlReadinessRejectsLayerFailures() {
    ls2k::port::ReferenceUsability usability{};
    ls2k::port::ReferenceLateralErrorEstimate lateral_error{};

    ls2k::port::ReferenceControlReadiness readiness =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, lateral_error, false);
    Expect(!readiness.ready && readiness.reason == "reference_unusable",
           "unusable reference must stop before lateral-error readiness");

    usability.usable = true;
    readiness = ls2k::legacy::EvaluateReferenceControlReadiness(usability, lateral_error, false);
    Expect(!readiness.ready && readiness.reason == "lateral_error_uncomputed",
           "uncomputed lateral error must stop reference-control readiness");
}

void TestSafetyGateOwnsProjectorAndLowVoltageVetoes() {
    ls2k::runtime::ControlGateInputs inputs{};
    inputs.perception_published = true;
    inputs.perception_fresh = true;
    inputs.perception_projector_ok = true;
    inputs.reference_control_ready = true;
    inputs.imu_valid = true;
    inputs.encoder_valid = true;
    inputs.now_ms = 100;
    inputs.perception_publish_time_ms = 100;
    inputs.perception_stale_ms = 120;

    inputs.perception_projector_ok = false;
    ls2k::runtime::ControlGateDecision gate = ls2k::runtime::EvaluateControlGate(inputs);
    Expect(gate.veto_active && gate.veto_reason == ls2k::runtime::ControlVetoReason::kPerceptionInvalid,
           "projector health must be owned by the safety gate as perception_invalid");

    inputs.low_voltage_emergency = true;
    inputs.perception_fresh = false;
    gate = ls2k::runtime::EvaluateControlGate(inputs);
    Expect(gate.veto_active && gate.veto_reason == ls2k::runtime::ControlVetoReason::kLowVoltage,
           "low voltage must take priority before stale perception in the safety gate");

    inputs.low_voltage_emergency = false;
    inputs.perception_projector_ok = true;
    gate = ls2k::runtime::EvaluateControlGate(inputs);
    Expect(gate.veto_active && gate.veto_reason == ls2k::runtime::ControlVetoReason::kPerceptionStale,
           "stale perception must be reported when low voltage is not active");

    inputs.perception_fresh = true;
    inputs.imu_valid = false;
    gate = ls2k::runtime::EvaluateControlGate(inputs);
    Expect(gate.veto_active && gate.veto_reason == ls2k::runtime::ControlVetoReason::kImuInvalid,
           "IMU validity must be owned by the safety gate");
}

}  // namespace

int main() {
    try {
        TestUsabilityRequiresConfiguredMinimumLeadingReferenceSamples();
        TestConfiguredMinimumLeadingReferenceSamplesCanChange();
        TestLeadingContinuityIsRequired();
        TestSourceDoesNotAffectUsabilityOrLateralError();
        TestStraightReferenceProducesZeroLateralError();
        TestConstantOffsetReferencePreservesOffset();
        TestNearSamplesHaveMoreWeightThanFarSamples();
        TestGapStopsWeightedSamples();
        TestMaxWeightedSampleIndexStopsFarSamples();
        TestFarWeightUsesConfiguredExponentialEndpoint();
        TestTurnOutputTargetUsesLateralErrorGainAndUnclampedSpeedScale();
        TestGyroTurnUsesGateApprovedGyroValueOnly();
        TestReferenceControlReadinessUsesHoldSelectedNotReferenceSource();
        TestReferenceControlReadinessRejectsLayerFailures();
        TestSafetyGateOwnsProjectorAndLowVoltageVetoes();
    } catch (const TestFailure& failure) {
        std::cerr << "reference_usability_lateral_error_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "reference_usability_lateral_error_test passed\n";
    return EXIT_SUCCESS;
}
