#ifndef LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
#define LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP

#include <cstdint>

#include "runtime/camera_frame_store.hpp"
#include "runtime/runtime_state.hpp"
#include "runtime/steering_frame_perception_pipeline.hpp"

namespace ls2k::runtime {

/// 感知前端 —— 运行时感知管线调度器。
/// 负责相机帧捕获、故障注入、感知管线执行、结果缓存和生命周期管理。
class PerceptionFrontend {
public:
    /// 构造感知前端
    /// @param frame_store 相机帧存储
    /// @param state       运行时状态
    /// @param diagnostics 诊断输出接口
    PerceptionFrontend(CameraFrameStore& frame_store,
                       RuntimeState& state,
                       port::DiagnosticSink& diagnostics);

    /// 配置感知管线
    /// @param params  运行时参数
    /// @return        配置是否成功
    bool Configure(const port::RuntimeParameters& params);
    /// 处理一帧图像：非阻塞取最新帧 → 故障注入 → 感知管线 → 结果缓存
    /// @param params  运行时参数
    void ProcessOneFrame(const port::RuntimeParameters& params);

private:
    /// 消费感知内存复位请求（清空感知管线记忆）
    void ConsumeMemoryResetRequest();

    CameraFrameStore& frame_store_;                     ///< 相机帧存储
    RuntimeState& state_;                               ///< 运行时状态引用
    port::DiagnosticSink& diagnostics_;                  ///< 诊断输出引用
    SteeringFramePerceptionPipeline frame_pipeline_{};   ///< 转向帧感知管线
    uint64_t processed_frames_ = 0;                      ///< 已处理的帧计数
    uint64_t last_processed_frame_id_ = 0;                ///< 已处理的最新相机帧ID
    uint64_t consumed_perception_memory_reset_generation_ = 0;  ///< 已消费的复位代数
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_PERCEPTION_FRONTEND_HPP
