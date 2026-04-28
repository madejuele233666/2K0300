#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_bev_projector.hpp"

namespace {

constexpr int kMinBoundaryEvidenceImageMarginPx = 12;

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

struct JsonValue {
    enum class Kind {
        kNull,
        kBool,
        kNumber,
        kString,
        kArray,
        kObject
    };

    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    Kind kind = Kind::kNull;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value{};
    Array array_value{};
    Object object_value{};
};

class JsonParser final {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue Parse() {
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (pos_ != text_.size()) {
            Fail("unexpected trailing input");
        }
        return value;
    }

private:
    JsonValue ParseValue() {
        SkipWhitespace();
        if (pos_ >= text_.size()) {
            Fail("expected value");
        }

        const char ch = text_[pos_];
        if (ch == '{') {
            return ParseObject();
        }
        if (ch == '[') {
            return ParseArray();
        }
        if (ch == '"') {
            JsonValue value{};
            value.kind = JsonValue::Kind::kString;
            value.string_value = ParseStringLiteral();
            return value;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return ParseNumber();
        }
        if (StartsWith("true")) {
            pos_ += 4;
            JsonValue value{};
            value.kind = JsonValue::Kind::kBool;
            value.bool_value = true;
            return value;
        }
        if (StartsWith("false")) {
            pos_ += 5;
            JsonValue value{};
            value.kind = JsonValue::Kind::kBool;
            value.bool_value = false;
            return value;
        }
        if (StartsWith("null")) {
            pos_ += 4;
            JsonValue value{};
            value.kind = JsonValue::Kind::kNull;
            return value;
        }
        Fail("expected object, array, string, number, boolean, or null");
    }

    JsonValue ParseObject() {
        ExpectChar('{');
        JsonValue value{};
        value.kind = JsonValue::Kind::kObject;

        SkipWhitespace();
        if (TryChar('}')) {
            return value;
        }

        while (true) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                Fail("expected object key");
            }
            std::string key = ParseStringLiteral();
            SkipWhitespace();
            ExpectChar(':');
            JsonValue element = ParseValue();
            const auto inserted = value.object_value.emplace(std::move(key), std::move(element));
            if (!inserted.second) {
                Fail("duplicate object key");
            }

            SkipWhitespace();
            if (TryChar('}')) {
                return value;
            }
            ExpectChar(',');
        }
    }

    JsonValue ParseArray() {
        ExpectChar('[');
        JsonValue value{};
        value.kind = JsonValue::Kind::kArray;

        SkipWhitespace();
        if (TryChar(']')) {
            return value;
        }

        while (true) {
            value.array_value.push_back(ParseValue());
            SkipWhitespace();
            if (TryChar(']')) {
                return value;
            }
            ExpectChar(',');
        }
    }

    JsonValue ParseNumber() {
        const std::size_t start = pos_;
        if (TryChar('-')) {
            if (pos_ >= text_.size()) {
                Fail("incomplete number");
            }
        }

        if (TryChar('0')) {
        } else {
            RequireDigit("expected number digit");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        }

        if (TryChar('.')) {
            RequireDigit("expected fractional digit");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        }

        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            RequireDigit("expected exponent digit");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        }

        JsonValue value{};
        value.kind = JsonValue::Kind::kNumber;
        value.number_value = std::stod(text_.substr(start, pos_ - start));
        return value;
    }

    std::string ParseStringLiteral() {
        ExpectChar('"');
        std::string output{};
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') {
                return output;
            }
            if (ch == '\\') {
                if (pos_ >= text_.size()) {
                    Fail("unterminated string escape");
                }
                const char escaped = text_[pos_++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        output.push_back(escaped);
                        break;
                    case 'b':
                        output.push_back('\b');
                        break;
                    case 'f':
                        output.push_back('\f');
                        break;
                    case 'n':
                        output.push_back('\n');
                        break;
                    case 'r':
                        output.push_back('\r');
                        break;
                    case 't':
                        output.push_back('\t');
                        break;
                    case 'u':
                        for (int i = 0; i < 4; ++i) {
                            if (pos_ >= text_.size() ||
                                std::isxdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
                                Fail("invalid unicode escape");
                            }
                            ++pos_;
                        }
                        output.push_back('?');
                        break;
                    default:
                        Fail("invalid string escape");
                }
            } else {
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    Fail("control character in string");
                }
                output.push_back(ch);
            }
        }
        Fail("unterminated string");
    }

    void SkipWhitespace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool StartsWith(const char* literal) const {
        const std::string expected(literal);
        return text_.compare(pos_, expected.size(), expected) == 0;
    }

    bool TryChar(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void ExpectChar(char expected) {
        if (!TryChar(expected)) {
            Fail(std::string("expected '") + expected + "'");
        }
    }

    void RequireDigit(const std::string& message) {
        if (pos_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
            Fail(message);
        }
        ++pos_;
    }

    [[noreturn]] void Fail(const std::string& message) const {
        throw std::runtime_error("manifest json parse error at byte " + std::to_string(pos_) + ": " + message);
    }

    std::string text_{};
    std::size_t pos_ = 0U;
};

