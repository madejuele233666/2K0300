#ifndef LS2K_LEGACY_ATTITUDE_LOGIC_HPP
#define LS2K_LEGACY_ATTITUDE_LOGIC_HPP

#include "port/sensor_sample_types.hpp"

namespace ls2k::legacy {

class LegacyAttitudeLogic {
public:
    void Reset();
    void UpdateFromImu(const port::ImuSample& sample, float dt_sec);
    float yaw_deg() const { return yaw_deg_; }

private:
    float yaw_deg_ = 0.0F;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_ATTITUDE_LOGIC_HPP
