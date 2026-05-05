#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "legacy/steering_otsu_threshold.hpp"
#include "legacy/steering_reference_control_readiness.hpp"
#include "legacy/steering_reference_lateral_error.hpp"
#include "legacy/steering_reference_usability.hpp"
#include "legacy/steering_visual_element_pipeline.hpp"
#include "legacy/steering_visual_reference_orchestration.hpp"
#include "port/steering_state_types.hpp"

namespace {

using ls2k::legacy::BEVSimplePerceptionResult;
using ls2k::legacy::BEVSimplePixelClass;
using ls2k::legacy::BEVSimpleImage;
using ls2k::port::BEVPathSample;
using ls2k::port::LegacyCameraFrame;
using ls2k::port::LegacyCameraFrameView;
using ls2k::port::RuntimeParameters;
using ls2k::port::VisualElementEvidenceFrame;
using ls2k::port::VisualElementEvidenceRecord;

struct ProbePipelineResult {
    BEVSimplePerceptionResult simple{};
    VisualElementEvidenceFrame element_evidence{};
    ls2k::port::VisualReferenceSelection visual_selection{};
    ls2k::port::ReferenceContinuityResult continuity{};
    ls2k::port::ReferenceUsability selected_usability{};
    ls2k::port::ReferenceLateralErrorEstimate lateral_error{};
    ls2k::port::ReferenceControlReadiness reference_control{};
    ls2k::port::SteeringPerceptionMemory next_memory{};
    bool memory_update_valid = false;
    int threshold = 0;
};

const char* BoolToken(bool value) {
    return value ? "true" : "false";
}

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels{};

    RgbImage(int image_width, int image_height, Color fill) : width(image_width), height(image_height) {
        pixels.resize(static_cast<std::size_t>(width * height * 3));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                SetPixel(x, y, fill);
            }
        }
    }

    void SetPixel(int x, int y, Color color) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::size_t index =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            3U;
        pixels[index] = color.r;
        pixels[index + 1U] = color.g;
        pixels[index + 2U] = color.b;
    }
};

LegacyCameraFrame ReadRawFrame(const std::string& path) {
    LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open raw frame: " + path);
    }
    input.read(reinterpret_cast<char*>(frame.gray.data()),
               static_cast<std::streamsize>(frame.width * frame.height));
    if (input.gcount() != frame.width * frame.height) {
        throw std::runtime_error("unexpected raw frame size: " + path);
    }
    return frame;
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open params json: " + path);
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool ReadNumberField(const std::string& text, const std::string& key, double& out) {
    const std::string quoted = "\"" + key + "\"";
    const std::size_t key_pos = text.find(quoted);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = text.find(':', key_pos + quoted.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    const char* begin = text.c_str() + colon_pos + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (begin == end) {
        return false;
    }
    out = value;
    return true;
}

bool ExtractObjectBlock(const std::string& text, const std::string& key, std::string& out) {
    const std::string quoted = "\"" + key + "\"";
    const std::size_t key_pos = text.find(quoted);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t open_pos = text.find('{', key_pos + quoted.size());
    if (open_pos == std::string::npos) {
        return false;
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t pos = open_pos; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                out = text.substr(open_pos, pos - open_pos + 1U);
                return true;
            }
        }
    }
    return false;
}

void ReadFloatField(const std::string& block, const std::string& key, float& out) {
    double value = 0.0;
    if (ReadNumberField(block, key, value)) {
        out = static_cast<float>(value);
    }
}

void ReadDoubleField(const std::string& block, const std::string& key, double& out) {
    double value = 0.0;
    if (ReadNumberField(block, key, value)) {
        out = value;
    }
}

void ReadIntField(const std::string& block, const std::string& key, int& out) {
    double value = 0.0;
    if (ReadNumberField(block, key, value)) {
        out = static_cast<int>(std::lround(value));
    }
}

void ReadBoolField(const std::string& block, const std::string& key, bool& out) {
    double value = 0.0;
    if (ReadNumberField(block, key, value)) {
        out = std::abs(value) > 0.5;
    }
}

