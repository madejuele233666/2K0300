#ifndef LS2K_RUNTIME_RUNTIME_STATE_HPP
#define LS2K_RUNTIME_RUNTIME_STATE_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "port/camera_frame_types.hpp"
#include "port/perception_result.hpp"
#include "port/sensor_sample_types.hpp"
#include "port/steering_state_types.hpp"
#include "runtime/control_decision.hpp"
#include "runtime/control_debug_snapshot.hpp"
#include "runtime/motion_types.hpp"
#include "runtime/tuning_state.hpp"

namespace ls2k::runtime {

/// 重置转向感知记忆（清空结构体）
/// @param memory  要重置的感知记忆
inline void ResetSteeringPerceptionMemory(port::SteeringPerceptionMemory& memory) {
    memory = {};
}

/// 重置普通参考连续性记忆，不触碰 scene-owned 记忆。
/// @param memory  要重置的感知记忆
inline void ResetSteeringReferenceHoldMemory(port::SteeringPerceptionMemory& memory) {
    memory.reference_hold = {};
}

/// 重置转向控制记忆（清空结构体）
/// @param memory  要重置的控制记忆
inline void ResetSteeringControlMemory(port::SteeringControlMemory& memory) {
    memory = {};
}

/// 自有相机帧槽状态枚举
enum class OwnedCameraFrameSlotState {
    kFree,      ///< 空闲（可写入）
    kReady,     ///< 已就绪（包含有效帧数据）
    kEncoding   ///< 正在编码（不可写入）
};

/// 相机帧句柄 —— 引用存储在帧槽中的帧的轻量级描述符
struct CameraFrameHandle {
    bool valid = false;            ///< 句柄是否有效
    std::size_t slot_id = 0;      ///< 帧槽索引
    uint64_t generation = 0;       ///< 帧槽代数（用于检测帧是否被覆盖）
    uint64_t frame_id = 0;         ///< 相机帧 ID
    uint64_t capture_time_ms = 0;  ///< 帧捕获时间戳（ms）
    int width = 0;                 ///< 图像宽度
    int height = 0;                ///< 图像高度
    int stride = 0;                ///< 图像行跨度
    port::CameraRawFrameMetadata metadata{};  ///< 相机采集/提交元数据
};

/// 自有相机帧槽 —— 存储相机帧数据的固定槽位
struct OwnedCameraFrameSlot {
    std::size_t slot_id = 0;      ///< 槽位 ID
    uint64_t generation = 0;       ///< 代数计数器
    uint64_t frame_id = 0;         ///< 相机帧 ID
    uint64_t capture_time_ms = 0;  ///< 帧捕获时间戳（ms）
    int width = 0;                 ///< 图像宽度
    int height = 0;                ///< 图像高度
    int stride = 0;                ///< 图像行跨度
    port::CameraRawFrameMetadata metadata{};  ///< 相机采集/提交元数据
    OwnedCameraFrameSlotState state = OwnedCameraFrameSlotState::kFree;  ///< 槽状态
    std::array<std::uint8_t, port::kCompiledCameraFrameWidth * port::kCompiledCameraFrameHeight> gray{};  ///< 灰度图像数据
};

/// 相机捕获历史 —— 环形缓冲区，保存最近若干帧的句柄以便按帧 ID 和时间戳查找
struct CameraCaptureHistory {
    static constexpr std::size_t kCapacity = 8;  ///< 历史容量

    /// 向环形缓冲区压入一个帧句柄
    void Push(const CameraFrameHandle& handle) {
        handles[next_index] = handle;
        next_index = (next_index + 1) % kCapacity;
        if (count < kCapacity) {
            ++count;
        }
    }

    /// 按帧 ID 和捕获时间戳精确查找帧句柄（从最新向最旧搜索）
    /// @param frame_id         帧 ID
    /// @param capture_time_ms  捕获时间戳
    /// @return                 匹配的帧句柄指针，未找到返回 nullptr
    const CameraFrameHandle* FindExact(uint64_t frame_id, uint64_t capture_time_ms) const {
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t index = (next_index + kCapacity - 1 - offset) % kCapacity;
            const CameraFrameHandle& handle = handles[index];
            if (handle.valid && handle.frame_id == frame_id && handle.capture_time_ms == capture_time_ms) {
                return &handle;
            }
        }
        return nullptr;
    }

    std::array<CameraFrameHandle, kCapacity> handles{};  ///< 环形缓冲区数组
    std::size_t next_index = 0;   ///< 下一个写入位置
    std::size_t count = 0;        ///< 有效句柄数量
};

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

