#include <chrono>
#include <csignal>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>

#include "platform/bootstrap.hpp"
#include "port/diagnostics.hpp"
#include "runtime/control_loop.hpp"
#include "runtime/perception_frontend.hpp"
#include "runtime/shutdown.hpp"
#include "runtime/startup.hpp"

namespace {

volatile std::sig_atomic_t g_stop_signal = 0;

int ReadIntEnv(const char* key, int fallback) {
    const char* value = std::getenv(key);
    if (value == nullptr) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string ReadStringEnv(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return std::string(fallback);
    }
    return std::string(value);
}

void HandleStopSignal(int) {
    g_stop_signal = 1;
}

bool RunBenchPwmPulse(ls2k::port::PlatformBundle& platform,
                      ls2k::runtime::RuntimeState& runtime_state,
                      ls2k::port::DiagnosticSink& diagnostics) {
    const int pulse_ms = ReadIntEnv("LS2K_BENCH_PWM_MS", 0);
    if (pulse_ms <= 0) {
        return false;
    }

    const int left_pwm = ReadIntEnv("LS2K_BENCH_PWM_LEFT", 0);
    const int right_pwm = ReadIntEnv("LS2K_BENCH_PWM_RIGHT", left_pwm);
    const int settle_ms = ReadIntEnv("LS2K_BENCH_SETTLE_MS", 80);

    diagnostics.Emit({ls2k::port::DiagnosticLevel::kWarning,
                      "bench.pwm.start",
                      "running bench PWM pulse test with logical_left=" + std::to_string(left_pwm) +
                          " logical_right=" + std::to_string(right_pwm) +
                          " pulse_ms=" + std::to_string(pulse_ms),
                      ls2k::port::NowMs()});

    const ls2k::port::LowVoltageSample power_sample = platform.power->SampleLowVoltage(diagnostics);
    const bool low_voltage_emergency =
        runtime_state.low_voltage_emergency.load() || !power_sample.valid || power_sample.emergency;
    if (low_voltage_emergency) {
        std::ostringstream blocked;
        blocked << "bench PWM pulse blocked by low-voltage fail-safe"
                << " sample_valid=" << (power_sample.valid ? "true" : "false")
                << " sample_emergency=" << (power_sample.emergency ? "true" : "false")
                << " raw=" << power_sample.raw_value;
        diagnostics.Emit({ls2k::port::DiagnosticLevel::kFailSafe,
                          "bench.pwm.blocked.low_voltage",
                          blocked.str(),
                          ls2k::port::NowMs()});
        return true;
    }

    (void)platform.encoder->ReadDelta(diagnostics);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, settle_ms)));
    const ls2k::port::EncoderDelta before = platform.encoder->ReadDelta(diagnostics);

    const ls2k::port::ActuatorCommand pulse = {left_pwm, right_pwm, false};
    const bool apply_ok = platform.motor->Apply(pulse, diagnostics);
    const int first_slice_ms = std::max(1, pulse_ms / 2);
    const int second_slice_ms = std::max(0, pulse_ms - first_slice_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(first_slice_ms));
    const ls2k::port::EncoderDelta during = platform.encoder->ReadDelta(diagnostics);
    std::this_thread::sleep_for(std::chrono::milliseconds(second_slice_ms));
    platform.motor->Disable(diagnostics);

    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, settle_ms)));
    const ls2k::port::EncoderDelta after_first = platform.encoder->ReadDelta(diagnostics);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const ls2k::port::EncoderDelta after_second = platform.encoder->ReadDelta(diagnostics);

    std::ostringstream summary;
    summary << "bench PWM pulse apply_ok=" << (apply_ok ? "true" : "false")
            << " before_valid=" << (before.valid ? "true" : "false")
            << " before_left=" << before.left
            << " before_right=" << before.right
            << " during_valid=" << (during.valid ? "true" : "false")
            << " during_left=" << during.left
            << " during_right=" << during.right
            << " after1_valid=" << (after_first.valid ? "true" : "false")
            << " after1_left=" << after_first.left
            << " after1_right=" << after_first.right
            << " after2_valid=" << (after_second.valid ? "true" : "false")
            << " after2_left=" << after_second.left
            << " after2_right=" << after_second.right;
    diagnostics.Emit({apply_ok ? ls2k::port::DiagnosticLevel::kInfo
                               : ls2k::port::DiagnosticLevel::kFailSafe,
                      "bench.pwm.summary",
                      summary.str(),
                      ls2k::port::NowMs()});
    return true;
}

}  // namespace

