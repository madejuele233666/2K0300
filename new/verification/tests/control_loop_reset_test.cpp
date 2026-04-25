#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "port/platform_adapter.hpp"
#include "runtime/control_loop.hpp"

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
    if (std::fabs(actual - expected) > tolerance) {
        throw TestFailure{message + " actual=" + std::to_string(actual) +
                          " expected=" + std::to_string(expected)};
    }
}

struct RecordingDiagnostics final : ls2k::port::DiagnosticSink {
    std::vector<ls2k::port::DiagnosticEvent> events{};

    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }
};

class ImuMock final : public ls2k::port::IImuAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override { return true; }
    ls2k::port::ImuSample Read(ls2k::port::DiagnosticSink&) override { return sample; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return true; }

    ls2k::port::ImuSample sample{true, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0};
};

class EncoderMock final : public ls2k::port::IEncoderAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override { return true; }
    ls2k::port::EncoderDelta ReadDelta(ls2k::port::DiagnosticSink&) override { return sample; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return true; }

    ls2k::port::EncoderDelta sample{true, 0, 0, 0};
};

class MotorMock final : public ls2k::port::IMotorAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override { return true; }
    bool Apply(const ls2k::port::ActuatorCommand& command, ls2k::port::DiagnosticSink&) override {
        applied_commands.push_back(command);
        return true;
    }
    void Disable(ls2k::port::DiagnosticSink&) override { ++disable_calls; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return true; }

    int disable_calls = 0;
    std::vector<ls2k::port::ActuatorCommand> applied_commands{};
};

class TimerMock final : public ls2k::port::ITimerAdapter {
public:
    bool Start(const ls2k::port::SubsystemProfile&,
               uint32_t,
               std::function<void()> callback,
               std::function<void()> on_failure,
               ls2k::port::DiagnosticSink&) override {
        running_ = true;
        callback_ = std::move(callback);
        on_failure_ = std::move(on_failure);
        return true;
    }

    void Stop(ls2k::port::DiagnosticSink&) override {
        running_ = false;
    }

    bool Running() const override { return running_; }

    void Fire() {
        if (running_ && callback_) {
            callback_();
        }
    }

private:
    bool running_ = false;
    std::function<void()> callback_{};
    std::function<void()> on_failure_{};
};

class PowerMock final : public ls2k::port::IPowerMonitorAdapter {
public:
    bool Initialize(ls2k::port::DiagnosticSink&) override { return true; }
    ls2k::port::LowVoltageSample SampleLowVoltage(ls2k::port::DiagnosticSink&) override { return sample; }
    bool Ready() const override { return true; }

    ls2k::port::LowVoltageSample sample{true, false, 1200, 0, 0, "mock"};
};

struct TestRig {
    ls2k::port::PlatformBundle bundle{};
    ls2k::port::HardwareProfile profile{};
    ls2k::runtime::RuntimeState state{};
    RecordingDiagnostics diagnostics{};
    TimerMock* timer = nullptr;
    MotorMock* motor = nullptr;
    ImuMock* imu = nullptr;
    EncoderMock* encoder = nullptr;
    PowerMock* power = nullptr;

    TestRig() {
        profile.motor.mode = ls2k::port::SubsystemMode::kDirectMatch;
        profile.timer.mode = ls2k::port::SubsystemMode::kDirectMatch;

        auto timer_owner = std::make_unique<TimerMock>();
        auto motor_owner = std::make_unique<MotorMock>();
        auto imu_owner = std::make_unique<ImuMock>();
        auto encoder_owner = std::make_unique<EncoderMock>();
        auto power_owner = std::make_unique<PowerMock>();

        timer = timer_owner.get();
        motor = motor_owner.get();
        imu = imu_owner.get();
        encoder = encoder_owner.get();
        power = power_owner.get();

        bundle.timer = std::move(timer_owner);
        bundle.motor = std::move(motor_owner);
        bundle.imu = std::move(imu_owner);
        bundle.encoder = std::move(encoder_owner);
        bundle.power = std::move(power_owner);

        state.startup_complete = true;
        state.degraded_startup = false;
    }
};

ls2k::port::RuntimeParameters BuildParams() {
    ls2k::port::RuntimeParameters params{};
    params.Speed_base = 60.0;
    params.control_period_ms = 5;
    params.control_snapshot_emit_interval_ms = 1;
    params.motion_unveto_confirm_cycles = 1;
    params.motion_spinup_ms = 600;
    params.motion_turn_limit_spinup = 1.0;
    params.pid_turn_camera_use_fuzzy = false;
    params.pid_turn_camera_p = 2.0;
    params.pid_turn_camera_p_scale = 1.0;
    params.pid_turn_camera_d = 1.0;
    params.pid_turn_gyro_camera_p = 1.0;
    params.pid_turn_gyro_camera_i = 0.0;
    params.pid_turn_gyro_camera_d = 0.0;
    params.raw_turn_output_limit = 3000;
    params.pwm_limit = 9000;
    params.wheel_turn_target_scale = 35.0;
    params.prohibit_reverse_pwm = false;
    return params;
}

