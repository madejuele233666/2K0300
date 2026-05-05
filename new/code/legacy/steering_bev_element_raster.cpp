#include "legacy/steering_bev_element_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include "legacy/steering_bev_simple_perception.hpp"

namespace ls2k::legacy {
namespace {

constexpr std::uint8_t kInvalidGray = 0U;
constexpr int kBilinearWeightScale = 256;

bool SameCalibration(const port::BEVProjectorCalibration& lhs,
                     const port::BEVProjectorCalibration& rhs) {
    if (lhs.valid != rhs.valid ||
        lhs.debug_grid_width != rhs.debug_grid_width ||
        lhs.debug_grid_height != rhs.debug_grid_height ||
        lhs.projector_id != rhs.projector_id ||
        lhs.projector_hash != rhs.projector_hash) {
        return false;
    }
    for (std::size_t index = 0; index < port::kBevCalibrationPointCount; ++index) {
        if (lhs.source_points[index].row_px != rhs.source_points[index].row_px ||
            lhs.source_points[index].col_px != rhs.source_points[index].col_px ||
            lhs.target_points[index].forward_m != rhs.target_points[index].forward_m ||
            lhs.target_points[index].lateral_m != rhs.target_points[index].lateral_m) {
            return false;
        }
    }
    return true;
}

bool LutMatches(const BEVElementRasterLut& lut,
                const port::LegacyCameraFrameView& frame,
                const port::RuntimeParameters& params,
                const BEVProjector& projector,
                int width,
                int height,
                float lateral_limit,
                float forward_max) {
    return lut.valid &&
           lut.frame_width == frame.width &&
           lut.frame_height == frame.height &&
           lut.frame_stride == frame.stride &&
           lut.params.enabled == params.bev_element_raster.enabled &&
           lut.params.width == params.bev_element_raster.width &&
           lut.width == width &&
           lut.height == height &&
           lut.lateral_limit_m == lateral_limit &&
           lut.forward_max_m == forward_max &&
           SameCalibration(lut.calibration, projector.Calibration()) &&
           lut.entries.size() == static_cast<std::size_t>(width * height);
}

port::BEVPoint MetricPointForCell(int x,
                                  int y,
                                  int width,
                                  int height,
                                  float lateral_limit_m,
                                  float forward_max_m) {
    const float normalized_x =
        width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.5F;
    const float normalized_y =
        height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 1.0F;
    port::BEVPoint point{};
    point.lateral_m = normalized_x * (2.0F * lateral_limit_m) - lateral_limit_m;
    point.forward_m = (1.0F - normalized_y) * forward_max_m;
    return point;
}

int RasterHeightForWidth(int width, float lateral_limit_m, float forward_max_m) {
    const float metric_width = std::max(1.0e-4F, lateral_limit_m * 2.0F);
    const float scale_px_per_m = static_cast<float>(width) / metric_width;
    return std::max(2, static_cast<int>(std::lround(forward_max_m * scale_px_per_m)));
}

bool BuildSample(const port::LegacyCameraFrameView& frame,
                 float row_px,
                 float col_px,
                 BEVElementRasterLutEntry& entry) {
    if (row_px < 0.0F || col_px < 0.0F ||
        row_px > static_cast<float>(frame.height - 1) ||
        col_px > static_cast<float>(frame.width - 1)) {
        entry.state = port::BEVElementRasterProjectionState::kOutsideFrame;
        return false;
    }

    const int row0 = static_cast<int>(std::floor(row_px));
    const int col0 = static_cast<int>(std::floor(col_px));
    const int row1 = std::min(row0 + 1, frame.height - 1);
    const int col1 = std::min(col0 + 1, frame.width - 1);
    const float row_frac = row_px - static_cast<float>(row0);
    const float col_frac = col_px - static_cast<float>(col0);

    entry.source_indices[0] =
        static_cast<std::uint32_t>(row0 * frame.stride + col0);
    entry.source_indices[1] =
        static_cast<std::uint32_t>(row0 * frame.stride + col1);
    entry.source_indices[2] =
        static_cast<std::uint32_t>(row1 * frame.stride + col0);
    entry.source_indices[3] =
        static_cast<std::uint32_t>(row1 * frame.stride + col1);

    const float weights[4] = {
        (1.0F - row_frac) * (1.0F - col_frac),
        (1.0F - row_frac) * col_frac,
        row_frac * (1.0F - col_frac),
        row_frac * col_frac,
    };
    int accumulated = 0;
    for (int index = 0; index < 3; ++index) {
        const int quantized =
            std::clamp(static_cast<int>(std::lround(weights[index] * kBilinearWeightScale)),
                       0,
                       kBilinearWeightScale);
        entry.weights[static_cast<std::size_t>(index)] =
            static_cast<std::uint16_t>(quantized);
        accumulated += quantized;
    }
    entry.weights[3] =
        static_cast<std::uint16_t>(std::clamp(kBilinearWeightScale - accumulated,
                                              0,
                                              kBilinearWeightScale));
    entry.state = port::BEVElementRasterProjectionState::kSampleable;
    return true;
}

port::BEVElementRasterCellClass ToRasterClass(BEVSimplePixelClass class_kind) {
    switch (class_kind) {
        case BEVSimplePixelClass::kWhite:
            return port::BEVElementRasterCellClass::kWhite;
        case BEVSimplePixelClass::kBlack:
            return port::BEVElementRasterCellClass::kBlack;
        case BEVSimplePixelClass::kUnknown:
            return port::BEVElementRasterCellClass::kUnknown;
        case BEVSimplePixelClass::kInvalid:
            break;
    }
    return port::BEVElementRasterCellClass::kInvalid;
}

std::array<port::BEVElementRasterCellClass, 256> BuildClassTable(
    int threshold,
    const port::BEVClassificationParameters& classification) {
    std::array<port::BEVElementRasterCellClass, 256> table{};
    for (std::size_t gray = 0; gray < table.size(); ++gray) {
        table[gray] = ToRasterClass(ClassifyBevPixel(static_cast<std::uint8_t>(gray),
                                                     threshold,
                                                     classification));
    }
    return table;
}

void PrepareRasterStorage(BEVElementRasterFrame& raster,
                          const BEVElementRasterLut& lut) {
    raster.valid = lut.valid;
    raster.enabled = true;
    raster.width = lut.width;
    raster.height = lut.height;
    raster.lateral_limit_m = lut.lateral_limit_m;
    raster.forward_max_m = lut.forward_max_m;
    const std::size_t cell_count = static_cast<std::size_t>(lut.width * lut.height);
    if (raster.gray.size() != cell_count) {
        raster.gray.resize(cell_count);
    }
    if (raster.classes.size() != cell_count) {
        raster.classes.resize(cell_count);
    }
    if (raster.projection_states.size() != cell_count) {
        raster.projection_states.resize(cell_count);
    }
}

BEVElementRasterFrame BuildRasterFromLut(const port::LegacyCameraFrameView& frame,
                                         int threshold,
                                         const port::RuntimeParameters& params,
                                         const BEVElementRasterLut& lut,
                                         BEVElementRasterFrame raster) {
    if (!lut.valid) {
        return {};
    }
    PrepareRasterStorage(raster, lut);
    const std::array<port::BEVElementRasterCellClass, 256> class_table =
        BuildClassTable(threshold, params.bev_classification);
    for (std::size_t index = 0; index < lut.entries.size(); ++index) {
        const BEVElementRasterLutEntry& entry = lut.entries[index];
        raster.projection_states[index] = entry.state;
        if (entry.state != port::BEVElementRasterProjectionState::kSampleable) {
            raster.gray[index] = kInvalidGray;
            raster.classes[index] = port::BEVElementRasterCellClass::kInvalid;
            continue;
        }
        std::uint32_t weighted_sum = 0U;
        for (std::size_t sample = 0; sample < entry.source_indices.size(); ++sample) {
            weighted_sum += static_cast<std::uint32_t>(frame.gray[entry.source_indices[sample]]) *
                            static_cast<std::uint32_t>(entry.weights[sample]);
        }
        const std::uint8_t gray =
            static_cast<std::uint8_t>((weighted_sum + (kBilinearWeightScale / 2)) /
                                      kBilinearWeightScale);
        raster.gray[index] = gray;
        raster.classes[index] = class_table[gray];
    }
    return raster;
}

}  // namespace

bool BEVElementRasterFrame::InBounds(int x, int y) const {
    return valid && x >= 0 && y >= 0 && x < width && y < height;
}

std::size_t BEVElementRasterFrame::Index(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

port::BEVPoint BEVElementRasterFrame::CellToMetric(int x, int y) const {
    return MetricPointForCell(x, y, width, height, lateral_limit_m, forward_max_m);
}

bool BEVElementRasterFrame::MetricToCell(const port::BEVPoint& point, int& x, int& y) const {
    if (!valid || width <= 1 || height <= 1 || lateral_limit_m <= 0.0F || forward_max_m <= 0.0F) {
        return false;
    }
    const float normalized_x = (point.lateral_m + lateral_limit_m) / (2.0F * lateral_limit_m);
    const float normalized_y = 1.0F - (point.forward_m / forward_max_m);
    if (normalized_x < 0.0F || normalized_x > 1.0F ||
        normalized_y < 0.0F || normalized_y > 1.0F) {
        return false;
    }
    x = static_cast<int>(std::lround(normalized_x * static_cast<float>(width - 1)));
    y = static_cast<int>(std::lround(normalized_y * static_cast<float>(height - 1)));
    return InBounds(x, y);
}

port::BEVElementRasterCellClass BEVElementRasterFrame::ClassAt(int x, int y) const {
    if (!InBounds(x, y)) {
        return port::BEVElementRasterCellClass::kInvalid;
    }
    return classes[Index(x, y)];
}

bool BEVElementRasterFrame::SegmentTouchesBlackCells(int x0, int y0, int x1, int y1) const {
    if (!InBounds(x0, y0) || !InBounds(x1, y1)) {
        return false;
    }
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int x = x0;
    int y = y0;
    while (true) {
        if (ClassAt(x, y) == port::BEVElementRasterCellClass::kBlack) {
            return true;
        }
        if (x == x1 && y == y1) {
            break;
        }
        const int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y += sy;
        }
    }
    return false;
}

bool BEVElementRasterFrame::SegmentTouchesBlack(const port::BEVPoint& begin,
                                                const port::BEVPoint& end) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!MetricToCell(begin, x0, y0) || !MetricToCell(end, x1, y1)) {
        return false;
    }
    return SegmentTouchesBlackCells(x0, y0, x1, y1);
}