void LoadRuntimeParamsJson(const std::string& path, RuntimeParameters& params) {
    if (path.empty()) {
        return;
    }
    const std::string json = ReadTextFile(path);
    std::string block;
    if (ExtractObjectBlock(json, "BEV_PROJECTOR", block)) {
        ReadIntField(block, "DEBUG_GRID_WIDTH", params.bev_projector.debug_grid_width);
        ReadIntField(block, "DEBUG_GRID_HEIGHT", params.bev_projector.debug_grid_height);
        for (int i = 0; i < static_cast<int>(ls2k::port::kBevCalibrationPointCount); ++i) {
            ReadFloatField(block, "SOURCE_ROW_" + std::to_string(i), params.bev_projector.source_points[i].row_px);
            ReadFloatField(block, "SOURCE_COL_" + std::to_string(i), params.bev_projector.source_points[i].col_px);
            ReadFloatField(block,
                           "TARGET_FORWARD_" + std::to_string(i),
                           params.bev_projector.target_points[i].forward_m);
            ReadFloatField(block,
                           "TARGET_LATERAL_" + std::to_string(i),
                           params.bev_projector.target_points[i].lateral_m);
        }
    }
    if (ExtractObjectBlock(json, "BEV_GEOMETRY", block)) {
        for (int i = 0; i < static_cast<int>(ls2k::port::kBevReferenceSampleCount); ++i) {
            ReadFloatField(block,
                           "FORWARD_SAMPLE_" + std::to_string(i),
                           params.bev_geometry.forward_samples_m[static_cast<std::size_t>(i)]);
        }
        ReadFloatField(block, "SEARCH_LATERAL_LIMIT_M", params.bev_geometry.search_lateral_limit_m);
        ReadFloatField(block, "LATERAL_STEP_M", params.bev_geometry.lateral_step_m);
    }
    if (ExtractObjectBlock(json, "BEV_CLASSIFICATION", block)) {
        ReadFloatField(block, "WHITE_CONFIDENCE_MIN", params.bev_classification.white_confidence_min);
        ReadFloatField(block, "UNKNOWN_CONFIDENCE_MIN", params.bev_classification.unknown_confidence_min);
        ReadIntField(block, "HOLD_LAST_MAX_CYCLES", params.bev_classification.hold_last_max_cycles);
    }
    if (ExtractObjectBlock(json, "BEV_CONTROL_MODEL", block)) {
        ReadDoubleField(block,
                        "LATERAL_ERROR_FAR_WEIGHT",
                        params.bev_control_model.lateral_error_far_weight);
        ReadIntField(block,
                     "LATERAL_ERROR_MAX_WEIGHTED_SAMPLE_INDEX",
                     params.bev_control_model.lateral_error_max_weighted_sample_index);
        ReadDoubleField(block,
                        "LATERAL_ERROR_TO_WHEEL_DELTA_GAIN",
                        params.bev_control_model.lateral_error_to_wheel_delta_gain);
        ReadIntField(block,
                     "MIN_LEADING_REFERENCE_SAMPLES",
                     params.bev_control_model.min_leading_reference_samples);
    }
    if (ExtractObjectBlock(json, "BEV_ELEMENT_RASTER", block)) {
        ReadBoolField(block, "ENABLED", params.bev_element_raster.enabled);
        ReadIntField(block, "WIDTH", params.bev_element_raster.width);
    }
}

void WriteBmp(const std::string& path, const RgbImage& image) {
    const int row_stride = image.width * 3;
    const int row_padding = (4 - (row_stride % 4)) % 4;
    const int pixel_bytes = (row_stride + row_padding) * image.height;
    const int file_size = 54 + pixel_bytes;
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to open output bmp: " + path);
    }
    const auto write_u16 = [&output](std::uint16_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
    };
    const auto write_u32 = [&output](std::uint32_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
        output.put(static_cast<char>((value >> 16U) & 0xFFU));
        output.put(static_cast<char>((value >> 24U) & 0xFFU));
    };
    output.put('B');
    output.put('M');
    write_u32(static_cast<std::uint32_t>(file_size));
    write_u16(0);
    write_u16(0);
    write_u32(54);
    write_u32(40);
    write_u32(static_cast<std::uint32_t>(image.width));
    write_u32(static_cast<std::uint32_t>(image.height));
    write_u16(1);
    write_u16(24);
    write_u32(0);
    write_u32(static_cast<std::uint32_t>(pixel_bytes));
    write_u32(2835);
    write_u32(2835);
    write_u32(0);
    write_u32(0);
    const std::array<std::uint8_t, 3> padding{{0, 0, 0}};
    for (int row = image.height - 1; row >= 0; --row) {
        for (int col = 0; col < image.width; ++col) {
            const std::size_t index =
                (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(col)) *
                3U;
            output.put(static_cast<char>(image.pixels[index + 2U]));
            output.put(static_cast<char>(image.pixels[index + 1U]));
            output.put(static_cast<char>(image.pixels[index]));
        }
        output.write(reinterpret_cast<const char*>(padding.data()), row_padding);
    }
}

Color ClassColor(BEVSimplePixelClass class_kind, std::uint8_t gray) {
    switch (class_kind) {
        case BEVSimplePixelClass::kWhite:
            return Color{240, 240, 240};
        case BEVSimplePixelClass::kBlack:
            return Color{std::uint8_t(gray / 4U), std::uint8_t(gray / 4U), std::uint8_t(gray / 4U)};
        case BEVSimplePixelClass::kUnknown:
            return Color{150, 112, 52};
        case BEVSimplePixelClass::kInvalid:
            return Color{28, 28, 34};
    }
    return Color{28, 28, 34};
}

void DrawLine(RgbImage& image, int x0, int y0, int x1, int y1, Color color) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        image.SetPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void DrawSquare(RgbImage& image, int center_x, int center_y, int radius, Color color) {
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            image.SetPixel(x, y, color);
        }
    }
}

bool ProjectBevToPanel(const BEVSimpleImage& bev,
                       const ls2k::port::BEVPoint& point,
                       int panel_x,
                       int panel_y,
                       int& x,
                       int& y) {
    if (!bev.valid || bev.lateral_limit_m <= 1.0e-4F || bev.forward_max_m <= 1.0e-4F) {
        return false;
    }
    const float normalized_x =
        (point.lateral_m + bev.lateral_limit_m) / (2.0F * bev.lateral_limit_m);
    const float normalized_y = 1.0F - point.forward_m / bev.forward_max_m;
    x = panel_x + static_cast<int>(std::lround(std::clamp(normalized_x, 0.0F, 1.0F) *
                                               static_cast<float>(bev.width - 1)));
    y = panel_y + static_cast<int>(std::lround(std::clamp(normalized_y, 0.0F, 1.0F) *
                                               static_cast<float>(bev.height - 1)));
    return true;
}

void DrawRawPanel(RgbImage& image, const LegacyCameraFrame& frame, int panel_x, int panel_y) {
    for (int row = 0; row < frame.height; ++row) {
        for (int col = 0; col < frame.width; ++col) {
            const std::uint8_t gray =
                frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                           static_cast<std::size_t>(col)];
            image.SetPixel(panel_x + col, panel_y + row, Color{gray, gray, gray});
        }
    }
}

