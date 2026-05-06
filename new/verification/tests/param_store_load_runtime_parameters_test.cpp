#include <fstream>
#include <cmath>
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
                      "  \"BEV_ELEMENT\": {"
                      "\"CROSS_EXIT_TAKEOVER_ENABLED\": 1,"
                      "\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\": 0.98,"
                      "\"CIRCLE_EVIDENCE_ENABLED\": 0,"
                      "\"CIRCLE_MIN_SUPPORT_ROWS\": 5,"
                      "\"CIRCLE_MIN_SAMPLEABLE_PER_ROW\": 18,"
                      "\"CIRCLE_OPEN_EXPANSION_MIN_M\": 0.22,"
                      "\"CIRCLE_OPENING_EXPANSION_RATIO_MIN\": 0.12,"
                      "\"CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M\": 0.06,"
                      "\"CIRCLE_OPPOSITE_SHRINK_RATIO_MIN\": 0.14,"
                      "\"CIRCLE_PRESENT_CONFIDENCE_MIN\": 0.75}"));
        CaptureDiagnostics enabled_diagnostics{};
        const ls2k::port::RuntimeParameters enabled =
            LoadFixture(enabled_path, enabled_diagnostics);
        Expect(!enabled.loaded_from_defaults, "enabled fixture should not fall back to defaults");
        Expect(!enabled.parse_failure, "enabled fixture should parse cleanly");
        Expect(enabled.bev_element.cross_exit_takeover_enabled,
               "CROSS_EXIT_TAKEOVER_ENABLED=1 should parse true");
        Expect(std::abs(enabled.bev_element.cross_wide_row_white_ratio_min - 0.98F) < 1.0e-6F,
               "CROSS_WIDE_ROW_WHITE_RATIO_MIN should parse");
        Expect(!enabled.bev_element.circle_evidence_enabled,
               "CIRCLE_EVIDENCE_ENABLED=0 should parse false");
        Expect(enabled.bev_element.circle_min_support_rows == 5,
               "CIRCLE_MIN_SUPPORT_ROWS should parse");
        Expect(enabled.bev_element.circle_min_sampleable_per_row == 18,
               "CIRCLE_MIN_SAMPLEABLE_PER_ROW should parse");
        Expect(std::abs(enabled.bev_element.circle_open_expansion_min_m - 0.22F) < 1.0e-6F,
               "CIRCLE_OPEN_EXPANSION_MIN_M should parse");
        Expect(std::abs(enabled.bev_element.circle_opening_expansion_ratio_min - 0.12F) < 1.0e-6F,
               "CIRCLE_OPENING_EXPANSION_RATIO_MIN should parse");
        Expect(std::abs(enabled.bev_element.circle_opposite_straight_drift_max_m - 0.06F) < 1.0e-6F,
               "CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M should parse");
        Expect(std::abs(enabled.bev_element.circle_opposite_shrink_ratio_min - 0.14F) < 1.0e-6F,
               "CIRCLE_OPPOSITE_SHRINK_RATIO_MIN should parse");
        Expect(std::abs(enabled.bev_element.circle_present_confidence_min - 0.75F) < 1.0e-6F,
               "CIRCLE_PRESENT_CONFIDENCE_MIN should parse");
        Expect(enabled.bev_element_raster.enabled,
               "missing BEV_ELEMENT_RASTER should keep raster enabled by default");
        Expect(enabled.bev_element_raster.width == 320,
               "missing BEV_ELEMENT_RASTER should use default raster width");

        const std::string absent_path = base + "_absent.json";
        WriteText(absent_path, MinimalRuntimeParametersJson(""));
        CaptureDiagnostics absent_diagnostics{};
        const ls2k::port::RuntimeParameters absent =
            LoadFixture(absent_path, absent_diagnostics);
        Expect(!absent.loaded_from_defaults, "absent BEV_ELEMENT should not fall back to defaults");
        Expect(!absent.parse_failure, "absent BEV_ELEMENT should parse cleanly");
        Expect(!absent.bev_element.cross_exit_takeover_enabled,
               "missing BEV_ELEMENT should keep takeover disabled");
        Expect(std::abs(absent.bev_element.cross_wide_row_white_ratio_min - 0.95F) <
                   1.0e-6F,
               "missing BEV_ELEMENT should keep cross white-ratio default");
        Expect(absent.bev_element.circle_evidence_enabled,
               "missing BEV_ELEMENT should keep circle evidence enabled");
        Expect(absent.bev_element.circle_min_support_rows == 4,
               "missing BEV_ELEMENT should keep circle support row default");
        Expect(absent.bev_element.circle_min_sampleable_per_row == 16,
               "missing BEV_ELEMENT should keep circle sampleable default");
        Expect(std::abs(absent.bev_element.circle_opening_expansion_ratio_min - 0.10F) <
                   1.0e-6F,
               "missing BEV_ELEMENT should keep circle opening-ratio default");
        Expect(std::abs(absent.bev_element.circle_opposite_shrink_ratio_min - 0.10F) <
                   1.0e-6F,
               "missing BEV_ELEMENT should keep circle shrink-ratio default");
        Expect(absent.bev_element_raster.enabled,
               "missing BEV_ELEMENT_RASTER should parse with enabled default");
        Expect(absent.bev_element_raster.width == 320,
               "missing BEV_ELEMENT_RASTER should parse with width default");

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

        const std::string malformed_circle_path = base + "_malformed_circle.json";
        WriteText(malformed_circle_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_PRESENT_CONFIDENCE_MIN\": 1.5}"));
        CaptureDiagnostics malformed_circle_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_circle =
            LoadFixture(malformed_circle_path, malformed_circle_diagnostics);
        Expect(malformed_circle.loaded_from_defaults,
               "out-of-range circle confidence should fall back to defaults");
        Expect(malformed_circle.parse_failure,
               "out-of-range circle confidence should set parse_failure");
        Expect(malformed_circle.bev_element.circle_evidence_enabled,
               "circle fallback should keep evidence enabled");
        Expect(std::abs(malformed_circle.bev_element.circle_present_confidence_min - 0.65F) <
                   1.0e-6F,
               "circle fallback should keep default confidence threshold");
        Expect(malformed_circle_diagnostics.SawCode("params.parse"),
               "out-of-range circle confidence should emit params.parse");

        const std::string malformed_cross_path = base + "_malformed_cross.json";
        WriteText(malformed_cross_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\": 1.5}"));
        CaptureDiagnostics malformed_cross_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_cross =
            LoadFixture(malformed_cross_path, malformed_cross_diagnostics);
        Expect(malformed_cross.loaded_from_defaults,
               "out-of-range cross white ratio should fall back to defaults");
        Expect(malformed_cross.parse_failure,
               "out-of-range cross white ratio should set parse_failure");
        Expect(std::abs(malformed_cross.bev_element.cross_wide_row_white_ratio_min - 0.95F) <
                   1.0e-6F,
               "cross fallback should keep default white ratio");
        Expect(malformed_cross_diagnostics.SawCode("params.parse"),
               "out-of-range cross white ratio should emit params.parse");

        const std::string malformed_raster_path = base + "_malformed_raster.json";
        WriteText(malformed_raster_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT_RASTER\": {\"ENABLED\": 1, \"WIDTH\": 1}"));
        CaptureDiagnostics malformed_raster_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_raster =
            LoadFixture(malformed_raster_path, malformed_raster_diagnostics);
        Expect(malformed_raster.loaded_from_defaults,
               "out-of-range BEV_ELEMENT_RASTER.WIDTH should fall back to defaults");
        Expect(malformed_raster.parse_failure,
               "out-of-range BEV_ELEMENT_RASTER.WIDTH should set parse_failure");
        Expect(malformed_raster.bev_element_raster.enabled,
               "raster fallback should keep default enabled");
        Expect(malformed_raster.bev_element_raster.width == 320,
               "raster fallback should keep default width");
        Expect(malformed_raster_diagnostics.SawCode("params.parse"),
               "out-of-range BEV_ELEMENT_RASTER.WIDTH should emit params.parse");

        std::cout << "param_store_load_runtime_parameters_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "param_store_load_runtime_parameters_test failed: " << error.what()
                  << "\n";
        return 1;
    }
}
