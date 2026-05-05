#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "port/runtime_parameter_types.hpp"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string ReadText(const std::string& path) {
    std::ifstream input(path);
    Expect(input.is_open(), "failed to open default_params.json");
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string ObjectBody(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t key_pos = json.find(marker);
    Expect(key_pos != std::string::npos, "missing object key: " + key);
    const std::size_t open = json.find('{', key_pos + marker.size());
    Expect(open != std::string::npos, "missing object body for: " + key);
    int depth = 0;
    for (std::size_t pos = open; pos < json.size(); ++pos) {
        if (json[pos] == '{') {
            ++depth;
        } else if (json[pos] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(open + 1, pos - open - 1);
            }
        }
    }
    throw std::runtime_error("unterminated object body for: " + key);
}

double NumberField(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(\\.[0-9]+)?)");
    std::smatch match;
    Expect(std::regex_search(text, match, pattern), "missing numeric field: " + key);
    return std::stod(match[1].str());
}

std::string StringField(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    Expect(std::regex_search(text, match, pattern), "missing string field: " + key);
    return match[1].str();
}

void ExpectNear(double actual, double expected, const std::string& label) {
    const double tolerance = 1.0e-6;
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream error;
        error << label << " mismatch: actual=" << actual << " expected=" << expected;
        throw std::runtime_error(error.str());
    }
}

void ExpectInRange(double value, double min_value, double max_value, const std::string& label) {
    Expect(std::isfinite(value), label + " must be finite");
    Expect(value >= min_value && value <= max_value, label + " outside documented range");
}

void ExpectInt(int actual, double expected, const std::string& label) {
    Expect(actual == static_cast<int>(expected), label + " mismatch");
}

void ExpectBool(bool actual, double expected, const std::string& label) {
    Expect(actual == (std::abs(expected) > 0.5), label + " mismatch");
}

void CompareWheelPid(const ls2k::port::WheelPidParameters& params,
                     const std::string& object,
                     const std::string& label) {
    ExpectNear(params.p, NumberField(object, "P"), label + ".P");
    ExpectNear(params.i, NumberField(object, "I"), label + ".I");
    ExpectNear(params.d, NumberField(object, "D"), label + ".D");
    ExpectNear(params.integral_limit, NumberField(object, "INTEGRAL_LIMIT"), label + ".INTEGRAL_LIMIT");
    ExpectNear(params.measurement_filter_alpha,
               NumberField(object, "MEASUREMENT_FILTER_ALPHA"),
               label + ".MEASUREMENT_FILTER_ALPHA");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Expect(argc == 2, "usage: runtime_parameter_defaults_test <default_params.json>");
        const std::string json = ReadText(argv[1]);
        const ls2k::port::RuntimeParameters params{};

        ExpectNear(params.running_speed_target, NumberField(json, "RUNNING_SPEED_TARGET"), "RUNNING_SPEED_TARGET");
        const std::string yaw_pid = ObjectBody(json, "YAW_RATE_PID");
        ExpectNear(params.yaw_rate_pid_p, NumberField(yaw_pid, "P"), "YAW_RATE_PID.P");
        ExpectNear(params.yaw_rate_pid_i, NumberField(yaw_pid, "I"), "YAW_RATE_PID.I");
        ExpectNear(params.yaw_rate_pid_d, NumberField(yaw_pid, "D"), "YAW_RATE_PID.D");

        ExpectInt(params.exp_light, NumberField(json, "exp_light"), "exp_light");
        CompareWheelPid(params.left_wheel_pid, ObjectBody(json, "LEFT_WHEEL_PID"), "LEFT_WHEEL_PID");
        CompareWheelPid(params.right_wheel_pid, ObjectBody(json, "RIGHT_WHEEL_PID"), "RIGHT_WHEEL_PID");
        ExpectInt(params.low_voltage_raw_threshold,
                  NumberField(json, "low_voltage_raw_threshold"),
                  "low_voltage_raw_threshold");
        ExpectInt(params.control_period_ms, NumberField(json, "control_period_ms"), "control_period_ms");
        ExpectInt(params.perception_stale_ms, NumberField(json, "perception_stale_ms"), "perception_stale_ms");
        ExpectInt(params.pwm_limit, NumberField(json, "pwm_limit"), "pwm_limit");
        ExpectInt(params.raw_turn_output_limit, NumberField(json, "raw_turn_output_limit"), "raw_turn_output_limit");
        ExpectInt(params.pwm_floor, NumberField(json, "pwm_floor"), "pwm_floor");
        ExpectBool(params.prohibit_reverse_pwm, NumberField(json, "prohibit_reverse_pwm"), "prohibit_reverse_pwm");
        ExpectInt(params.prohibit_reverse_pwm_step_limit,
                  NumberField(json, "prohibit_reverse_pwm_step_limit"),
                  "prohibit_reverse_pwm_step_limit");
        ExpectInt(params.motion_unveto_confirm_cycles,
                  NumberField(json, "motion_unveto_confirm_cycles"),
                  "motion_unveto_confirm_cycles");
        ExpectInt(params.motion_spinup_ms, NumberField(json, "motion_spinup_ms"), "motion_spinup_ms");
        ExpectNear(params.motion_turn_limit_spinup,
                   NumberField(json, "motion_turn_limit_spinup"),
                   "motion_turn_limit_spinup");
        ExpectInt(params.motion_pwm_step_limit, NumberField(json, "motion_pwm_step_limit"), "motion_pwm_step_limit");
        ExpectInt(params.motion_stop_ms, NumberField(json, "motion_stop_ms"), "motion_stop_ms");
        ExpectInt(params.motion_stop_encoder_threshold,
                  NumberField(json, "motion_stop_encoder_threshold"),
                  "motion_stop_encoder_threshold");
        ExpectInt(params.motion_fault_rearm_hold_ms,
                  NumberField(json, "motion_fault_rearm_hold_ms"),
                  "motion_fault_rearm_hold_ms");
        Expect(json.find("\"turn_output_to_wheel_delta_gain\"") == std::string::npos,
               "default_params.json must not retain turn_output_to_wheel_delta_gain alias");
        Expect(json.find("\"wheel_turn_target_scale\"") == std::string::npos,
               "default_params.json must not retain wheel_turn_target_scale alias");
        ExpectInt(params.control_snapshot_emit_interval_ms,
                  NumberField(json, "control_snapshot_emit_interval_ms"),
                  "control_snapshot_emit_interval_ms");
        ExpectBool(params.assistant_enabled, NumberField(json, "assistant_enabled"), "assistant_enabled");
        ExpectBool(params.steering_media_enabled,
                   NumberField(json, "steering_media_enabled"),
                   "steering_media_enabled");
        ExpectInt(params.steering_media_port, NumberField(json, "steering_media_port"), "steering_media_port");
        ExpectInt(params.steering_media_publish_interval_ms,
                  NumberField(json, "steering_media_publish_interval_ms"),
                  "steering_media_publish_interval_ms");
        ExpectInt(params.low_voltage_sample_interval_ms,
                  NumberField(json, "low_voltage_sample_interval_ms"),
                  "low_voltage_sample_interval_ms");
        ExpectInt(params.camera_frame_width, NumberField(json, "camera_frame_width"), "camera_frame_width");
        ExpectInt(params.camera_frame_height, NumberField(json, "camera_frame_height"), "camera_frame_height");

        const std::string assistant_tcp = ObjectBody(json, "assistant_tcp");
        Expect(params.assistant_tcp.host == StringField(assistant_tcp, "host"), "assistant_tcp.host mismatch");
        ExpectInt(params.assistant_tcp.port, NumberField(assistant_tcp, "port"), "assistant_tcp.port");

        const std::string projector = ObjectBody(json, "BEV_PROJECTOR");
        ExpectBool(params.bev_projector.valid, NumberField(projector, "VALID"), "BEV_PROJECTOR.VALID");
        Expect(params.bev_projector.projector_id == StringField(projector, "PROJECTOR_ID"),
               "BEV_PROJECTOR.PROJECTOR_ID mismatch");
        Expect(params.bev_projector.projector_hash == StringField(projector, "PROJECTOR_HASH"),
               "BEV_PROJECTOR.PROJECTOR_HASH mismatch");
        ExpectInt(params.bev_projector.debug_grid_width,
                  NumberField(projector, "DEBUG_GRID_WIDTH"),
                  "BEV_PROJECTOR.DEBUG_GRID_WIDTH");
        ExpectInt(params.bev_projector.debug_grid_height,
                  NumberField(projector, "DEBUG_GRID_HEIGHT"),
                  "BEV_PROJECTOR.DEBUG_GRID_HEIGHT");
        for (std::size_t index = 0; index < ls2k::port::kBevCalibrationPointCount; ++index) {
            ExpectNear(params.bev_projector.source_points[index].row_px,
                       NumberField(projector, "SOURCE_ROW_" + std::to_string(index)),
                       "BEV_PROJECTOR.SOURCE_ROW_" + std::to_string(index));
            ExpectNear(params.bev_projector.source_points[index].col_px,
                       NumberField(projector, "SOURCE_COL_" + std::to_string(index)),
                       "BEV_PROJECTOR.SOURCE_COL_" + std::to_string(index));
            ExpectNear(params.bev_projector.target_points[index].forward_m,
                       NumberField(projector, "TARGET_FORWARD_" + std::to_string(index)),
                       "BEV_PROJECTOR.TARGET_FORWARD_" + std::to_string(index));
            ExpectNear(params.bev_projector.target_points[index].lateral_m,
                       NumberField(projector, "TARGET_LATERAL_" + std::to_string(index)),
                       "BEV_PROJECTOR.TARGET_LATERAL_" + std::to_string(index));
        }

        const std::string geometry = ObjectBody(json, "BEV_GEOMETRY");
        for (std::size_t index = 0; index < ls2k::port::kBevReferenceSampleCount; ++index) {
            ExpectNear(params.bev_geometry.forward_samples_m[index],
                       NumberField(geometry, "FORWARD_SAMPLE_" + std::to_string(index)),
                       "BEV_GEOMETRY.FORWARD_SAMPLE_" + std::to_string(index));
        }
        ExpectNear(params.bev_geometry.search_lateral_limit_m,
                   NumberField(geometry, "SEARCH_LATERAL_LIMIT_M"),
                   "BEV_GEOMETRY.SEARCH_LATERAL_LIMIT_M");
        ExpectNear(params.bev_geometry.lateral_step_m,
                   NumberField(geometry, "LATERAL_STEP_M"),
                   "BEV_GEOMETRY.LATERAL_STEP_M");

        const std::string classification = ObjectBody(json, "BEV_CLASSIFICATION");
        ExpectNear(params.bev_classification.white_confidence_min,
                   NumberField(classification, "WHITE_CONFIDENCE_MIN"),
                   "BEV_CLASSIFICATION.WHITE_CONFIDENCE_MIN");
        ExpectNear(params.bev_classification.unknown_confidence_min,
                   NumberField(classification, "UNKNOWN_CONFIDENCE_MIN"),
                   "BEV_CLASSIFICATION.UNKNOWN_CONFIDENCE_MIN");
        ExpectInt(params.bev_classification.hold_last_max_cycles,
                  NumberField(classification, "HOLD_LAST_MAX_CYCLES"),
                  "BEV_CLASSIFICATION.HOLD_LAST_MAX_CYCLES");

        const std::string control_model = ObjectBody(json, "BEV_CONTROL_MODEL");
        ExpectNear(params.bev_control_model.lateral_error_far_weight,
                   NumberField(control_model, "LATERAL_ERROR_FAR_WEIGHT"),
                   "BEV_CONTROL_MODEL.LATERAL_ERROR_FAR_WEIGHT");
        ExpectInRange(params.bev_control_model.lateral_error_far_weight,
                      0.01,
                      1.0,
                      "BEV_CONTROL_MODEL.LATERAL_ERROR_FAR_WEIGHT");
        ExpectInt(params.bev_control_model.lateral_error_max_weighted_sample_index,
                  NumberField(control_model, "LATERAL_ERROR_MAX_WEIGHTED_SAMPLE_INDEX"),
                  "BEV_CONTROL_MODEL.LATERAL_ERROR_MAX_WEIGHTED_SAMPLE_INDEX");
        ExpectInRange(params.bev_control_model.lateral_error_max_weighted_sample_index,
                      0.0,
                      static_cast<double>(ls2k::port::kBevReferenceSampleCount - 1U),
                      "BEV_CONTROL_MODEL.LATERAL_ERROR_MAX_WEIGHTED_SAMPLE_INDEX");
        ExpectNear(params.bev_control_model.lateral_error_to_wheel_delta_gain,
                   NumberField(control_model, "LATERAL_ERROR_TO_WHEEL_DELTA_GAIN"),
                   "BEV_CONTROL_MODEL.LATERAL_ERROR_TO_WHEEL_DELTA_GAIN");
        ExpectInRange(params.bev_control_model.lateral_error_to_wheel_delta_gain,
                      0.0,
                      1000.0,
                      "BEV_CONTROL_MODEL.LATERAL_ERROR_TO_WHEEL_DELTA_GAIN");
        Expect(control_model.find("\"LOOKAHEAD_VISIBLE_RANGE_RATIO\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain LOOKAHEAD_VISIBLE_RANGE_RATIO");
        Expect(control_model.find("\"LOOKAHEAD_MIN_M\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain LOOKAHEAD_MIN_M");
        Expect(control_model.find("\"LOOKAHEAD_MAX_M\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain LOOKAHEAD_MAX_M");
        Expect(control_model.find("\"PURE_PURSUIT_GAIN\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain PURE_PURSUIT_GAIN");
        Expect(control_model.find("\"CURVATURE_COMMAND_LIMIT\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain CURVATURE_COMMAND_LIMIT");
        Expect(control_model.find("\"CURVATURE_TO_TURN_OUTPUT_GAIN\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain CURVATURE_TO_TURN_OUTPUT_GAIN");
        Expect(control_model.find("\"CURVATURE_TO_YAW_RATE_TARGET_GAIN\"") == std::string::npos,
               "BEV_CONTROL_MODEL must not retain CURVATURE_TO_YAW_RATE_TARGET_GAIN alias");
        ExpectInt(params.bev_control_model.min_leading_reference_samples,
                  NumberField(control_model, "MIN_LEADING_REFERENCE_SAMPLES"),
                  "BEV_CONTROL_MODEL.MIN_LEADING_REFERENCE_SAMPLES");

        const std::string element = ObjectBody(json, "BEV_ELEMENT");
        ExpectBool(params.bev_element.cross_exit_takeover_enabled,
                   NumberField(element, "CROSS_EXIT_TAKEOVER_ENABLED"),
                   "BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED");

        const std::string element_raster = ObjectBody(json, "BEV_ELEMENT_RASTER");
        ExpectBool(params.bev_element_raster.enabled,
                   NumberField(element_raster, "ENABLED"),
                   "BEV_ELEMENT_RASTER.ENABLED");
        ExpectInt(params.bev_element_raster.width,
                  NumberField(element_raster, "WIDTH"),
                  "BEV_ELEMENT_RASTER.WIDTH");

        std::cout << "runtime_parameter_defaults_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime_parameter_defaults_test failed: " << error.what() << '\n';
        return 1;
    }
}