void ExpectCleanBaseline(const ls2k::runtime::RuntimeState& state) {
    Expect(state.steering_state.highest_line == 0, "highest_line baseline must be zero");
    Expect(state.steering_state.farthest_line == 0, "farthest_line baseline must be zero");
    Expect(state.steering_state.steering_reference_col == 160,
           "steering_reference_col baseline must be 160");
    Expect(state.steering_state.active_module == "straight", "active_module baseline must be straight");
    Expect(state.steering_state.scene_phase == "idle", "scene_phase baseline must be idle");
    Expect(state.steering_state.scene_override_source == "none",
           "scene_override_source baseline must be none");
    Expect(state.steering_state.roadblock_interface_state == "supported_not_implemented",
           "roadblock interface baseline must be supported_not_implemented");
    Expect(state.steering_state.last_special_scene_correction == "none",
           "last_special_scene_correction baseline must be none");
    Expect(!state.steering_state.roadblock_active, "roadblock baseline must be inactive");
    ExpectNear(state.steering_state.controller_memory.w_target_last, 0.0F, 0.0001F,
               "w_target_last baseline must be zero");
    ExpectNear(state.steering_state.controller_memory.camera_error_last, 0.0F, 0.0001F,
               "camera_error_last baseline must be zero");
    ExpectNear(state.steering_state.controller_memory.gyro_error_last, 0.0F, 0.0001F,
               "gyro_error_last baseline must be zero");
    ExpectNear(state.steering_state.controller_memory.gyro_i_accumulator, 0.0F, 0.0001F,
               "gyro_i_accumulator baseline must be zero");
}

void TestStopResetsRuntimeOwnedSteeringState() {
    TestRig rig;
    ls2k::runtime::ControlLoop loop(rig.bundle, rig.profile, rig.state, rig.diagnostics);
    const ls2k::port::RuntimeParameters params = BuildParams();

    Expect(loop.Start(params), "control loop should start in the test rig");

    rig.state.steering_state.highest_line = 77;
    rig.state.steering_state.farthest_line = 65;
    rig.state.steering_state.steering_reference_col = 109;
    rig.state.steering_state.active_module = "bend";
    rig.state.steering_state.scene_phase = "tracking";
    rig.state.steering_state.scene_override_source = "scene_module";
    rig.state.steering_state.roadblock_interface_state = "unexpected";
    rig.state.steering_state.last_special_scene_correction = "bend_bias";
    rig.state.steering_state.roadblock_active = true;
    rig.state.steering_state.controller_memory.w_target_last = 123.0F;
    rig.state.steering_state.controller_memory.camera_error_last = 45.0F;
    rig.state.steering_state.controller_memory.gyro_error_last = 67.0F;
    rig.state.steering_state.controller_memory.gyro_i_accumulator = 89.0F;
    rig.state.last_command = {111, 222, false};
    rig.state.control_debug_snapshot.valid = true;
    rig.state.control_debug_snapshot.raw_turn_output = 456;
    rig.state.control_debug_snapshot.applied_turn_output = 321;
    rig.state.control_observation.actuators_armed = true;
    rig.state.motion_state.phase = ls2k::runtime::MotionPhase::kRunning;

    loop.Stop();

    ExpectCleanBaseline(rig.state);
    Expect(rig.state.motion_state.phase == ls2k::runtime::MotionPhase::kDisarmed,
           "stop must restore DISARMED phase");
    Expect(!rig.state.control_debug_snapshot.valid, "stop must clear debug snapshot validity");
    Expect(rig.state.control_debug_snapshot.steering.active_module == "straight",
           "cleared steering snapshot must fall back to straight module");
    Expect(rig.state.control_debug_snapshot.steering.scene_phase == "idle",
           "cleared steering snapshot must fall back to idle scene");
    Expect(rig.state.control_debug_snapshot.raw_turn_output == 0,
           "cleared debug snapshot must zero raw turn");
    Expect(rig.state.control_debug_snapshot.applied_turn_output == 0,
           "cleared debug snapshot must zero applied turn");
    Expect(rig.state.last_command.left_pwm == 0 && rig.state.last_command.right_pwm == 0 &&
               rig.state.last_command.emergency_stop,
           "stop must clear the last applied command");
    Expect(rig.motor->disable_calls >= 1, "stop must disable the motor adapter");
}

