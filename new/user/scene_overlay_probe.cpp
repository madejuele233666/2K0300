#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "legacy/camera_logic.hpp"
#include "legacy/steering_scene_common.hpp"

namespace {

using ls2k::legacy::EdgeAnchor;
using ls2k::legacy::EdgeObservationState;
using ls2k::legacy::LaneEdgeMetrics;
using ls2k::legacy::LaneMetrics;
using ls2k::port::LegacyCameraFrame;
using ls2k::port::RuntimeParameters;

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels{};

    explicit RgbImage(const LegacyCameraFrame& frame) : width(frame.width), height(frame.height) {
        pixels.resize(static_cast<std::size_t>(width * height * 3));
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                const std::size_t gray_index = static_cast<std::size_t>(row) * width + col;
                const uint8_t gray = frame.gray[gray_index];
                const std::size_t rgb_index = gray_index * 3U;
                pixels[rgb_index] = gray;
                pixels[rgb_index + 1U] = gray;
                pixels[rgb_index + 2U] = gray;
            }
        }
    }

    void SetPixel(int x, int y, const Color& color) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 3U;
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

void DrawVerticalLine(RgbImage& image, int x, const Color& color) {
    for (int y = 0; y < image.height; ++y) {
        image.SetPixel(x, y, color);
    }
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

void DrawCross(RgbImage& image, int x, int y, int radius, const Color& color) {
    for (int delta = -radius; delta <= radius; ++delta) {
        image.SetPixel(x + delta, y + delta, color);
        image.SetPixel(x + delta, y - delta, color);
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

Color ObservedColor(bool left_side, EdgeObservationState state) {
    if (state == EdgeObservationState::border_truncated) {
        return left_side ? Color{255, 180, 0} : Color{255, 96, 0};
    }
    return left_side ? Color{0, 255, 0} : Color{0, 128, 255};
}

Color BendColor(bool left_side, const EdgeAnchor& anchor) {
    if (anchor.history_fallback) {
        return left_side ? Color{255, 0, 255} : Color{255, 0, 160};
    }
    if (anchor.current_frame_extrapolated) {
        return left_side ? Color{255, 255, 0} : Color{160, 255, 255};
    }
    return left_side ? Color{0, 200, 80} : Color{64, 160, 255};
}

bool AnchorAvailable(const EdgeAnchor& anchor) {
    return anchor.state != EdgeObservationState::missing || anchor.current_frame_extrapolated ||
           anchor.history_fallback;
}

void DrawAnchorOverlay(RgbImage& image,
                       const LaneEdgeMetrics& edge,
                       bool left_side,
                       const std::string& label) {
    std::cout << label << " observed:";
    for (const EdgeAnchor& anchor : edge.observed_anchors) {
        std::cout << " (" << anchor.row << "," << anchor.col << ",state="
                  << static_cast<int>(anchor.state) << ")";
    }
    std::cout << "\n";
    std::cout << label << " bend:";
    for (const EdgeAnchor& anchor : edge.bend_anchors) {
        std::cout << " (" << anchor.row << "," << anchor.col << ",state="
                  << static_cast<int>(anchor.state) << ",ex=" << anchor.current_frame_extrapolated
                  << ",hist=" << anchor.history_fallback << ")";
    }
    std::cout << "\n";

    for (std::size_t i = 1; i < edge.observed_anchors.size(); ++i) {
        const EdgeAnchor& previous = edge.observed_anchors[i - 1U];
        const EdgeAnchor& current = edge.observed_anchors[i];
        if (!AnchorAvailable(previous) || !AnchorAvailable(current)) {
            continue;
        }
        DrawLine(image,
                 previous.col,
                 previous.row,
                 current.col,
                 current.row,
                 left_side ? Color{0, 96, 0} : Color{0, 64, 160});
    }

    for (std::size_t i = 1; i < edge.bend_anchors.size(); ++i) {
        const EdgeAnchor& previous = edge.bend_anchors[i - 1U];
        const EdgeAnchor& current = edge.bend_anchors[i];
        if (!AnchorAvailable(previous) || !AnchorAvailable(current)) {
            continue;
        }
        DrawLine(image,
                 previous.col,
                 previous.row,
                 current.col,
                 current.row,
                 left_side ? Color{255, 255, 0} : Color{255, 0, 255});
    }

    for (const EdgeAnchor& anchor : edge.observed_anchors) {
        if (!AnchorAvailable(anchor)) {
            continue;
        }
        DrawSquare(image, anchor.col, anchor.row, 4, ObservedColor(left_side, anchor.state), true);
    }
    for (const EdgeAnchor& anchor : edge.bend_anchors) {
        if (!AnchorAvailable(anchor)) {
            continue;
        }
        DrawCross(image, anchor.col, anchor.row, 5, BendColor(left_side, anchor));
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

    const auto write_u16 = [&output](uint16_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
    };
    const auto write_u32 = [&output](uint32_t value) {
        output.put(static_cast<char>(value & 0xFFU));
        output.put(static_cast<char>((value >> 8U) & 0xFFU));
        output.put(static_cast<char>((value >> 16U) & 0xFFU));
        output.put(static_cast<char>((value >> 24U) & 0xFFU));
    };

    output.put('B');
    output.put('M');
    write_u32(static_cast<uint32_t>(file_size));
    write_u16(0);
    write_u16(0);
    write_u32(54);

    write_u32(40);
    write_u32(static_cast<uint32_t>(image.width));
    write_u32(static_cast<uint32_t>(image.height));
    write_u16(1);
    write_u16(24);
    write_u32(0);
    write_u32(static_cast<uint32_t>(pixel_bytes));
    write_u32(2835);
    write_u32(2835);
    write_u32(0);
    write_u32(0);

    const std::array<uint8_t, 3> padding{{0, 0, 0}};
    for (int row = image.height - 1; row >= 0; --row) {
        for (int col = 0; col < image.width; ++col) {
            const std::size_t index = (static_cast<std::size_t>(row) * image.width + col) * 3U;
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

        const ls2k::legacy::SteeringAnalysisResult analysis =
            ls2k::legacy::AnalyzeFrame(frame, params, {}, {}, false, 1, 1);
        const LaneMetrics metrics =
            ls2k::legacy::ExtractLaneMetrics(frame, analysis.perception.threshold, params, {}, {}, 1);

        std::cout << "module=" << analysis.perception.active_module
                  << " scene=" << analysis.perception.scene_phase
                  << " circle_direction=" << analysis.perception.circle_direction
                  << " circle_reference_mode=" << analysis.perception.circle_reference_mode
                  << " circle_fallback_reason=" << analysis.perception.circle_fallback_reason
                  << " threshold=" << analysis.perception.threshold
                  << " lateral_error=" << analysis.perception.lateral_error
                  << " steering_reference_col=" << analysis.perception.steering_reference_col << "\n";

        RgbImage image(frame);
        DrawVerticalLine(image, frame.width / 2, Color{255, 0, 0});
        DrawVerticalLine(image, analysis.perception.steering_reference_col, Color{0, 255, 255});
        DrawSquare(image, frame.width / 2, frame.height - 12, 5, Color{255, 0, 0}, false);
        DrawSquare(image,
                   analysis.perception.steering_reference_col,
                   frame.height - 24,
                   5,
                   Color{0, 255, 255},
                   false);

        DrawAnchorOverlay(image, metrics.left_edge, true, "left");
        DrawAnchorOverlay(image, metrics.right_edge, false, "right");

        WriteBmp(output_path, image);
        std::cout << "wrote_overlay=" << output_path << "\n";
    } catch (const std::exception& error) {
        std::cerr << "scene_overlay_probe failed: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
