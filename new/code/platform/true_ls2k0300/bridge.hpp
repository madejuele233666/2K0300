#ifndef LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

// 桥接操作状态结果 —— 指示操作是否成功及详细描述信息
struct BridgeStatus {
    bool ok = false;        // 操作是否成功
    std::string detail;     // 操作的详细描述或错误信息
};

// 相机帧视图 —— 包含灰度图像数据指针及图像尺寸
struct CameraFrameView {
    bool valid = false;              // 帧数据是否有效
    const uint8_t* gray = nullptr;   // 灰度图像像素数据缓冲区指针
    int width = 0;                   // 图像宽度（像素）
    int height = 0;                  // 图像高度（像素）
};

// IMU 初始化结果 —— 包含就绪状态、型号类型和设备源路径
struct ImuInitResult {
    bool ready = false;       // IMU 是否已初始化就绪
    uint8_t imu_type = 0;     // 探测到的 IMU 型号（DEV_IMU660RA / DEV_IMU660RB / DEV_IMU963RA）
    std::string source;       // IMU 设备 sysfs 源路径
    std::string detail;       // 初始化详细描述或错误信息
};

// IMU 桥接采样数据 —— 包含加速度计/陀螺仪/磁力计三轴原始读数
struct ImuBridgeSample {
    bool valid = false;       // 样本是否有效
    uint8_t imu_type = 0;     // IMU 型号类型
    std::string source;       // 采样来源的设备路径
    std::string detail;       // 采样详细描述或错误信息
    int16_t acc_x = 0;        // X 轴加速度计原始值
    int16_t acc_y = 0;        // Y 轴加速度计原始值
    int16_t acc_z = 0;        // Z 轴加速度计原始值
    int16_t gyro_x = 0;       // X 轴陀螺仪原始值（角速度）
    int16_t gyro_y = 0;       // Y 轴陀螺仪原始值（角速度）
    int16_t gyro_z = 0;       // Z 轴陀螺仪原始值（角速度）
    int16_t mag_x = 0;        // X 轴磁力计原始值（仅 IMU963RA 支持）
    int16_t mag_y = 0;        // Y 轴磁力计原始值（仅 IMU963RA 支持）
    int16_t mag_z = 0;        // Z 轴磁力计原始值（仅 IMU963RA 支持）
};

// 编码器计数值 —— 左右轮编码器脉冲计数
struct EncoderCounts {
    bool valid = false;     // 计数值是否有效
    int left = 0;           // 左轮编码器计数值
    int right = 0;          // 右轮编码器计数值
    std::string detail;     // 读取详细描述或错误信息
};

// 电池 ADC 原始读取结果
struct BatteryRawResult {
    bool valid = false;      // 结果是否有效
    int raw_value = -1;      // ADC 转换原始整数值
    std::string source;      // ADC 设备 sysfs 路径
    std::string detail;      // 读取详细描述或错误信息
};

// 初始化 UVC 相机设备
// @param video_path 视频设备路径（如 /dev/video0）
// @return true 初始化成功，false 初始化失败
bool InitializeCamera(const std::string& video_path);

// 采集一帧相机灰度图像
// @return 相机帧视图，包含灰度图像数据和尺寸信息；valid=false 表示采集失败
CameraFrameView CaptureCameraFrame();

// 关闭相机设备并释放相关资源
void ShutdownCamera();

// 初始化 IMU 传感器 —— 从 IIO sysfs 自动探测支持的型号
// @return IMU 初始化结果，包含型号、设备路径和就绪状态
ImuInitResult InitializeImu();

// 读取 IMU 传感器样本 —— 获取加速度/角速度/磁力计各轴原始值
// @return IMU 采样数据，valid=false 表示读取失败
ImuBridgeSample ReadImuSample();

// 关闭 IMU 桥接层资源
void ShutdownImu();

// 初始化编码器设备 —— 探测左右轮编码器字符设备的可访问性
// @return 桥接状态，ok=false 表示初始化失败
BridgeStatus InitializeEncoder();

// 读取左右轮编码器计数值
// @return 编码器计数值，valid=false 表示读取失败
EncoderCounts ReadEncoderCounts();

// 初始化电机 PWM/GPIO 设备 —— 探测路径可写性
// @return 桥接状态，ok=false 表示初始化失败
BridgeStatus InitializeMotor();

// 施加电机差速驱动命令（左右独立 PWM）
// @param left_pwm 左电机有符号 PWM 值（范围 -9000~9000，负值反转）
// @param right_pwm 右电机有符号 PWM 值（范围 -9000~9000，负值反转）
// @return 桥接状态，任一路失败时会自动回滚禁用全部输出
BridgeStatus ApplyMotorCommand(int left_pwm, int right_pwm);

// 禁用电机输出 —— 将左右 PWM 置零
// @return 桥接状态
BridgeStatus DisableMotorOutput();

// 从指定 sysfs 路径读取电池 ADC 原始电压值
// @param adc_path ADC 设备 sysfs 路径
// @return ADC 原始读取结果
BatteryRawResult ReadBatteryRaw(const std::string& adc_path);

// TimerBridge —— 基于 timerfd 的周期定时器封装类
class TimerBridge {
public:
    TimerBridge();
    ~TimerBridge();

    // 启动周期定时器
    // @param period_ms 定时周期（毫秒）
    // @param callback 定时到期回调函数
    // @param on_failure 故障回调函数（工作线程异常退出时调用）
    // @return true 启动成功，false 启动失败
    bool Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure);
    // 停止定时器 —— 通知工作线程退出并等待其结束
    void Stop();
    // 检查定时器是否正在运行
    // @return true 运行中，false 已停止
    bool Running() const;

private:
    struct Impl;                    // PIMPL 模式：实现隐藏
    std::unique_ptr<Impl> impl_;    // 实现实例的唯一所有权指针
};

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP
