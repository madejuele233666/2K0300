#ifndef LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP
#define LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP

// BEV 稀疏采样器 —— 将 BEV 坐标系下的规则网格点投影到图像，
// 采样灰度值并分类为 Drivable/Background/Unknown/Invalid。
// 这是寻线和元素识别（路口/环岛）共用的基础证据层。

#include <array>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 单层采样结果：包含该层的前向距离和该层所有横向采样点
struct BEVSampleLayer {
    float forward_m = 0.0F;          //!< 该层前向距离（米）
    std::vector<port::BEVSample> samples{}; //!< 该层的横向采样点序列
};

// 完整稀疏采样网格：24 层前向采样，每层包含多个横向采样点
struct BEVSparseSampleGrid {
    bool valid = false;                                        //!< 网格是否有效
    std::array<BEVSampleLayer, port::kBevTrackSampleCount> layers{};  //!< 24 层采样结果
};

// 对一帧灰度图执行 BEV 稀疏网格采样
// frame: 输入灰度帧，threshold: 二值化阈值
// params: 运行时参数（含采样范围和步长），projector: BEV 投影器
// 返回包含所有层采样点和分类结果的稀疏网格
BEVSparseSampleGrid SparseMetricSample(const port::LegacyCameraFrame& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_SPARSE_SAMPLER_HPP
