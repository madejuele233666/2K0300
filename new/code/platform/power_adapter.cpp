#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace ls2k::platform {
namespace {

constexpr int kDefaultLowVoltageRawThreshold = 400;

bool TryParsePositiveInt(const char* text, int& out) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    try {
        std::size_t parsed = 0;
        const int value = std::stoi(text, &parsed);
        if (text[parsed] != '\0' || value <= 0) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

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

class PowerMonitorAdapter final : public port::IPowerMonitorAdapter {
public:
    void ConfigureLowVoltageThreshold(int raw_threshold, port::DiagnosticSink& diagnostics) override {
        if (raw_threshold > 0) {
            configured_raw_threshold_ = raw_threshold;
            return;
        }
        configured_raw_threshold_ = kDefaultLowVoltageRawThreshold;
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "power.low_voltage.threshold_config_invalid",
                          "invalid low-voltage raw threshold; using built-in fail-safe default",
                          port::NowMs()});
    }

    bool Initialize(port::DiagnosticSink& diagnostics) override {
        initialized_ = true;
        const char* override_path = std::getenv("LS2K_LOW_VOLTAGE_RAW_PATH");
        const char* adc_path =
            (override_path != nullptr && override_path[0] != '\0') ? override_path : true_ls2k0300::kBatteryAdcPath;
        const true_ls2k0300::BatteryRawResult probe = true_ls2k0300::ReadBatteryRaw(adc_path);
        ready_ = probe.valid;
        if (ready_) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "power.init",
                              "power monitor initialized with true_ls2k0300 adc bridge path=" + probe.source,
                              port::NowMs()});
            return true;
        }
        diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                          "power.init",
                          probe.detail.empty() ? "power monitor backend unavailable during init"
                                               : probe.detail,
                          port::NowMs()});
        return true;
    }

    port::LowVoltageSample SampleLowVoltage(port::DiagnosticSink& diagnostics) override {
        port::LowVoltageSample sample{};
        sample.capture_time_ms = port::NowMs();
        sample.threshold = configured_raw_threshold_;
        const char* threshold_env = std::getenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD");
        if (threshold_env != nullptr && threshold_env[0] != '\0') {
            int override_threshold = 0;
            if (TryParsePositiveInt(threshold_env, override_threshold)) {
                sample.threshold = override_threshold;
            } else {
                port::EmitRateLimited(diagnostics,
                                      {port::DiagnosticLevel::kWarning,
                                       "power.low_voltage.threshold_env_invalid",
                                       std::string("ignoring invalid LS2K_LOW_VOLTAGE_RAW_THRESHOLD value=") +
                                           threshold_env,
                                       sample.capture_time_ms},
                                      1000);
            }
        }

        const char* forced_raw = std::getenv("LS2K_FORCE_LOW_VOLTAGE");
        if (const std::optional<bool> forced = ReadBoolEnv("LS2K_FORCE_LOW_VOLTAGE"); forced.has_value()) {
            sample.valid = true;
            sample.emergency = *forced;
            sample.source = "forced-env";
            port::EmitRateLimited(diagnostics,
                                  {*forced ? port::DiagnosticLevel::kFailSafe : port::DiagnosticLevel::kInfo,
                                   "power.low_voltage.injected",
                                   std::string("forced low-voltage emergency=") +
                                       (*forced ? "true" : "false"),
                                   sample.capture_time_ms},
                                  1000);
            return sample;
        } else if (forced_raw != nullptr && forced_raw[0] != '\0') {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "power.low_voltage.invalid_env",
                                   std::string("ignoring invalid LS2K_FORCE_LOW_VOLTAGE value=") + forced_raw,
                                   sample.capture_time_ms},
                                  1000);
        }

        const char* override_path = std::getenv("LS2K_LOW_VOLTAGE_RAW_PATH");
        const char* adc_path =
            (override_path != nullptr && override_path[0] != '\0') ? override_path : true_ls2k0300::kBatteryAdcPath;
        const true_ls2k0300::BatteryRawResult bridge_sample = true_ls2k0300::ReadBatteryRaw(adc_path);

        if (bridge_sample.valid) {
            sample.raw_value = bridge_sample.raw_value;
            sample.valid = true;
            sample.emergency = sample.raw_value <= sample.threshold;
            sample.source = bridge_sample.source;
            std::ostringstream message;
            message << "low-voltage raw check path=" << sample.source << " raw=" << sample.raw_value
                    << " threshold=" << sample.threshold;
            port::EmitRateLimited(diagnostics,
                                  {sample.emergency ? port::DiagnosticLevel::kFailSafe
                                                    : port::DiagnosticLevel::kInfo,
                                   "startup.low_voltage.raw",
                                   message.str(),
                                   sample.capture_time_ms},
                                  1000);
            return sample;
        }

        sample.valid = false;
        sample.emergency = true;
        sample.source = bridge_sample.source.empty() ? "unavailable" : bridge_sample.source;
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kFailSafe,
                               "startup.low_voltage.unavailable",
                               bridge_sample.detail.empty() ? "low-voltage backend unavailable; forcing fail-safe emergency veto"
                                                            : bridge_sample.detail,
                               sample.capture_time_ms},
                              1000);
        return sample;
    }

    bool Ready() const override { return initialized_ && ready_; }

private:
    bool initialized_ = false;
    bool ready_ = false;
    int configured_raw_threshold_ = kDefaultLowVoltageRawThreshold;
};

}  // namespace

std::unique_ptr<port::IPowerMonitorAdapter> MakePowerMonitorAdapter() {
    return std::make_unique<PowerMonitorAdapter>();
}

}  // namespace ls2k::platform
