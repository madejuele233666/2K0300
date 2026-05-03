#ifndef LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP
#define LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP

#include <cstdint>
#include <string>

namespace ls2k::port {

struct ImuSample {
    bool valid = false;
    float acc_x = 0.0F;
    float acc_y = 0.0F;
    float acc_z = 0.0F;
    float gyro_x = 0.0F;
    float gyro_y = 0.0F;
    float gyro_z = 0.0F;
    uint64_t capture_time_ms = 0;
};

struct EncoderDelta {
    bool valid = false;
    int left = 0;
    int right = 0;
    uint64_t capture_time_ms = 0;
};

struct LowVoltageSample {
    bool valid = false;
    bool emergency = true;
    int raw_value = -1;
    int threshold = 0;
    uint64_t capture_time_ms = 0;
    std::string source = "unavailable";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_SENSOR_SAMPLE_TYPES_HPP