void DrawBevPanel(RgbImage& image,
                  const BEVSimpleImage& bev,
                  const BEVSimplePerceptionResult& simple,
                  int panel_x,
                  int panel_y) {
    for (int y = 0; y < bev.height; ++y) {
        for (int x = 0; x < bev.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * bev.width + x);
            image.SetPixel(panel_x + x,
                           panel_y + y,
                           ClassColor(bev.classes[index], bev.gray[index]));
        }
    }

    for (const ls2k::legacy::BEVSimpleRowScan& row : simple.rows) {
        for (const ls2k::legacy::BEVSimpleWhiteInterval& interval : row.intervals) {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            ProjectBevToPanel(bev, {interval.forward_m, interval.left_m}, panel_x, panel_y, x0, y0);
            ProjectBevToPanel(bev, {interval.forward_m, interval.right_m}, panel_x, panel_y, x1, y1);
            DrawLine(image, x0, y0, x1, y1, Color{42, 190, 210});
        }
    }

    int previous_x = 0;
    int previous_y = 0;
    bool have_previous = false;
    for (const BEVPathSample& sample : simple.reference_path.sampled_path) {
        if (!sample.present) {
            continue;
        }
        int x = 0;
        int y = 0;
        if (!ProjectBevToPanel(bev, sample.point, panel_x, panel_y, x, y)) {
            continue;
        }
        const Color color = sample.source == ls2k::port::BEVPathPointSource::kHold
                                ? Color{255, 220, 64}
                                : Color{255, 255, 255};
        if (have_previous) {
            DrawLine(image, previous_x, previous_y, x, y, color);
        }
        DrawSquare(image, x, y, 2, color);
        previous_x = x;
        previous_y = y;
        have_previous = true;
    }
}

