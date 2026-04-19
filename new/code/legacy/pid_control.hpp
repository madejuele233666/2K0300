#ifndef LS2K_LEGACY_PID_CONTROL_HPP
#define LS2K_LEGACY_PID_CONTROL_HPP

#include "legacy/fuzzy_pid_ucas.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

class LegacyPidControl {
public:
    void Configure(const port::RuntimeParameters& params);
    void Reset();

    float ComputeTurnTarget(const port::PerceptionResult& perception, float& w_target_last);
    float ComputeGyroTurn(float w_target, float gyro_z);

private:
    FuzzyPidUcas fuzzy_{};

    float cam_d_ = 5.0F;
    float gyro_p_ = 20.0F;
    float gyro_i_ = 0.0F;
    float gyro_d_ = 9.0F;

    float camera_error_last_ = 0.0F;
    float gyro_error_last_ = 0.0F;
    float gyro_i_count_ = 0.0F;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_PID_CONTROL_HPP