JsonValue::Object RequireObject(const JsonValue& value, const std::string& context) {
    if (value.kind != JsonValue::Kind::kObject) {
        throw std::runtime_error(context + " must be an object");
    }
    return value.object_value;
}

JsonValue::Array RequireArray(const JsonValue& value, const std::string& context) {
    if (value.kind != JsonValue::Kind::kArray) {
        throw std::runtime_error(context + " must be an array");
    }
    return value.array_value;
}

std::string RequireString(const JsonValue& value, const std::string& context) {
    if (value.kind != JsonValue::Kind::kString) {
        throw std::runtime_error(context + " must be a string");
    }
    return value.string_value;
}

bool RequireBool(const JsonValue& value, const std::string& context) {
    if (value.kind != JsonValue::Kind::kBool) {
        throw std::runtime_error(context + " must be a boolean");
    }
    return value.bool_value;
}

double RequireNumber(const JsonValue& value, const std::string& context) {
    if (value.kind != JsonValue::Kind::kNumber) {
        throw std::runtime_error(context + " must be a number");
    }
    return value.number_value;
}

const JsonValue& RequireMember(const JsonValue::Object& object,
                               const std::string& key,
                               const std::string& context) {
    const auto it = object.find(key);
    if (it == object.end()) {
        throw std::runtime_error(context + " missing required field " + key);
    }
    return it->second;
}

const JsonValue* FindMember(const JsonValue::Object& object, const std::string& key) {
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string ReadOptionalStringField(const JsonValue::Object& object,
                                    const std::string& key,
                                    const std::string& fallback,
                                    const std::string& context) {
    const JsonValue* value = FindMember(object, key);
    return value == nullptr ? fallback : RequireString(*value, context + "." + key);
}

bool ReadOptionalBoolField(const JsonValue::Object& object,
                           const std::string& key,
                           bool fallback,
                           const std::string& context) {
    const JsonValue* value = FindMember(object, key);
    return value == nullptr ? fallback : RequireBool(*value, context + "." + key);
}

std::vector<std::string> ReadStringArrayField(const JsonValue::Object& object,
                                              const std::string& key,
                                              const std::string& context) {
    std::vector<std::string> output{};
    JsonValue::Array values = RequireArray(RequireMember(object, key, context), context + "." + key);
    for (std::size_t index = 0; index < values.size(); ++index) {
        output.push_back(RequireString(values[index], context + "." + key + "[" + std::to_string(index) + "]"));
    }
    return output;
}

ls2k::port::ImagePoint ReadImagePoint(const JsonValue& value, const std::string& context) {
    const JsonValue::Object object = RequireObject(value, context);
    ls2k::port::ImagePoint point{};
    point.row_px = static_cast<float>(RequireNumber(RequireMember(object, "row_px", context),
                                                    context + ".row_px"));
    point.col_px = static_cast<float>(RequireNumber(RequireMember(object, "col_px", context),
                                                    context + ".col_px"));
    return point;
}

ls2k::port::BEVPoint ReadBevPoint(const JsonValue& value, const std::string& context) {
    const JsonValue::Object object = RequireObject(value, context);
    ls2k::port::BEVPoint point{};
    point.forward_m =
        static_cast<float>(RequireNumber(RequireMember(object, "forward_m", context),
                                         context + ".forward_m"));
    point.lateral_m =
        static_cast<float>(RequireNumber(RequireMember(object, "lateral_m", context),
                                         context + ".lateral_m"));
    return point;
}

ls2k::port::BEVProjectorCalibration ReadProjectorCalibration(const JsonValue& value,
                                                             const std::string& context,
                                                             const std::string& projector_id) {
    const JsonValue::Object object = RequireObject(value, context);
    const JsonValue::Array source_points =
        RequireArray(RequireMember(object, "source_points", context), context + ".source_points");
    const JsonValue::Array target_points =
        RequireArray(RequireMember(object, "target_points", context), context + ".target_points");
    Expect(source_points.size() == ls2k::port::kBevCalibrationPointCount,
           context + ".source_points must contain exactly " +
               std::to_string(ls2k::port::kBevCalibrationPointCount) + " points");
    Expect(target_points.size() == ls2k::port::kBevCalibrationPointCount,
           context + ".target_points must contain exactly " +
               std::to_string(ls2k::port::kBevCalibrationPointCount) + " points");

    ls2k::port::BEVProjectorCalibration calibration{};
    calibration.valid = true;
    calibration.projector_id = projector_id;
    calibration.projector_hash =
        ReadOptionalStringField(object, "projector_hash", calibration.projector_hash, context);
    for (std::size_t index = 0; index < ls2k::port::kBevCalibrationPointCount; ++index) {
        calibration.source_points[index] =
            ReadImagePoint(source_points[index], context + ".source_points[" + std::to_string(index) + "]");
        calibration.target_points[index] =
            ReadBevPoint(target_points[index], context + ".target_points[" + std::to_string(index) + "]");
    }
    return calibration;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open text file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct SampleContract {
    std::string file{};
    std::string label{};
    std::string authority_role{};
    bool require_valid_track = false;
    bool forbid_valid_track = false;
    std::vector<std::string> require_active_modules{};
    std::vector<std::string> forbid_active_modules{};
    std::vector<std::string> require_primary_candidates{};
    std::vector<std::string> forbid_primary_candidates{};
};

struct Manifest {
    std::string schema{};
    std::string projector_id{};
    double cross_bilateral_open_min_m = 0.0;
    bool has_projector_calibration = false;
    ls2k::port::BEVProjectorCalibration projector_calibration{};
    std::vector<SampleContract> samples{};
};

Manifest LoadManifest(const std::filesystem::path& dataset_dir) {
    const std::filesystem::path manifest_path = dataset_dir / "manifest.json";
    JsonValue root = JsonParser(ReadTextFile(manifest_path)).Parse();
    JsonValue::Object root_object = RequireObject(root, "manifest root");

    Manifest manifest{};
    manifest.schema = RequireString(RequireMember(root_object, "schema", "manifest"), "manifest.schema");
    Expect(manifest.schema == "bev-authority-baseline-v1",
           manifest_path.string() + " must use schema bev-authority-baseline-v1");
    manifest.projector_id =
        RequireString(RequireMember(root_object, "projector_id", "manifest"), "manifest.projector_id");
    if (const JsonValue* calibration = FindMember(root_object, "projector_calibration")) {
        manifest.projector_calibration =
            ReadProjectorCalibration(*calibration, "manifest.projector_calibration", manifest.projector_id);
        manifest.has_projector_calibration = true;
    }

    JsonValue::Object parameter_contract =
        RequireObject(RequireMember(root_object, "parameter_contract", "manifest"),
                      "manifest.parameter_contract");
    manifest.cross_bilateral_open_min_m =
        RequireNumber(RequireMember(parameter_contract,
                                    "cross_bilateral_open_min_m",
                                    "manifest.parameter_contract"),
                      "manifest.parameter_contract.cross_bilateral_open_min_m");

    JsonValue::Array samples =
        RequireArray(RequireMember(root_object, "samples", "manifest"), "manifest.samples");
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const std::string context = "manifest.samples[" + std::to_string(index) + "]";
        JsonValue::Object object = RequireObject(samples[index], context);

        SampleContract sample{};
        sample.file = RequireString(RequireMember(object, "file", context), context + ".file");
        sample.label = RequireString(RequireMember(object, "label", context), context + ".label");
        sample.authority_role =
            RequireString(RequireMember(object, "authority_role", context), context + ".authority_role");
        sample.require_valid_track =
            RequireBool(RequireMember(object, "require_valid_track", context), context + ".require_valid_track");
        sample.forbid_valid_track =
            ReadOptionalBoolField(object, "forbid_valid_track", false, context);
        sample.require_active_modules = ReadStringArrayField(object, "require_active_modules", context);
        sample.forbid_active_modules = ReadStringArrayField(object, "forbid_active_modules", context);
        sample.require_primary_candidates =
            ReadStringArrayField(object, "require_primary_candidates", context);
        sample.forbid_primary_candidates =
            ReadStringArrayField(object, "forbid_primary_candidates", context);
        manifest.samples.push_back(std::move(sample));
    }

    Expect(!manifest.samples.empty(), manifest_path.string() + " must list at least one sample");
    return manifest;
}

ls2k::port::LegacyCameraFrame ReadRawFrame(const std::filesystem::path& path) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open raw frame: " + path.string());
    }
    input.read(reinterpret_cast<char*>(frame.gray.data()),
               static_cast<std::streamsize>(frame.PixelCount()));
    if (input.gcount() != static_cast<std::streamsize>(frame.PixelCount())) {
        throw std::runtime_error("unexpected raw frame size: " + path.string());
    }
    return frame;
}

