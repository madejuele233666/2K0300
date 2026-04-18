#include "legacy/attitude_logic.hpp"

namespace ls2k::legacy {

void LegacyAttitudeLogic::UpdateFromImu(const port::ImuSample& sample, float dt_sec) {
    if (!sample.valid) {
        return;
    }
    yaw_deg_ += sample.gyro_z * dt_sec * 57.324841F;
}

}  // namespace ls2k::legacy
