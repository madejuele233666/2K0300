#ifndef LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP
#define LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP

// 观测组装层 —— 将原始感知输出（轨迹估计、BEV 投影）与车辆上下文
// 组装为完整的 BEVSceneObservation 和 ControlConstraintSet。
// 负责计算场景候选标记（cross/circle/zebra）、约束条件（降级/抑制）。

#include "legacy/steering_bev_projector.hpp"
#include "port/control_types.hpp"

namespace ls2k::legacy {

// 观测组装结果 —— 包含车辆上下文、场景观测和控制约束
struct ObservationAssemblyResult {
    port::VehicleContext vehicle{};              //!< 车辆上下文（IMU、帧ID、时间）
    port::BEVSceneObservation observation{};     //!< BEV 场景观测（候选标记、展开比、航向等）
    port::ControlConstraintSet constraints{};     //!< 控制约束（fail-safe、降级、增益/限速缩放）
};

// 组装完整观测
// 整合帧数据、轨迹估计、IMU、低电压标志，输出场景观测和约束
ObservationAssemblyResult AssembleObservation(const port::LegacyCameraFrame& frame,
                                              int threshold,
                                              const port::RuntimeParameters& params,
                                              const port::LegacySteeringState& prior_state,
                                              const port::ImuSample& imu,
                                              bool low_voltage_emergency,
                                              uint64_t frame_id,
                                              uint64_t capture_time_ms,
                                              const port::BEVTrackEstimate& track,
                                              const BEVProjector& projector);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_OBSERVATION_ASSEMBLY_HPP