struct SequenceResult {
    ls2k::legacy::SteeringAnalysisResult final{};
    ls2k::legacy::SteeringAnalysisResult best_valid{};
    bool have_best_valid = false;
    bool saw_valid_track = false;
    bool saw_straight = false;
    bool saw_cross = false;
    bool saw_circle = false;
    bool saw_lost_prediction = false;
};

SequenceResult AnalyzeWithConfirm(const ls2k::port::LegacyCameraFrame& frame,
                                  const ls2k::port::RuntimeParameters& params) {
    ls2k::port::LegacySteeringState state{};
    SequenceResult sequence{};
    for (int cycle = 0; cycle < 4; ++cycle) {
        sequence.final =
            ls2k::legacy::AnalyzeFrame(frame, params, state, {}, false, static_cast<std::uint64_t>(cycle + 1), 1);
        Expect(sequence.final.steering_state_update_valid, "AnalyzeFrame must return a state update");

        sequence.saw_straight = sequence.saw_straight || sequence.final.perception.active_module == "straight";
        sequence.saw_cross = sequence.saw_cross || sequence.final.perception.active_module == "cross";
        sequence.saw_circle = sequence.saw_circle || sequence.final.perception.active_module == "circle";
        sequence.saw_lost_prediction =
            sequence.saw_lost_prediction || sequence.final.perception.scene_phase == "lost_prediction";
        sequence.saw_valid_track = sequence.saw_valid_track || sequence.final.track_estimate.valid;

        if (sequence.final.track_estimate.valid) {
            sequence.best_valid = sequence.final;
            sequence.have_best_valid = true;
        }
        state = sequence.final.steering_state_update;
    }
    return sequence;
}

