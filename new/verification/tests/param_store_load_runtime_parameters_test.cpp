#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/bootstrap.hpp"

namespace {

class CaptureDiagnostics final : public ls2k::port::DiagnosticSink {
public:
    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }

    bool SawCode(const std::string& code) const {
        for (const ls2k::port::DiagnosticEvent& event : events) {
            if (event.code == code) {
                return true;
            }
        }
        return false;
    }

    std::vector<ls2k::port::DiagnosticEvent> events{};
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string MinimalRuntimeParametersJson(const std::string& element_block) {
    std::string json =
        "{\n"
        "  \"RUNNING_SPEED_TARGET\": 300,\n"
        "  \"YAW_RATE_PID\": {\"P\": 12, \"I\": 0, \"D\": 0},\n"
        "  \"exp_light\": 65,\n"
        "  \"LEFT_WHEEL_PID\": {\"P\": 0, \"I\": 0, \"D\": 0, \"INTEGRAL_LIMIT\": 1000},\n"
        "  \"RIGHT_WHEEL_PID\": {\"P\": 0, \"I\": 0, \"D\": 0, \"INTEGRAL_LIMIT\": 1000},\n"
        "  \"assistant_tcp\": {\"host\": \"127.0.0.1\", \"port\": 8888}";
    if (!element_block.empty()) {
        json += ",\n";
        json += element_block;
    }
    json += "\n}\n";
    return json;
}

void WriteText(const std::string& path, const std::string& text) {
    std::ofstream output(path);
    Expect(output.is_open(), "failed to open fixture for write: " + path);
    output << text;
}

ls2k::port::RuntimeParameters LoadFixture(const std::string& path, CaptureDiagnostics& diagnostics) {
    ls2k::port::RuntimeParameters params{};
    const std::unique_ptr<ls2k::port::IParamStore> store = ls2k::platform::MakeParamStore();
    Expect(store != nullptr, "MakeParamStore returned null");
    Expect(store->LoadRuntimeParameters(path, params, diagnostics), "LoadRuntimeParameters returned false");
    return params;
}

}  // namespace

int main() {
    try {
        const std::string base = "/tmp/param_store_load_runtime_parameters_test";

        const std::string enabled_path = base + "_enabled.json";
        WriteText(enabled_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CROSS_EXIT_TAKEOVER_ENABLED\": 1}"));
        CaptureDiagnostics enabled_diagnostics{};
        const ls2k::port::RuntimeParameters enabled =
            LoadFixture(enabled_path, enabled_diagnostics);
        Expect(!enabled.loaded_from_defaults, "enabled fixture should not fall back to defaults");
        Expect(!enabled.parse_failure, "enabled fixture should parse cleanly");
        Expect(enabled.bev_element.cross_exit_takeover_enabled,
               "CROSS_EXIT_TAKEOVER_ENABLED=1 should parse true");

        const std::string absent_path = base + "_absent.json";
        WriteText(absent_path, MinimalRuntimeParametersJson(""));
        CaptureDiagnostics absent_diagnostics{};
        const ls2k::port::RuntimeParameters absent =
            LoadFixture(absent_path, absent_diagnostics);
        Expect(!absent.loaded_from_defaults, "absent BEV_ELEMENT should not fall back to defaults");
        Expect(!absent.parse_failure, "absent BEV_ELEMENT should parse cleanly");
        Expect(!absent.bev_element.cross_exit_takeover_enabled,
               "missing BEV_ELEMENT should keep takeover disabled");

        const std::string malformed_path = base + "_malformed.json";
        WriteText(malformed_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CROSS_EXIT_TAKEOVER_ENABLED\": {\"bad\": 1}}"));
        CaptureDiagnostics malformed_diagnostics{};
        const ls2k::port::RuntimeParameters malformed =
            LoadFixture(malformed_path, malformed_diagnostics);
        Expect(malformed.loaded_from_defaults,
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should fall back to defaults");
        Expect(malformed.parse_failure,
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should set parse_failure");
        Expect(!malformed.bev_element.cross_exit_takeover_enabled,
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should fail closed");
        Expect(malformed_diagnostics.SawCode("params.parse"),
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should emit params.parse");

        std::cout << "param_store_load_runtime_parameters_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "param_store_load_runtime_parameters_test failed: " << error.what()
                  << "\n";
        return 1;
    }
}
