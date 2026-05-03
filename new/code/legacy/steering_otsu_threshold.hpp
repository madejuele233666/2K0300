#ifndef LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
#define LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP

#include "port/camera_frame_types.hpp"

namespace ls2k::legacy {

int ComputeOtsuThreshold(const port::LegacyCameraFrameView& frame);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
