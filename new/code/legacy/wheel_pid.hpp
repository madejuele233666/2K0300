#ifndef LS2K_LEGACY_WHEEL_PID_HPP
#define LS2K_LEGACY_WHEEL_PID_HPP

#include "port/control_types.hpp"

namespace ls2k::legacy {

class WheelPidController {
public:
    void Configure(const port::WheelPidParameters& params);
    void Reset();
    int Compute(double target_speed, double measured_speed, int pwm_limit);

private:
    double p_ = 240.0;
    double i_ = 10.0;
    double d_ = 20.0;
    double integral_limit_ = 2200.0;
    double last_error_ = 0.0;
    double integral_ = 0.0;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_WHEEL_PID_HPP
