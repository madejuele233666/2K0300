/**
 * @file bev_element_raster_types.hpp
 * @brief BEV元素栅格类型定义
 *
 * 定义BEV（鸟瞰视角）下对元素进行栅格化所需的枚举与参数类型，
 * 用于对感知到的车道元素（白线、黑线、未知区域）进行逐栅格分类与投影状态跟踪。
 */

#ifndef LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP
#define LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP

#include <cstdint>

namespace ls2k::port {

/**
 * @enum BEVElementRasterCellClass
 * @brief 栅格单元的视觉分类结果
 *
 * 对BEV投影后的每个栅格单元，根据像素值或模型输出进行分类。
 * kBlack 代表黑色元素（如深色路面标记），kWhite 代表白色元素（如车道线）。
 */
enum class BEVElementRasterCellClass : std::uint8_t {
    kInvalid,   ///< 无效/未初始化
    kUnknown,   ///< 无法确定分类
    kBlack,     ///< 黑色元素
    kWhite,     ///< 白色元素
};

/**
 * @enum BEVElementRasterProjectionState
 * @brief 栅格单元的投影状态
 *
 * 记录从图像像素到BEV栅格的反向投影过程中每个栅格的状态。
 * 用于区分正常采样、超出图像边界和投影失败等情况。
 */
enum class BEVElementRasterProjectionState : std::uint8_t {
    kUnavailable,       ///< 不可用（未初始化或无效）
    kSampleable,        ///< 可正常采样（投影成功且在图像范围内）
    kOutsideFrame,      ///< 超出图像帧边界
    kProjectionFailed,  ///< 投影计算失败（如透视变换奇异）
};

/**
 * @struct BEVElementRasterParameters
 * @brief BEV元素栅格的运行时参数
 *
 * 控制栅格化的启用状态和输出分辨率。
 */
struct BEVElementRasterParameters {
    bool enabled = false; ///< 是否启用栅格化处理
    int width = 320;      ///< 栅格宽度（像素），高度由几何参数推导
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_ELEMENT_RASTER_TYPES_HPP
