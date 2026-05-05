#ifndef LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP
#define LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP

namespace ls2k::port {

enum class BEVElementRasterCellClass {
    kInvalid,
    kUnknown,
    kBlack,
    kWhite,
};

enum class BEVElementRasterProjectionState {
    kUnavailable,
    kSampleable,
    kOutsideFrame,
    kProjectionFailed,
};

struct BEVElementRasterParameters {
    bool enabled = true;
    int width = 320;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP
