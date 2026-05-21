/**
 * @file perf_counter.cpp
 * @brief 性能计数器的平台相关实现
 *
 * 支持两种计时模式：
 * 1. 硬件周期计数器（LoongArch rdcntv 指令），需要运行时标定频率
 * 2. 稳态时钟（std::chrono::steady_clock）回退方案
 *
 * 采用无锁原子操作实现线程安全的窗口统计。
 */

#include "port/perf_counter.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

namespace ls2k::port {
namespace {

/**
 * @struct PerfStageCounters
 * @brief 单个性能阶段的原子计数器集合
 *
 * 使用原子操作支持无锁并发访问。
 * 所有计数器按时间窗口统计，定期由 EmitPerfWindowDiagnostics 读取并重置。
 */
struct PerfStageCounters {
    std::atomic<std::uint64_t> window_count{0};       ///< 窗口内发生次数
    std::atomic<std::uint64_t> window_total_us{0};    ///< 窗口内总耗时（微秒）
    std::atomic<std::uint64_t> window_max_us{0};      ///< 窗口内最大单次耗时（微秒）
    std::atomic<std::uint64_t> last_us{0};            ///< 最近一次耗时（微秒）
};

constexpr std::size_t kPerfStageCount = static_cast<std::size_t>(PerfStage::kCount);  ///< 性能阶段总数

[[maybe_unused]] std::array<PerfStageCounters, kPerfStageCount> g_counters{};  ///< 全局计数器数组
std::atomic<bool> g_initialized{false};           ///< 是否已初始化
std::atomic<bool> g_enabled{false};               ///< 是否已启用
std::atomic<bool> g_uses_arch_counter{false};     ///< 是否使用硬件周期计数器
std::atomic<std::uint64_t> g_ticks_per_us_x1000{1000000};  ///< 每微秒计数周期数（x1000）

/**
 * @brief 性能阶段枚举转字符串
 * @param stage 性能阶段枚举
 * @return 对应的字符串标识
 */
[[maybe_unused]] const char* StageName(PerfStage stage) {
    switch (stage) {
        case PerfStage::kMainLoop:
            return "main.loop";
        case PerfStage::kPerceptionFrame:
            return "perception.frame";
        case PerfStage::kCameraCapture:
            return "camera.capture";
        case PerfStage::kCameraFrameMaterialize:
            return "camera.frame_materialize";
        case PerfStage::kCameraV4l2Poll:
            return "camera.v4l2_poll";
        case PerfStage::kCameraV4l2Dequeue:
            return "camera.v4l2_dequeue";
        case PerfStage::kCameraYuyvToGray:
            return "camera.yuyv_to_gray";
        case PerfStage::kCameraStoreSubmit:
            return "camera.store_submit";
        case PerfStage::kCameraFrameAge:
            return "camera.frame_age";
        case PerfStage::kPerceptionPublish:
            return "perception.publish";
        case PerfStage::kPerceptionOtsu:
            return "perception.otsu";
        case PerfStage::kPerceptionBev:
            return "perception.bev";
        case PerfStage::kBevSimple:
            return "bev.simple";
        case PerfStage::kBevSimpleLut:
            return "bev.simple.lut";
        case PerfStage::kBevSimpleScanRows:
            return "bev.simple.scan_rows";
        case PerfStage::kBevSimpleBuildReference:
            return "bev.simple.build_reference";
        case PerfStage::kPerceptionElementRaster:
            return "perception.element_raster";
        case PerfStage::kPerceptionElementRasterLut:
            return "perception.element_raster.lut";
        case PerfStage::kPerceptionElementRasterStorage:
            return "perception.element_raster.storage";
        case PerfStage::kPerceptionElementRasterClassTable:
            return "perception.element_raster.class_table";
        case PerfStage::kPerceptionElementRasterCells:
            return "perception.element_raster.cells";
        case PerfStage::kVisualElementPipeline:
            return "visual.element_pipeline";
        case PerfStage::kVisualLineCandidate:
            return "visual.line_candidate";
        case PerfStage::kVisualReferenceSelect:
            return "visual.reference_select";
        case PerfStage::kReferenceUsability:
            return "reference.usability";
        case PerfStage::kReferenceHold:
            return "reference.hold";
        case PerfStage::kReferenceTimeAlignment:
            return "reference.time_alignment";
        case PerfStage::kReferenceLateralError:
            return "reference.lateral_error";
        case PerfStage::kReferenceControlReadiness:
            return "reference.control_readiness";
        case PerfStage::kPerceptionResultBuild:
            return "perception.result_build";
        case PerfStage::kControlTick:
            return "control.tick";
        case PerfStage::kControlImuRead:
            return "control.imu_read";
        case PerfStage::kControlEncoderRead:
            return "control.encoder_read";
        case PerfStage::kMotionHistoryRecord:
            return "motion.history_record";
        case PerfStage::kControlDecision:
            return "control.decision";
        case PerfStage::kControlApply:
            return "control.apply";
        case PerfStage::kAssistantTick:
            return "assistant.tick";
        case PerfStage::kSteeringMediaTick:
            return "steering_media.tick";
        case PerfStage::kMediaEncode:
            return "media.encode";
        case PerfStage::kMediaSend:
            return "media.send";
        case PerfStage::kCount:
            break;
    }
    return "unknown";
}

/**
 * @brief 读取稳态时钟的纳秒值
 * @return 当前单调时钟的纳秒数
 */
std::uint64_t ReadSteadyClockNs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

/**
 * @brief 读取LoongArch硬件周期计数器的值
 *
 * 使用 rdcntvh.w / rdcntvl.w 指令读取64位周期计数器。
 * 通过循环确保高低32位读取的一致性（防止高位溢出）。
 * 在非LoongArch平台上回退到稳态时钟。
 *
 * @return 当前周期计数值
 */
[[maybe_unused]] std::uint64_t ReadArchCounterTicks() {
#if LS2K_PERF_ENABLED && LS2K_PERF_USE_CYCLE_COUNTER && defined(__loongarch64)
    std::uint32_t hi_before = 0;
    std::uint32_t hi_after = 0;
    std::uint32_t lo = 0;
    do {
        asm volatile("rdcntvh.w %0" : "=r"(hi_before));
        asm volatile("rdcntvl.w %0" : "=r"(lo));
        asm volatile("rdcntvh.w %0" : "=r"(hi_after));
    } while (hi_before != hi_after);
    return (static_cast<std::uint64_t>(hi_after) << 32U) | static_cast<std::uint64_t>(lo);
#else
    return ReadSteadyClockNs();
#endif
}

/**
 * @brief 标定硬件周期计数器的频率
 *
 * 通过测量2ms睡眠前后的计数器差值计算每微秒的周期数。
 * 标定成功则设置 g_uses_arch_counter = true。
 *
 * @return 标定是否成功
 */
[[maybe_unused]] bool CalibrateArchCounter() {
#if LS2K_PERF_ENABLED && LS2K_PERF_USE_CYCLE_COUNTER && defined(__loongarch64)
    const std::uint64_t steady_begin = ReadSteadyClockNs();
    const std::uint64_t ticks_begin = ReadArchCounterTicks();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const std::uint64_t ticks_end = ReadArchCounterTicks();
    const std::uint64_t steady_end = ReadSteadyClockNs();
    if (ticks_end <= ticks_begin || steady_end <= steady_begin) {
        return false;
    }
    const std::uint64_t elapsed_us = (steady_end - steady_begin) / 1000U;
    if (elapsed_us == 0U) {
        return false;
    }
    const std::uint64_t ticks_per_us_x1000 =
        ((ticks_end - ticks_begin) * 1000U) / elapsed_us;
    if (ticks_per_us_x1000 == 0U) {
        return false;
    }
    g_ticks_per_us_x1000.store(ticks_per_us_x1000);
    g_uses_arch_counter.store(true);
    return true;
#else
    return false;
#endif
}

}  // namespace

bool InitializePerfCounter() {
#if LS2K_PERF_ENABLED
    if (g_initialized.exchange(true)) {
        return g_enabled.load();
    }
    if (!CalibrateArchCounter()) {
        // 标定失败时使用默认的1 tick = 1 ns的保守配置
        g_ticks_per_us_x1000.store(1000000);
        g_uses_arch_counter.store(false);
    }
    g_enabled.store(true);
    return true;
#else
    // 性能计数编译时禁用
    g_initialized.store(true);
    g_enabled.store(false);
    g_uses_arch_counter.store(false);
    return false;
#endif
}

std::uint64_t ReadPerfTicks() {
#if LS2K_PERF_ENABLED
    return g_uses_arch_counter.load() ? ReadArchCounterTicks() : ReadSteadyClockNs();
#else
    return 0U;
#endif
}

std::uint64_t PerfTicksToUs(std::uint64_t ticks) {
#if LS2K_PERF_ENABLED
    const std::uint64_t scale = g_ticks_per_us_x1000.load();
    return scale == 0U ? 0U : (ticks * 1000U) / scale;
#else
    (void)ticks;
    return 0U;
#endif
}

bool PerfCounterUsesArchCounter() {
    return g_uses_arch_counter.load();
}

std::uint64_t PerfTicksPerUsX1000() {
    return g_ticks_per_us_x1000.load();
}

bool PerfCounterEnabled() {
    return g_enabled.load();
}

void RecordPerfStage(PerfStage stage, std::uint64_t elapsed_ticks) {
#if LS2K_PERF_ENABLED
    const std::size_t index = static_cast<std::size_t>(stage);
    if (index >= kPerfStageCount) {
        return;
    }
    const std::uint64_t elapsed_us = PerfTicksToUs(elapsed_ticks);
    PerfStageCounters& counters = g_counters[index];
    counters.window_count.fetch_add(1U, std::memory_order_relaxed);
    counters.window_total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    counters.last_us.store(elapsed_us, std::memory_order_relaxed);
    // 原子比较-交换更新最大值（无锁）
    std::uint64_t current_max = counters.window_max_us.load(std::memory_order_relaxed);
    while (elapsed_us > current_max &&
           !counters.window_max_us.compare_exchange_weak(current_max,
                                                         elapsed_us,
                                                         std::memory_order_relaxed,
                                                         std::memory_order_relaxed)) {
    }
#else
    (void)stage;
    (void)elapsed_ticks;
#endif
}

void EmitPerfWindowDiagnostics(DiagnosticSink& diagnostics, std::uint64_t now_ms) {
#if LS2K_PERF_ENABLED
    if (!PerfCounterEnabled()) {
        return;
    }
    for (std::size_t index = 0; index < kPerfStageCount; ++index) {
        PerfStageCounters& counters = g_counters[index];
        // 原子读取并重置窗口计数器
        const std::uint64_t count = counters.window_count.exchange(0U, std::memory_order_relaxed);
        const std::uint64_t total_us = counters.window_total_us.exchange(0U, std::memory_order_relaxed);
        const std::uint64_t max_us = counters.window_max_us.exchange(0U, std::memory_order_relaxed);
        const std::uint64_t last_us = counters.last_us.load(std::memory_order_relaxed);
        if (count == 0U) {
            continue;
        }
        std::ostringstream message;
        message << "stage=" << StageName(static_cast<PerfStage>(index))
                << " count=" << count
                << " avg_us=" << (total_us / count)
                << " max_us=" << max_us
                << " last_us=" << last_us
                << " arch_counter=" << (PerfCounterUsesArchCounter() ? "true" : "false")
                << " ticks_per_us_x1000=" << PerfTicksPerUsX1000();
        diagnostics.Emit({DiagnosticLevel::kInfo, "perf.window", message.str(), now_ms});
    }
#else
    (void)diagnostics;
    (void)now_ms;
#endif
}

}  // namespace ls2k::port
