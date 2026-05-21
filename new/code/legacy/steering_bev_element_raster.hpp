#ifndef LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP
#define LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/bev_element_raster_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

/// BEV元素栅格查找表条目，描述了一个栅格单元从图像中采样的投影状态和双线性插值参数
struct BEVElementRasterLutEntry {
    port::BEVElementRasterProjectionState state =        ///< 投影状态（不可用/可采样/超出画面/投影失败）
        port::BEVElementRasterProjectionState::kUnavailable;
    port::BEVPoint metric_point{};                       ///< 该栅格单元对应的BEV坐标点（米）
    std::array<std::uint32_t, 4> source_indices{};       ///< 4个源图像像素的线性索引（双线性插值）
    std::array<std::uint16_t, 4> weights{};              ///< 4个源像素的插值权重（总和为kBilinearWeightScale）
};

/// BEV元素栅格查找表，缓存投影映射以避免重复计算
struct BEVElementRasterLut {
    bool valid = false;                                    ///< 查找表是否有效
    port::BEVProjectorCalibration calibration{};           ///< 生成此表时的标定参数（用于校验一致性）
    port::BEVElementRasterParameters params{};             ///< 生成此表时的栅格参数
    int frame_width = 0;                                   ///< 源图像宽度（像素）
    int frame_height = 0;                                  ///< 源图像高度（像素）
    int frame_stride = 0;                                  ///< 源图像行跨度（像素）
    int width = 0;                                         ///< 栅格宽度（栅格单元数）
    int height = 0;                                        ///< 栅格高度（栅格单元数）
    float lateral_limit_m = 0.0F;                          ///< 横向半宽限制（米）
    float forward_max_m = 0.0F;                            ///< 最大前向距离（米）
    std::vector<BEVElementRasterLutEntry> entries{};       ///< 所有栅格单元的查找表条目
};

/// BEV元素栅格帧，存储一帧图像投影到BEV栅格后的分类结果
struct BEVElementRasterFrame {
    bool valid = false;                               ///< 栅格帧数据是否有效
    bool enabled = false;                             ///< 该功能是否启用
    int width = 0;                                    ///< 栅格宽度（栅格单元数）
    int height = 0;                                   ///< 栅格高度（栅格单元数）
    float lateral_limit_m = 0.0F;                     ///< 横向半宽限制（米）
    float forward_max_m = 0.0F;                       ///< 最大前向距离（米）
    std::vector<port::BEVElementRasterCellClass> classes{};          ///< 每个栅格单元的分类结果
    std::vector<port::BEVElementRasterProjectionState> projection_states{};  ///< 每个栅格单元的投影状态

    /// 检查给定栅格坐标是否在有效范围内
    bool InBounds(int x, int y) const;
    /// 计算栅格坐标对应的一维数组索引
    std::size_t Index(int x, int y) const;
    /// 将栅格坐标转换为对应的BEV度量坐标（米）
    port::BEVPoint CellToMetric(int x, int y) const;
    /// 将BEV度量坐标转换为栅格坐标
    bool MetricToCell(const port::BEVPoint& point, int& x, int& y) const;
    /// 获取指定栅格位置的分类结果，越界返回kInvalid
    port::BEVElementRasterCellClass ClassAt(int x, int y) const;
    /// 使用Bresenham算法检查线段是否经过黑色栅格单元
    bool SegmentTouchesBlackCells(int x0, int y0, int x1, int y1) const;
    /// 检查BEV坐标系下两点连线是否经过黑色栅格单元
    bool SegmentTouchesBlack(const port::BEVPoint& begin, const port::BEVPoint& end) const;
};

/// BEV元素栅格构建器，维护内部查找表和栅格帧缓存，支持增量更新
class BEVElementRasterBuilder {
public:
    /// 根据当前帧构建BEV元素栅格
    /// @param frame 原始相机帧视图
    /// @param threshold 二值化阈值
    /// @param params 运行时参数
    /// @param projector BEV投影器
    /// @return 构建完成的栅格帧（内部缓存，每次调用会复用）
    const BEVElementRasterFrame& Build(const port::LegacyCameraFrameView& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

    /// 重置内部缓存，使下次Build重新计算查找表
    void Reset();

private:
    BEVElementRasterLut lut_{};       ///< 内部缓存的查找表
    BEVElementRasterFrame raster_{};  ///< 内部缓存的栅格帧
};

/// 将栅格单元分类枚举转换为可读字符串
const char* ToString(port::BEVElementRasterCellClass class_kind);
/// 将投影状态枚举转换为可读字符串
const char* ToString(port::BEVElementRasterProjectionState state);

/// 确保查找表有效且匹配当前帧/参数/投影器，否则重建查找表
/// @param lut 查找表（若无效或不匹配将被重建）
/// @param frame 当前相机帧视图
/// @param params 运行时参数
/// @param projector BEV投影器
/// @return 查找表是否有效
bool EnsureBEVElementRasterLut(BEVElementRasterLut& lut,
                               const port::LegacyCameraFrameView& frame,
                               const port::RuntimeParameters& params,
                               const BEVProjector& projector);

/// 一次性构建BEV元素栅格（不缓存查找表，但可传入外部lut实现缓存）
/// @param frame 原始相机帧视图
/// @param threshold 二值化阈值
/// @param params 运行时参数
/// @param projector BEV投影器
/// @param lut 可选的外部查找表指针（为nullptr时自动创建临时表）
/// @return 构建完成的栅格帧
BEVElementRasterFrame BuildBEVElementRaster(const port::LegacyCameraFrameView& frame,
                                            int threshold,
                                            const port::RuntimeParameters& params,
                                            const BEVProjector& projector,
                                            BEVElementRasterLut* lut);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_ELEMENT_RASTER_HPP
