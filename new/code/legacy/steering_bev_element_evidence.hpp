#ifndef LS2K_LEGACY_STEERING_BEV_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_BEV_ELEMENT_EVIDENCE_HPP

// 元素证据提取层 —— 从稀疏采样网格中检测十字路口（cross band）
// 和环岛转角（circle corner）。这是场景 FSM 的底层输入。

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_sparse_sampler.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 从稀疏采样网格中提取元素证据（cross band / circle corner）
// frame: 输入帧（用于稠密重采样），sparse_grid: 稀疏采样网格
// 返回十字路口带证据 + 左右环岛转角证据
port::BEVElementEvidence ExtractBEVElementEvidence(const port::LegacyCameraFrame& frame,
                                                   int threshold,
                                                   const port::RuntimeParameters& params,
                                                   const BEVProjector& projector,
                                                   const BEVSparseSampleGrid& sparse_grid);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_ELEMENT_EVIDENCE_HPP
