#include <chrono>
#include <csignal>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "platform/bootstrap.hpp"
#include "port/diagnostics.hpp"
#include "runtime/assistant_service.hpp"
#include "runtime/control_loop.hpp"
#include "runtime/perception_frontend.hpp"
#include "runtime/shutdown.hpp"
#include "runtime/startup.hpp"

namespace {

volatile std::sig_atomic_t g_exit_signal = 0;
volatile std::sig_atomic_t g_force_exit_signal = 0;
volatile std::sig_atomic_t g_start_signal = 0;
volatile std::sig_atomic_t g_reset_signal = 0;

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

std::optional<bool> ReadBoolEnv(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    const std::string token(value);
    if (token == "1" || token == "true" || token == "TRUE" || token == "yes" || token == "on") {
        return true;
    }
    if (token == "0" || token == "false" || token == "FALSE" || token == "no" || token == "off") {
        return false;
    }
    return std::nullopt;
}

struct AutomationConfig {
    bool auto_start = false;
    int auto_start_delay_ms = 0;
    int auto_stop_after_ms = 0;
    bool auto_reset_fault = false;
    bool emit_frame_progress = false;
};

struct MotionSnapshot {
    ls2k::runtime::MotionPhase phase = ls2k::runtime::MotionPhase::kDisarmed;
    bool reset_ready = false;
    bool exit_requested = false;
};

void HandleExitSignal(int) {
    g_exit_signal = 1;
}

void HandleForceExitSignal(int) {
    g_force_exit_signal = 1;
}

void HandleStartSignal(int) {
    g_start_signal = 1;
}

void HandleResetSignal(int) {
    g_reset_signal = 1;
}

AutomationConfig LoadAutomationConfig() {
    AutomationConfig config{};
    config.auto_start = ReadBoolEnv("LS2K_AUTO_START").value_or(false);
    config.auto_start_delay_ms = std::max(0, ReadIntEnv("LS2K_AUTO_START_DELAY_MS", 0));
    config.auto_stop_after_ms = std::max(0, ReadIntEnv("LS2K_AUTO_STOP_AFTER_MS", 0));
    config.auto_reset_fault = ReadBoolEnv("LS2K_AUTO_RESET_FAULT").value_or(false);
    config.emit_frame_progress = ReadBoolEnv("LS2K_EMIT_FRAME_PROGRESS").value_or(false);
    return config;
}

MotionSnapshot ReadMotionSnapshot(ls2k::runtime::RuntimeState& state) {
    std::lock_guard<std::mutex> lock(state.shared_mutex);
    MotionSnapshot snapshot{};
    snapshot.phase = state.motion_state.phase;
    snapshot.reset_ready = state.control_observation.motion_reset_ready;
    snapshot.exit_requested = state.exit_requested;
    return snapshot;
}

void RequestStart(ls2k::runtime::RuntimeState& state,
                  ls2k::port::DiagnosticSink& diagnostics,
                  const std::string& source) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        if (!state.motion_intent.start_requested || state.motion_intent.stop_requested) {
            state.motion_intent.start_requested = true;
            state.motion_intent.stop_requested = false;
            changed = true;
        }
    }
    if (changed) {
        diagnostics.Emit({ls2k::port::DiagnosticLevel::kInfo,
                          "motion.start.requested",
                          "motion start requested by " + source,
                          ls2k::port::NowMs()});
    }
}

void RequestControlledStop(ls2k::runtime::RuntimeState& state,
                           ls2k::port::DiagnosticSink& diagnostics,
                           const std::string& source) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        if (!state.exit_requested) {
            state.exit_requested = true;
            changed = true;
        }
        state.motion_intent.stop_requested = true;
        state.motion_intent.start_requested = false;
    }
    if (changed) {
        diagnostics.Emit({ls2k::port::DiagnosticLevel::kInfo,
                          "motion.stop.requested",
                          "controlled stop requested by " + source,
                          ls2k::port::NowMs()});
    }
}

void RequestFaultReset(ls2k::runtime::RuntimeState& state,
                       ls2k::port::DiagnosticSink& diagnostics,
                       const std::string& source) {
    bool accepted = false;
    bool already_pending = false;
    {
        std::lock_guard<std::mutex> lock(state.shared_mutex);
        if (state.motion_state.phase == ls2k::runtime::MotionPhase::kFailSafeLatched) {
            already_pending = state.motion_intent.reset_fault_requested;
            state.motion_intent.reset_fault_requested = true;
            accepted = !already_pending;
        } else {
            state.motion_intent.reset_fault_requested = false;
        }
    }
    if (!accepted) {
        diagnostics.Emit({ls2k::port::DiagnosticLevel::kWarning,
                          "motion.failsafe.reset_ignored",
                          already_pending ? "fail-safe reset already pending for current fault episode"
                                          : "fail-safe reset ignored because runtime is not latched",
                          ls2k::port::NowMs()});
        return;
    }
    diagnostics.Emit({ls2k::port::DiagnosticLevel::kInfo,
                      "motion.failsafe.reset_requested",
                      "fail-safe reset requested by " + source,
                      ls2k::port::NowMs()});
}

