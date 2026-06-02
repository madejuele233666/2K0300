#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "legacy/wheel_target_mixer.hpp"

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void ExpectNear(double actual, double expected, const std::string& label) {
    constexpr double kTolerance = 1.0e-6;
    if (std::abs(actual - expected) > kTolerance) {
        std::ostringstream error;
        error << label << " mismatch: actual=" << actual << " expected=" << expected;
        throw std::runtime_error(error.str());
    }
}

ls2k::legacy::WheelSpeedTargets ComputeWithPwmLimit(int pwm_limit, int applied_turn_output) {
    (void)pwm_limit;
    ls2k::legacy::WheelTargetMixer mixer;
    return mixer.Compute(140.0, applied_turn_output);
}

void TestPwmLimitDoesNotScaleWheelTargets() {
    const ls2k::legacy::WheelSpeedTargets first = ComputeWithPwmLimit(5000, 28);
    const ls2k::legacy::WheelSpeedTargets second = ComputeWithPwmLimit(9000, 28);

    ExpectNear(first.left, second.left, "left target independent of pwm_limit");
    ExpectNear(first.right, second.right, "right target independent of pwm_limit");
}

void TestPositiveTurnOutputSplitsWheelTargets() {
    const ls2k::legacy::WheelSpeedTargets targets = ComputeWithPwmLimit(5000, 28);

    ExpectNear(targets.left, 168.0, "positive turn left target");
    ExpectNear(targets.right, 112.0, "positive turn right target");
}

void TestNegativeTurnOutputSplitsWheelTargets() {
    const ls2k::legacy::WheelSpeedTargets negative = ComputeWithPwmLimit(5000, -28);
    ExpectNear(negative.left, 112.0, "negative turn left target");
    ExpectNear(negative.right, 168.0, "negative turn right target");
}

void TestZeroTurnOutputKeepsBothWheelsAtBaseTarget() {
    const ls2k::legacy::WheelSpeedTargets zero = ComputeWithPwmLimit(5000, 0);
    ExpectNear(zero.left, 140.0, "zero turn left target");
    ExpectNear(zero.right, 140.0, "zero turn right target");
}

void TestLargeTurnOnlyClampsNegativeWheelSpeed() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets = mixer.Compute(50.0, -100);

    ExpectNear(targets.left, 0.0, "large turn left target");
    ExpectNear(targets.right, 150.0, "large turn right target");
    Require(targets.right > 50.0, "large turn must accelerate the outer wheel");
}

void TestTurnBeyondHalfSpeedStillChangesBothTargets() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets = mixer.Compute(250.0, 200);

    ExpectNear(targets.left, 450.0, "beyond half-speed turn left target");
    ExpectNear(targets.right, 50.0, "beyond half-speed turn right target");
}

void TestPositiveTurnUsesAccelLeftAndDecelRightScales() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(140.0, 20, {2.0, 0.5});

    ExpectNear(targets.left, 180.0, "positive turn scaled accel-side left target");
    ExpectNear(targets.right, 130.0, "positive turn scaled decel-side right target");
}

void TestNegativeTurnUsesAccelRightAndDecelLeftScales() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(140.0, -20, {2.0, 0.5});

    ExpectNear(targets.left, 130.0, "negative turn scaled decel-side left target");
    ExpectNear(targets.right, 180.0, "negative turn scaled accel-side right target");
}

void TestZeroDecelScaleOnlyChangesAccelSide() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(140.0, 20, {1.5, 0.0});

    ExpectNear(targets.left, 170.0, "zero decel scale positive turn left target");
    ExpectNear(targets.right, 140.0, "zero decel scale positive turn right target");
}

void TestZeroAccelScaleOnlyChangesDecelSide() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(140.0, -20, {0.0, 1.5});

    ExpectNear(targets.left, 110.0, "zero accel scale negative turn left target");
    ExpectNear(targets.right, 140.0, "zero accel scale negative turn right target");
}

void TestScaledDecelStillClampsNegativeWheelSpeed() {
    ls2k::legacy::WheelTargetMixer mixer;
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(50.0, 40, {2.0, 2.0});

    ExpectNear(targets.left, 130.0, "scaled large turn left target");
    ExpectNear(targets.right, 0.0, "scaled large turn right target");
}

}  // namespace

int main() {
    try {
        TestPwmLimitDoesNotScaleWheelTargets();
        TestPositiveTurnOutputSplitsWheelTargets();
        TestNegativeTurnOutputSplitsWheelTargets();
        TestZeroTurnOutputKeepsBothWheelsAtBaseTarget();
        TestLargeTurnOnlyClampsNegativeWheelSpeed();
        TestTurnBeyondHalfSpeedStillChangesBothTargets();
        TestPositiveTurnUsesAccelLeftAndDecelRightScales();
        TestNegativeTurnUsesAccelRightAndDecelLeftScales();
        TestZeroDecelScaleOnlyChangesAccelSide();
        TestZeroAccelScaleOnlyChangesDecelSide();
        TestScaledDecelStillClampsNegativeWheelSpeed();
    } catch (const std::exception& error) {
        std::cerr << "wheel_target_mixer_test failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "wheel_target_mixer_test passed\n";
    return EXIT_SUCCESS;
}
