#ifndef LS2K_LEGACY_CAMERA_LOGIC_HPP
#define LS2K_LEGACY_CAMERA_LOGIC_HPP

#include "port/control_types.hpp"

namespace ls2k::legacy {

SteeringAnalysisResult AnalyzeFrame(const port::LegacyCameraFrame& frame,
                                    const port::RuntimeParameters& params,
                                    const port::LegacySteeringState& prior_state,
                                    bool low_voltage_emergency,
                                    uint64_t frame_id,
                                    uint64_t capture_time_ms);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_CAMERA_LOGIC_HPP
