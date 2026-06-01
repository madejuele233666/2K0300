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
                      "  \"steering_media_downsample\": 4,\n"
                      "  \"steering_media_publish_latest_frame\": 1,\n"
                      "  \"steering_media_gray_bits\": 4,\n"
                      "  \"brushless_debug_fixed_pwm_enabled\": 0,\n"
                      "  \"brushless_debug_fixed_pwm\": 750,\n"
                      "  \"BEV_GEOMETRY\": {"
                      "\"NOMINAL_ROAD_HALF_WIDTH_M\": 0.33,"
                      "\"SPARSE_ROW_COUNT\": 12,"
                      "\"REFERENCE_LATERAL_JUMP_GATE_M\": 0.42,"
                      "\"BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M\": 0.37},\n"
                      "  \"BEV_CLASSIFICATION\": {"
                      "\"WHITE_CONFIDENCE_MIN\": 0.66,"
                      "\"UNKNOWN_CONFIDENCE_MIN\": 0.33,"
                      "\"HOLD_LAST_MAX_CYCLES\": 24},\n"
                      "  \"BEV_CONTROL_MODEL\": {"
                      "\"LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN\": 321,"
                      "\"HEADING_ERROR_TO_WHEEL_DELTA_GAIN\": 45,"
                      "\"CURVATURE_TO_WHEEL_DELTA_GAIN\": 67,"
                      "\"TRACKING_FIT_MIN_SAMPLES\": 5},\n"
                      "  \"BEV_ELEMENT\": {"
                      "\"CROSS_EXIT_TAKEOVER_ENABLED\": 1,"
                      "\"CROSS_WIDE_ROW_WHITE_RATIO_MIN\": 0.98,"
                      "\"CIRCLE_V2_ENABLED\": 1,"
                      "\"CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG\": 300,"
                      "\"CIRCLE_V2_EXIT_HOLD_FRAMES\": 4,"
                      "\"CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS\": 4500,"
                      "\"CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG\": 12.5,"
                      "\"CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M\": 0.07,"
                      "\"CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN\": 0.63,"
                      "\"CIRCLE_V2_ENTRY_BOTTOM_ROW_COUNT\": 3,"
                      "\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MIN_M\": 0.05,"
                      "\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MAX_M\": 0.85}"));
        CaptureDiagnostics enabled_diagnostics{};
        const ls2k::port::RuntimeParameters enabled =
            LoadFixture(enabled_path, enabled_diagnostics);
        Expect(!enabled.loaded_from_defaults, "enabled fixture should not fall back to defaults");
        Expect(!enabled.parse_failure, "enabled fixture should parse cleanly");
        Expect(enabled.steering_media_downsample == 4,
               "steering_media_downsample should parse");
        Expect(enabled.steering_media_publish_latest_frame,
               "steering_media_publish_latest_frame should parse true");
        Expect(enabled.steering_media_gray_bits == 4,
               "steering_media_gray_bits should parse");
        Expect(!enabled.brushless_debug_fixed_pwm_enabled,
               "brushless_debug_fixed_pwm_enabled should parse false");
        Expect(enabled.brushless_debug_fixed_pwm == 750,
               "brushless_debug_fixed_pwm should parse");
        Expect(std::abs(enabled.bev_geometry.nominal_road_half_width_m - 0.33F) <
                   1.0e-6F,
               "BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M should parse");
        Expect(enabled.bev_geometry.sparse_row_count == 12,
               "BEV_GEOMETRY.SPARSE_ROW_COUNT should parse");
        Expect(std::abs(enabled.bev_geometry.reference_lateral_jump_gate_m -
                        0.42F) < 1.0e-6F,
               "BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M should parse");
        Expect(std::abs(enabled.bev_geometry.boundary_trace_max_adjacent_distance_m -
                        0.37F) < 1.0e-6F,
               "BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M should parse");
        Expect(std::abs(enabled.bev_classification.white_confidence_min -
                        0.66F) < 1.0e-6F,
               "BEV_CLASSIFICATION.WHITE_CONFIDENCE_MIN should parse");
        Expect(std::abs(enabled.bev_classification.unknown_confidence_min -
                        0.33F) < 1.0e-6F,
               "BEV_CLASSIFICATION.UNKNOWN_CONFIDENCE_MIN should parse");
        Expect(enabled.bev_classification.hold_last_max_cycles == 24,
               "BEV_CLASSIFICATION.HOLD_LAST_MAX_CYCLES should parse");
        Expect(std::abs(enabled.bev_control_model.lateral_offset_to_wheel_delta_gain -
                        321.0) < 1.0e-6,
               "BEV_CONTROL_MODEL.LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN should parse");
        Expect(std::abs(enabled.bev_control_model.heading_error_to_wheel_delta_gain -
                        45.0) < 1.0e-6,
               "BEV_CONTROL_MODEL.HEADING_ERROR_TO_WHEEL_DELTA_GAIN should parse");
        Expect(std::abs(enabled.bev_control_model.curvature_to_wheel_delta_gain -
                        67.0) < 1.0e-6,
               "BEV_CONTROL_MODEL.CURVATURE_TO_WHEEL_DELTA_GAIN should parse");
        Expect(enabled.bev_control_model.tracking_fit_min_samples == 5,
               "BEV_CONTROL_MODEL.TRACKING_FIT_MIN_SAMPLES should parse");
        Expect(std::abs(enabled.bev_control_model.lateral_error_to_wheel_delta_gain -
                        enabled.bev_control_model.lateral_offset_to_wheel_delta_gain) < 1.0e-6,
               "legacy lateral-error gain field should mirror lateral offset gain");
        Expect(enabled.bev_element.cross_exit_takeover_enabled,
               "CROSS_EXIT_TAKEOVER_ENABLED=1 should parse true");
        Expect(std::abs(enabled.bev_element.cross_wide_row_white_ratio_min - 0.98F) < 1.0e-6F,
               "CROSS_WIDE_ROW_WHITE_RATIO_MIN should parse");
        Expect(enabled.bev_element.circle_v2_enabled,
               "CIRCLE_V2_ENABLED=1 should parse true");
        Expect(std::abs(enabled.bev_element.circle_v2_exit_yaw_threshold_deg - 300.0F) <
                   1.0e-6F,
               "CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG should parse");
        Expect(enabled.bev_element.circle_v2_exit_hold_frames == 4,
               "CIRCLE_V2_EXIT_HOLD_FRAMES should parse");
        Expect(enabled.bev_element.circle_v2_inner_trace_stall_timeout_ms == 4500,
               "CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS should parse");
        Expect(std::abs(enabled.bev_element.circle_v2_inner_trace_stall_yaw_min_deg -
                        12.5F) < 1.0e-6F,
               "CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG should parse");
        Expect(std::abs(enabled.bev_element.circle_v2_inner_trace_path_offset_m -
                        0.07F) < 1.0e-6F,
               "CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M should parse");
        Expect(std::abs(enabled.bev_element.circle_v2_opposite_straight_confidence_min -
                        0.63F) < 1.0e-6F,
               "CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN should parse");
        Expect(enabled.bev_element.circle_v2_entry_bottom_row_count == 3,
               "CIRCLE_V2_ENTRY_BOTTOM_ROW_COUNT should parse");
        Expect(std::abs(enabled.bev_element.circle_v2_entry_bottom_forward_min_m -
                        0.05F) < 1.0e-6F,
               "CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MIN_M should parse");
        Expect(std::abs(enabled.bev_element.circle_v2_entry_bottom_forward_max_m -
                        0.85F) < 1.0e-6F,
               "CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MAX_M should parse");
        Expect(!enabled.bev_element_raster.enabled,
               "missing BEV_ELEMENT_RASTER should keep raster disabled by default");
        Expect(enabled.bev_element_raster.width == 320,
               "missing BEV_ELEMENT_RASTER should use default raster width");

        const std::string absent_path = base + "_absent.json";
        WriteText(absent_path, MinimalRuntimeParametersJson(""));
        CaptureDiagnostics absent_diagnostics{};
        const ls2k::port::RuntimeParameters absent =
            LoadFixture(absent_path, absent_diagnostics);
        Expect(!absent.loaded_from_defaults, "absent BEV_ELEMENT should not fall back to defaults");
        Expect(!absent.parse_failure, "absent BEV_ELEMENT should parse cleanly");
        Expect(absent.steering_media_downsample == 1,
               "missing steering_media_downsample should keep downsample default");
        Expect(!absent.steering_media_publish_latest_frame,
               "missing steering_media_publish_latest_frame should keep strict snapshot alignment");
        Expect(absent.steering_media_gray_bits == 2,
               "missing steering_media_gray_bits should keep gray2 default");
        Expect(absent.brushless_debug_fixed_pwm_enabled,
               "missing brushless_debug_fixed_pwm_enabled should keep enabled default");
        Expect(absent.brushless_debug_fixed_pwm == 600,
               "missing brushless_debug_fixed_pwm should keep 600 default");
        Expect(absent.bev_element.cross_exit_takeover_enabled,
               "missing BEV_ELEMENT should keep takeover enabled");
        Expect(std::abs(absent.bev_element.cross_wide_row_white_ratio_min - 0.95F) <
                   1.0e-6F,
               "missing BEV_ELEMENT should keep cross white-ratio default");
        Expect(absent.bev_element.circle_v2_enabled,
               "missing BEV_ELEMENT should keep CircleV2 enabled");
        Expect(std::abs(absent.bev_element.circle_v2_exit_yaw_threshold_deg - 330.0F) <
                   1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 yaw threshold default");
        Expect(absent.bev_element.circle_v2_exit_hold_frames == 60,
               "missing BEV_ELEMENT should keep CircleV2 hold default");
        Expect(absent.bev_element.circle_v2_inner_trace_stall_timeout_ms == 4000,
               "missing BEV_ELEMENT should keep CircleV2 stall timeout default");
        Expect(std::abs(absent.bev_element.circle_v2_inner_trace_stall_yaw_min_deg -
                        16.5F) < 1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 stall yaw default");
        Expect(std::abs(absent.bev_element.circle_v2_inner_trace_path_offset_m -
                        0.1F) < 1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 inner path offset default");
        Expect(std::abs(absent.bev_element.circle_v2_opposite_straight_confidence_min -
                        0.50F) < 1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 opposite-straight confidence default");
        Expect(absent.bev_element.circle_v2_entry_bottom_row_count == 4,
               "missing BEV_ELEMENT should keep CircleV2 entry bottom row-count default");
        Expect(std::abs(absent.bev_element.circle_v2_entry_bottom_forward_min_m -
                        0.0F) < 1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 entry bottom min default");
        Expect(std::abs(absent.bev_element.circle_v2_entry_bottom_forward_max_m -
                        0.25F) < 1.0e-6F,
               "missing BEV_ELEMENT should keep CircleV2 entry bottom max default");
        Expect(std::abs(absent.bev_geometry.nominal_road_half_width_m - 0.19F) <
                   1.0e-6F,
               "missing BEV_GEOMETRY should keep nominal road half-width default");
        Expect(absent.bev_geometry.sparse_row_count ==
                   static_cast<int>(ls2k::port::kBevReferenceSampleCount),
               "missing BEV_GEOMETRY should keep sparse row count default");
        Expect(std::abs(absent.bev_geometry.reference_lateral_jump_gate_m -
                        1000.0F) < 1.0e-6F,
               "missing BEV_GEOMETRY should keep reference jump gate disabled");
        Expect(std::abs(absent.bev_geometry.boundary_trace_max_adjacent_distance_m -
                        0.15F) < 1.0e-6F,
               "missing BEV_GEOMETRY should keep boundary trace distance default");
        Expect(std::abs(absent.bev_classification.white_confidence_min -
                        0.55F) < 1.0e-6F,
               "missing BEV_CLASSIFICATION should keep white confidence default");
        Expect(std::abs(absent.bev_classification.unknown_confidence_min -
                        0.25F) < 1.0e-6F,
               "missing BEV_CLASSIFICATION should keep unknown confidence default");
        Expect(absent.bev_classification.hold_last_max_cycles == 32,
               "missing BEV_CLASSIFICATION should keep hold default");
        const ls2k::port::RuntimeParameters builtin_defaults{};
        Expect(std::abs(absent.bev_control_model.lateral_offset_to_wheel_delta_gain -
                        builtin_defaults.bev_control_model.lateral_offset_to_wheel_delta_gain) < 1.0e-6,
               "missing BEV_CONTROL_MODEL should keep lateral offset gain default");
        Expect(absent.bev_control_model.tracking_fit_min_samples == 3,
               "missing BEV_CONTROL_MODEL should keep tracking fit sample default");
        Expect(!absent.bev_element_raster.enabled,
               "missing BEV_ELEMENT_RASTER should parse with disabled default");
        Expect(absent.bev_element_raster.width == 320,
               "missing BEV_ELEMENT_RASTER should parse with width default");

        const std::string zero_hold_path = base + "_zero_hold.json";
        WriteText(zero_hold_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_CLASSIFICATION\": {"
                      "\"HOLD_LAST_MAX_CYCLES\": 0}"));
        CaptureDiagnostics zero_hold_diagnostics{};
        const ls2k::port::RuntimeParameters zero_hold =
            LoadFixture(zero_hold_path, zero_hold_diagnostics);
        Expect(!zero_hold.loaded_from_defaults,
               "zero BEV hold cycles should parse as hold disabled");
        Expect(!zero_hold.parse_failure,
               "zero BEV hold cycles should not set parse_failure");
        Expect(zero_hold.bev_classification.hold_last_max_cycles == 0,
               "zero BEV hold cycles should be preserved");

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
        Expect(malformed.bev_element.cross_exit_takeover_enabled,
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should fall back to takeover enabled default");
        Expect(malformed_diagnostics.SawCode("params.parse"),
               "malformed CROSS_EXIT_TAKEOVER_ENABLED should emit params.parse");

        const std::string malformed_geometry_path = base + "_malformed_geometry.json";
        WriteText(malformed_geometry_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_GEOMETRY\": {\"NOMINAL_ROAD_HALF_WIDTH_M\": 0}"));
        CaptureDiagnostics malformed_geometry_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_geometry =
            LoadFixture(malformed_geometry_path, malformed_geometry_diagnostics);
        Expect(malformed_geometry.loaded_from_defaults,
               "zero nominal road half width should fall back to defaults");
        Expect(malformed_geometry.parse_failure,
               "zero nominal road half width should set parse_failure");
        Expect(std::abs(malformed_geometry.bev_geometry.nominal_road_half_width_m -
                        0.19F) < 1.0e-6F,
               "nominal road half-width fallback should keep default");
        Expect(malformed_geometry_diagnostics.SawCode("params.parse"),
               "zero nominal road half width should emit params.parse");

        const std::string malformed_reference_jump_path =
            base + "_malformed_reference_jump.json";
        WriteText(malformed_reference_jump_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_GEOMETRY\": {"
                      "\"REFERENCE_LATERAL_JUMP_GATE_M\": -0.1}"));
        CaptureDiagnostics malformed_reference_jump_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_reference_jump =
            LoadFixture(malformed_reference_jump_path,
                        malformed_reference_jump_diagnostics);
        Expect(malformed_reference_jump.loaded_from_defaults,
               "negative reference jump gate should fall back to defaults");
        Expect(malformed_reference_jump.parse_failure,
               "negative reference jump gate should set parse_failure");
        Expect(std::abs(malformed_reference_jump.bev_geometry
                            .reference_lateral_jump_gate_m -
                        1000.0F) < 1.0e-6F,
               "reference jump gate fallback should keep disabled default");
        Expect(malformed_reference_jump_diagnostics.SawCode("params.parse"),
               "negative reference jump gate should emit params.parse");

        const std::string malformed_boundary_trace_path =
            base + "_malformed_boundary_trace.json";
        WriteText(malformed_boundary_trace_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_GEOMETRY\": {"
                      "\"BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M\": 0}"));
        CaptureDiagnostics malformed_boundary_trace_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_boundary_trace =
            LoadFixture(malformed_boundary_trace_path,
                        malformed_boundary_trace_diagnostics);
        Expect(malformed_boundary_trace.loaded_from_defaults,
               "zero boundary trace distance should fall back to defaults");
        Expect(malformed_boundary_trace.parse_failure,
               "zero boundary trace distance should set parse_failure");
        Expect(std::abs(malformed_boundary_trace.bev_geometry
                            .boundary_trace_max_adjacent_distance_m -
                        0.15F) < 1.0e-6F,
               "boundary trace distance fallback should keep default");
        Expect(malformed_boundary_trace_diagnostics.SawCode("params.parse"),
               "zero boundary trace distance should emit params.parse");

        const std::string malformed_classification_path =
            base + "_malformed_classification.json";
        WriteText(malformed_classification_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_CLASSIFICATION\": {"
                      "\"WHITE_CONFIDENCE_MIN\": 0.2,"
                      "\"UNKNOWN_CONFIDENCE_MIN\": 0.4}"));
        CaptureDiagnostics malformed_classification_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_classification =
            LoadFixture(malformed_classification_path,
                        malformed_classification_diagnostics);
        Expect(malformed_classification.loaded_from_defaults,
               "inverted BEV classification confidence should fall back to defaults");
        Expect(malformed_classification.parse_failure,
               "inverted BEV classification confidence should set parse_failure");
        Expect(std::abs(malformed_classification.bev_classification
                            .white_confidence_min -
                        0.55F) < 1.0e-6F,
               "BEV classification fallback should keep white confidence default");
        Expect(std::abs(malformed_classification.bev_classification
                            .unknown_confidence_min -
                        0.25F) < 1.0e-6F,
               "BEV classification fallback should keep unknown confidence default");
        Expect(malformed_classification_diagnostics.SawCode("params.parse"),
               "inverted BEV classification confidence should emit params.parse");

        const std::string malformed_unknown_confidence_path =
            base + "_malformed_unknown_confidence.json";
        WriteText(malformed_unknown_confidence_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_CLASSIFICATION\": {"
                      "\"UNKNOWN_CONFIDENCE_MIN\": 0}"));
        CaptureDiagnostics malformed_unknown_confidence_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_unknown_confidence =
            LoadFixture(malformed_unknown_confidence_path,
                        malformed_unknown_confidence_diagnostics);
        Expect(malformed_unknown_confidence.loaded_from_defaults,
               "zero BEV unknown confidence should fall back to defaults");
        Expect(malformed_unknown_confidence.parse_failure,
               "zero BEV unknown confidence should set parse_failure");
        Expect(std::abs(malformed_unknown_confidence.bev_classification
                            .unknown_confidence_min -
                        0.25F) < 1.0e-6F,
               "zero unknown confidence fallback should keep default");
        Expect(malformed_unknown_confidence_diagnostics.SawCode("params.parse"),
               "zero BEV unknown confidence should emit params.parse");

        const std::string malformed_hold_cycles_path =
            base + "_malformed_hold_cycles.json";
        WriteText(malformed_hold_cycles_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_CLASSIFICATION\": {"
                      "\"HOLD_LAST_MAX_CYCLES\": -1}"));
        CaptureDiagnostics malformed_hold_cycles_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_hold_cycles =
            LoadFixture(malformed_hold_cycles_path,
                        malformed_hold_cycles_diagnostics);
        Expect(malformed_hold_cycles.loaded_from_defaults,
               "negative BEV hold cycles should fall back to defaults");
        Expect(malformed_hold_cycles.parse_failure,
               "negative BEV hold cycles should set parse_failure");
        Expect(malformed_hold_cycles.bev_classification.hold_last_max_cycles == 32,
               "negative hold cycles fallback should keep default");
        Expect(malformed_hold_cycles_diagnostics.SawCode("params.parse"),
               "negative BEV hold cycles should emit params.parse");

        const std::string malformed_v2_yaw_path = base + "_malformed_v2_yaw.json";
        WriteText(malformed_v2_yaw_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG\": 0}"));
        CaptureDiagnostics malformed_v2_yaw_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_yaw =
            LoadFixture(malformed_v2_yaw_path, malformed_v2_yaw_diagnostics);
        Expect(malformed_v2_yaw.loaded_from_defaults,
               "zero CircleV2 yaw threshold should fall back to defaults");
        Expect(malformed_v2_yaw.parse_failure,
               "zero CircleV2 yaw threshold should set parse_failure");
        Expect(std::abs(malformed_v2_yaw.bev_element.circle_v2_exit_yaw_threshold_deg - 330.0F) <
                   1.0e-6F,
               "CircleV2 yaw fallback should keep nonzero default threshold");
        Expect(malformed_v2_yaw_diagnostics.SawCode("params.parse"),
               "zero CircleV2 yaw threshold should emit params.parse");

        const std::string malformed_v2_hold_path = base + "_malformed_v2_hold.json";
        WriteText(malformed_v2_hold_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_V2_EXIT_HOLD_FRAMES\": 1}"));
        CaptureDiagnostics malformed_v2_hold_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_hold =
            LoadFixture(malformed_v2_hold_path, malformed_v2_hold_diagnostics);
        Expect(malformed_v2_hold.loaded_from_defaults,
               "CircleV2 hold below two should fall back to defaults");
        Expect(malformed_v2_hold.parse_failure,
               "CircleV2 hold below two should set parse_failure");
        Expect(malformed_v2_hold.bev_element.circle_v2_exit_hold_frames == 60,
               "CircleV2 hold fallback should keep default hold frames");
        Expect(malformed_v2_hold_diagnostics.SawCode("params.parse"),
               "CircleV2 hold below two should emit params.parse");

        const std::string malformed_v2_stall_timeout_path =
            base + "_malformed_v2_stall_timeout.json";
        WriteText(malformed_v2_stall_timeout_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS\": 0}"));
        CaptureDiagnostics malformed_v2_stall_timeout_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_stall_timeout =
            LoadFixture(malformed_v2_stall_timeout_path,
                        malformed_v2_stall_timeout_diagnostics);
        Expect(malformed_v2_stall_timeout.loaded_from_defaults,
               "CircleV2 stall timeout below one should fall back to defaults");
        Expect(malformed_v2_stall_timeout.parse_failure,
               "CircleV2 stall timeout below one should set parse_failure");
        Expect(malformed_v2_stall_timeout.bev_element
                   .circle_v2_inner_trace_stall_timeout_ms == 4000,
               "CircleV2 stall timeout fallback should keep default timeout");
        Expect(malformed_v2_stall_timeout_diagnostics.SawCode("params.parse"),
               "CircleV2 stall timeout below one should emit params.parse");

        const std::string malformed_v2_path_offset_path =
            base + "_malformed_v2_path_offset.json";
        WriteText(malformed_v2_path_offset_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M\": -0.1}"));
        CaptureDiagnostics malformed_v2_path_offset_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_path_offset =
            LoadFixture(malformed_v2_path_offset_path,
                        malformed_v2_path_offset_diagnostics);
        Expect(malformed_v2_path_offset.loaded_from_defaults,
               "negative CircleV2 inner path offset should fall back to defaults");
        Expect(malformed_v2_path_offset.parse_failure,
               "negative CircleV2 inner path offset should set parse_failure");
        Expect(std::abs(malformed_v2_path_offset.bev_element
                            .circle_v2_inner_trace_path_offset_m -
                        0.1F) < 1.0e-6F,
               "CircleV2 inner path offset fallback should keep default");
        Expect(malformed_v2_path_offset_diagnostics.SawCode("params.parse"),
               "negative CircleV2 inner path offset should emit params.parse");

        const std::string malformed_v2_opposite_confidence_path =
            base + "_malformed_v2_opposite_confidence.json";
        WriteText(malformed_v2_opposite_confidence_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {"
                      "\"CIRCLE_V2_OPPOSITE_STRAIGHT_CONFIDENCE_MIN\": 1.2}"));
        CaptureDiagnostics malformed_v2_opposite_confidence_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_opposite_confidence =
            LoadFixture(malformed_v2_opposite_confidence_path,
                        malformed_v2_opposite_confidence_diagnostics);
        Expect(malformed_v2_opposite_confidence.loaded_from_defaults,
               "CircleV2 opposite-straight confidence above one should fall back to defaults");
        Expect(malformed_v2_opposite_confidence.parse_failure,
               "CircleV2 opposite-straight confidence above one should set parse_failure");
        Expect(std::abs(malformed_v2_opposite_confidence.bev_element
                            .circle_v2_opposite_straight_confidence_min -
                        0.50F) < 1.0e-6F,
               "CircleV2 opposite-straight confidence fallback should keep default");
        Expect(malformed_v2_opposite_confidence_diagnostics.SawCode("params.parse"),
               "CircleV2 opposite-straight confidence above one should emit params.parse");

        const std::string malformed_v2_entry_forward_path =
            base + "_malformed_v2_entry_forward.json";
        WriteText(malformed_v2_entry_forward_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {"
                      "\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MIN_M\": 0.9,"
                      "\"CIRCLE_V2_ENTRY_BOTTOM_FORWARD_MAX_M\": 0.8}"));
        CaptureDiagnostics malformed_v2_entry_forward_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_entry_forward =
            LoadFixture(malformed_v2_entry_forward_path,
                        malformed_v2_entry_forward_diagnostics);
        Expect(malformed_v2_entry_forward.loaded_from_defaults,
               "CircleV2 entry bottom inverted interval should fall back to defaults");
        Expect(malformed_v2_entry_forward.parse_failure,
               "CircleV2 entry bottom inverted interval should set parse_failure");
        Expect(std::abs(malformed_v2_entry_forward.bev_element
                            .circle_v2_entry_bottom_forward_min_m -
                        0.0F) < 1.0e-6F,
               "CircleV2 entry bottom min fallback should keep default");
        Expect(std::abs(malformed_v2_entry_forward.bev_element
                            .circle_v2_entry_bottom_forward_max_m -
                        0.25F) < 1.0e-6F,
               "CircleV2 entry bottom max fallback should keep default");
        Expect(malformed_v2_entry_forward_diagnostics.SawCode("params.parse"),
               "CircleV2 entry bottom inverted interval should emit params.parse");

        const std::string malformed_v2_entry_rows_path =
            base + "_malformed_v2_entry_rows.json";
        WriteText(malformed_v2_entry_rows_path,
                  MinimalRuntimeParametersJson(
                      "  \"BEV_ELEMENT\": {\"CIRCLE_V2_ENTRY_BOTTOM_ROW_COUNT\": 0}"));
        CaptureDiagnostics malformed_v2_entry_rows_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_v2_entry_rows =
            LoadFixture(malformed_v2_entry_rows_path,
                        malformed_v2_entry_rows_diagnostics);
        Expect(malformed_v2_entry_rows.loaded_from_defaults,
               "CircleV2 entry bottom row count below one should fall back to defaults");
        Expect(malformed_v2_entry_rows.parse_failure,
               "CircleV2 entry bottom row count below one should set parse_failure");
        Expect(malformed_v2_entry_rows.bev_element.circle_v2_entry_bottom_row_count == 4,
               "CircleV2 entry bottom row-count fallback should keep default");
        Expect(malformed_v2_entry_rows_diagnostics.SawCode("params.parse"),
               "CircleV2 entry bottom row count below one should emit params.parse");

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
        Expect(!malformed_raster.bev_element_raster.enabled,
               "raster fallback should keep default disabled");
        Expect(malformed_raster.bev_element_raster.width == 320,
               "raster fallback should keep default width");
        Expect(malformed_raster_diagnostics.SawCode("params.parse"),
               "out-of-range BEV_ELEMENT_RASTER.WIDTH should emit params.parse");

        const std::string malformed_downsample_path = base + "_malformed_downsample.json";
        WriteText(malformed_downsample_path,
                  MinimalRuntimeParametersJson("  \"steering_media_downsample\": 0"));
        CaptureDiagnostics malformed_downsample_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_downsample =
            LoadFixture(malformed_downsample_path, malformed_downsample_diagnostics);
        Expect(malformed_downsample.loaded_from_defaults,
               "out-of-range steering_media_downsample should fall back to defaults");
        Expect(malformed_downsample.parse_failure,
               "out-of-range steering_media_downsample should set parse_failure");
        Expect(malformed_downsample.steering_media_downsample == 1,
               "downsample fallback should keep default downsample");
        Expect(malformed_downsample_diagnostics.SawCode("params.parse"),
               "out-of-range steering_media_downsample should emit params.parse");

        const std::string malformed_brushless_pwm_path = base + "_malformed_brushless_pwm.json";
        WriteText(malformed_brushless_pwm_path,
                  MinimalRuntimeParametersJson("  \"brushless_debug_fixed_pwm\": 1200"));
        CaptureDiagnostics malformed_brushless_pwm_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_brushless_pwm =
            LoadFixture(malformed_brushless_pwm_path, malformed_brushless_pwm_diagnostics);
        Expect(malformed_brushless_pwm.loaded_from_defaults,
               "out-of-range brushless_debug_fixed_pwm should fall back to defaults");
        Expect(malformed_brushless_pwm.parse_failure,
               "out-of-range brushless_debug_fixed_pwm should set parse_failure");
        Expect(malformed_brushless_pwm.brushless_debug_fixed_pwm == 600,
               "brushless PWM fallback should keep default 600");
        Expect(malformed_brushless_pwm_diagnostics.SawCode("params.parse"),
               "out-of-range brushless_debug_fixed_pwm should emit params.parse");

        const std::string malformed_gray_bits_path = base + "_malformed_gray_bits.json";
        WriteText(malformed_gray_bits_path,
                  MinimalRuntimeParametersJson("  \"steering_media_gray_bits\": 5"));
        CaptureDiagnostics malformed_gray_bits_diagnostics{};
        const ls2k::port::RuntimeParameters malformed_gray_bits =
            LoadFixture(malformed_gray_bits_path, malformed_gray_bits_diagnostics);
        Expect(malformed_gray_bits.loaded_from_defaults,
               "out-of-range steering_media_gray_bits should fall back to defaults");
        Expect(malformed_gray_bits.parse_failure,
               "out-of-range steering_media_gray_bits should set parse_failure");
        Expect(malformed_gray_bits.steering_media_gray_bits == 2,
               "gray_bits fallback should keep default gray2");
        Expect(malformed_gray_bits_diagnostics.SawCode("params.parse"),
               "out-of-range steering_media_gray_bits should emit params.parse");

        std::cout << "param_store_load_runtime_parameters_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "param_store_load_runtime_parameters_test failed: " << error.what()
                  << "\n";
        return 1;
    }
}
