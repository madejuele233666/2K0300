#ifndef LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP
#define LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "vision/image/bev_pixel_classifier.hpp"
#include "vision/bev/bev_projector.hpp"
#include "port/bev_reference_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::vision {

struct ReferenceConnectivityFrameView;

/// 稠密BEV图像（用于调试，包含每个像素的灰度和分类）
struct BEVSimpleImage {
    bool valid = false;               ///< 图像数据是否有效
    int width = 0;                    ///< 图像宽度（像素）
    int height = 0;                   ///< 图像高度（像素）
    float lateral_limit_m = 0.0F;     ///< 横向半宽限制（米）
    float forward_max_m = 0.0F;       ///< 最大前向距离（米）
    std::vector<std::uint8_t> gray{}; ///< 灰度值数组
    std::vector<BEVSimplePixelClass> classes{}; ///< 像素分类数组
};

/// BEV行扫描中的白色连通区间（一条白色标记段的左右边界）
struct BEVSimpleWhiteInterval {
    float forward_m = 0.0F;   ///< 该区间所处的前向距离（米）
    float left_m = 0.0F;      ///< 区间横向低坐标边界（米），不是物理左侧语义
    float right_m = 0.0F;     ///< 区间横向高坐标边界（米），不是物理右侧语义
    float center_m = 0.0F;    ///< 区间中心横向坐标（米）
    float width_m = 0.0F;     ///< 区间宽度（米）
    int left_px = 0;          ///< 区间左边界像素索引
    int right_px = 0;         ///< 区间右边界像素索引
};

/// BEV稀疏行扫描结果，包含一行中各类像素的统计和白色区间
struct BEVSimpleRowScan {
    bool valid = false;               ///< 扫描行是否有效
    float forward_m = 0.0F;           ///< 此行对应的前向距离（米）
    int row_px = 0;                   ///< 在采样行数组中的索引
    std::size_t sampleable_count = 0; ///< 可采样的横向样本总数
    std::size_t white_count = 0;      ///< 白色像素计数
    std::size_t black_count = 0;      ///< 黑色像素计数
    std::size_t unknown_count = 0;    ///< 不确定像素计数
    std::size_t unavailable_count = 0;///< 不可用像素计数
    float sampleable_left_m = 0.0F;   ///< 可采样区域横向低坐标边界（米）
    float sampleable_right_m = 0.0F;  ///< 可采样区域横向高坐标边界（米）
    float sampleable_width_m = 0.0F;  ///< 可采样区域宽度（米）
    bool sampleable_left_unknown_run = false;  ///< 可采样左边界是否以 unknown 连续段开始
    float sampleable_left_unknown_run_right_m = 0.0F; ///< 左边界 unknown 段的最右横向坐标
    bool sampleable_right_unknown_run = false; ///< 可采样右边界是否以 unknown 连续段结束
    float sampleable_right_unknown_run_left_m = 0.0F; ///< 右边界 unknown 段的最左横向坐标
    std::vector<BEVSimpleWhiteInterval> intervals{}; ///< 该行的白色连通区间列表
};

/// 采样投影状态（用于查找表条目）
enum class BEVSampleProjectionState {
    kSampleable,         ///< 可采样（投影在图像范围内）
    kOutsideFrame,       ///< 投影点超出图像范围
    kProjectionFailed,   ///< 投影失败
};

/// 采样投影查找表条目（缓存一个BEV点投影到图像的结果）
struct BEVSampleProjectionEntry {
    BEVSampleProjectionState state = BEVSampleProjectionState::kProjectionFailed; ///< 投影状态
    float forward_m = 0.0F;  ///< BEV前向距离（米）
    float lateral_m = 0.0F;  ///< BEV横向偏移（米）
    float image_row_px = 0.0F; ///< 投影到图像的行坐标（像素）
    float image_col_px = 0.0F; ///< 投影到图像的列坐标（像素）
};

