#include "runtime/startup.hpp"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>

namespace ls2k::runtime {
namespace {

std::optional<bool> ReadBoolEnv(const char* key) {
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return std::nullopt;
    }
    std::string token(raw);
    for (char& c : token) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (token == "1" || token == "true" || token == "yes" || token == "on") {
        return true;
    }
    if (token == "0" || token == "false" || token == "no" || token == "off") {
        return false;
    }
    return std::nullopt;
}

bool RequireReady(const char* subsystem,
                  const port::SubsystemProfile& profile,
                  bool ready,
                  bool degraded_startup,
                  port::DiagnosticSink& diagnostics) {
    if (ready) {
        return true;
    }
    if (degraded_startup && !port::IsEnabled(profile)) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          std::string("startup.") + subsystem + ".disabled.degraded",
                          std::string(subsystem) +
                              " is disabled by profile and degraded startup mode is enabled",
                          port::NowMs()});
        return true;
    }
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      std::string("startup.") + subsystem + ".not_ready",
                      std::string(subsystem) + " adapter initialized but not ready",
                      port::NowMs()});
    return false;
}

bool RequireDirectMatch(const char* subsystem,
                        const port::SubsystemProfile& profile,
                        bool degraded_startup,
                        port::DiagnosticSink& diagnostics) {
    if (profile.mode == port::SubsystemMode::kDirectMatch) {
        return true;
    }
    if (degraded_startup) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          std::string("startup.profile.") + subsystem + ".degraded",
                          std::string(subsystem) + " profile is " + port::ToString(profile.mode) + ":" +
                              profile.hook + " (allowed only in degraded startup mode)",
                          port::NowMs()});
        return true;
    }
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      std::string("startup.profile.") + subsystem + ".not_direct_match",
                      std::string(subsystem) +
                          " must be direct-match in phase-1 runtime; set LS2K_ALLOW_DEGRADED_STARTUP=1 for explicit diagnostic startup",
                      port::NowMs()});
    return false;
}

bool ValidateProfileContracts(const port::HardwareProfile& profile,
                              bool degraded_startup,
                              port::DiagnosticSink& diagnostics) {
    if (!port::IsEnabled(profile.persistence)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.profile.persistence.disabled",
                          "persistence subsystem is disabled in hardware profile; startup cannot proceed",
                          port::NowMs()});
        return false;
    }
    if (profile.persistence.mode == port::SubsystemMode::kAdaptationHook) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.profile.persistence.hook",
                          "persistence adaptation hook is not implemented in phase 1: " + profile.persistence.hook,
                          port::NowMs()});
        return false;
    }

    if (!port::IsEnabled(profile.timer)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.profile.timer.disabled",
                          "timer subsystem is disabled in hardware profile; control loop cannot run",
                          port::NowMs()});
        return false;
    }
    if (profile.timer.mode == port::SubsystemMode::kAdaptationHook && !degraded_startup) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.profile.timer.hook",
                          "timer adaptation hook requires explicit degraded startup mode",
                          port::NowMs()});
        return false;
    }
    if (profile.timer.mode == port::SubsystemMode::kAdaptationHook && degraded_startup) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "startup.profile.timer.hook",
                          "timer routed through adaptation hook: " + profile.timer.hook,
                          port::NowMs()});
    }

    return RequireDirectMatch("camera", profile.camera, degraded_startup, diagnostics) &&
           RequireDirectMatch("imu", profile.imu, degraded_startup, diagnostics) &&
           RequireDirectMatch("encoder", profile.encoder, degraded_startup, diagnostics) &&
           RequireDirectMatch("motor", profile.motor, degraded_startup, diagnostics);
}

