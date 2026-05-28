/**
 * @file bev_reference_types.hpp
 * @brief BEV参考路径类型定义
 *
 * 定义BEV（鸟瞰视角）参考路径的数据结构，包括路径采样点、路径模式、
 * 保持状态和连续性仲裁结果等，是视觉感知到控制决策的核心中间数据结构。
 */

#ifndef LS2K_PORT_BEV_REFERENCE_TYPES_HPP
#define LS2K_PORT_BEV_REFERENCE_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "port/bev_geometry_types.hpp"

namespace ls2k::port {

/**
 * @enum ReferenceMode
 * @brief 参考路径的模式
 *
 * 标识当前参考路径的来源模式，用于路径连续性仲裁。
 */
enum class ReferenceMode {
    kNone,           ///< 无有效参考路径
    kIntervalCenter, ///< 基于区间中心构建的参考路径（正常视觉跟踪模式）
    kHoldLast        ///< 保持上一次的参考路径（感知短暂丢失时的保持策略）
};

/**
 * @enum BEVPathPointSource
 * @brief 路径采样点的来源标识
 *
 * 记录每个BEV路径采样点是由哪种策略生成的，用于调试和仲裁。
 */
enum class BEVPathPointSource {
    kNone,           ///< 无来源（未初始化）
    kIntervalCenter, ///< 由区间中心算法生成
    kHold            ///< 由保持策略生成（复用上一帧的值）
};

/**
 * @struct BEVPathSample
 * @brief BEV参考路径的单个采样点
 *
 * 包含前向/横向坐标、置信度和来源标记。
 * 24个采样点构成一帧完整的参考路径。
 */
struct BEVPathSample {
    bool present = false;              ///< 该采样点是否有效存在
    BEVPoint point{};                  ///< BEV坐标（前向/横向，单位：米）
    float confidence = 0.0F;           ///< 该采样点的置信度（0~1）
    BEVPathPointSource source = BEVPathPointSource::kNone;  ///< 采样点来源
};

/**
 * @struct BEVReferencePath
 * @brief BEV参考路径
 *
 * 由24个前向采样点组成的完整参考路径，代表车辆期望行驶轨迹。
 */
struct BEVReferencePath {
    ReferenceMode mode = ReferenceMode::kNone;  ///< 当前参考路径的模式
    std::array<BEVPathSample, kBevReferenceSampleCount> sampled_path{};  ///< 24个路径采样点
};

/**
 * @struct ReferenceGeometryIdentity
 * @brief 参考路径几何标识
 *
 * 用于检测参考路径的几何参数是否发生变化，从而决定保持状态是否有效。
 */
struct ReferenceGeometryIdentity {
    bool initialized = false;                              ///< 是否已初始化
    std::array<float, kBevReferenceSampleCount> forward_samples_m{};  ///< 前向采样距离集合
    int sparse_row_count = static_cast<int>(kBevReferenceSampleCount); ///< 启用的前向采样前缀长度
    float search_lateral_limit_m = 0.0F;                   ///< 横向搜索限制
    float lateral_step_m = 0.0F;                           ///< 横向步长
};

/**
 * @struct ReferenceHoldState
 * @brief 参考路径保持状态
 *
 * 当感知短暂丢失时，系统会保持最近的参考路径。
 * 该结构记录保持计数和最后有效的参考路径数据。
 */
struct ReferenceHoldState {
    int hold_cycles = 0;                                                 ///< 当前已保持的周期数
    std::array<BEVPathSample, kBevReferenceSampleCount> last_reference{};  ///< 最后有效的参考路径
    ReferenceGeometryIdentity geometry_identity{};                        ///< 保持时的几何参数标识
    uint64_t reference_capture_time_ms = 0;                                ///< 最后参考的图像时间
};

/**
 * @struct ReferenceContinuityResult
 * @brief 参考路径连续性仲裁结果
 *
 * 路径连续性处理模块的输出，包含仲裁后的参考路径、
 * 选中的模式、来源字符串和更新后的保持状态。
 */
struct ReferenceContinuityResult {
    BEVReferencePath reference_path{};  ///< 仲裁后的参考路径
    ReferenceMode mode = ReferenceMode::kNone;  ///< 仲裁后选中的模式
    std::string source = "none";       ///< 仲裁结果的来源描述
    bool hold_selected = false;        ///< 是否选择了保持模式
    uint64_t reference_capture_time_ms = 0;  ///< 仲裁后 reference 的图像时间
    ReferenceHoldState next_hold_state{};  ///< 更新后的保持状态（供下一帧使用）
};

/// Reference time alignment 输出事实
struct ReferenceTimeAlignmentFacts {
    bool enabled = false;                  ///< 是否启用
    bool valid = false;                    ///< 对齐结果是否可用于控制
    std::string reason = "disabled";       ///< 状态原因
    uint64_t age_ms = 0;                   ///< reference 年龄
    uint64_t reference_capture_time_ms = 0; ///< reference 图像时间
    uint64_t control_time_ms = 0;          ///< 控制时间
    double delta_s_m = 0.0;                ///< 对齐区间前进距离
    double delta_yaw_rad = 0.0;            ///< 对齐区间 yaw 变化
    std::size_t input_sample_count = 0;    ///< 输入 reference 样本数
    std::size_t aligned_sample_count = 0;  ///< 输出 reference 样本数
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_REFERENCE_TYPES_HPP
