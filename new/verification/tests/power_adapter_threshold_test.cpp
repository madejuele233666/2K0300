#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "platform/bootstrap.hpp"

namespace {

class NullDiagnostics final : public ls2k::port::DiagnosticSink {
public:
    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        last_code = event.code;
        ++count;
    }

    std::string last_code{};
    int count = 0;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteRawValue(const std::string& path, int raw_value) {
    std::ofstream output(path);
    Expect(output.is_open(), "failed to write temporary ADC raw file");
    output << raw_value << "\n";
}

}  // namespace

int main() {
    try {
        const std::string raw_path = "/tmp/ls2k_power_adapter_threshold_test_raw.txt";
        WriteRawValue(raw_path, 450);
        setenv("LS2K_LOW_VOLTAGE_RAW_PATH", raw_path.c_str(), 1);
        unsetenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD");
        unsetenv("LS2K_FORCE_LOW_VOLTAGE");

        NullDiagnostics diagnostics{};
        std::unique_ptr<ls2k::port::IPowerMonitorAdapter> power =
            ls2k::platform::MakePowerMonitorAdapter();
        Expect(power != nullptr, "power adapter factory returned null");
        Expect(power->Initialize(diagnostics), "power adapter initialize must not fail");

        power->ConfigureLowVoltageThreshold(500, diagnostics);
        ls2k::port::LowVoltageSample sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.valid, "configured threshold sample must be valid");
        Expect(sample.threshold == 500, "configured threshold must be recorded in sample");
        Expect(sample.emergency, "raw value below configured threshold must trigger emergency");

        power->ConfigureLowVoltageThreshold(400, diagnostics);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.valid, "reconfigured threshold sample must be valid");
        Expect(sample.threshold == 400, "reconfigured threshold must be recorded in sample");
        Expect(!sample.emergency, "raw value above configured threshold must not trigger emergency");

        setenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD", "460", 1);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.valid, "env override threshold sample must be valid");
        Expect(sample.threshold == 460, "env override threshold must be recorded in sample");
        Expect(sample.emergency, "env override threshold must take priority over configured threshold");
        unsetenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD");

        power->ConfigureLowVoltageThreshold(0, diagnostics);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.valid, "zero configured threshold sample must be valid");
        Expect(sample.threshold == 400, "zero configured threshold must fall back to built-in default");
        Expect(!sample.emergency, "zero configured threshold must not disable low-voltage protection");

        power->ConfigureLowVoltageThreshold(-1, diagnostics);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.valid, "negative configured threshold sample must be valid");
        Expect(sample.threshold == 400, "negative configured threshold must fall back to built-in default");
        Expect(!sample.emergency, "negative configured threshold must not disable low-voltage protection");

        power->ConfigureLowVoltageThreshold(500, diagnostics);
        setenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD", "0", 1);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.threshold == 500, "zero env override must fall back to configured threshold");
        Expect(sample.emergency, "zero env override must not disable configured protection");

        setenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD", "-1", 1);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.threshold == 500, "negative env override must fall back to configured threshold");
        Expect(sample.emergency, "negative env override must not disable configured protection");

        setenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD", "not-an-int", 1);
        sample = power->SampleLowVoltage(diagnostics);
        Expect(sample.threshold == 500, "non-integer env override must fall back to configured threshold");
        Expect(sample.emergency, "non-integer env override must not disable configured protection");

        unsetenv("LS2K_LOW_VOLTAGE_RAW_THRESHOLD");
        unsetenv("LS2K_LOW_VOLTAGE_RAW_PATH");
        std::remove(raw_path.c_str());

    } catch (const std::exception& error) {
        std::cerr << "power_adapter_threshold_test failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "power_adapter_threshold_test passed\n";
    return EXIT_SUCCESS;
}