void PrintSimpleDiagnostics(const BEVSimpleImage& bev,
                            const BEVSimplePerceptionResult& simple,
                            const ProbePipelineResult& pipeline,
                            const std::string& output_path) {
    std::cout << "overlay_algorithm=bev_simple_pipeline"
              << " perception_health.projector_ok=true"
              << " perception_health.reason=ok"
              << " element_evidence.cross_exit.present="
              << (pipeline.element_evidence.cross_exit.present ? "true" : "false")
              << " element_evidence.cross_exit.confidence="
              << pipeline.element_evidence.cross_exit.confidence
              << " element_evidence.cross_exit.forward_min_m="
              << pipeline.element_evidence.cross_exit.forward_min_m
              << " element_evidence.cross_exit.forward_max_m="
              << pipeline.element_evidence.cross_exit.forward_max_m
              << " element_evidence.cross_exit.lateral_min_m="
              << pipeline.element_evidence.cross_exit.lateral_min_m
              << " element_evidence.cross_exit.lateral_max_m="
              << pipeline.element_evidence.cross_exit.lateral_max_m
              << " element_evidence.cross_exit.sampleable_count="
              << pipeline.element_evidence.cross_exit.sampleable_count
              << " element_evidence.cross_exit.supporting_white_count="
              << pipeline.element_evidence.cross_exit.supporting_white_count
              << " element_evidence.cross_exit.unknown_count="
              << pipeline.element_evidence.cross_exit.unknown_count
              << " element_evidence.cross_exit.reason="
              << pipeline.element_evidence.cross_exit.reason
              << " element_evidence.cross_exit.candidate.built="
              << (pipeline.element_evidence.cross_exit.candidate.built ? "true" : "false")
              << " element_evidence.cross_exit.candidate.takeover_enabled="
              << (pipeline.element_evidence.cross_exit.candidate.takeover_enabled ? "true" : "false")
              << " element_evidence.cross_exit.candidate.included_in_arbitration="
              << (pipeline.element_evidence.cross_exit.candidate.included_in_arbitration ? "true" : "false")
              << " element_evidence.cross_exit.candidate.reason="
              << pipeline.element_evidence.cross_exit.candidate.reason
              << " element_evidence.records.count="
              << pipeline.element_evidence.records.size()
              << " visual_reference.present=" << (pipeline.visual_selection.present ? "true" : "false")
              << " visual_reference.source=" << pipeline.visual_selection.source
              << " visual_reference.reason=" << pipeline.visual_selection.reason
              << " visual_reference.candidate_count=" << pipeline.visual_selection.candidate_count
              << " visual_reference.rejected_candidate_reason="
              << pipeline.visual_selection.rejected_candidate_reason
              << " reference.mode=" << ls2k::legacy::ToString(pipeline.continuity.mode)
              << " reference.source=" << pipeline.continuity.source
              << " eligibility.usable=" << (pipeline.selected_usability.usable ? "true" : "false")
              << " eligibility.leading_usable_samples="
              << pipeline.selected_usability.leading_usable_samples
              << " eligibility.leading_min_forward_m=" << pipeline.selected_usability.leading_min_forward_m
              << " eligibility.leading_max_forward_m=" << pipeline.selected_usability.leading_max_forward_m
              << " eligibility.reason=" << pipeline.selected_usability.reason
              << " lateral_error.computed=" << (pipeline.lateral_error.computed ? "true" : "false")
              << " lateral_error.weighted_lateral_error_m="
              << pipeline.lateral_error.weighted_lateral_error_m
              << " lateral_error.weighted_sample_count="
              << pipeline.lateral_error.weighted_sample_count
              << " lateral_error.weight_sum=" << pipeline.lateral_error.weight_sum
              << " lateral_error.reason=" << pipeline.lateral_error.reason
              << " reference_control.ready=" << (pipeline.reference_control.ready ? "true" : "false")
              << " reference_control.reason=" << pipeline.reference_control.reason
              << " degraded.active=" << (pipeline.reference_control.degraded ? "true" : "false")
              << " bev_width=" << bev.width
              << " bev_height=" << bev.height
              << " lateral_limit_m=" << bev.lateral_limit_m
              << " forward_max_m=" << bev.forward_max_m
              << "\n";
    for (std::size_t index = 0; index < pipeline.element_evidence.records.size(); ++index) {
        const VisualElementEvidenceRecord& record = pipeline.element_evidence.records[index];
        std::cout << "element_evidence.records[" << index << "].id=" << record.id
                  << " element_evidence.records[" << index << "].present="
                  << BoolToken(record.present)
                  << " element_evidence.records[" << index << "].confidence="
                  << record.confidence
                  << " element_evidence.records[" << index << "].reason=" << record.reason
                  << " element_evidence.records[" << index << "].bounds.forward_min_m="
                  << record.bounds.forward_min_m
                  << " element_evidence.records[" << index << "].bounds.forward_max_m="
                  << record.bounds.forward_max_m
                  << " element_evidence.records[" << index << "].bounds.lateral_min_m="
                  << record.bounds.lateral_min_m
                  << " element_evidence.records[" << index << "].bounds.lateral_max_m="
                  << record.bounds.lateral_max_m
                  << " element_evidence.records[" << index << "].support.sampleable_count="
                  << record.support.sampleable_count
                  << " element_evidence.records[" << index << "].support.supporting_white_count="
                  << record.support.supporting_white_count
                  << " element_evidence.records[" << index << "].support.supporting_black_count="
                  << record.support.supporting_black_count
                  << " element_evidence.records[" << index << "].support.unknown_count="
                  << record.support.unknown_count
                  << " element_evidence.records[" << index << "].candidate.built="
                  << BoolToken(record.candidate.built)
                  << " element_evidence.records[" << index << "].candidate.takeover_enabled="
                  << BoolToken(record.candidate.takeover_enabled)
                  << " element_evidence.records[" << index
                  << "].candidate.included_in_arbitration="
                  << BoolToken(record.candidate.included_in_arbitration)
                  << " element_evidence.records[" << index << "].candidate.reason="
                  << record.candidate.reason
                  << "\n";
    }
    for (std::size_t index = 0;
         index < pipeline.visual_selection.reference_path.sampled_path.size();
         ++index) {
        const BEVPathSample& sample =
            pipeline.visual_selection.reference_path.sampled_path[index];
        std::cout << "visual_path_point index=" << index
                  << " present=" << (sample.present ? "true" : "false")
                  << " forward_m=" << sample.point.forward_m
                  << " lateral_m=" << sample.point.lateral_m
                  << " source=" << ls2k::legacy::ToString(sample.source)
                  << "\n";
    }
    for (std::size_t index = 0; index < pipeline.continuity.reference_path.sampled_path.size(); ++index) {
        const BEVPathSample& sample = pipeline.continuity.reference_path.sampled_path[index];
        std::cout << "path_point index=" << index
                  << " present=" << (sample.present ? "true" : "false")
                  << " forward_m=" << sample.point.forward_m
                  << " lateral_m=" << sample.point.lateral_m
                  << " source=" << ls2k::legacy::ToString(sample.source)
                  << "\n";
    }
    for (std::size_t row_index = 0; row_index < simple.rows.size(); ++row_index) {
        const auto& row = simple.rows[row_index];
        for (std::size_t interval_index = 0; interval_index < row.intervals.size(); ++interval_index) {
            const auto& interval = row.intervals[interval_index];
            std::cout << "row_interval row=" << row_index
                      << " interval=" << interval_index
                      << " forward_m=" << interval.forward_m
                      << " left_m=" << interval.left_m
                      << " right_m=" << interval.right_m
                      << " center_m=" << interval.center_m
                      << " width_m=" << interval.width_m
                      << "\n";
        }
    }
    std::cout << "wrote_overlay=" << output_path << "\n";
}

