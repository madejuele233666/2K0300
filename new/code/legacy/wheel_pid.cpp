#include "legacy/wheel_pid.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {

void WheelPidController::Configure(const port::WheelPidParameters& params) {
    p_ = params.p;
    i_ = params.i;
    d_ = params.d;
    integral_limit_ = std::max(0.0, params.integral_limit);
}

void WheelPidController::Reset() {
    last_error_ = 0.0;
    integral_ = 0.0;
}

int WheelPidController::Compute(double target_speed, double measured_speed, int pwm_limit) {
    const double error = target_speed - measured_speed;
    integral_ = std::clamp(integral_ + error, -integral_limit_, integral_limit_);
    const double output = p_ * error + i_ * integral_ + d_ * (error - last_error_);
    last_error_ = error;
    return static_cast<int>(
        std::round(std::clamp(output, -static_cast<double>(pwm_limit), static_cast<double>(pwm_limit))));
}

}  // namespace ls2k::legacy
