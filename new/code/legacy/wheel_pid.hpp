#ifndef LS2K_LEGACY_WHEEL_PID_HPP
#define LS2K_LEGACY_WHEEL_PID_HPP

#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

class WheelPidController {
public:
    void Configure(const port::WheelPidParameters& params);
    void Reset();
    int Compute(double target_speed, double measured_speed, int pwm_limit);

private:
    double p_ = 84.0;
    double i_ = 2.4;
    double d_ = 0.75;
    double integral_limit_ = 5000.0;
    double measurement_filter_alpha_ = 0.4;
    double last_error_ = 0.0;
    double integral_ = 0.0;
    double filtered_measured_speed_ = 0.0;
    bool filtered_measured_ready_ = false;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_WHEEL_PID_HPP
