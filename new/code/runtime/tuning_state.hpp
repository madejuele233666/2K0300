#ifndef LS2K_RUNTIME_TUNING_STATE_HPP
#define LS2K_RUNTIME_TUNING_STATE_HPP

#include <cstdint>
#include <string>

namespace ls2k::runtime {

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

RuntimeTuningSnapshot SnapshotRuntimeTuningState(const RuntimeTuningState& state);
bool RuntimeTuningSnapshotActive(const RuntimeTuningSnapshot& snapshot);
bool RuntimeTuningOverrideActiveAt(const RuntimeTuningSnapshot& snapshot, std::uint64_t now_ms);
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

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_TUNING_STATE_HPP