bool ApplyStartupLowVoltage(port::PlatformBundle& platform,
                            RuntimeState& state,
                            bool degraded_startup,
                            port::DiagnosticSink& diagnostics) {
    if (!platform.power->Initialize(diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.low_voltage.init",
                          "power monitor adapter failed to initialize",
                          port::NowMs()});
        return false;
    }
    const port::LowVoltageSample sample = platform.power->SampleLowVoltage(diagnostics);
    if (!sample.valid) {
        if (!degraded_startup) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "startup.low_voltage.invalid",
                              "low-voltage sample is unavailable; refusing startup in fail-closed mode",
                              port::NowMs()});
            return false;
        }
        state.low_voltage_emergency.store(true);
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "startup.low_voltage.invalid.degraded",
                          "low-voltage sample unavailable; degraded startup keeps emergency veto asserted",
                          port::NowMs()});
        return true;
    }

    state.low_voltage_emergency.store(sample.emergency);
    if (sample.emergency) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.low_voltage.emergency",
                          "low-voltage emergency asserted during startup; control loop remains fail-safe until voltage recovers",
                          port::NowMs()});
    } else {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "startup.low_voltage.clear",
                          "startup low-voltage check is clear",
                          port::NowMs()});
    }
    return true;
}

}  // namespace

bool RunStartup(const port::HardwareProfile& profile,
                port::RuntimeParameters& params,
                port::PlatformBundle& platform,
                RuntimeState& state,
                port::DiagnosticSink& diagnostics) {
    if (!platform.params || !platform.camera || !platform.imu || !platform.encoder || !platform.motor ||
        !platform.timer || !platform.power) {
        diagnostics.Emit({port::DiagnosticLevel::kError,
                          "startup.bundle.missing",
                          "platform bundle is incomplete",
                          port::NowMs()});
        return false;
    }

    const bool degraded_startup = ReadBoolEnv("LS2K_ALLOW_DEGRADED_STARTUP").value_or(false);
    state.degraded_startup = degraded_startup;
    if (degraded_startup) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "startup.mode.degraded",
                          "degraded startup mode enabled: non-direct-match profiles may pass startup for diagnostics only",
                          port::NowMs()});
    }

    if (!ValidateProfileContracts(profile, degraded_startup, diagnostics)) {
        return false;
    }

    platform.params->ApplyStartupCritical(params, diagnostics);
    if (!params.startup_critical_applied) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.params.critical",
                          "P_Mode/exp_light could not be applied before bring-up",
                          port::NowMs()});
        return false;
    }

    if (!ApplyStartupLowVoltage(platform, state, degraded_startup, diagnostics)) {
        return false;
    }

    if (!platform.camera->Initialize(profile, params, diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.camera.init",
                          "camera adapter initialization failed",
                          port::NowMs()});
        return false;
    }
    if (!RequireReady("camera", profile.camera, platform.camera->Ready(), degraded_startup, diagnostics)) {
        return false;
    }

    if (!platform.imu->Initialize(profile, diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.imu.init",
                          "imu adapter initialization failed",
                          port::NowMs()});
        return false;
    }
    if (!RequireReady("imu", profile.imu, platform.imu->Ready(), degraded_startup, diagnostics)) {
        return false;
    }

    if (!platform.encoder->Initialize(profile, diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.encoder.init",
                          "encoder adapter initialization failed",
                          port::NowMs()});
        return false;
    }
    if (!RequireReady("encoder", profile.encoder, platform.encoder->Ready(), degraded_startup, diagnostics)) {
        return false;
    }

    if (!platform.motor->Initialize(profile, diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "startup.motor.init",
                          "motor adapter initialization failed",
                          port::NowMs()});
        return false;
    }
    if (!RequireReady("motor", profile.motor, platform.motor->Ready(), degraded_startup, diagnostics)) {
        return false;
    }

    state.startup_complete = true;
    state.timer_started = false;
    state.actuators_armed = false;
    state.stop_requested = false;
    state.exit_requested = false;
    state.automation_start_fired = false;
    state.motion_intent = {};
    state.motion_state = {};
    state.last_command = {};
    state.control_observation = {};
    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "startup.complete",
                      "startup validated required adapters and applied critical params",
                      port::NowMs()});
    return true;
}

}  // namespace ls2k::runtime