const char* ToString(port::BEVElementRasterCellClass class_kind) {
    switch (class_kind) {
        case port::BEVElementRasterCellClass::kInvalid:
            return "invalid";
        case port::BEVElementRasterCellClass::kUnknown:
            return "unknown";
        case port::BEVElementRasterCellClass::kBlack:
            return "black";
        case port::BEVElementRasterCellClass::kWhite:
            return "white";
    }
    return "invalid";
}

const char* ToString(port::BEVElementRasterProjectionState state) {
    switch (state) {
        case port::BEVElementRasterProjectionState::kUnavailable:
            return "unavailable";
        case port::BEVElementRasterProjectionState::kSampleable:
            return "sampleable";
        case port::BEVElementRasterProjectionState::kOutsideFrame:
            return "outside_frame";
        case port::BEVElementRasterProjectionState::kProjectionFailed:
            return "projection_failed";
    }
    return "unavailable";
}

bool EnsureBEVElementRasterLut(BEVElementRasterLut& lut,
                               const port::LegacyCameraFrameView& frame,
                               const port::RuntimeParameters& params,
                               const BEVProjector& projector) {
    if (!params.bev_element_raster.enabled) {
        lut = {};
        return false;
    }
    const float lateral_limit = std::max(0.1F, params.bev_geometry.search_lateral_limit_m);
    const float forward_max = params.bev_geometry.forward_samples_m.back();
    const int width = std::max(2, params.bev_element_raster.width);
    const int height = RasterHeightForWidth(width, lateral_limit, forward_max);
    if (!projector.Valid() || !frame.Valid()) {
        lut = {};
        return false;
    }
    if (LutMatches(lut, frame, params, projector, width, height, lateral_limit, forward_max)) {
        return true;
    }

    BEVElementRasterLut rebuilt{};
    rebuilt.valid = true;
    rebuilt.calibration = projector.Calibration();
    rebuilt.params = params.bev_element_raster;
    rebuilt.frame_width = frame.width;
    rebuilt.frame_height = frame.height;
    rebuilt.frame_stride = frame.stride;
    rebuilt.width = width;
    rebuilt.height = height;
    rebuilt.lateral_limit_m = lateral_limit;
    rebuilt.forward_max_m = forward_max;
    rebuilt.entries.resize(static_cast<std::size_t>(width * height));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            BEVElementRasterLutEntry& entry =
                rebuilt.entries[static_cast<std::size_t>(y * width + x)];
            entry.metric_point = MetricPointForCell(x, y, width, height, lateral_limit, forward_max);
            port::ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage(entry.metric_point, image_point)) {
                entry.state = port::BEVElementRasterProjectionState::kProjectionFailed;
                continue;
            }
            (void)BuildSample(frame, image_point.row_px, image_point.col_px, entry);
        }
    }

    lut = std::move(rebuilt);
    return true;
}

BEVElementRasterFrame BuildBEVElementRaster(const port::LegacyCameraFrameView& frame,
                                            int threshold,
                                            const port::RuntimeParameters& params,
                                            const BEVProjector& projector,
                                            BEVElementRasterLut* lut) {
    BEVElementRasterLut local_lut{};
    BEVElementRasterLut& active_lut = lut == nullptr ? local_lut : *lut;
    if (!EnsureBEVElementRasterLut(active_lut, frame, params, projector)) {
        return {};
    }
    return BuildRasterFromLut(frame, threshold, params, active_lut, {});
}

const BEVElementRasterFrame& BEVElementRasterBuilder::Build(
    const port::LegacyCameraFrameView& frame,
    int threshold,
    const port::RuntimeParameters& params,
    const BEVProjector& projector) {
    if (!EnsureBEVElementRasterLut(lut_, frame, params, projector)) {
        raster_ = {};
        raster_.enabled = params.bev_element_raster.enabled;
        return raster_;
    }
    raster_ = BuildRasterFromLut(frame, threshold, params, lut_, std::move(raster_));
    return raster_;
}

void BEVElementRasterBuilder::Reset() {
    lut_ = {};
    raster_ = {};
}

}  // namespace ls2k::legacy