bool SawActiveModule(const SequenceResult& sequence, const std::string& module) {
    if (module == "straight") {
        return sequence.saw_straight;
    }
    if (module == "cross") {
        return sequence.saw_cross;
    }
    if (module == "circle") {
        return sequence.saw_circle;
    }
    if (module == "lost_prediction") {
        return sequence.saw_lost_prediction;
    }
    throw TestFailure{"unsupported active_module contract: " + module};
}

bool PrimaryCandidate(const ls2k::legacy::SteeringAnalysisResult& result,
                      const ls2k::port::RuntimeParameters& params,
                      const std::string& candidate) {
    const ls2k::port::TopologyEvidence& evidence = result.topology_evidence;
    if (candidate == "topology_track") {
        return result.track_estimate.valid && result.track_estimate.source == "bev_corridor_topology";
    }
    if (candidate == "ordinary_topology") {
        return evidence.ordinary_score > 0.0F;
    }
    if (candidate == "bend_veto_topology") {
        return evidence.bend_veto_score > 0.20F;
    }
    if (candidate == "cross_topology") {
        return evidence.cross_score >= params.bev_topology_evidence.cross_enter_score;
    }
    if (candidate == "circle_left_topology") {
        return evidence.left_circle_score >= params.bev_topology_evidence.circle_enter_score;
    }
    if (candidate == "circle_right_topology") {
        return evidence.right_circle_score >= params.bev_topology_evidence.circle_enter_score;
    }
    if (candidate == "lost_topology") {
        return evidence.lost_score > 0.60F;
    }
    throw TestFailure{"unsupported topology candidate contract: " + candidate};
}

