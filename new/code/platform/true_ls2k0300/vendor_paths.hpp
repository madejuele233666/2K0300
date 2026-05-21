#ifndef LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP

namespace ls2k::platform::true_ls2k0300 {

// 默认相机视频设备路径
inline constexpr char kDefaultCameraPath[] = "/dev/video0";
// 左轮编码器字符设备路径
inline constexpr char kLeftEncoderPath[] = "/dev/zf_encoder_1";
// 右轮编码器字符设备路径
inline constexpr char kRightEncoderPath[] = "/dev/zf_encoder_2";
// 左电机 PWM 控制字符设备路径
inline constexpr char kLeftMotorPwmPath[] = "/dev/zf_device_pwm_motor_1";
// 右电机 PWM 控制字符设备路径
inline constexpr char kRightMotorPwmPath[] = "/dev/zf_device_pwm_motor_2";
// 左电机方向 GPIO 控制字符设备路径
inline constexpr char kLeftMotorGpioPath[] = "/dev/zf_driver_gpio_motor_1";
// 右电机方向 GPIO 控制字符设备路径
inline constexpr char kRightMotorGpioPath[] = "/dev/zf_driver_gpio_motor_2";
// 电池电压 ADC 输入 sysfs 路径
inline constexpr char kBatteryAdcPath[] = "/sys/bus/iio/devices/iio:device0/in_voltage7_raw";

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_VENDOR_PATHS_HPP
