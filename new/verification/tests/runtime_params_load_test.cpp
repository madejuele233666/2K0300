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
  "SCENE_WIDE_CLASSIFIER": {
    "LOWER_ROW_START": 156,
    "LOWER_ROW_END": 184,
    "MIDDLE_ROW_START": 120,
    "MIDDLE_ROW_END": 148,
    "UPPER_ROW_START": 80,
    "UPPER_ROW_END": 112,
    "ROW_STEP": 4,
    "EDGE_MARGIN_PX": 12,
    "UPPER_FULL_SPAN_WIDTH_RATIO": 0.95,
    "SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO": 0.38,
    "SPECIAL_WIDE_VALID_ROWS_MIN": 10,
    "EDGE_MOTION_MIN_PX": 8,
    "EDGE_CURVATURE_MIN_PX": 6,
    "OPPOSITE_EDGE_STRAIGHT_MAX_CURVATURE_PX": 5,
    "OPPOSITE_EDGE_BORDER_TOUCH_MAX_RATIO": 0.45,
    "CIRCLE_OPEN_MIN_PX": 24,
    "CIRCLE_CONTRACT_MIN_PX": 14,
    "CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN": 3,
    "CROSS_UPPER_FULL_SPAN_MIN_RATIO": 0.45,
    "TO_CROSS_MARGIN": 0.2,
    "TO_CIRCLE_MARGIN": 0.2,
    "TO_CIRCLE_OVER_BEND_MARGIN": 2.0,
    "ENTER_CONFIRM_CYCLES": 2,
    "EXIT_CONFIRM_CYCLES": 2,
    "CROSS_WEIGHT_FULL_SPAN": 1.25,
    "CROSS_WEIGHT_BOTH_OPEN": 0.4,
    "CIRCLE_CURVE_WEIGHT": 1.2,
    "CIRCLE_OPPOSITE_STRAIGHT_WEIGHT": 1.0,
    "CIRCLE_WEIGHT_OPEN": 0.25,
    "CIRCLE_WEIGHT_CONTRACT": 0.2
  },
  "CIRCLE_SCENE": {
    "ACTIVE_VALID_ROWS_MIN": 8,
    "MINIMUM_TRACK_CONFIDENCE": 0.35
  },
  "CIRCLE_ENTRY": {
    "ENTRY_INNER_OFFSET_NEAR_PX": 48,
    "ENTRY_INNER_OFFSET_FAR_PX": 28,
    "ENTRY_REPAIR_OVER_DEG": 45.0,
    "ENTRY_SETTLE_CONFIRM_CYCLES": 3,
    "ENTRY_RELEASE_LOSS_CYCLES": 2
  },
  "CIRCLE_INTERIOR": {
    "INTERIOR_INNER_OFFSET_PX": 40,
    "INTERIOR_BLEND_ENABLE": 1,
    "INTERIOR_BLEND_MIN_CONFIDENCE": 0.55
  },
  "CIRCLE_EXIT": {
    "EXIT_OUTER_OFFSET_NEAR_PX": 34,
    "EXIT_OUTER_OFFSET_FAR_PX": 20,
    "EXIT_HANDOVER_START_DEG": 180.0,
    "HANDOVER_CONFIRM_CYCLES": 2,
    "HANDOVER_RAMP_CYCLES": 4,
    "EXIT_RELEASE_CYCLES": 3,
    "EXIT_COMPLETE_DEG": 300.0,
    "EXIT_OPPOSITE_EDGE_STRAIGHT_CONFIRM_CYCLES": 2,
    "EXIT_OPPOSITE_EDGE_MAX_CURVATURE_PX": 5,
    "EXIT_OPPOSITE_EDGE_MIN_VISIBLE_ROWS": 3,
    "EXIT_FIXSTEER_START_DEG": 235.0,
    "EXIT_FALLBACK_MAX_CYCLES": 6
  },
  "CIRCLE_FALLBACK": {
    "FIXSTEER_BIAS_SCALE": 0.55
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
    Expect(params.scene_wide_classifier.enter_confirm_cycles == 2,
           "SCENE_WIDE_CLASSIFIER block must load independently");
    Expect(params.scene_wide_classifier.cross_weight_full_span == 1.25,
           "SCENE_WIDE_CLASSIFIER weights must load without touching legacy fields");
    Expect(params.scene_wide_classifier.edge_motion_min_px == 8,
           "SCENE_WIDE_CLASSIFIER geometry thresholds must load correctly");
    Expect(params.scene_wide_classifier.circle_curve_weight == 1.2,
           "SCENE_WIDE_CLASSIFIER circle weights must load correctly");
    Expect(params.scene_wide_classifier.to_circle_over_bend_margin == 2.0,
           "SCENE_WIDE_CLASSIFIER bend arbitration margin must load correctly");
    Expect(params.circle_entry.inner_offset_near_px == 48,
           "CIRCLE_ENTRY group must load independently");
    Expect(params.circle_entry.release_loss_cycles == 2,
           "CIRCLE_ENTRY release loss cycles must load independently");
    Expect(params.circle_exit.exit_complete_deg == 300.0,
           "CIRCLE_EXIT group must load independently");
    Expect(params.circle_fallback.fixsteer_bias_scale == 0.55,
           "CIRCLE_FALLBACK group must load independently");
    Expect(HasDiagnosticCode(diagnostics, "params.loaded"),
           "successful parse must emit params.loaded");
}

void TestMissingRemovedLegacyFieldsDoesNotFailParsing() {
    RecordingDiagnostics diagnostics;
    auto store = ls2k::platform::MakeParamStore();
    ls2k::port::RuntimeParameters params{};
    TempFile json(ValidRuntimeParamsJson());

    const bool ok = store->LoadRuntimeParameters(json.path(), params, diagnostics);

    Expect(ok, "trimmed parameter surface must still load successfully");
    Expect(!params.loaded_from_defaults,
           "removed legacy keys must not be treated as missing required fields");
    Expect(!params.parse_failure, "removed legacy keys must not trigger parse failure");
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
        TestMissingRemovedLegacyFieldsDoesNotFailParsing();
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
