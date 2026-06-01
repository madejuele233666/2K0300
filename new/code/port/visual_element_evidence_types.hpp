/**
 * @file visual_element_evidence_types.hpp
 * @brief 视觉元素证据类型定义
 *
 * 定义BEV（鸟瞰视角）下的视觉元素检测证据类型。
 * 包含十字路口出口检测、圆形元素（转弯）检测的证据结构，
 * 以及元素候选摘要、边界框、统计支持和运行时参数。
 */

#ifndef LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP
#define LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ls2k::port {

/**
 * @struct VisualElementCandidateSummary
 * @brief 视觉元素候选摘要
 *
 * 描述一个视觉元素候选是否已构建、是否启用接管、是否被纳入仲裁。
 */
struct VisualElementCandidateSummary {
    bool built = false;                    ///< 是否已构建候选
    bool takeover_enabled = false;         ///< 是否启用接管功能
    bool included_in_arbitration = false;  ///< 是否已纳入仲裁
    std::string reason = "not_built";      ///< 未构建/未纳入的原因
};

/**
 * @struct CrossExitElementEvidence
 * @brief 十字路口出口元素证据
 *
 * 描述十字路口出口处检测到的视觉元素（如斑马线、路口标记）的
 * 位置范围、置信度、白点/未知点统计和候选状态。
 */
struct CrossExitElementEvidence {
    bool present = false;              ///< 元素是否存在
    float confidence = 0.0F;           ///< 检测置信度
    float forward_min_m = 0.0F;        ///< 元素前向最小距离（米）
    float forward_max_m = 0.0F;        ///< 元素前向最大距离（米）
    float lateral_min_m = 0.0F;        ///< 元素横向最小位置（米）
    float lateral_max_m = 0.0F;        ///< 元素横向最大位置（米）
    std::size_t sampleable_count = 0;      ///< 可采样的栅格单元数
    std::size_t supporting_white_count = 0;  ///< 支持判定的白色单元数
    std::size_t unknown_count = 0;          ///< 无法分类的单元数
    std::string reason = "not_evaluated";  ///< 未评估的原因
    VisualElementCandidateSummary candidate{};  ///< 元素候选摘要
};

/**
 * @struct VisualElementEvidenceBounds
 * @brief 视觉元素证据边界
 *
 * 定义元素在BEV坐标系中的空间范围（前向和横向的min/max）。
 */
struct VisualElementEvidenceBounds {
    float forward_min_m = 0.0F;  ///< 前向最小距离（米）
    float forward_max_m = 0.0F;  ///< 前向最大距离（米）
    float lateral_min_m = 0.0F;  ///< 横向最小位置（米）
    float lateral_max_m = 0.0F;  ///< 横向最大位置（米）
};

/**
 * @struct VisualElementEvidenceSupport
 * @brief 视觉元素证据统计支持
 *
 * 统计可采样单元数、支持白色/黑色判定的单元数和未知单元数。
 */
struct VisualElementEvidenceSupport {
    std::size_t sampleable_count = 0;       ///< 可采样单元总数
    std::size_t supporting_white_count = 0;  ///< 支持白色判定的单元数
    std::size_t supporting_black_count = 0;  ///< 支持黑色判定的单元数
    std::size_t unknown_count = 0;           ///< 无法分类的单元数
};

/**
 * @struct VisualElementEvidenceRecord
 * @brief 单个视觉元素证据记录
 *
 * 包含元素的完整证据信息：ID、存在性、置信度、边界、统计支持和候选状态。
 */
struct VisualElementEvidenceRecord {
    std::string id{};                     ///< 元素ID
    bool present = false;                 ///< 元素是否存在
    float confidence = 0.0F;              ///< 检测置信度
    std::string reason = "not_evaluated";  ///< 评估结果原因
    VisualElementEvidenceBounds bounds{};  ///< 元素空间边界
    VisualElementEvidenceSupport support{};  ///< 统计支持数据
    VisualElementCandidateSummary candidate{};  ///< 候选摘要
};

/**
 * @struct VisualElementEvidenceFrame
 * @brief 一帧中的所有视觉元素证据
 *
 * 包含十字路口出口证据和通用元素记录列表。
 */
struct VisualElementEvidenceFrame {
    CrossExitElementEvidence cross_exit{};          ///< 十字路口出口证据
    std::vector<VisualElementEvidenceRecord> records{};  ///< 通用元素记录列表
};

/**
 * @struct BEVElementParameters
 * @brief BEV元素检测的运行参数
 *
 * 控制十字路口出口检测和 Circle V2 场景注册/退出门限。
 */
struct BEVElementParameters {
    // 十字路口出口检测参数
    bool cross_exit_takeover_enabled = true;   ///< 是否启用十字路口出口接管
    float cross_wide_row_white_ratio_min = 0.95F;  ///< 十字路口宽行白色比例最小值

    // Circle V2 场景状态机参数
    bool circle_v2_enabled = true;                  ///< 是否注册 CircleV2Scene
    float circle_v2_exit_yaw_threshold_deg = 330.0F; ///< B->C 出环角度阈值（度）
    int circle_v2_exit_hold_frames = 60;             ///< C 状态保持帧数
    int circle_v2_inner_trace_stall_timeout_ms = 4000; ///< InnerTrace 无明显 yaw 积分退回 Idle 超时
    float circle_v2_inner_trace_stall_yaw_min_deg = 16.5F; ///< InnerTrace 超时退回 Idle 的最小明显 yaw 积分
    float circle_v2_inner_trace_path_offset_m = 0.1F; ///< InnerTrace 从内圆边线向道路内部偏移的距离
    float circle_v2_opposite_straight_confidence_min = 0.50F; ///< CircleV2 对侧直线最低拟合置信度
    int circle_v2_entry_bottom_row_count = 4; ///< Approach entry gate 使用的下部 ROI 行数
    float circle_v2_entry_bottom_forward_min_m = 0.0F; ///< Approach entry gate 下部 ROI 前向下限
    float circle_v2_entry_bottom_forward_max_m = 0.25F; ///< Approach entry gate 下部 ROI 前向上限
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_VISUAL_ELEMENT_EVIDENCE_TYPES_HPP
