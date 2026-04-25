#ifndef LS2K_LEGACY_PID_CONTROL_HPP
#define LS2K_LEGACY_PID_CONTROL_HPP

#include "legacy/fuzzy_pid_ucas.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

struct CameraTurnComputation {
    float resolved_fuzzy_p = 0.0F;
    float camera_p_term = 0.0F;
    float camera_d_term = 0.0F;
    float w_target = 0.0F;
};

struct GyroTurnComputation {
    float gyro_z = 0.0F;
    float gyro_error = 0.0F;
    float gyro_p_term = 0.0F;
    float gyro_d_term = 0.0F;
    float raw_turn_output = 0.0F;
};

class LegacyPidControl {
public:
    void Configure(const port::RuntimeParameters& params);
    void Reset();

    CameraTurnComputation ComputeTurnTarget(const port::PerceptionResult& perception,
                                            port::LegacySteeringControllerMemory& memory);
    GyroTurnComputation ComputeGyroTurn(float w_target,
                                        float gyro_z,
                                        port::LegacySteeringControllerMemory& memory);

private:
    FuzzyPidUcas fuzzy_{};

    float cam_p_ = 14.75F;
    float cam_p_scale_ = 1.0F;
    float cam_d_ = 5.0F;
    float gyro_p_ = 20.0F;
    float gyro_i_ = 0.0F;
    float gyro_d_ = 9.0F;
    bool cam_use_fuzzy_ = false;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_PID_CONTROL_HPP