/// 物化自有相机帧：将 LegacyCameraFrameView 的数据复制到帧槽中并返回句柄
/// @param slots           帧槽数组
/// @param next_slot_index  下一个帧槽索引（轮转）
/// @param view            相机帧视图
/// @return                帧句柄（无效时返回空句柄）
inline CameraFrameHandle MaterializeOwnedCameraFrame(
    std::array<OwnedCameraFrameSlot, 3>& slots,
    std::size_t& next_slot_index,
    const port::LegacyCameraFrameView& view,
    port::CameraRawFrameMetadata metadata = {}) {
    CameraFrameHandle handle{};
    if (!view.Valid() ||
        view.width > port::kCompiledCameraFrameWidth ||
        view.height > port::kCompiledCameraFrameHeight) {
        return handle;
    }

    constexpr std::size_t kSlotCount = 3;
    std::size_t selected = kSlotCount;
    for (std::size_t attempt = 0; attempt < kSlotCount; ++attempt) {
        const std::size_t index = (next_slot_index + attempt) % kSlotCount;
        if (slots[index].state != OwnedCameraFrameSlotState::kEncoding) {
            selected = index;
            break;
        }
    }
    if (selected == kSlotCount) {
        return handle;
    }
    next_slot_index = (selected + 1) % kSlotCount;

    OwnedCameraFrameSlot& slot = slots[selected];
    slot.slot_id = selected;
    slot.state = OwnedCameraFrameSlotState::kReady;
    slot.generation = slot.generation == UINT64_MAX ? 1 : slot.generation + 1;
    slot.frame_id = view.frame_id;
    slot.capture_time_ms = view.capture_time_ms;
    slot.width = view.width;
    slot.height = view.height;
    slot.stride = view.width;
    metadata.frame_id = view.frame_id;
    metadata.capture_time_ms = view.capture_time_ms;
    slot.metadata = metadata;
    for (int row = 0; row < view.height; ++row) {
        const std::uint8_t* src =
            view.gray + static_cast<std::size_t>(row) * static_cast<std::size_t>(view.stride);
        std::uint8_t* dst =
            slot.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot.stride);
        std::copy(src, src + view.width, dst);
    }

    handle.valid = true;
    handle.slot_id = selected;
    handle.generation = slot.generation;
    handle.frame_id = slot.frame_id;
    handle.capture_time_ms = slot.capture_time_ms;
    handle.width = slot.width;
    handle.height = slot.height;
    handle.stride = slot.stride;
    handle.metadata = slot.metadata;
    return handle;
}

/// 通过句柄从帧槽复制相机帧数据到输出帧
/// @param slots   帧槽数组
/// @param handle  帧句柄（需通过 MaterializeOwnedCameraFrame 获取）
/// @param out     输出帧（LegacyCameraFrame 格式）
/// @return        复制是否成功
inline bool CopyOwnedCameraFrameByHandle(const std::array<OwnedCameraFrameSlot, 3>& slots,
                                         const CameraFrameHandle& handle,
                                         port::LegacyCameraFrame& out) {
    if (!handle.valid || handle.slot_id >= slots.size()) {
        return false;
    }
    const OwnedCameraFrameSlot& slot = slots[handle.slot_id];
    if (slot.state == OwnedCameraFrameSlotState::kFree ||
        slot.generation != handle.generation ||
        slot.frame_id != handle.frame_id ||
        slot.capture_time_ms != handle.capture_time_ms ||
        slot.width <= 0 ||
        slot.height <= 0 ||
        slot.width > port::kCompiledCameraFrameWidth ||
        slot.height > port::kCompiledCameraFrameHeight) {
        return false;
    }
    out = {};
    out.width = slot.width;
    out.height = slot.height;
    for (int row = 0; row < slot.height; ++row) {
        const std::uint8_t* src =
            slot.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot.stride);
        std::uint8_t* dst =
            out.gray.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(out.width);
        std::copy(src, src + slot.width, dst);
    }
    return true;
}

/// 运行时状态 —— 控制系统各模块之间共享的核心状态结构。
/// 包含感知结果、传感器数据、控制命令、调试快照、生命周期标志等。
struct RuntimeState {
    // Shared runtime channels.
    std::mutex shared_mutex{};                           ///< 保护共享状态互斥锁
    port::PerceptionResult perception{};                 ///< 最新感知结果
    port::ImuSample imu{};                               ///< 最新 IMU 采样
    port::EncoderDelta encoder{};                        ///< 最新编码器差值
    CameraFrameHandle latest_camera_frame{};             ///< 最新相机帧句柄
    CameraCaptureHistory recent_camera_captures{};       ///< 近期相机捕获历史
    std::array<OwnedCameraFrameSlot, 3> camera_frame_slots{};  ///< 相机帧槽数组
    std::size_t next_camera_frame_slot = 0;              ///< 下一帧槽轮转索引
    port::CameraFrameStoreHealth camera_frame_store_health{};  ///< 相机帧存储统计
    port::ActuatorCommand last_command{};                 ///< 上一周期执行器命令
    ControlCycleObservation control_observation{};        ///< 控制周期观察结果
    ControlDebugSnapshot control_debug_snapshot{};        ///< 控制调试快照
    port::LowVoltageSample low_voltage_last_sample{};     ///< 上次低电压采样结果
    MotionHistory motion_history{};                       ///< control tick 运动历史

    // Lifecycle flags.
    bool startup_complete = false;                        ///< 启动是否完成
    bool degraded_startup = false;                        ///< 是否降级启动
    bool timer_started = false;                           ///< 控制定时器是否已启动
    bool actuators_armed = false;                         ///< 执行器是否已就绪（原子保护）
    std::atomic<bool> stop_requested{false};              ///< 停止请求标志
    std::atomic<bool> exit_requested{false};              ///< 退出请求标志
    bool automation_start_fired = false;                  ///< 自动化启动是否已触发
    MotionIntent motion_intent{};                         ///< 运动意图
    MotionSupervisorState motion_state{};                 ///< 运动监督器状态
    RuntimeTuningState tuning_state{};                    ///< 运行时调参状态
    std::atomic<bool> low_voltage_emergency{false};       ///< 低电压紧急标志
    std::atomic<uint64_t> perception_memory_reset_generation{0};  ///< 感知记忆复位代数

    std::atomic<uint64_t> control_cycle_count{0};         ///< 控制周期计数
    std::atomic<uint64_t> perception_publish_count{0};    ///< 感知结果发布计数
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_RUNTIME_STATE_HPP
