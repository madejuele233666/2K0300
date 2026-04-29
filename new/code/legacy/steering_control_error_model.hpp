#ifndef LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP
#define LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP

// 控制误差模型层 —— 从参考路径计算车辆控制误差。
// 核心输出：近侧横向误差、远侧航向误差、预览曲率、前视距离误差、
// 曲率命令（纯追踪 + 航向 + 前馈）、角速度目标。
// 负责将感知层的路径估计转换为底层控制器的输入。

#include "port/control_types.hpp"

namespace ls2k::legacy {

// 计算控制误差模型输出
// input: 包含参考路径、轨迹估计、车辆上下文、控制约束
// params: 控制模型参数（采样索引、前视距离、增益等）
port::ControlErrorModelOutput ComputeControlErrorModel(const port::ControlErrorModelInput& input,
                                                       const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CONTROL_ERROR_MODEL_HPP
