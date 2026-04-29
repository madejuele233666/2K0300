#ifndef LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP
#define LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP

// steering::AnalyzeFrame() 的总返回结构。
// 打包整个感知管线从 BEV 投影到控制误差模型输出的全部结果。

#include <string>

#include "port/control_types.hpp"

namespace ls2k::legacy {

// AnalyzeFrame 的完整分析结果，包含感知、轨迹、道路假设、拓扑证据、
// 参考路径、控制输出以及跨帧转向状态更新
struct SteeringAnalysisResult {
    port::PerceptionResult perception{};        //!< 感知主结果
    port::BEVTrackEstimate track_estimate{};    //!< BEV 轨迹估计
    port::RoadHypotheses road_hypotheses{};     //!< 候选路径假设集合
    port::TopologyEvidence topology_evidence{};  //!< 拓扑场景证据
    port::BEVSceneObservation scene_observation{}; //!< 场景观测数据
    port::BEVReferencePath reference_path{};     //!< 最终选定的参考路径
    port::ControlConstraintSet control_constraints{}; //!< 控制约束
    port::ControlErrorModelOutput control_output{};   //!< 控制误差模型输出
    std::string scene_debug_candidate = "none";       //!< 调试用候选场景名
    int scene_debug_candidate_streak = 0;             //!< 调试候选连续帧数
    float scene_cross_candidate_score_last = 0.0F;    //!< 上次十字路口候选评分
    float scene_circle_left_candidate_score_last = 0.0F; //!< 上次左环岛候选评分
    float scene_circle_right_candidate_score_last = 0.0F; //!< 上次右环岛候选评分
    port::LaneGeometryHistorySnapshot lane_geometry_snapshot{}; //!< 车道几何快照（旧版兼容）
    port::TrackHistorySnapshot track_history_snapshot{}; //!< 轨迹历史快照（旧版兼容）
    port::GyroContinuityState gyro_continuity_state{}; //!< 陀螺仪连续性状态
    port::LegacySteeringState steering_state_update{}; //!< 跨帧转向状态更新
    bool steering_state_update_valid = false;          //!< steering_state_update 是否有效
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_ANALYSIS_RESULT_HPP
