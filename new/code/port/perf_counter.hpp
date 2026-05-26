/**
 * @file perf_counter.hpp
 * @brief 性能计数器基础设施
 *
 * 提供分阶段的性能计数能力，支持硬件周期计数器（LoongArch）和稳态时钟两种计时方式。
 * 每个性能阶段独立统计计数、平均耗时、最大耗时和最近耗时。
 * 通过 LS2K_PERF_SCOPE 宏在代码中插入RAII风格的作用域计时。
 */

#ifndef LS2K_PORT_PERF_COUNTER_HPP
#define LS2K_PORT_PERF_COUNTER_HPP

#include <cstddef>
#include <cstdint>

#include "port/diagnostics.hpp"

#ifndef LS2K_PERF_ENABLED
#define LS2K_PERF_ENABLED 0  ///< 性能计数器全局开关（编译期决定）
#endif

#ifndef LS2K_PERF_USE_CYCLE_COUNTER
#define LS2K_PERF_USE_CYCLE_COUNTER 1  ///< 是否使用硬件周期计数器（仅在LoongArch下生效）
#endif

namespace ls2k::port {

/**
 * @enum PerfStage
 * @brief 性能计数阶段枚举
 *
 * 定义整个主循环中各阶段的性能计数点，按执行顺序排列。
 * kCount 作为哨兵值表示阶段总数，不参与计时。
 */
enum class PerfStage : std::size_t {
    kMainLoop = 0,          ///< 主循环总体计时
    kPerceptionFrame,       ///< 感知帧处理
    kCameraCapture,         ///< 相机采集
    kCameraFrameMaterialize, ///< 相机帧物化到共享槽
    kCameraV4l2Poll,        ///< V4L2 poll 等待
    kCameraV4l2Dequeue,     ///< V4L2 dequeue/drain
    kCameraYuyvToGray,      ///< YUYV 转灰度
    kCameraStoreSubmit,     ///< 相机帧提交到 Frame Store
    kCameraFrameAge,        ///< 消费相机帧年龄统计
    kPerceptionPublish,     ///< 感知结果发布到共享状态
    kPerceptionOtsu,        ///< Otsu二值化
    kPerceptionBev,         ///< BEV投影
    kBevSimple,             ///< 基础稀疏 BEV 寻线事实
    kBevSimpleLut,          ///< 基础稀疏 BEV LUT 准备
    kBevSimpleScanRows,     ///< 基础稀疏 BEV 行扫描
    kBevSimpleBuildReference, ///< 基础稀疏 BEV reference 构建
    kPerceptionElementRaster,  ///< 元素栅格化
    kPerceptionElementRasterLut, ///< 元素栅格 LUT 准备
    kPerceptionElementRasterStorage, ///< 元素栅格存储准备
    kPerceptionElementRasterClassTable, ///< 元素栅格分类表构建
    kPerceptionElementRasterCells, ///< 元素栅格逐 cell 采样分类
    kCirclePhase1Rows,      ///< circle Phase1 sparse row evidence
    kCirclePhase2RoiScan,   ///< circle Phase2 ROI scan
    kCirclePhase2ReferenceBuild, ///< circle Phase2 reference candidate build
    kVisualElementPipeline, ///< 视觉元素 pipeline
    kVisualLineCandidate,   ///< 基础 line candidate 包装
    kVisualReferenceSelect, ///< 视觉 reference arbitration
    kReferenceUsability,    ///< reference 可用性评估
    kReferenceHold,         ///< hold-last reference 评估
    kReferenceTimeAlignment, ///< reference 时间坐标对齐
    kReferenceLateralError, ///< reference 横向误差计算
    kReferenceControlReadiness, ///< reference 控制就绪评估
    kPerceptionResultBuild, ///< 感知结果结构组装
    kControlTick,           ///< 控制节拍
    kControlImuRead,        ///< IMU读取
    kControlEncoderRead,    ///< 编码器读取
    kMotionHistoryRecord,   ///< 运动历史记录
    kControlDecision,       ///< 控制决策
    kControlApply,          ///< 控制执行
    kAssistantTick,         ///< 助理连接心跳
    kSteeringMediaTick,     ///< 转向媒体服务心跳
    kMediaEncode,           ///< 媒体编码
    kMediaSend,             ///< 媒体发送
    kCount                  ///< 阶段总数（哨兵）
};

/** @brief 初始化性能计数器 */
bool InitializePerfCounter();

/** @brief 读取当前性能计时器的计数值 */
std::uint64_t ReadPerfTicks();

/** @brief 将计数值转换为微秒 */
std::uint64_t PerfTicksToUs(std::uint64_t ticks);

/** @brief 检查是否使用硬件周期计数器 */
bool PerfCounterUsesArchCounter();

/** @brief 获取每微秒的计数值（x1000） */
std::uint64_t PerfTicksPerUsX1000();

/** @brief 检查性能计数器是否已启用 */
bool PerfCounterEnabled();

/**
 * @brief 记录一个阶段的耗时
 * @param stage 阶段枚举
 * @param elapsed_ticks 经过的计数值
 */
void RecordPerfStage(PerfStage stage, std::uint64_t elapsed_ticks);

/**
 * @brief 输出当前时间窗口内所有阶段的性能诊断信息
 * @param diagnostics 诊断接收器
 * @param now_ms 当前时间戳
 */
void EmitPerfWindowDiagnostics(DiagnosticSink& diagnostics, std::uint64_t now_ms);

/**
 * @class PerfScope
 * @brief RAII风格的作用域计时器
 *
 * 构造时记录起始计数值，析构时自动计算并记录耗时。
 * 拷贝和赋值被禁用。
 */
class PerfScope final {
public:
    /** @brief 构造时记录起始计数值 */
    explicit PerfScope(PerfStage stage)
        : stage_(stage),
          start_ticks_(PerfCounterEnabled() ? ReadPerfTicks() : 0U) {}

    /** @brief 析构时自动计算并记录耗时 */
    ~PerfScope() {
        if (start_ticks_ != 0U) {
            RecordPerfStage(stage_, ReadPerfTicks() - start_ticks_);
        }
    }

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    PerfStage stage_;               ///< 当前计时阶段
    std::uint64_t start_ticks_;     ///< 起始计数值
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
