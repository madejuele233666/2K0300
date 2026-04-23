#include "runtime/tuning_state.hpp"

namespace ls2k::runtime {
namespace {

void ResetOverride(RuntimeTuningState& state) {
    state.target_speed_override_enabled = false;
    state.target_speed_override_value = 0.0;
    state.target_speed_override_expire_at_ms = 0;
}

}  // namespace

RuntimeTuningSnapshot SnapshotRuntimeTuningState(const RuntimeTuningState& state) {
    RuntimeTuningSnapshot snapshot{};
    snapshot.tuning_mode_enabled = state.tuning_mode_enabled;
    snapshot.turn_suppressed = state.turn_suppressed;
    snapshot.target_speed_override_enabled = state.target_speed_override_enabled;
    snapshot.target_speed_override_value = state.target_speed_override_value;
    snapshot.target_speed_override_expire_at_ms = state.target_speed_override_expire_at_ms;
    snapshot.has_last_seq = state.has_last_seq;
    snapshot.last_seq = state.last_seq;
    return snapshot;
}

bool RuntimeTuningSnapshotActive(const RuntimeTuningSnapshot& snapshot) {
    return snapshot.tuning_mode_enabled || snapshot.turn_suppressed ||
           snapshot.target_speed_override_enabled;
}

bool RuntimeTuningOverrideActiveAt(const RuntimeTuningSnapshot& snapshot, std::uint64_t now_ms) {
    return snapshot.target_speed_override_enabled &&
           snapshot.target_speed_override_expire_at_ms > now_ms;
}

double ResolveRuntimeSpeedTarget(const RuntimeTuningSnapshot& snapshot,
                                 double default_speed_target,
                                 std::uint64_t now_ms) {
    if (RuntimeTuningOverrideActiveAt(snapshot, now_ms)) {
        return snapshot.target_speed_override_value;
    }
    // Remote tuning owns the drive target explicitly. When the override is not
    // active, keep the lifecycle armed but hold the wheel target at zero so the
    // host can separate ACK transport from the first motion-driving command.
    return snapshot.tuning_mode_enabled ? 0.0 : default_speed_target;
}

void NoteRuntimeTuningSeq(RuntimeTuningState& state, std::uint64_t seq) {
    state.has_last_seq = true;
    state.last_seq = seq;
}

void EnableRuntimeTuningMode(RuntimeTuningState& state, std::uint64_t seq) {
    NoteRuntimeTuningSeq(state, seq);
    state.tuning_mode_enabled = true;
    // Dynamic tuning relies on turn suppression immediately after enable to avoid
    // a second fragile round-trip before the first motion command.
    state.turn_suppressed = true;
}

RuntimeTuningEvent DisableRuntimeTuningMode(RuntimeTuningState& state, std::uint64_t seq) {
    NoteRuntimeTuningSeq(state, seq);
    return ClearRuntimeTuningSnapshot(state, "tuning disabled by command", true);
}

void SetRuntimeTurnSuppressed(RuntimeTuningState& state, bool suppressed, std::uint64_t seq) {
    NoteRuntimeTuningSeq(state, seq);
    state.turn_suppressed = suppressed;
}

void SetRuntimeTargetSpeedOverride(RuntimeTuningState& state,
                                   double value,
                                   std::uint64_t expire_at_ms,
                                   std::uint64_t seq) {
    NoteRuntimeTuningSeq(state, seq);
    state.target_speed_override_enabled = true;
    state.target_speed_override_value = value;
    state.target_speed_override_expire_at_ms = expire_at_ms;
}

RuntimeTuningEvent ClearExpiredRuntimeTuningOverride(RuntimeTuningState& state,
                                                     std::uint64_t now_ms) {
    if (!state.target_speed_override_enabled || state.target_speed_override_expire_at_ms > now_ms) {
        return {};
    }

    ResetOverride(state);
    RuntimeTuningEvent event{};
    event.type = RuntimeTuningEventType::kOverrideCleared;
    event.reason = "override TTL expired";
    return event;
}

RuntimeTuningEvent ClearRuntimeTuningSnapshot(RuntimeTuningState& state,
                                              const std::string& reason,
                                              bool force_event) {
    const bool active = state.tuning_mode_enabled || state.turn_suppressed ||
                        state.target_speed_override_enabled || state.has_last_seq;
    state.tuning_mode_enabled = false;
    state.turn_suppressed = false;
    ResetOverride(state);
    state.has_last_seq = false;
    state.last_seq = 0;
    if (!active && !force_event) {
        return {};
    }

    RuntimeTuningEvent event{};
    event.type = RuntimeTuningEventType::kSnapshotCleared;
    event.reason = reason;
    return event;
}

}  // namespace ls2k::runtime
