#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/bootstrap.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct RecordingDiagnostics final : ls2k::port::DiagnosticSink {
    std::vector<ls2k::port::DiagnosticEvent> events{};

    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }
};

class TempFile final {
public:
    explicit TempFile(std::string contents) {
        char pattern[] = "/tmp/ls2k_runtime_params_test_XXXXXX.json";
        const int fd = mkstemps(pattern, 5);
        if (fd < 0) {
            throw std::runtime_error("mkstemps failed");
        }
        path_ = pattern;
        std::FILE* file = fdopen(fd, "w");
        if (file == nullptr) {
            close(fd);
            std::remove(path_.c_str());
            throw std::runtime_error("fdopen failed");
        }
        const std::size_t written = std::fwrite(contents.data(), 1, contents.size(), file);
        if (written != contents.size() || std::fclose(file) != 0) {
            std::remove(path_.c_str());
            throw std::runtime_error("failed to write temp file");
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    TempFile(TempFile&& other) noexcept : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    TempFile& operator=(TempFile&& other) noexcept {
        if (this != &other) {
            Cleanup();
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    ~TempFile() {
        Cleanup();
    }

    const std::string& path() const {
        return path_;
    }

private:
    void Cleanup() {
        if (!path_.empty()) {
            std::remove(path_.c_str());
        }
    }

    std::string path_{};
};

bool HasDiagnosticCode(const RecordingDiagnostics& diagnostics, const std::string& code) {
    for (const auto& event : diagnostics.events) {
        if (event.code == code) {
            return true;
        }
    }
    return false;
}

std::string ValidRuntimeParamsJson() {
    return R"JSON({
  "Speed_base": 150.0,
  "see_max": 24.0,
  "PID_TURN_CAMERA": {
    "USE_FUZZY": 0,
    "P": 58.0,
    "P_SCALE": 1.0,
    "D": 10.0
  },
  "PID_TURN_GYRO_CAMERA": {
    "P": 1.0,
    "I": 0.0,
    "D": 0.20
  },
  "P_Mode": 3,
  "exp_light": 65,
  "LEFT_WHEEL_PID": {
    "P": 84.0,
    "I": 2.4,
    "D": 0.75,
    "INTEGRAL_LIMIT": 2200.0,
    "MEASUREMENT_FILTER_ALPHA": 0.4
  },
  "RIGHT_WHEEL_PID": {
    "P": 96.0,
    "I": 2.2,
    "D": 0.2,
    "INTEGRAL_LIMIT": 2200.0,
    "MEASUREMENT_FILTER_ALPHA": 0.4
  },
  "BEV_PROJECTOR": {
    "VALID": 1,
    "PROJECTOR_ID": "bev_projector_default_v1",
    "PROJECTOR_HASH": "bev-projector-default-20260426",
    "DEBUG_GRID_WIDTH": 160,
    "DEBUG_GRID_HEIGHT": 128,
    "SOURCE_ROW_0": 220.0,
    "SOURCE_COL_0": 72.0,
    "SOURCE_ROW_1": 220.0,
    "SOURCE_COL_1": 248.0,
    "SOURCE_ROW_2": 148.0,
    "SOURCE_COL_2": 122.0,
    "SOURCE_ROW_3": 148.0,
    "SOURCE_COL_3": 198.0,
    "TARGET_FORWARD_0": 0.45,
    "TARGET_LATERAL_0": -0.23,
    "TARGET_FORWARD_1": 0.45,
    "TARGET_LATERAL_1": 0.23,
    "TARGET_FORWARD_2": 2.25,
    "TARGET_LATERAL_2": -0.36,
    "TARGET_FORWARD_3": 2.25,
    "TARGET_LATERAL_3": 0.36
  },
  "BEV_GEOMETRY": {
    "FORWARD_SAMPLE_0": 0.30,
    "FORWARD_SAMPLE_1": 0.55,
    "FORWARD_SAMPLE_2": 0.80,
    "FORWARD_SAMPLE_3": 1.10,
    "FORWARD_SAMPLE_4": 1.40,
    "FORWARD_SAMPLE_5": 1.70,
    "FORWARD_SAMPLE_6": 2.00,
    "FORWARD_SAMPLE_7": 2.30,
    "SEARCH_LATERAL_LIMIT_M": 0.65,
    "LATERAL_STEP_M": 0.02,
    "NOMINAL_LANE_WIDTH_M": 0.42,
    "MIN_LANE_WIDTH_M": 0.20,
    "MAX_LANE_WIDTH_M": 0.75,
    "MIN_VISIBLE_RANGE_M": 0.80,
    "MIN_TRACK_CONFIDENCE": 0.25,
    "CONTINUITY_BREAK_THRESHOLD_M": 0.28,
    "SAMPLE_ROW_STEP_PX": 4,
    "IMAGE_BORDER_TRUNCATION_MARGIN_PX": 3
  },
  "BEV_SCENE_FSM": {
    "BEND_SEVERITY_CONFIRM": 0.20,
    "CROSS_EXPAND_RATIO_MIN": 1.18,
    "CROSS_BILATERAL_OPEN_MIN_M": 0.05,
    "CROSS_CONFIRM_CYCLES": 2,
    "CROSS_HOLD_CYCLES": 2,
    "ZEBRA_TRANSITION_DENSITY_MIN": 7.0,
    "ZEBRA_HOLD_CYCLES": 2,
    "CIRCLE_OPEN_SCORE_MIN": 0.18,
    "CIRCLE_CONTRACT_SCORE_MIN": 0.12,
    "CIRCLE_OPPOSITE_HEADING_ABS_MAX": 0.22,
    "CIRCLE_CONFIRM_CYCLES": 2,
    "CIRCLE_RELEASE_CYCLES": 3,
    "RELEASE_TRACK_CONFIDENCE_MIN": 0.55
  },
  "BEV_CONTROL_MODEL": {
    "NEAR_SAMPLE_INDEX": 1,
    "FAR_SAMPLE_INDEX": 4,
    "CURVATURE_SAMPLE_INDEX": 5,
    "LOOKAHEAD_VISIBLE_RANGE_RATIO": 0.35,
    "LOOKAHEAD_MIN_M": 1.2,
    "LOOKAHEAD_MAX_M": 2.0,
    "PURE_PURSUIT_GAIN": 1.0,
    "HEADING_CURVATURE_GAIN": 0.35,
    "CURVATURE_FEEDFORWARD_GAIN": 0.20,
    "CURVATURE_COMMAND_LIMIT": 0.12,
    "CURVATURE_TO_W_TARGET_GAIN": 12000,
    "LOW_CONFIDENCE_THRESHOLD": 0.35,
    "STEERING_SUPPRESSION_CONFIDENCE": 0.12,
    "LOW_VISIBLE_RANGE_M": 0.80,
    "MIN_GAIN_SCALE": 0.25,
    "MIN_SPEED_LIMIT_SCALE": 0.35,
    "MAX_REFERENCE_BIAS_M": 0.20
  },
  "assistant_tcp": {
    "host": "10.100.170.115",
    "port": 8888
  },
  "assistant_enabled": 1,
  "camera_frame_width": 320,
  "camera_frame_height": 240
})JSON";
}

void TestLoadSucceedsWithoutRemovedLegacyFields() {
    RecordingDiagnostics diagnostics;
    auto store = ls2k::platform::MakeParamStore();
    ls2k::port::RuntimeParameters params{};
    TempFile json(ValidRuntimeParamsJson());

    const bool ok = store->LoadRuntimeParameters(json.path(), params, diagnostics);

    Expect(ok, "parameter load should return true for a valid JSON file");
    Expect(!params.loaded_from_defaults, "valid parameter file must not fall back to defaults");
    Expect(!params.parse_failure, "valid parameter file must not report parse failure");
    Expect(params.Speed_base == 150.0, "Speed_base must load from the trimmed runtime parameter surface");
    Expect(params.see_max == 24.0, "see_max must remain required and load correctly");
    Expect(params.pid_turn_camera_d == 10.0, "PID_TURN_CAMERA.D must remain required");
    Expect(params.pid_turn_gyro_camera_d == 0.20, "PID_TURN_GYRO_CAMERA.D must remain required");
    Expect(params.P_Mode == 3, "P_Mode must remain required");
    Expect(params.exp_light == 65, "exp_light must remain required");
    Expect(params.bev_projector.valid, "BEV_PROJECTOR must load validity");
    Expect(params.bev_projector.projector_hash == "bev-projector-default-20260426",
           "BEV projector hash must load");
    Expect(params.bev_projector.target_points[2].forward_m == 2.25F,
           "BEV projector target points must load");
    Expect(params.bev_geometry.forward_samples_m[4] == 1.40F,
           "BEV geometry forward samples must load");
    Expect(params.bev_geometry.nominal_lane_width_m == 0.42F,
           "BEV geometry lane width contract must load");
    Expect(params.bev_geometry.image_border_truncation_margin_px == 3,
           "BEV geometry border truncation margin must load");
    Expect(params.bev_scene_fsm.circle_release_cycles == 3,
           "BEV scene FSM cycles must load");
    Expect(params.bev_scene_fsm.cross_bilateral_open_min_m == 0.05F,
           "BEV scene FSM cross bilateral-open threshold must load");
    Expect(params.bev_control_model.lookahead_visible_range_ratio == 0.35,
           "BEV control model lookahead ratio must load");
    Expect(params.bev_control_model.curvature_to_w_target_gain == 12000.0,
           "BEV control model curvature gain must load");
    Expect(HasDiagnosticCode(diagnostics, "params.loaded"),
           "successful parse must emit params.loaded");
}

void TestMissingRemovedLegacySceneGroupsDoesNotFailParsing() {
    RecordingDiagnostics diagnostics;
    auto store = ls2k::platform::MakeParamStore();
    ls2k::port::RuntimeParameters params{};
    TempFile json(ValidRuntimeParamsJson());

    const bool ok = store->LoadRuntimeParameters(json.path(), params, diagnostics);

    Expect(ok, "trimmed parameter surface must still load successfully");
    Expect(!params.loaded_from_defaults,
           "removed legacy scene groups must not be treated as missing required fields");
    Expect(!params.parse_failure, "removed legacy scene groups must not trigger parse failure");
    Expect(params.left_wheel_pid.p == 84.0, "other required fields must still load unchanged");
    Expect(params.right_wheel_pid.p == 96.0, "other required fields must still load unchanged");
}

void TestStillFailsWhenCurrentRequiredFieldIsMissing() {
    RecordingDiagnostics diagnostics;
    auto store = ls2k::platform::MakeParamStore();
    ls2k::port::RuntimeParameters params{};

    std::string json_text = ValidRuntimeParamsJson();
    const std::string needle = R"(  "see_max": 24.0,
)";
    const std::size_t position = json_text.find(needle);
    Expect(position != std::string::npos, "test fixture must include see_max");
    json_text.erase(position, needle.size());
    TempFile json(std::move(json_text));

    const bool ok = store->LoadRuntimeParameters(json.path(), params, diagnostics);

    Expect(ok, "parse failures still return true while falling back to defaults");
    Expect(params.loaded_from_defaults,
           "missing current required fields must still fall back to built-in defaults");
    Expect(params.parse_failure, "missing current required fields must still report parse failure");
    Expect(HasDiagnosticCode(diagnostics, "params.parse"),
           "parse fallback must emit params.parse");
}

}  // namespace

int main() {
    try {
        TestLoadSucceedsWithoutRemovedLegacyFields();
        TestMissingRemovedLegacySceneGroupsDoesNotFailParsing();
        TestStillFailsWhenCurrentRequiredFieldIsMissing();
    } catch (const TestFailure& failure) {
        std::cerr << "runtime_params_load_test failed: " << failure.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "runtime_params_load_test unexpected exception: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