ProbePipelineResult RunProbePipeline(const LegacyCameraFrameView& frame_view,
                                     const RuntimeParameters& params,
                                     const ls2k::port::SteeringPerceptionMemory& prior_memory,
                                     ls2k::legacy::BEVProjector& projector,
                                     ls2k::legacy::BEVSampleProjectionLut& lut) {
    ProbePipelineResult result{};
    result.threshold = ls2k::legacy::ComputeOtsuThreshold(frame_view);
    const ls2k::legacy::BEVElementRasterFrame element_raster =
        ls2k::legacy::BuildBEVElementRaster(frame_view, result.threshold, params, projector, nullptr);
    result.simple = ls2k::legacy::RunBEVSimplePerception(frame_view,
                                                         result.threshold,
                                                         params,
                                                         projector,
                                                         &lut,
                                                         &element_raster);
    const ls2k::port::VisualReferenceCandidate line_candidate =
        ls2k::legacy::MakeLineVisualReferenceCandidate(result.simple.reference_path,
                                                       result.simple.reference_source);
    ls2k::legacy::VisualElementPipelineInput element_input{};
    element_input.sparse_rows = &result.simple.rows;
    element_input.element_raster = &element_raster;
    element_input.line_candidate = line_candidate;
    const ls2k::legacy::VisualElementPipelineResult element_result =
        ls2k::legacy::RunVisualElementPipeline(element_input, params);
    result.element_evidence = element_result.evidence;
    std::vector<ls2k::port::VisualReferenceCandidate> candidates;
    candidates.reserve(1U + element_result.candidates.size());
    candidates.push_back(line_candidate);
    candidates.insert(candidates.end(),
                      element_result.candidates.begin(),
                      element_result.candidates.end());
    result.visual_selection = ls2k::legacy::SelectVisualReference(candidates);
    const ls2k::port::ReferenceUsability current_usability =
        ls2k::legacy::EvaluateReferenceUsability(result.visual_selection.reference_path, params);
    if (current_usability.usable) {
        result.continuity.reference_path = result.visual_selection.reference_path;
        result.continuity.mode = result.visual_selection.reference_path.mode;
        result.continuity.source = result.visual_selection.source;
        result.continuity.next_hold_state =
            ls2k::legacy::MakeReferenceHoldState(result.visual_selection.reference_path, params);
        result.selected_usability = current_usability;
    } else {
        const ls2k::port::ReferenceContinuityResult hold_candidate =
            ls2k::legacy::BuildReferenceHoldCandidate(prior_memory.reference_hold, params);
        const ls2k::port::ReferenceUsability hold_usability =
            ls2k::legacy::EvaluateReferenceUsability(hold_candidate.reference_path, params);
        if (hold_usability.usable) {
            result.continuity = hold_candidate;
            result.selected_usability = hold_usability;
        } else {
            result.continuity = {};
            result.selected_usability =
                ls2k::legacy::EvaluateReferenceUsability(result.continuity.reference_path, params);
        }
    }
    result.lateral_error =
        ls2k::legacy::ComputeReferenceLateralError(result.continuity.reference_path,
                                                  result.selected_usability,
                                                  params);
    result.reference_control =
        ls2k::legacy::EvaluateReferenceControlReadiness(result.selected_usability,
                                                        result.lateral_error,
                                                        result.continuity.hold_selected);
    result.next_memory = prior_memory;
    result.next_memory.reference_hold = result.continuity.next_hold_state;
    result.memory_update_valid = true;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: scene_overlay_probe <input.raw> <output.bmp> [params.json]"
                     " [--bev-only] [--warmup <input.raw> ...] [--confirm-cycles <count>]\n";
        return 1;
    }

    try {
        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        RuntimeParameters params{};
        bool bev_only = false;
        int confirm_cycles = 1;
        std::string params_path;
        std::vector<std::string> warmup_paths;

        for (int arg_index = 3; arg_index < argc; ++arg_index) {
            const std::string arg = argv[arg_index];
            if (arg == "--bev-only") {
                bev_only = true;
            } else if (arg == "--warmup") {
                if (arg_index + 1 >= argc) {
                    throw std::runtime_error("--warmup requires a raw frame path");
                }
                warmup_paths.push_back(argv[++arg_index]);
            } else if (arg == "--confirm-cycles") {
                if (arg_index + 1 >= argc) {
                    throw std::runtime_error("--confirm-cycles requires a count");
                }
                confirm_cycles = std::max(1, std::stoi(argv[++arg_index]));
            } else {
                params_path = arg;
            }
        }
        if (params_path.empty()) {
            if (const char* env_path = std::getenv("LS2K_PARAMS_PATH")) {
                params_path = env_path;
            }
        }
        LoadRuntimeParamsJson(params_path, params);

        ls2k::legacy::BEVProjector projector;
        if (!projector.Configure(params.bev_projector)) {
            throw std::runtime_error("failed to configure BEV projector");
        }
        ls2k::legacy::BEVSampleProjectionLut lut{};
        ls2k::port::SteeringPerceptionMemory prior_memory{};
        std::uint64_t frame_id = 1;
        for (const std::string& warmup_path : warmup_paths) {
            const LegacyCameraFrame warmup_frame = ReadRawFrame(warmup_path);
            const LegacyCameraFrameView warmup_view = warmup_frame.View(frame_id++, 1);
            const ProbePipelineResult warmup =
                RunProbePipeline(warmup_view, params, prior_memory, projector, lut);
            if (warmup.memory_update_valid) {
                prior_memory = warmup.next_memory;
            }
        }

        const LegacyCameraFrame frame = ReadRawFrame(input_path);
        ProbePipelineResult pipeline{};
        for (int cycle = 0; cycle < confirm_cycles; ++cycle) {
            const LegacyCameraFrameView frame_view = frame.View(frame_id++, 1);
            pipeline = RunProbePipeline(frame_view, params, prior_memory, projector, lut);
            if (pipeline.memory_update_valid) {
                prior_memory = pipeline.next_memory;
            }
        }

        BEVSimplePerceptionResult selected_reference_overlay = pipeline.simple;
        selected_reference_overlay.reference_path = pipeline.continuity.reference_path;
        selected_reference_overlay.reference_mode = ls2k::legacy::ToString(pipeline.continuity.mode);
        selected_reference_overlay.reference_source = pipeline.continuity.source;
        const BEVSimpleImage debug_bev = ls2k::legacy::BuildDebugDenseBevImage(frame.View(frame_id, 1),
                                                                               pipeline.threshold,
                                                                               params,
                                                                               projector);

        constexpr int kGapPx = 12;
        const int raw_x = 0;
        const int raw_y = 0;
        const int bev_x = bev_only ? 0 : frame.width + kGapPx;
        const int bev_y = 0;
        const int output_width = bev_only ? debug_bev.width : frame.width + kGapPx + debug_bev.width;
        const int output_height = bev_only ? debug_bev.height : std::max(frame.height, debug_bev.height);
        RgbImage image(output_width, output_height, Color{18, 18, 18});
        if (!bev_only) {
            DrawRawPanel(image, frame, raw_x, raw_y);
        }
        DrawBevPanel(image, debug_bev, selected_reference_overlay, bev_x, bev_y);
        WriteBmp(output_path, image);
        PrintSimpleDiagnostics(debug_bev, pipeline.simple, pipeline, output_path);
    } catch (const std::exception& error) {
        std::cerr << "scene_overlay_probe failed: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
