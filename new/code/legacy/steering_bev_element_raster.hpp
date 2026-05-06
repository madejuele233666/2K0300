#ifndef LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP
#define LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/bev_element_raster_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

struct BEVElementRasterLutEntry {
    port::BEVElementRasterProjectionState state =
        port::BEVElementRasterProjectionState::kUnavailable;
    port::BEVPoint metric_point{};
    std::array<std::uint32_t, 4> source_indices{};
    std::array<std::uint16_t, 4> weights{};
};

struct BEVElementRasterLut {
    bool valid = false;
    port::BEVProjectorCalibration calibration{};
    port::BEVElementRasterParameters params{};
    int frame_width = 0;
    int frame_height = 0;
    int frame_stride = 0;
    int width = 0;
    int height = 0;
    float lateral_limit_m = 0.0F;
    float forward_max_m = 0.0F;
    std::vector<BEVElementRasterLutEntry> entries{};
};

struct BEVElementRasterFrame {
    bool valid = false;
    bool enabled = false;
    int width = 0;
    int height = 0;
    float lateral_limit_m = 0.0F;
    float forward_max_m = 0.0F;
    std::vector<port::BEVElementRasterCellClass> classes{};
    std::vector<port::BEVElementRasterProjectionState> projection_states{};

    bool InBounds(int x, int y) const;
    std::size_t Index(int x, int y) const;
    port::BEVPoint CellToMetric(int x, int y) const;
    bool MetricToCell(const port::BEVPoint& point, int& x, int& y) const;
    port::BEVElementRasterCellClass ClassAt(int x, int y) const;
    bool SegmentTouchesBlackCells(int x0, int y0, int x1, int y1) const;
    bool SegmentTouchesBlack(const port::BEVPoint& begin, const port::BEVPoint& end) const;
};

class BEVElementRasterBuilder {
public:
    const BEVElementRasterFrame& Build(const port::LegacyCameraFrameView& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

    void Reset();

private:
    BEVElementRasterLut lut_{};
    BEVElementRasterFrame raster_{};
};

const char* ToString(port::BEVElementRasterCellClass class_kind);
const char* ToString(port::BEVElementRasterProjectionState state);

bool EnsureBEVElementRasterLut(BEVElementRasterLut& lut,
                               const port::LegacyCameraFrameView& frame,
                               const port::RuntimeParameters& params,
                               const BEVProjector& projector);

BEVElementRasterFrame BuildBEVElementRaster(const port::LegacyCameraFrameView& frame,
                                            int threshold,
                                            const port::RuntimeParameters& params,
                                            const BEVProjector& projector,
                                            BEVElementRasterLut* lut);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP
