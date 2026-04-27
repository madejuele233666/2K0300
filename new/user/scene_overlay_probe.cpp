#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_bev_projector.hpp"

namespace {

using ls2k::port::BEVPathSample;
using ls2k::port::BEVPoint;
using ls2k::port::ImagePoint;
using ls2k::port::LegacyCameraFrame;
using ls2k::port::RuntimeParameters;

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
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                const std::size_t index =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(col)) *
                    3U;
                pixels[index] = fill.r;
                pixels[index + 1U] = fill.g;
                pixels[index + 2U] = fill.b;
            }
        }
    }

    void SetPixel(int x, int y, const Color& color) {
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

struct PanelLayout {
    int raw_x = 0;
    int raw_y = 0;
    int raw_width = 0;
    int raw_height = 0;
    int bev_x = 0;
    int bev_y = 0;
    int bev_width = 0;
    int bev_height = 0;
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

void DrawLine(RgbImage& image, int x0, int y0, int x1, int y1, const Color& color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        image.SetPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void DrawSquare(RgbImage& image, int x, int y, int radius, const Color& color, bool filled) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const bool border = std::abs(dx) == radius || std::abs(dy) == radius;
            if (filled || border) {
                image.SetPixel(x + dx, y + dy, color);
            }
        }
    }
}

void DrawFrameBorder(RgbImage& image, int x, int y, int width, int height, const Color& color) {
    DrawLine(image, x, y, x + width - 1, y, color);
    DrawLine(image, x, y, x, y + height - 1, color);
    DrawLine(image, x + width - 1, y, x + width - 1, y + height - 1, color);
    DrawLine(image, x, y + height - 1, x + width - 1, y + height - 1, color);
}

void BlitRawFrame(RgbImage& image, const LegacyCameraFrame& frame, int offset_x, int offset_y) {
    for (int row = 0; row < frame.height; ++row) {
        for (int col = 0; col < frame.width; ++col) {
            const std::size_t index =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(col);
            const std::uint8_t gray = frame.gray[index];
            image.SetPixel(offset_x + col, offset_y + row, Color{gray, gray, gray});
        }
    }
}

void DrawVerticalMarker(RgbImage& image,
                        int x,
                        int y0,
                        int y1,
                        const Color& color) {
    DrawLine(image, x, y0, x, y1, color);
}

bool ProjectSampleToRaw(const ls2k::legacy::BEVProjector& projector,
                        const BEVPathSample& sample,
                        int panel_x,
                        int panel_y,
                        int raw_width,
                        int raw_height,
                        int& out_x,
                        int& out_y) {
    if (!sample.valid) {
        return false;
    }
    ImagePoint point{};
    if (!projector.ProjectVehicleToImage(sample.point, point)) {
        return false;
    }
    const int image_row = static_cast<int>(std::lround(point.row_px));
    const int image_col = static_cast<int>(std::lround(point.col_px));
    if (image_row < 0 || image_col < 0 || image_row >= raw_height || image_col >= raw_width) {
        return false;
    }
    out_x = panel_x + image_col;
    out_y = panel_y + image_row;
    return true;
}

void DrawProjectedPath(RgbImage& image,
                       const ls2k::legacy::BEVProjector& projector,
                       const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
                       int panel_x,
                       int panel_y,
                       int raw_width,
                       int raw_height,
                       const Color& color,
                       int marker_radius) {
    bool have_previous = false;
    int previous_x = 0;
    int previous_y = 0;
    for (const BEVPathSample& sample : path) {
        int x = 0;
        int y = 0;
        if (!ProjectSampleToRaw(projector, sample, panel_x, panel_y, raw_width, raw_height, x, y)) {
            have_previous = false;
            continue;
        }
        if (have_previous) {
            DrawLine(image, previous_x, previous_y, x, y, color);
        }
        DrawSquare(image, x, y, marker_radius, color, true);
        previous_x = x;
        previous_y = y;
        have_previous = true;
    }
}

bool ProjectBevToPanel(const BEVPoint& point,
                       const PanelLayout& layout,
                       float lateral_limit_m,
                       float forward_max_m,
                       int& out_x,
                       int& out_y) {
    if (lateral_limit_m <= 1e-4F || forward_max_m <= 1e-4F) {
        return false;
    }
    const float normalized_x = (point.lateral_m + lateral_limit_m) / (2.0F * lateral_limit_m);
    const float normalized_y = 1.0F - point.forward_m / forward_max_m;
    out_x = layout.bev_x +
            static_cast<int>(std::lround(std::clamp(normalized_x, 0.0F, 1.0F) * (layout.bev_width - 1)));
    out_y = layout.bev_y +
            static_cast<int>(std::lround(std::clamp(normalized_y, 0.0F, 1.0F) * (layout.bev_height - 1)));
    return true;
}