void TestRestartFirstTickIsZeroAndNextTickRecomputesFromZeroMemory() {
    TestRig rig;
    const ls2k::port::RuntimeParameters params = BuildParams();
    ls2k::runtime::ControlLoop loop(rig.bundle, rig.profile, rig.state, rig.diagnostics);

    Expect(loop.Start(params), "initial control loop start should succeed");
    rig.state.steering_state.controller_memory.w_target_last = 100.0F;
    rig.state.steering_state.controller_memory.camera_error_last = 7.0F;
    rig.state.steering_state.controller_memory.gyro_error_last = 11.0F;
    rig.state.steering_state.controller_memory.gyro_i_accumulator = 50.0F;
    rig.state.steering_state.active_module = "zebra";
    loop.Stop();

    Expect(loop.Start(params), "restart after stop should succeed");
    ExpectCleanBaseline(rig.state);

    const std::uint64_t now_ms = ls2k::port::NowMs();
    rig.state.perception.published = true;
    rig.state.perception.fresh = true;
    rig.state.perception.emergency_veto = false;
    rig.state.perception.low_voltage_veto = false;
    rig.state.perception.threshold_veto = false;
    rig.state.perception.geometry_veto = false;
    rig.state.perception.frame_id = 1;
    rig.state.perception.capture_time_ms = now_ms;
    rig.state.perception.publish_time_ms = now_ms;
    rig.state.perception.lateral_error = 10.0F;
    rig.state.perception.highest_line = 30;
    rig.state.perception.farthest_line = 50;
    rig.state.perception.steering_reference_col = 170;
    rig.state.perception.active_module = "bend";
    rig.state.perception.scene_phase = "tracking";
    rig.state.perception.scene_override_source = "lane_geometry";
    rig.state.perception.last_special_scene_correction = "bend_bias";
    rig.state.imu.capture_time_ms = now_ms;
    rig.state.encoder.capture_time_ms = now_ms;
    rig.state.motion_intent.start_requested = true;
    rig.state.motion_intent.stop_requested = false;

    rig.timer->Fire();

    Expect(rig.state.control_debug_snapshot.valid, "first post-reset tick must publish a snapshot");
    Expect(rig.state.control_debug_snapshot.motion_phase == ls2k::runtime::MotionPhase::kStartRequested,
           "first post-reset tick should stage through START_REQUESTED");
    Expect(rig.state.control_debug_snapshot.raw_turn_output == 0,
           "first post-reset tick must not inherit raw turn from the prior run");
    Expect(rig.state.control_debug_snapshot.applied_turn_output == 0,
           "first post-reset tick must not inherit applied turn from the prior run");
    ExpectCleanBaseline(rig.state);

    rig.timer->Fire();

    Expect(rig.state.control_debug_snapshot.motion_phase == ls2k::runtime::MotionPhase::kSpinup,
           "second post-reset tick should enter SPINUP once the gate confirms");
    Expect(rig.state.control_debug_snapshot.raw_turn_output == 27,
           "post-reset restart tick must recompute raw turn from zero memory");
    Expect(rig.state.control_debug_snapshot.applied_turn_output == 27,
           "post-reset restart tick must recompute applied turn from zero memory");
    ExpectNear(rig.state.steering_state.controller_memory.w_target_last, 27.0F, 0.0001F,
               "w_target_last must be recomputed from zero memory");
    ExpectNear(rig.state.steering_state.controller_memory.camera_error_last, 10.0F, 0.0001F,
               "camera_error_last must reflect the new frame only");
    ExpectNear(rig.state.steering_state.controller_memory.gyro_error_last, 27.0F, 0.0001F,
               "gyro_error_last must reflect the new frame only");
    ExpectNear(rig.state.steering_state.controller_memory.gyro_i_accumulator, 27.0F, 0.0001F,
               "gyro accumulator must restart from zero memory");
    Expect(rig.state.control_debug_snapshot.steering.active_module == "bend",
           "post-reset restart tick must use the new frame's module");
    Expect(rig.state.control_debug_snapshot.steering.scene_phase == "tracking",
           "post-reset restart tick must use the new frame's scene phase");
    Expect(!rig.motor->applied_commands.empty(), "post-reset restart tick should reach the motor apply path");
}

}  // namespace

int main() {
    try {
        TestStopResetsRuntimeOwnedSteeringState();
        TestRestartFirstTickIsZeroAndNextTickRecomputesFromZeroMemory();
    } catch (const TestFailure& failure) {
        std::cerr << "control_loop_reset_test failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "control_loop_reset_test unexpected exception: " << error.what() << "\n";
        return 1;
    }

    std::cout << "control_loop_reset_test passed\n";
    return 0;
}
