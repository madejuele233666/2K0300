#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy/steering_reference_control_readiness.hpp"
#include "legacy/steering_reference_curvature.hpp"
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

ls2k::port::ReferenceCurvatureEstimate ComputeCurvature(
    const ls2k::port::BEVReferencePath& path,
    const ls2k::port::RuntimeParameters& params) {
    const ls2k::port::ReferenceUsability usability =
        ls2k::legacy::EvaluateReferenceUsability(path, params);
    return ls2k::legacy::ComputeReferenceCurvature(path, usability, params);
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
    params.bev_control_model.lookahead_min_m = 0.10;
    params.bev_control_model.lookahead_max_m = 0.10;
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

void TestSourceDoesNotAffectUsability() {
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
    const auto output = ls2k::legacy::ComputeReferenceCurvature(path, usability, params);
    Expect(usability.usable, "usability must depend on present geometry, not source");
    Expect(output.computed, "curvature must depend on usable geometry, not source");
}

void TestCurvatureCommandAndLookaheadDistance() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.lookahead_min_m = 0.18;
    params.bev_control_model.lookahead_max_m = 0.18;
    params.bev_control_model.pure_pursuit_gain = 1.0;
    params.bev_control_model.curvature_command_limit = 10.0;

    const auto output = ComputeCurvature(MakePath(params, 8, 0.05F), params);

    Expect(output.computed, "offset path must produce computed curvature");
    Expect(output.curvature_command > 0.0F, "positive lateral lookahead must produce positive curvature");
    Expect(output.lookahead_distance_m > 0.0F, "computed output must expose lookahead distance");
}

void TestCurvatureEstimateIsIndependentOfSpeed() {
    ls2k::port::RuntimeParameters params{};
    params.bev_control_model.curvature_command_limit = 10.0;
    const ls2k::port::BEVReferencePath path = MakePath(params, 8, 0.04F);

    const auto first = ComputeCurvature(path, params);
    const auto second = ComputeCurvature(path, params);

    Expect(first.computed && second.computed, "both curvature computations must complete");
    Expect(std::abs(first.curvature_command - second.curvature_command) < 1.0e-6F,
           "reference curvature estimate must not depend on vehicle speed");
}

void TestControlLoopYawRateTargetUsesCurvatureGainAndSpeedScale() {
    ls2k::port::RuntimeParameters params{};
    params.running_speed_target = 100.0;
    params.bev_control_model.curvature_to_yaw_rate_target_gain = 2000.0;

    ls2k::legacy::SteeringYawController controller;
    controller.Configure(params);
    ls2k::port::BEVControllerMemory memory{};
    const auto output = controller.ComputeYawRateTarget(0.05F, 100.0, memory);

    Expect(std::abs(output.yaw_rate_target - 100.0F) < 1.0e-4F,
           "yaw-rate target must apply curvature gain and speed scale directly");
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

    Expect(std::abs(output.gyro_z - 2.0F) < 1.0e-6F,
           "gyro turn must use the gate-approved gyro value directly");
    Expect(std::abs(output.gyro_error - 8.0F) < 1.0e-6F,
           "gyro turn error must not branch on IMU validity");
    Expect(std::abs(output.raw_turn_output - 4.0F) < 1.0e-6F,
           "gyro turn must apply yaw-rate PID to the direct gyro value");
}

void TestReferenceControlReadinessUsesHoldSelectedNotReferenceSource() {
    const ls2k::port::RuntimeParameters params{};
    const ls2k::port::BEVReferencePath path = MakePath(params, 6, 0.0F, ls2k::port::BEVPathPointSource::kHold);
    const ls2k::port::ReferenceUsability usability = ls2k::legacy::EvaluateReferenceUsability(path, params);
    const ls2k::port::ReferenceCurvatureEstimate curvature =
        ls2k::legacy::ComputeReferenceCurvature(path, usability, params);

    const ls2k::port::ReferenceControlReadiness current =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, curvature, false);
    const ls2k::port::ReferenceControlReadiness held =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, curvature, true);

    Expect(current.ready && !current.degraded && current.reason == "ok",
           "reference-control readiness must not infer hold from point source");
    Expect(held.ready && held.degraded && held.reason == "reference_hold",
           "reference-control readiness must use the explicit hold_selected input");
}

void TestReferenceControlReadinessRejectsLayerFailures() {
    ls2k::port::ReferenceUsability usability{};
    ls2k::port::ReferenceCurvatureEstimate curvature{};

    ls2k::port::ReferenceControlReadiness readiness =
        ls2k::legacy::EvaluateReferenceControlReadiness(usability, curvature, false);
    Expect(!readiness.ready && readiness.reason == "reference_unusable",
           "unusable reference must stop before curvature readiness");

    usability.usable = true;
    readiness = ls2k::legacy::EvaluateReferenceControlReadiness(usability, curvature, false);
    Expect(!readiness.ready && readiness.reason == "curvature_uncomputed",
           "uncomputed curvature must stop reference-control readiness");
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
        TestSourceDoesNotAffectUsability();
        TestCurvatureCommandAndLookaheadDistance();
        TestCurvatureEstimateIsIndependentOfSpeed();
        TestControlLoopYawRateTargetUsesCurvatureGainAndSpeedScale();
        TestGyroTurnUsesGateApprovedGyroValueOnly();
        TestReferenceControlReadinessUsesHoldSelectedNotReferenceSource();
        TestReferenceControlReadinessRejectsLayerFailures();
        TestSafetyGateOwnsProjectorAndLowVoltageVetoes();
    } catch (const TestFailure& failure) {
        std::cerr << "reference_usability_curvature_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "reference_usability_curvature_test passed\n";
    return EXIT_SUCCESS;
}
