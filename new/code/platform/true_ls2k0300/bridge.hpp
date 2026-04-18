#ifndef LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

struct BridgeStatus {
    bool ok = false;
    std::string detail;
};

struct CameraFrameView {
    bool valid = false;
    const uint8_t* gray = nullptr;
    int width = 0;
    int height = 0;
};

struct ImuInitResult {
    bool ready = false;
    uint8_t imu_type = 0;
    std::string source;
    std::string detail;
};

struct ImuBridgeSample {
    bool valid = false;
    uint8_t imu_type = 0;
    std::string source;
    std::string detail;
    int16_t acc_x = 0;
    int16_t acc_y = 0;
    int16_t acc_z = 0;
    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;
    int16_t mag_x = 0;
    int16_t mag_y = 0;
    int16_t mag_z = 0;
};

struct EncoderCounts {
    bool valid = false;
    int left = 0;
    int right = 0;
    std::string detail;
};

struct BatteryRawResult {
    bool valid = false;
    int raw_value = -1;
    std::string source;
    std::string detail;
};

bool InitializeCamera(const std::string& video_path);
CameraFrameView CaptureCameraFrame();
void ShutdownCamera();

ImuInitResult InitializeImu();
ImuBridgeSample ReadImuSample();

BridgeStatus InitializeEncoder();
EncoderCounts ReadEncoderCounts();

BridgeStatus InitializeMotor();
BridgeStatus ApplyMotorCommand(int left_pwm, int right_pwm);
BridgeStatus DisableMotorOutput();

BatteryRawResult ReadBatteryRaw(const std::string& adc_path);

class TimerBridge {
public:
    TimerBridge();
    ~TimerBridge();

    bool Start(uint32_t period_ms, std::function<void()> callback, bool use_vendor_timer);
    void Stop();
    bool Running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_BRIDGE_HPP
