#ifndef LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP

namespace ls2k::platform::true_ls2k0300 {

inline constexpr char kDefaultCameraPath[] = "/dev/video0";
inline constexpr char kLeftEncoderPath[] = "/dev/zf_encoder_1";
inline constexpr char kRightEncoderPath[] = "/dev/zf_encoder_2";
inline constexpr char kLeftMotorPwmPath[] = "/dev/zf_device_pwm_motor_1";
inline constexpr char kRightMotorPwmPath[] = "/dev/zf_device_pwm_motor_2";
inline constexpr char kLeftMotorGpioPath[] = "/dev/zf_driver_gpio_motor_1";
inline constexpr char kRightMotorGpioPath[] = "/dev/zf_driver_gpio_motor_2";
inline constexpr char kBatteryAdcPath[] = "/sys/bus/iio/devices/iio:device0/in_voltage7_raw";

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP
