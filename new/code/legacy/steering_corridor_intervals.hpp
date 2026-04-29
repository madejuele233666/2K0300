#ifndef LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP
#define LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP

// 走廊间隔提取层 —— 把每一层稀疏采样点聚合成可行驶支撑区间，
// 并判断边界是观测到的真实边缘还是截断（搜索窗口/图像边界/无效投影）。
// 只有 kObservedDrivableBackground 才算真实观测边缘。

#include <array>
#include <vector>

#include "legacy/steering_bev_sparse_sampler.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 单层走廊间隔集合
struct CorridorIntervalLayer {
    float forward_m = 0.0F;                     //!< 该层前向距离
    std::vector<port::CorridorInterval> intervals{}; //!< 该层的间隔列表
};

// 完整走廊间隔集合：24 层，每层 0~N 个间隔
struct CorridorIntervalSet {
    bool valid = false;                                              //!< 是否有任何有效间隔
    std::array<CorridorIntervalLayer, port::kBevTrackSampleCount> layers{}; //!< 24 层间隔
};

// 从稀疏采样网格中提取走廊间隔
// 扫描每层横向采样点，将连续的 drivable 采样点聚合成区间
CorridorIntervalSet ExtractCorridorIntervals(const BEVSparseSampleGrid& samples,
                                             const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CORRIDOR_INTERVALS_HPP