void DrawBevGrid(RgbImage& image,
                 const PanelLayout& layout,
                 float lateral_limit_m,
                 float forward_max_m) {
    const Color grid_color{40, 40, 40};
    const Color axis_color{90, 90, 90};
    DrawFrameBorder(image, layout.bev_x, layout.bev_y, layout.bev_width, layout.bev_height, axis_color);

    for (float lateral = -lateral_limit_m; lateral <= lateral_limit_m + 1e-4F; lateral += 0.2F) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (!ProjectBevToPanel(BEVPoint{0.0F, lateral}, layout, lateral_limit_m, forward_max_m, x0, y0) ||
            !ProjectBevToPanel(BEVPoint{forward_max_m, lateral},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x1,
                               y1)) {
            continue;
        }
        DrawLine(image, x0, y0, x1, y1, std::abs(lateral) < 1e-4F ? axis_color : grid_color);
    }

    for (float forward = 0.0F; forward <= forward_max_m + 1e-4F; forward += 0.25F) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (!ProjectBevToPanel(BEVPoint{forward, -lateral_limit_m},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x0,
                               y0) ||
            !ProjectBevToPanel(BEVPoint{forward, lateral_limit_m},
                               layout,
                               lateral_limit_m,
                               forward_max_m,
                               x1,
                               y1)) {
            continue;
        }
        DrawLine(image, x0, y0, x1, y1, forward < 1e-4F ? axis_color : grid_color);
    }
}

void DrawBevPath(RgbImage& image,
                 const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path,
                 const PanelLayout& layout,
                 float lateral_limit_m,
                 float forward_max_m,
                 const Color& color,
                 int marker_radius) {
    bool have_previous = false;
    int previous_x = 0;
    int previous_y = 0;
    for (const BEVPathSample& sample : path) {
        if (!sample.valid) {
            have_previous = false;
            continue;
        }
        int x = 0;
        int y = 0;
        if (!ProjectBevToPanel(sample.point, layout, lateral_limit_m, forward_max_m, x, y)) {
            have_previous = false;
            continue;
        }
        if (have_previous) {
            DrawLine(image, previous_x, previous_y, x, y, color);
        }
        DrawSquare(image, x, y, marker_radius, color, true);
        previous_x = x;
        previous_y = y;
        have_previous = true;
    }
}

