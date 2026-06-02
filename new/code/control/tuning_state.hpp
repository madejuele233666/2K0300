#ifndef LS2K_CONTROL_TUNING_STATE_HPP
#define LS2K_CONTROL_TUNING_STATE_HPP

#include <cstdint>
#include <string>

namespace ls2k::control {

enum class RuntimeTuningEventType {
    kNone,
    kOverrideCleared,
    kSnapshotCleared,
};

struct RuntimeTuningState {
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    std::uint64_t target_speed_override_expire_at_ms = 0;
    bool has_last_seq = false;
    std::uint64_t last_seq = 0;
};

// RuntimeTuningState 存放在 RuntimeState 中，由 RuntimeState::shared_mutex 保护。
// RuntimeTuningSnapshot 是在持锁期间复制出来的值对象，复制完成后可在锁外读取。
// 这里不是原子结构，也不是无锁快照；线程安全契约由调用方持锁保证。
struct RuntimeTuningSnapshot {
    bool tuning_mode_enabled = false;
    bool turn_suppressed = false;
    bool target_speed_override_enabled = false;
    double target_speed_override_value = 0.0;
    std::uint64_t target_speed_override_expire_at_ms = 0;
    bool has_last_seq = false;
    std::uint64_t last_seq = 0;
};

struct RuntimeTuningEvent {
    RuntimeTuningEventType type = RuntimeTuningEventType::kNone;
    std::string reason;
};

// 调用方必须持有 RuntimeState::shared_mutex，再复制 RuntimeTuningState。
// 这样可以避免控制线程和 assistant 线程同时读写调参状态时看到部分更新。
RuntimeTuningSnapshot SnapshotRuntimeTuningState(const RuntimeTuningState& state);
bool RuntimeTuningSnapshotActive(const RuntimeTuningSnapshot& snapshot);
bool RuntimeTuningOverrideActiveAt(const RuntimeTuningSnapshot& snapshot, std::uint64_t now_ms);
double ResolveRuntimeSpeedTarget(const RuntimeTuningSnapshot& snapshot,
                                 double default_speed_target,
                                 std::uint64_t now_ms);
void NoteRuntimeTuningSeq(RuntimeTuningState& state, std::uint64_t seq);
void EnableRuntimeTuningMode(RuntimeTuningState& state, std::uint64_t seq);
RuntimeTuningEvent DisableRuntimeTuningMode(RuntimeTuningState& state, std::uint64_t seq);
void SetRuntimeTurnSuppressed(RuntimeTuningState& state, bool suppressed, std::uint64_t seq);
void SetRuntimeTargetSpeedOverride(RuntimeTuningState& state,
                                   double value,
                                   std::uint64_t expire_at_ms,
                                   std::uint64_t seq);
RuntimeTuningEvent ClearExpiredRuntimeTuningOverride(RuntimeTuningState& state,
                                                     std::uint64_t now_ms);
RuntimeTuningEvent ClearRuntimeTuningSnapshot(RuntimeTuningState& state,
                                              const std::string& reason,
                                              bool force_event);

}  // namespace ls2k::control

#endif  // LS2K_CONTROL_TUNING_STATE_HPP
