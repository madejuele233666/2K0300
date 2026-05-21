#ifndef LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP
#define LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/diagnostics.hpp"
#include "port/perception_result.hpp"
#include "port/platform_adapter.hpp"
#include "port/steering_state_types.hpp"

namespace ls2k::runtime {

/// 转向帧感知管线 —— 单帧图像感知处理管线。
/// 包含 BEV 投影、Otsu 二值化、视觉元素检测、视觉参考选择与连续性跟踪。
class SteeringFramePerceptionPipeline {
public:
    /// 配置感知管线：初始化 BEV 投影器、重置采样 LUT
    /// @param params       运行时参数
    /// @param diagnostics  诊断输出接口
    /// @return             配置是否成功
    bool Configure(const port::RuntimeParameters& params,
                   port::DiagnosticSink& diagnostics);
    /// 重置感知记忆（清空参考连续性跟踪状态）
    void ResetMemory();
    /// 处理一帧图像：Otsu 阈值 → BEV 感知 → 元素检测 → 参考选择 → 横向误差计算
    /// @param capture   相机捕获数据
    /// @param params    运行时参数
    /// @return          处理后的感知结果
    port::PerceptionResult ProcessFrame(const port::CameraCapture& capture,
                                        const port::RuntimeParameters& params);

private:
    legacy::BEVProjector projector_{};                          ///< BEV 投影器
    legacy::BEVSampleProjectionLut sample_lut_{};               ///< 采样投影查找表
    port::SteeringPerceptionMemory perception_memory_{};        ///< 感知记忆（参考连续性）
    bool projector_configured_ = false;                         ///< 投影器是否已配置
};

}  // namespace ls2k::runtime

#endif  // LS2K_RUNTIME_STEERING_FRAME_PERCEPTION_PIPELINE_HPP