void EmitHarnessContext(ls2k::port::DiagnosticSink& diagnostics, const AutomationConfig& config) {
    std::ostringstream summary;
    summary << "automation_context auto_start=" << (config.auto_start ? "true" : "false")
            << " auto_start_delay_ms=" << config.auto_start_delay_ms
            << " auto_stop_after_ms=" << config.auto_stop_after_ms
            << " auto_reset_fault=" << (config.auto_reset_fault ? "true" : "false")
            << " emit_frame_progress=" << (config.emit_frame_progress ? "true" : "false");
    diagnostics.Emit({ls2k::port::DiagnosticLevel::kInfo,
                      "main.harness_context",
                      summary.str(),
                      ls2k::port::NowMs()});
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
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleExitSignal);
    std::signal(SIGTERM, HandleForceExitSignal);
    std::signal(SIGUSR1, HandleResetSignal);
    std::signal(SIGUSR2, HandleStartSignal);

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
    ls2k::runtime::AssistantService assistant_service;
    assistant_service.Start(params, diagnostics);
    const AutomationConfig automation = LoadAutomationConfig();
    EmitHarnessContext(diagnostics, automation);

    const uint64_t loop_start_ms = ls2k::port::NowMs();
    int processed_frames = 0;
    bool auto_reset_sent = false;

    while (!runtime_state.stop_requested) {
        if (g_start_signal != 0) {
            g_start_signal = 0;
            RequestStart(runtime_state, diagnostics, "SIGUSR2");
        }
        if (g_reset_signal != 0) {
            g_reset_signal = 0;
            RequestFaultReset(runtime_state, diagnostics, "SIGUSR1");
        }
        if (g_exit_signal != 0) {
            g_exit_signal = 0;
            RequestControlledStop(runtime_state, diagnostics, "signal");
        }
        if (g_force_exit_signal != 0) {
            g_force_exit_signal = 0;
            RequestControlledStop(runtime_state, diagnostics, "SIGTERM");
            runtime_state.stop_requested = true;
            diagnostics.Warn("main.exit.forced",
                             "forced shutdown requested by SIGTERM; exiting without waiting for DISARMED");
            break;
        }

        const uint64_t now_ms = ls2k::port::NowMs();
        const uint64_t elapsed_ms = now_ms >= loop_start_ms ? now_ms - loop_start_ms : 0;
        if (automation.auto_start && !runtime_state.automation_start_fired &&
            elapsed_ms >= static_cast<uint64_t>(automation.auto_start_delay_ms)) {
            runtime_state.automation_start_fired = true;
            RequestStart(runtime_state, diagnostics, "LS2K_AUTO_START");
        }

        perception.ProcessOneFrame(params);
        assistant_service.Tick(runtime_state, diagnostics);
        ++processed_frames;
        if (automation.emit_frame_progress) {
            diagnostics.Emit({ls2k::port::DiagnosticLevel::kInfo,
                              "main.frame.processed",
                              "processed_frames=" + std::to_string(processed_frames),
                              now_ms});
        }

        if (automation.auto_stop_after_ms > 0 &&
            elapsed_ms >= static_cast<uint64_t>(automation.auto_stop_after_ms)) {
            RequestControlledStop(runtime_state, diagnostics, "LS2K_AUTO_STOP_AFTER_MS");
        }

        const MotionSnapshot motion = ReadMotionSnapshot(runtime_state);
        if (automation.auto_reset_fault && motion.phase == ls2k::runtime::MotionPhase::kFailSafeLatched &&
            motion.reset_ready && !auto_reset_sent) {
            RequestFaultReset(runtime_state, diagnostics, "LS2K_AUTO_RESET_FAULT");
            auto_reset_sent = true;
        }
        if (motion.phase != ls2k::runtime::MotionPhase::kFailSafeLatched) {
            auto_reset_sent = false;
        }

        if (motion.exit_requested && motion.phase == ls2k::runtime::MotionPhase::kDisarmed) {
            runtime_state.stop_requested = true;
            diagnostics.Info("main.exit.ready", "controlled stop reached DISARMED; process may now exit");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    control_loop.Stop();
    ls2k::runtime::RunShutdown(platform, runtime_state, diagnostics);
    diagnostics.Info("main.exit", "runtime exit complete");
    return 0;
}
