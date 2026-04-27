#ifndef LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP
#define LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP

#include "port/control_types.hpp"

namespace ls2k::legacy {

port::ControlErrorModelOutput ComputeControlErrorModel(const port::ControlErrorModelInput& input,
                                                       const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP
