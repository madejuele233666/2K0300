#ifndef LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP
#define LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP

#include <cstdint>
#include <vector>

#include "transport/steering_media_link.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {

/// 转向媒体服务 —— 将调试快照与相机帧打包为媒体帧并通过 SteeringMediaLink 发送。
/// 用于远程监控和离线分析。
class SteeringMediaService {
public:
    SteeringMediaService() = default;
    /// 构造并绑定媒体链路
    explicit SteeringMediaService(transport::SteeringMediaLink link);

    /// 启动媒体服务：初始化参数、配置链路
    void Start(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);
    /// 周期性 Tick：检查连接 → 发布配置 → 查找新帧 → 发布图像帧
    void Tick(RuntimeState& state, port::DiagnosticSink& diagnostics);

private:
    /// 窗口统计结构 —— 用于每秒汇总媒体服务运行情况
    struct WindowStats {
        std::uint64_t ticks = 0;                  ///< Tick 计数
        std::uint64_t not_ready = 0;              ///< 链路未就绪次数
        std::uint64_t pending_flush_sent = 0;      ///< 待发送刷新成功次数
        std::uint64_t config_attempts = 0;         ///< 配置发布尝试次数
        std::uint64_t config_sent = 0;             ///< 配置发布成功次数
        std::uint64_t config_wait = 0;             ///< 等待配置次数
        std::uint64_t skip_no_capture = 0;         ///< 无匹配捕获帧跳过次数
        std::uint64_t skip_zero_frame = 0;         ///< 帧 ID 为零跳过次数
        std::uint64_t skip_disarmed = 0;           ///< 未就绪状态跳过次数
        std::uint64_t skip_duplicate = 0;          ///< 重复帧跳过次数
        std::uint64_t skip_interval = 0;           ///< 发布间隔跳过次数
        std::uint64_t image_sent = 0;              ///< 图像发送成功次数
        std::uint64_t image_queued = 0;            ///< 图像入队次数
        std::uint64_t image_unavailable = 0;       ///< 图像不可用次数
    };

    /// 构建参数配置快照（用于首次连接时下发参数）
    transport::SteeringMediaConfigSnapshot BuildConfigSnapshot(std::uint64_t now_ms) const;
    /// 将 SteeringDebugSnapshot 转换为媒体协议视图
    transport::SteeringMediaSnapshotView BuildSnapshotView(
        const observability::SteeringDebugSnapshot& snapshot) const;
    /// 填充图像帧数据（含降采样处理）
    void FillImageFrame(const port::LegacyCameraFrame& capture_frame,
                        transport::SteeringMediaImageFrame& frame);
    /// 重置窗口统计
    void ResetWindowStats();
    /// 按每秒间隔发射窗口统计摘要
    void MaybeEmitWindowSummary(std::uint64_t now_ms, port::DiagnosticSink& diagnostics);

    bool configured_ = false;                       ///< 是否已完成配置
    bool enabled_ = false;                          ///< 媒体服务是否启用
    bool config_sent_ = false;                      ///< 配置快照是否已发送
    bool publish_disarmed_ = false;                  ///< 是否在 DISARMED 状态也发布
    bool publish_latest_frame_ = false;              ///< 诊断模式：是否发布最新相机帧而非快照匹配帧
    int downsample_ = 1;                             ///< 图像降采样因子
    int gray_bits_ = 8;                              ///< 传输灰度位深
    int publish_interval_ms_ = 80;                   ///< 图像发布间隔（ms）
    std::uint64_t last_image_publish_ms_ = 0;        ///< 上次图像发布时间戳
    std::uint64_t last_image_frame_id_ = 0;          ///< 上次发布的帧 ID
    std::uint64_t last_summary_ms_ = 0;              ///< 上次窗口摘要时间戳
    WindowStats window_stats_{};                     ///< 窗口统计
    port::RuntimeParameters params_{};               ///< 运行时参数副本
    std::vector<std::uint8_t> downsample_buffer_{};  ///< 降采样缓冲
    std::vector<std::uint8_t> gray_pack_buffer_{};    ///< 低位深灰度打包缓冲
    transport::SteeringMediaLink link_{};             ///< 媒体链路层实例
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_MEDIA_SERVICE_HPP
