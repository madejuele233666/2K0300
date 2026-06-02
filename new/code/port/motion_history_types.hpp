#ifndef LS2K_PORT_MOTION_HISTORY_TYPES_HPP
#define LS2K_PORT_MOTION_HISTORY_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ls2k::port {

/// 控制侧运动历史采样，只记录传感器事实
struct MotionHistorySample {
    uint64_t time_ms = 0;          ///< control tick 时间
    bool imu_valid = false;        ///< IMU 是否有效
    float gyro_z = 0.0F;           ///< yaw rate
    bool encoder_valid = false;    ///< encoder 是否有效
    int left_encoder_delta = 0;    ///< 左编码器增量
    int right_encoder_delta = 0;   ///< 右编码器增量
};

/// 固定容量运动历史 ring buffer
struct MotionHistory {
    static constexpr std::size_t kCapacity = 2048;

    void Push(const MotionHistorySample& sample) {
        samples[next_index] = sample;
        next_index = (next_index + 1) % kCapacity;
        if (count < kCapacity) {
            ++count;
        }
    }

    std::array<MotionHistorySample, kCapacity> Ordered() const {
        std::array<MotionHistorySample, kCapacity> ordered{};
        for (std::size_t offset = 0; offset < count; ++offset) {
            ordered[offset] = OldestOffset(offset);
        }
        return ordered;
    }

    const MotionHistorySample& OldestOffset(std::size_t offset) const {
        const std::size_t src = (next_index + kCapacity - count + offset) % kCapacity;
        return samples[src];
    }

    std::array<MotionHistorySample, kCapacity> samples{};
    std::size_t next_index = 0;
    std::size_t count = 0;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_MOTION_HISTORY_TYPES_HPP
