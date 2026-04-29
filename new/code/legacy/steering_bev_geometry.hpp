#ifndef LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
#define LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP

// BEV 几何工具和统一拓扑管线。
// 提供路径法线偏移、前向重采样、平滑等几何函数。
// RunBEVTopologyPipeline() 在此串联稀疏采样、元素证据提取、走廊间隔/图和轨迹估计的完整管线。

#include <array>
#include <cstddef>

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_element_evidence.hpp"
#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// BEV 拓扑管线的完整输出结果，包含所有中间和最终产物
struct BEVTopologyPipelineResult {
    BEVSparseSampleGrid sparse_samples{};        //!< 稀疏采样网格
    CorridorIntervalSet corridor_intervals{};    //!< 走廊间隔集合
    CorridorGraph corridor_graph{};              //!< 走廊图
    port::BEVElementEvidence element_evidence{}; //!< 元素证据（cross band / circle corner）
    port::BEVTrackEstimate track_estimate{};     //!< 最终轨迹估计
};

// 沿路径某点的法线方向偏移指定距离
port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m);

// 沿法线偏移，允许指定切线拟合窗口大小
port::BEVPathSample OffsetPathSampleAlongNormal(
    const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    std::size_t sample_index,
    float offset_m,
    float tangent_fit_window_m);

// 整体沿法线路径偏移
void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m);

// 整体偏移，允许指定切线拟合窗口
void OffsetPathAlongNormals(std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                            float offset_m,
                            float tangent_fit_window_m);

// 将任意前向分布的路径采样点归一化到标准前向采样网格上
// 返回有效点数量
int NormalizePathToForwardSamples(
    std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
    const std::array<float, port::kBevTrackSampleCount>& forward_samples_m,
    float max_extrapolate_m,
    float max_lateral_smooth_adjust_m);

// BEV 拓扑管线的总入口：依次执行稀疏采样、元素证据提取、走廊间隔、
// 走廊图构建和轨迹估计，返回完整管线结果
BEVTopologyPipelineResult RunBEVTopologyPipeline(const port::LegacyCameraFrame& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const port::LegacySteeringState& prior_state,
                                                 const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_GEOMETRY_HPP
