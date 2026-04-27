#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "legacy/pid_control.hpp"
#include "legacy/wheel_target_mixer.hpp"

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
    params.bev_control_model.curvature_to_w_target_gain = 2.0;
    return params;
}

ls2k::port::RuntimeParameters BuildBevAuthorityParams() {
    ls2k::port::RuntimeParameters params{};
    params.Speed_base = 100.0;
    params.pid_turn_camera_use_fuzzy = false;
    params.pid_turn_camera_p = 3000.0;
    params.pid_turn_camera_p_scale = 1.0;
    params.pid_turn_camera_d = 0.0;
    params.pid_turn_gyro_camera_p = 0.5;
    params.pid_turn_gyro_camera_i = 0.0;
    params.pid_turn_gyro_camera_d = 0.0;
    params.P_Mode = 3;
    params.pwm_limit = 5000;
    params.wheel_turn_target_scale = 100.0;
    params.bev_control_model.curvature_to_w_target_gain = 3000.0;
    return params;
}

ls2k::port::PerceptionResult BuildPerception() {
    ls2k::port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.emergency_veto = false;
    perception.lateral_error = 10.0F;
    perception.near_lateral_error = 10.0F;
    perception.control_model.valid = true;
    perception.control_model.curvature_command = 10.0F;
    perception.control_model.steering_gain_scale = 1.0;
    perception.active_module = "straight";
    perception.scene_phase = "idle";
    return perception;
}

void TestCarryOverLivesInRuntimeOwnedMemory() {
    ls2k::legacy::LegacyPidControl pid;
    pid.Configure(BuildParams());

    const ls2k::port::PerceptionResult perception = BuildPerception();
    ls2k::port::LegacySteeringControllerMemory memory{};

    const auto camera_first = pid.ComputeTurnTarget(perception, 77.0, memory);
    ExpectNear(camera_first.camera_p_term, 20.0F, 0.0001F, "camera P term must reflect current frame");
    ExpectNear(camera_first.camera_d_term, 0.0F, 0.0001F, "camera D term must stay disabled in the fused target");
    ExpectNear(camera_first.w_target, 18.0F, 0.0001F, "first w_target must reflect the fused camera target");
    ExpectNear(memory.w_target_last, 18.0F, 0.0001F, "runtime-owned memory must retain w_target_last");
    ExpectNear(memory.camera_error_last, 10.0F, 0.0001F, "runtime-owned memory must retain camera error");

    const auto gyro_first = pid.ComputeGyroTurn(camera_first.w_target, 0.0F, true, memory);
    ExpectNear(gyro_first.raw_turn_output, 28.8F, 0.0001F, "first gyro output must use zeroed carry-over");
    ExpectNear(memory.gyro_error_last, 18.0F, 0.0001F, "runtime-owned memory must retain gyro error");
    ExpectNear(memory.gyro_i_accumulator, 18.0F, 0.0001F, "runtime-owned memory must retain gyro accumulator");

    const auto camera_second = pid.ComputeTurnTarget(perception, 77.0, memory);
    ExpectNear(camera_second.camera_p_term, 20.0F, 0.0001F, "camera P term must stay frame-driven");
    ExpectNear(camera_second.camera_d_term, 0.0F, 0.0001F, "second camera D term must remain disabled");
    ExpectNear(camera_second.w_target, 19.8F, 0.0001F, "second w_target must change only via carried memory");

    const auto gyro_second = pid.ComputeGyroTurn(camera_second.w_target, 0.0F, true, memory);
    ExpectNear(gyro_second.raw_turn_output, 24.48F, 0.0001F,
               "second gyro output must change because prior-cycle memory was retained");

    ls2k::port::LegacySteeringControllerMemory reset_memory{};
    const auto camera_after_reset = pid.ComputeTurnTarget(perception, 77.0, reset_memory);
    const auto gyro_after_reset = pid.ComputeGyroTurn(camera_after_reset.w_target, 0.0F, true, reset_memory);
    ExpectNear(camera_after_reset.w_target, camera_first.w_target, 0.0001F,
               "reset memory must reproduce the first-cycle w_target");
    ExpectNear(gyro_after_reset.raw_turn_output, gyro_first.raw_turn_output, 0.0001F,
               "reset memory must reproduce the first-cycle gyro output");
}

void TestBevMetricOffsetHasTurnAuthority() {
    const ls2k::port::RuntimeParameters params = BuildBevAuthorityParams();
    ls2k::legacy::LegacyPidControl pid;
    pid.Configure(params);

    ls2k::port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.visible_range_m = 4.5F;
    perception.control_model.valid = true;
    perception.control_model.near_lateral_error = 0.10F;
    perception.control_model.far_heading_error = 0.012F;
    perception.control_model.preview_curvature = 0.0F;
    perception.control_model.curvature_command = 0.10F;
    perception.control_model.steering_gain_scale = 1.0;

    ls2k::port::LegacySteeringControllerMemory memory{};
    const auto camera = pid.ComputeTurnTarget(perception, params.Speed_base, memory);
    const auto gyro = pid.ComputeGyroTurn(camera.w_target, 0.0F, true, memory);

    const int applied_turn = static_cast<int>(std::lround(gyro.raw_turn_output));
    ExpectNear(camera.camera_p_term, 300.0F, 0.0001F,
               "retuned camera P must give 10cm right-side BEV lateral offset visible authority");
    if (applied_turn < 120) {
        throw TestFailure{"10cm right-side BEV lateral offset must produce meaningful positive turn output actual=" +
                          std::to_string(applied_turn)};
    }

    ls2k::legacy::WheelTargetMixer mixer;
    mixer.Configure(params);
    const ls2k::legacy::WheelSpeedTargets targets =
        mixer.Compute(params.Speed_base, applied_turn, params.pwm_limit);
    const double split = targets.left - targets.right;
    if (split < 5.0) {
        throw TestFailure{"positive BEV turn must produce a right-turn wheel target split actual=" +
                          std::to_string(split)};
    }

    ls2k::port::RuntimeParameters decoupled_params = params;
    decoupled_params.pid_turn_camera_p = 999999.0;
    ls2k::legacy::LegacyPidControl decoupled_pid;
    decoupled_pid.Configure(decoupled_params);
    ls2k::port::LegacySteeringControllerMemory decoupled_memory{};
    const auto decoupled_camera =
        decoupled_pid.ComputeTurnTarget(perception, decoupled_params.Speed_base, decoupled_memory);
    ExpectNear(decoupled_camera.w_target, camera.w_target, 0.0001F,
               "PID_TURN_CAMERA.P must not scale the BEV curvature command");
}

}  // namespace

int main() {
    try {
        TestCarryOverLivesInRuntimeOwnedMemory();
        TestBevMetricOffsetHasTurnAuthority();
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