bool HasNearbyBoundarySupport(
    const std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
    std::size_t layer,
    const ls2k::port::RuntimeParameters& params) {
    if (layer >= path.size() || !path[layer].valid) {
        return false;
    }
    constexpr std::size_t kMaxLayerGap = 2U;
    const float jump_limit = std::max(0.04F, params.bev_corridor_graph.max_center_jump_m * 0.55F);
    for (std::size_t gap = 1U; gap <= kMaxLayerGap; ++gap) {
        if (layer >= gap && path[layer - gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer - gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
        if (layer + gap < path.size() && path[layer + gap].valid) {
            const float lateral_jump =
                std::abs(path[layer].point.lateral_m - path[layer + gap].point.lateral_m);
            if (lateral_jump <= jump_limit * static_cast<float>(gap)) {
                return true;
            }
        }
    }
    return false;
}

void ValidateObservedBoundaryContinuity(
    const std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
    const ls2k::port::RuntimeParameters& params,
    const std::filesystem::path& frame_path,
    const std::string& side) {
    for (std::size_t layer = 0; layer < path.size(); ++layer) {
        if (!path[layer].valid) {
            continue;
        }
        Expect(HasNearbyBoundarySupport(path, layer, params),
               frame_path.string() + " must not publish isolated " + side +
                   " observed boundary at layer " + std::to_string(layer));
    }
}

void ValidateObservedBoundaryAwayFromImageBorder(
    const std::array<ls2k::port::BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
    const ls2k::legacy::BEVProjector& projector,
    const ls2k::port::RuntimeParameters& params,
    const std::filesystem::path& frame_path,
    const std::string& side) {
    const int margin = std::max(0,
                                std::max(kMinBoundaryEvidenceImageMarginPx,
                                         params.bev_geometry.image_border_truncation_margin_px) +
                                    params.bev_topology_sampler.sample_patch_radius_px);
    for (std::size_t layer = 0; layer < path.size(); ++layer) {
        if (!path[layer].valid) {
            continue;
        }
        ls2k::port::ImagePoint image{};
        Expect(projector.ProjectVehicleToImage(path[layer].point, image),
               frame_path.string() + " " + side + " boundary at layer " +
                   std::to_string(layer) + " must project back to the image");
        Expect(image.row_px > static_cast<float>(margin) &&
                   image.col_px > static_cast<float>(margin) &&
                   image.row_px < static_cast<float>(ls2k::port::kCompiledCameraFrameHeight - 1 - margin) &&
                   image.col_px < static_cast<float>(ls2k::port::kCompiledCameraFrameWidth - 1 - margin),
               frame_path.string() + " must not publish " + side +
                   " observed boundary near the camera image edge at layer " + std::to_string(layer));
    }
}

std::string Join(const std::vector<std::string>& values) {
    std::string output{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            output += ", ";
        }
        output += values[index];
    }
    return output;
}

void ValidateOne(const std::filesystem::path& dataset_dir,
                 const SampleContract& contract,
                 const Manifest& manifest) {
    ls2k::port::RuntimeParameters params{};
    params.see_max = 24.0;
    params.emergency_threshold = 40;
    params.bev_scene_fsm.cross_bilateral_open_min_m =
        static_cast<float>(manifest.cross_bilateral_open_min_m);
    if (manifest.has_projector_calibration) {
        params.bev_projector = manifest.projector_calibration;
    }
    ls2k::legacy::BEVProjector projector{};
    Expect(projector.Configure(params.bev_projector), "authority validator projector must configure");

    const std::filesystem::path path = dataset_dir / contract.file;
    const ls2k::port::LegacyCameraFrame frame = ReadRawFrame(path);
    const SequenceResult sequence = AnalyzeWithConfirm(frame, params);
    const ls2k::legacy::SteeringAnalysisResult& result =
        sequence.have_best_valid ? sequence.best_valid : sequence.final;
    const ls2k::port::ControlErrorModelOutput& control = result.control_output;
    const ls2k::port::TopologyEvidence& evidence = result.topology_evidence;

    std::cout << contract.file
              << " label=" << contract.label
              << " role=" << contract.authority_role
              << " active_module=" << result.perception.active_module
              << " scene_phase=" << result.perception.scene_phase
              << " topology_source=" << result.track_estimate.source
              << " saw_valid_track=" << (sequence.saw_valid_track ? "true" : "false")
              << " saw_cross=" << (sequence.saw_cross ? "true" : "false")
              << " saw_circle=" << (sequence.saw_circle ? "true" : "false")
              << " saw_lost_prediction=" << (sequence.saw_lost_prediction ? "true" : "false")
              << " debug_candidate=" << result.scene_debug_candidate
              << " debug_candidate_streak=" << result.scene_debug_candidate_streak
              << " ordinary_score=" << evidence.ordinary_score
              << " bend_veto_score=" << evidence.bend_veto_score
              << " cross_score=" << evidence.cross_score
              << " left_circle_score=" << evidence.left_circle_score
              << " right_circle_score=" << evidence.right_circle_score
              << " zebra_score=" << evidence.zebra_score
              << " lost_score=" << evidence.lost_score
              << " control_valid=" << (control.valid ? "true" : "false")
              << " lookahead_distance_m=" << control.lookahead_distance_m
              << " lookahead_lateral_error=" << control.lookahead_lateral_error
              << " lookahead_heading_error=" << control.lookahead_heading_error
              << " reference_curvature=" << control.reference_curvature
              << " curvature_command=" << control.curvature_command
              << " yaw_rate_target=" << control.yaw_rate_target
              << " near_lateral_error=" << result.track_estimate.near_lateral_error
              << " far_heading_error=" << result.track_estimate.far_heading_error
              << " preview_curvature=" << result.track_estimate.preview_curvature
              << " visible_range_m=" << result.track_estimate.visible_range_m
              << " track_confidence=" << result.track_estimate.track_confidence << "\n";

    Expect(result.track_estimate.calibration_valid, path.string() + " must use a valid BEV projector");
    if (contract.require_valid_track) {
        Expect(sequence.saw_valid_track, path.string() + " must produce a valid BEV track");
    }
    if (contract.forbid_valid_track) {
        Expect(!sequence.saw_valid_track, path.string() + " must not produce a valid BEV track");
    }
    if (sequence.saw_valid_track) {
        Expect(result.track_estimate.source == "bev_corridor_topology",
               path.string() + " valid track must be sourced from corridor topology");
    }

    for (const std::string& module : contract.require_active_modules) {
        Expect(SawActiveModule(sequence, module), path.string() + " must observe active_module " + module);
    }
    for (const std::string& module : contract.forbid_active_modules) {
        Expect(!SawActiveModule(sequence, module), path.string() + " must not observe active_module " + module);
    }
    for (const std::string& candidate : contract.require_primary_candidates) {
        Expect(PrimaryCandidate(result, params, candidate),
               path.string() + " primary topology evidence must expose candidate " + candidate);
    }
    for (const std::string& candidate : contract.forbid_primary_candidates) {
        Expect(!PrimaryCandidate(result, params, candidate),
               path.string() + " primary topology evidence must not expose candidate " + candidate);
    }
    ValidateObservedBoundaryContinuity(result.track_estimate.sampled_left_boundary,
                                       params,
                                       path,
                                       "left");
    ValidateObservedBoundaryContinuity(result.track_estimate.sampled_right_boundary,
                                       params,
                                       path,
                                       "right");
    ValidateObservedBoundaryAwayFromImageBorder(result.track_estimate.sampled_left_boundary,
                                                projector,
                                                params,
                                                path,
                                                "left");
    ValidateObservedBoundaryAwayFromImageBorder(result.track_estimate.sampled_right_boundary,
                                                projector,
                                                params,
                                                path,
                                                "right");
}

void CheckManifestCoversRawFiles(const std::filesystem::path& dataset_dir,
                                 const Manifest& manifest) {
    std::vector<std::string> raw_files{};
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dataset_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".raw") {
            raw_files.push_back(entry.path().filename().string());
        }
    }
    std::sort(raw_files.begin(), raw_files.end());

    std::vector<std::string> manifest_files{};
    for (const SampleContract& sample : manifest.samples) {
        manifest_files.push_back(sample.file);
    }
    std::sort(manifest_files.begin(), manifest_files.end());

    Expect(!raw_files.empty(), dataset_dir.string() + " must contain raw images");
    Expect(raw_files == manifest_files,
           dataset_dir.string() + " manifest samples must exactly match raw images; raw=[" +
               Join(raw_files) + "] manifest=[" + Join(manifest_files) + "]");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path dataset_dir = "new/verification/test-images/authority-baseline";
        if (argc == 2) {
            dataset_dir = argv[1];
        } else if (argc > 2) {
            std::cerr << "usage: authority_baseline_validate [dataset_dir]\n";
            return EXIT_FAILURE;
        }

        const Manifest manifest = LoadManifest(dataset_dir);
        CheckManifestCoversRawFiles(dataset_dir, manifest);

        std::cout << "authority manifest schema=" << manifest.schema
                  << " projector_id=" << manifest.projector_id
                  << " cross_bilateral_open_min_m=" << manifest.cross_bilateral_open_min_m
                  << " has_projector_calibration="
                  << (manifest.has_projector_calibration ? "true" : "false")
                  << " samples=" << manifest.samples.size() << "\n";
        for (const SampleContract& sample : manifest.samples) {
            ValidateOne(dataset_dir, sample, manifest);
        }
    } catch (const TestFailure& failure) {
        std::cerr << "authority_baseline_validate failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "authority_baseline_validate unexpected exception: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "authority_baseline_validate passed\n";
    return EXIT_SUCCESS;
}