float MaxAbsLateral(const std::array<BEVPathSample, ls2k::port::kBevTrackSampleCount>& path) {
    float result = 0.0F;
    for (const BEVPathSample& sample : path) {
        if (!sample.valid) {
            continue;
        }
        result = std::max(result, std::abs(sample.point.lateral_m));
    }
    return result;
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: scene_overlay_probe <input.raw> <output.bmp>\n";
        return 1;
    }

    try {
        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        const LegacyCameraFrame frame = ReadRawFrame(input_path);

        RuntimeParameters params{};
        params.see_max = 24.0;
        params.emergency_threshold = 40;

        ls2k::legacy::BEVProjector projector;
        if (!projector.Configure(params.bev_projector)) {
            throw std::runtime_error("failed to configure BEV projector");
        }

        const ls2k::legacy::SteeringAnalysisResult analysis =
            ls2k::legacy::AnalyzeFrame(frame, params, {}, {}, false, 1, 1);

        constexpr int kPanelGapPx = 12;
        PanelLayout layout{};
        layout.raw_x = 0;
        layout.raw_y = 0;
        layout.raw_width = frame.width;
        layout.raw_height = frame.height;
        layout.bev_x = frame.width + kPanelGapPx;
        layout.bev_y = 0;
        layout.bev_width = params.bev_projector.debug_grid_width * 2;
        layout.bev_height = frame.height;

        RgbImage image(layout.bev_x + layout.bev_width, std::max(layout.raw_height, layout.bev_height), Color{18, 18, 18});
        BlitRawFrame(image, frame, layout.raw_x, layout.raw_y);
        DrawFrameBorder(image, layout.raw_x, layout.raw_y, layout.raw_width, layout.raw_height, Color{96, 96, 96});

        DrawVerticalMarker(image,
                           layout.raw_x + frame.width / 2,
                           layout.raw_y,
                           layout.raw_y + frame.height - 1,
                           Color{255, 64, 64});

        DrawProjectedPath(image,
                          projector,
                          analysis.track_estimate.sampled_drivable_left_boundary,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{255, 128, 0},
                          1);
        DrawProjectedPath(image,
                          projector,
                          analysis.track_estimate.sampled_drivable_right_boundary,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{0, 220, 220},
                          1);
        DrawProjectedPath(image,
                          projector,
                          analysis.track_estimate.sampled_left_boundary,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{32, 220, 32},
                          2);
        DrawProjectedPath(image,
                          projector,
                          analysis.track_estimate.sampled_right_boundary,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{48, 144, 255},
                          2);
        DrawProjectedPath(image,
                          projector,
                          analysis.track_estimate.sampled_centerline,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{255, 220, 32},
                          2);
        DrawProjectedPath(image,
                          projector,
                          analysis.reference_path.sampled_path,
                          layout.raw_x,
                          layout.raw_y,
                          frame.width,
                          frame.height,
                          Color{255, 64, 255},
                          3);

        const float lateral_limit_m =
            std::max(params.bev_geometry.search_lateral_limit_m,
                     std::max(MaxAbsLateral(analysis.track_estimate.sampled_drivable_left_boundary),
                              MaxAbsLateral(analysis.track_estimate.sampled_drivable_right_boundary)) +
                         0.05F);
        const float forward_max_m =
            std::max(params.bev_geometry.forward_samples_m.back(),
                     std::max(analysis.track_estimate.visible_range_m, 0.5F));
        DrawBevGrid(image, layout, lateral_limit_m, forward_max_m);
        DrawBevPath(image,
                    analysis.track_estimate.sampled_drivable_left_boundary,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{255, 128, 0},
                    1);
        DrawBevPath(image,
                    analysis.track_estimate.sampled_drivable_right_boundary,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{0, 220, 220},
                    1);
        DrawBevPath(image,
                    analysis.track_estimate.sampled_left_boundary,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{32, 220, 32},
                    2);
        DrawBevPath(image,
                    analysis.track_estimate.sampled_right_boundary,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{48, 144, 255},
                    2);
        DrawBevPath(image,
                    analysis.track_estimate.sampled_centerline,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{255, 220, 32},
                    2);
        DrawBevPath(image,
                    analysis.reference_path.sampled_path,
                    layout,
                    lateral_limit_m,
                    forward_max_m,
                    Color{255, 64, 255},
                    3);

        WriteBmp(output_path, image);

        std::cout << "module=" << analysis.perception.active_module
                  << " scene=" << analysis.perception.scene_phase
                  << " reference_mode=" << analysis.perception.reference_mode
                  << " cross_candidate=" << (analysis.scene_observation.cross_candidate ? "true" : "false")
                  << " width_expand_ratio=" << analysis.scene_observation.width_expand_ratio
                  << " cross_bilateral_open_score_m="
                  << analysis.scene_observation.cross_bilateral_open_score_m
                  << " left_open_score=" << analysis.scene_observation.left_open_score
                  << " right_open_score=" << analysis.scene_observation.right_open_score
                  << " near_lateral_error=" << analysis.perception.near_lateral_error
                  << " far_heading_error=" << analysis.perception.far_heading_error
                  << " preview_curvature=" << analysis.perception.preview_curvature
                  << " lookahead_distance_m=" << analysis.control_output.lookahead_distance_m
                  << " lookahead_lateral_error=" << analysis.control_output.lookahead_lateral_error
                  << " lookahead_heading_error=" << analysis.control_output.lookahead_heading_error
                  << " reference_curvature=" << analysis.control_output.reference_curvature
                  << " curvature_command=" << analysis.control_output.curvature_command
                  << " yaw_rate_target=" << analysis.control_output.yaw_rate_target
                  << " visible_range_m=" << analysis.perception.visible_range_m
                  << " track_confidence=" << analysis.perception.track_confidence
                  << " projector_id=" << params.bev_projector.projector_id << "\n";
        std::cout << "wrote_overlay=" << output_path << "\n";
    } catch (const std::exception& error) {
        std::cerr << "scene_overlay_probe failed: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
