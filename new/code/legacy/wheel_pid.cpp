#include "legacy/wheel_pid.hpp"

#include <algorithm>
#include <cmath>

namespace ls2k::legacy {
namespace {

double NormalizeMeasurementFilterAlpha(double alpha) {
    if (alpha <= 0.0) {
        return 1.0;
    }
    return std::clamp(alpha, 0.0, 1.0);
}

double FilterMeasuredSpeed(double measured_speed, double previous_filtered, bool& ready, double alpha) {
    if (!ready) {
        ready = true;
        return measured_speed;
    }
    return measured_speed * alpha + previous_filtered * (1.0 - alpha);
}

}  // namespace

void WheelPidController::Configure(const port::WheelPidParameters& params) {
    p_ = params.p;
    i_ = params.i;
    d_ = params.d;
    integral_limit_ = std::max(0.0, params.integral_limit);
    measurement_filter_alpha_ = NormalizeMeasurementFilterAlpha(params.measurement_filter_alpha);
}

void WheelPidController::Reset() {
    last_error_ = 0.0;
    integral_ = 0.0;
    filtered_measured_speed_ = 0.0;
    filtered_measured_ready_ = false;
}

// PID 输出基于固定控制周期计算：P 使用当前误差，I 累积误差，D 使用相邻周期误差差分。
// 当前接口没有 dt 参数，因此 D 项不是按秒归一化的微分项，也不存在 dt 除零路径。
// 如果未来控制周期变为可变周期，应同步调整接口和 D 项语义，而不是只改这里的注释。
int WheelPidController::Compute(double target_speed, double measured_speed, int pwm_limit) {
    filtered_measured_speed_ =
        FilterMeasuredSpeed(measured_speed, filtered_measured_speed_, filtered_measured_ready_, measurement_filter_alpha_);
    const double error = target_speed - filtered_measured_speed_;
    integral_ = std::clamp(integral_ + error, -integral_limit_, integral_limit_);
    const double output = p_ * error + i_ * integral_ + d_ * (error - last_error_);
    last_error_ = error;
    return static_cast<int>(
        std::round(std::clamp(output, -static_cast<double>(pwm_limit), static_cast<double>(pwm_limit))));
}

}  // namespace ls2k::legacy