/// BEV采样投影查找表，缓存所有采样点从BEV到图像的投影结果
struct BEVSampleProjectionLut {
    bool valid = false;                                             ///< 查找表是否有效
    port::BEVProjectorCalibration calibration{};                    ///< 生成此表时的标定参数
    int frame_width = 0;                                            ///< 源图像宽度
    int frame_height = 0;                                           ///< 源图像高度
    int frame_stride = 0;                                           ///< 源图像行跨度
    std::array<float, port::kBevReferenceSampleCount> forward_samples_m{}; ///< 前向采样距离数组
    std::size_t sparse_row_count = port::kBevReferenceSampleCount;          ///< 启用的前向采样前缀长度
    float lateral_limit_m = 0.0F;                                   ///< 横向采样限制（米）
    float lateral_step_m = 0.0F;                                    ///< 横向采样步长（米）
    std::size_t lateral_sample_count = 0;                           ///< 横向采样点数
    std::vector<BEVSampleProjectionEntry> entries{};                ///< 所有采样点的投影条目
};

/// 简单BEV感知结果，包含行扫描结果和构建的参考路径
struct BEVSimplePerceptionResult {
    int threshold = 0;                           ///< 使用的二值化阈值
    std::vector<BEVSimpleRowScan> rows{};        ///< 各行的扫描结果
    port::BEVReferencePath reference_path{};      ///< 构建的参考路径
    std::string reference_mode = "none";          ///< 参考路径模式字符串
    std::string reference_source = "none";        ///< 参考路径来源字符串
};

/// 将BEV简单像素分类枚举转换为可读字符串
const char* ToString(BEVSimplePixelClass class_kind);
/// 将采样投影状态枚举转换为可读字符串
const char* ToString(BEVSampleProjectionState state);
/// 将参考路径模式枚举转换为可读字符串
const char* ToString(port::ReferenceMode mode);
/// 将BEV路径点来源枚举转换为可读字符串
const char* ToString(port::BEVPathPointSource source);

/// 在原始帧上进行双线性插值采样
/// @param frame 原始相机帧
/// @param row_px 采样行坐标（像素浮点）
/// @param col_px 采样列坐标（像素浮点）
/// @param out_gray [输出] 采样得到的灰度值
/// @return 采样是否成功
bool SampleFrameBilinear(const port::LegacyCameraFrameView& frame,
                         float row_px,
                         float col_px,
                         std::uint8_t& out_gray);

/// 确保采样投影查找表有效且匹配当前帧/参数/投影器，否则重建
/// @param lut 查找表
/// @param frame 当前相机帧
/// @param params 运行时参数
/// @param projector BEV投影器
/// @return 查找表是否有效
bool EnsureBEVSampleProjectionLut(BEVSampleProjectionLut& lut,
                                  const port::LegacyCameraFrameView& frame,
                                  const port::RuntimeParameters& params,
                                  const BEVProjector& projector);

/// 从行扫描结果中提取第一个连续参考段（允许近端缺失，输出点保留真实 forward_m）
/// @param rows 行扫描结果数组
/// @param params 运行时参数
/// @return 提取的参考路径
port::BEVReferencePath ExtractStrictLeadingReferenceSegment(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const ReferenceConnectivityFrameView* connectivity_frame = nullptr);

/// 从行扫描结果构建完整的参考路径（目前委托给ExtractStrictLeadingReferenceSegment）
port::BEVReferencePath BuildReferencePath(const std::vector<BEVSimpleRowScan>& rows,
                                          const port::RuntimeParameters& params,
                                          const ReferenceConnectivityFrameView* connectivity_frame = nullptr);

/// 构建稠密BEV调试图像（全分辨率投影，仅用于调试/可视化）
/// @param frame 原始相机帧
/// @param classification_model 当前帧灰度分类模型
/// @param params 运行时参数
/// @param projector BEV投影器
/// @return 稠密BEV图像
BEVSimpleImage BuildDebugDenseBevImage(const port::LegacyCameraFrameView& frame,
                                       const BEVPixelClassificationModel& classification_model,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

/// 运行完整的BEV简单感知管线
/// @param frame 原始相机帧
/// @param classification_model 当前帧灰度分类模型
/// @param params 运行时参数
/// @param projector BEV投影器
/// @param lut 可选的外部查找表指针（为nullptr时自动创建临时表）
/// @return 感知结果（行扫描、参考路径等）
BEVSimplePerceptionResult RunBEVSimplePerception(const port::LegacyCameraFrameView& frame,
                                                 const BEVPixelClassificationModel& classification_model,
                                                 const port::RuntimeParameters& params,
                                                 const BEVProjector& projector,
                                                 BEVSampleProjectionLut* lut);

}  // namespace ls2k::vision

#endif  // LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP
