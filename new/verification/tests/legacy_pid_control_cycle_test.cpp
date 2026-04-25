#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/pid_control.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void ExpectNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw TestFailure{message + " actual=" + std::to_string(actual) +
                          " expected=" + std::to_string(expected)};
    }
}

ls2k::port::RuntimeParameters BuildParams() {
    ls2k::port::RuntimeParameters params{};
    params.pid_turn_camera_use_fuzzy = false;
    params.pid_turn_camera_p = 2.0;
    params.pid_turn_camera_p_scale = 1.0;
    params.pid_turn_camera_d = 1.0;
    params.pid_turn_gyro_camera_p = 1.0;
    params.pid_turn_gyro_camera_i = 0.1;
    params.pid_turn_gyro_camera_d = 0.5;
    params.P_Mode = 3;
    return params;
}

ls2k::port::PerceptionResult BuildPerception() {
    ls2k::port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.emergency_veto = false;
    perception.lateral_error = 10.0F;
    perception.highest_line = 40;
    perception.active_module = "straight";
    perception.scene_phase = "idle";
    return perception;
}

void TestCarryOverLivesInRuntimeOwnedMemory() {
    ls2k::legacy::LegacyPidControl pid;
    pid.Configure(BuildParams());

    const ls2k::port::PerceptionResult perception = BuildPerception();
    ls2k::port::LegacySteeringControllerMemory memory{};

    const auto camera_first = pid.ComputeTurnTarget(perception, memory);
    ExpectNear(camera_first.camera_p_term, 20.0F, 0.0001F, "camera P term must reflect current frame");
    ExpectNear(camera_first.camera_d_term, 10.0F, 0.0001F, "camera D term must use prior zero error");
    ExpectNear(camera_first.w_target, 27.0F, 0.0001F, "first w_target must start from zero memory");
    ExpectNear(memory.w_target_last, 27.0F, 0.0001F, "runtime-owned memory must retain w_target_last");
    ExpectNear(memory.camera_error_last, 10.0F, 0.0001F, "runtime-owned memory must retain camera error");

    const auto gyro_first = pid.ComputeGyroTurn(camera_first.w_target, 0.0F, memory);
    ExpectNear(gyro_first.raw_turn_output, 43.2F, 0.0001F, "first gyro output must use zeroed carry-over");
    ExpectNear(memory.gyro_error_last, 27.0F, 0.0001F, "runtime-owned memory must retain gyro error");
    ExpectNear(memory.gyro_i_accumulator, 27.0F, 0.0001F, "runtime-owned memory must retain gyro accumulator");

    const auto camera_second = pid.ComputeTurnTarget(perception, memory);
    ExpectNear(camera_second.camera_p_term, 20.0F, 0.0001F, "camera P term must stay frame-driven");
    ExpectNear(camera_second.camera_d_term, 0.0F, 0.0001F, "second camera D term must observe prior-cycle memory");
    ExpectNear(camera_second.w_target, 20.7F, 0.0001F, "second w_target must change only via carried memory");

    const auto gyro_second = pid.ComputeGyroTurn(camera_second.w_target, 0.0F, memory);
    ExpectNear(gyro_second.raw_turn_output, 22.32F, 0.0001F,
               "second gyro output must change because prior-cycle memory was retained");

    ls2k::port::LegacySteeringControllerMemory reset_memory{};
    const auto camera_after_reset = pid.ComputeTurnTarget(perception, reset_memory);
    const auto gyro_after_reset = pid.ComputeGyroTurn(camera_after_reset.w_target, 0.0F, reset_memory);
    ExpectNear(camera_after_reset.w_target, camera_first.w_target, 0.0001F,
               "reset memory must reproduce the first-cycle w_target");
    ExpectNear(gyro_after_reset.raw_turn_output, gyro_first.raw_turn_output, 0.0001F,
               "reset memory must reproduce the first-cycle gyro output");
}

}  // namespace

int main() {
    try {
        TestCarryOverLivesInRuntimeOwnedMemory();
    } catch (const TestFailure& failure) {
        std::cerr << "legacy_pid_control_cycle_test failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "legacy_pid_control_cycle_test unexpected exception: " << error.what() << "\n";
        return 1;
    }

    std::cout << "legacy_pid_control_cycle_test passed\n";
    return 0;
}
