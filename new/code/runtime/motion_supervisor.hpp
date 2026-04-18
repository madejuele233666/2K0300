#ifndef LS2K_RUNTIME_MOTION_SUPERVISOR_HPP
#define LS2K_RUNTIME_MOTION_SUPERVISOR_HPP

#include "runtime/motion_types.hpp"

namespace ls2k::runtime {

class MotionSupervisor {
public:
    MotionDecision Evaluate(const MotionSupervisorInputs& inputs) const;
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_MOTION_SUPERVISOR_HPP
