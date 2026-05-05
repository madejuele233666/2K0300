#ifndef LS2K_PORT_PERF_COUNTER_HPP
#define LS2K_PORT_PERF_COUNTER_HPP

#include <cstddef>
#include <cstdint>

#include "port/diagnostics.hpp"

#ifndef LS2K_PERF_ENABLED
#define LS2K_PERF_ENABLED 0
#endif

#ifndef LS2K_PERF_USE_CYCLE_COUNTER
#define LS2K_PERF_USE_CYCLE_COUNTER 1
#endif

namespace ls2k::port {

enum class PerfStage : std::size_t {
    kMainLoop = 0,
    kPerceptionFrame,
    kPerceptionOtsu,
    kPerceptionBev,
    kControlTick,
    kControlImuRead,
    kControlEncoderRead,
    kControlDecision,
    kControlApply,
    kAssistantTick,
    kSteeringMediaTick,
    kMediaEncode,
    kMediaSend,
    kCount
};

bool InitializePerfCounter();
std::uint64_t ReadPerfTicks();
std::uint64_t PerfTicksToUs(std::uint64_t ticks);
bool PerfCounterUsesArchCounter();
std::uint64_t PerfTicksPerUsX1000();
bool PerfCounterEnabled();
void RecordPerfStage(PerfStage stage, std::uint64_t elapsed_ticks);
void EmitPerfWindowDiagnostics(DiagnosticSink& diagnostics, std::uint64_t now_ms);

class PerfScope final {
public:
    explicit PerfScope(PerfStage stage)
        : stage_(stage),
          start_ticks_(PerfCounterEnabled() ? ReadPerfTicks() : 0U) {}

    ~PerfScope() {
        if (start_ticks_ != 0U) {
            RecordPerfStage(stage_, ReadPerfTicks() - start_ticks_);
        }
    }

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    PerfStage stage_;
    std::uint64_t start_ticks_;
};

}  // namespace ls2k::port

#define LS2K_PERF_CONCAT_INNER(a, b) a##b
#define LS2K_PERF_CONCAT(a, b) LS2K_PERF_CONCAT_INNER(a, b)

#if LS2K_PERF_ENABLED
#define LS2K_PERF_SCOPE(stage) \
    ::ls2k::port::PerfScope LS2K_PERF_CONCAT(ls2k_perf_scope_, __LINE__)(stage)
#else
#define LS2K_PERF_SCOPE(stage) ((void)0)
#endif

#endif  // LS2K_PORT_PERF_COUNTER_HPP