int main() {
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    ls2k::port::StdoutDiagnostics diagnostics;
    diagnostics.Info("main.start", "starting ls2k migration runtime");

    ls2k::port::HardwareProfile profile{};
    ls2k::port::RuntimeParameters params{};
    ls2k::runtime::RuntimeState runtime_state{};

    auto param_store = ls2k::platform::MakeParamStore();
    const std::string profile_path =
        ReadStringEnv("LS2K_PROFILE_PATH", "new/config/hardware_profile.json");
    const std::string params_path =
        ReadStringEnv("LS2K_PARAMS_PATH", "new/config/default_params.json");

    if (!param_store->LoadHardwareProfile(profile_path, profile, diagnostics)) {
        diagnostics.Error("main.profile", "failed to load hardware profile");
        return 1;
    }
    if (profile.persistence.mode != ls2k::port::SubsystemMode::kDirectMatch) {
        diagnostics.FailSafe("main.profile.persistence",
                             "phase-1 persistence requires direct-match json-file-store; refusing to load parameters for unsupported mode " +
                                 std::string(ls2k::port::ToString(profile.persistence.mode)) + ":" +
                                 profile.persistence.hook);
        return 1;
    }
    if (!param_store->LoadRuntimeParameters(params_path, params, diagnostics)) {
        diagnostics.Error("main.params", "failed to load runtime parameters");
        return 1;
    }

    ls2k::port::PlatformBundle platform = ls2k::platform::CreatePlatformBundle(profile, diagnostics);
    platform.params = std::move(param_store);

    diagnostics.Info("profile.camera", std::string(ls2k::port::ToString(profile.camera.mode)) + ":" + profile.camera.hook);
    diagnostics.Info("profile.imu", std::string(ls2k::port::ToString(profile.imu.mode)) + ":" + profile.imu.hook);
    diagnostics.Info("profile.encoder",
                     std::string(ls2k::port::ToString(profile.encoder.mode)) + ":" + profile.encoder.hook);
    diagnostics.Info("profile.motor", std::string(ls2k::port::ToString(profile.motor.mode)) + ":" + profile.motor.hook);
    diagnostics.Info("profile.timer", std::string(ls2k::port::ToString(profile.timer.mode)) + ":" + profile.timer.hook);

    if (!ls2k::runtime::RunStartup(profile, params, platform, runtime_state, diagnostics)) {
        diagnostics.FailSafe("main.startup", "startup failed, refusing to arm actuators");
        ls2k::runtime::RunShutdown(platform, runtime_state, diagnostics);
        return 1;
    }

    if (RunBenchPwmPulse(platform, runtime_state, diagnostics)) {
        ls2k::runtime::RunShutdown(platform, runtime_state, diagnostics);
        diagnostics.Info("main.exit", "bench PWM pulse test complete");
        return 0;
    }

    ls2k::runtime::ControlLoop control_loop(platform, profile, runtime_state, diagnostics);
    if (!control_loop.Start(params)) {
        diagnostics.FailSafe("main.control", "control loop start failed");
        ls2k::runtime::RunShutdown(platform, runtime_state, diagnostics);
        return 1;
    }

    ls2k::runtime::PerceptionFrontend perception(
        *platform.camera, *platform.power, runtime_state, diagnostics);
    const int max_frames = ReadIntEnv("LS2K_MAX_FRAMES", 0);
    int processed_frames = 0;
    while (!runtime_state.stop_requested) {
        if (g_stop_signal != 0) {
            runtime_state.stop_requested = true;
            diagnostics.Info("main.signal", "stop signal received, exiting runtime loop");
            break;
        }

        perception.ProcessOneFrame(params);
        ++processed_frames;
        if (max_frames > 0 && processed_frames >= max_frames) {
            runtime_state.stop_requested = true;
            diagnostics.Info("main.frame_limit",
                             "bounded runtime loop reached LS2K_MAX_FRAMES=" + std::to_string(max_frames));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    control_loop.Stop();
    ls2k::runtime::RunShutdown(platform, runtime_state, diagnostics);
    diagnostics.Info("main.exit", "runtime exit complete");
    return 0;
}
