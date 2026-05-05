#ifndef LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP
#define LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "port/bev_reference_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::legacy {

struct BEVElementRasterFrame;

enum class BEVSimplePixelClass {
    kInvalid,
    kUnknown,
    kBlack,
    kWhite,
};

struct BEVSimpleImage {
    bool valid = false;
    int width = 0;
    int height = 0;
    float lateral_limit_m = 0.0F;
    float forward_max_m = 0.0F;
    std::vector<std::uint8_t> gray{};
    std::vector<BEVSimplePixelClass> classes{};
};

struct BEVSimpleWhiteInterval {
    float forward_m = 0.0F;
    float left_m = 0.0F;
    float right_m = 0.0F;
    float center_m = 0.0F;
    float width_m = 0.0F;
    int left_px = 0;
    int right_px = 0;
};

struct BEVSimpleRowScan {
    bool valid = false;
    float forward_m = 0.0F;
    int row_px = 0;
    std::size_t sampleable_count = 0;
    std::size_t white_count = 0;
    std::size_t black_count = 0;
    std::size_t unknown_count = 0;
    std::size_t unavailable_count = 0;
    float sampleable_left_m = 0.0F;
    float sampleable_right_m = 0.0F;
    float sampleable_width_m = 0.0F;
    std::vector<BEVSimpleWhiteInterval> intervals{};
};

enum class BEVSampleProjectionState {
    kSampleable,
    kOutsideFrame,
    kProjectionFailed,
};

struct BEVSampleProjectionEntry {
    BEVSampleProjectionState state = BEVSampleProjectionState::kProjectionFailed;
    float forward_m = 0.0F;
    float lateral_m = 0.0F;
    float image_row_px = 0.0F;
    float image_col_px = 0.0F;
};

struct BEVSampleProjectionLut {
    bool valid = false;
    port::BEVProjectorCalibration calibration{};
    int frame_width = 0;
    int frame_height = 0;
    int frame_stride = 0;
    std::array<float, port::kBevReferenceSampleCount> forward_samples_m{};
    float lateral_limit_m = 0.0F;
    float lateral_step_m = 0.0F;
    std::size_t lateral_sample_count = 0;
    std::vector<BEVSampleProjectionEntry> entries{};
};

struct BEVSimplePerceptionResult {
    int threshold = 0;
    std::vector<BEVSimpleRowScan> rows{};
    port::BEVReferencePath reference_path{};
    std::string reference_mode = "none";
    std::string reference_source = "none";
};

const char* ToString(BEVSimplePixelClass class_kind);
const char* ToString(BEVSampleProjectionState state);
const char* ToString(port::ReferenceMode mode);
const char* ToString(port::BEVPathPointSource source);

BEVSimplePixelClass ClassifyBevPixel(std::uint8_t gray,
                                     int threshold,
                                     const port::BEVClassificationParameters& classification);

bool SampleFrameBilinear(const port::LegacyCameraFrameView& frame,
                         float row_px,
                         float col_px,
                         std::uint8_t& out_gray);

bool EnsureBEVSampleProjectionLut(BEVSampleProjectionLut& lut,
                                  const port::LegacyCameraFrameView& frame,
                                  const port::RuntimeParameters& params,
                                  const BEVProjector& projector);

port::BEVReferencePath ExtractRasterConnectedLeadingReferenceSegment(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params,
    const BEVElementRasterFrame* element_raster = nullptr);

port::BEVReferencePath BuildReferencePath(const std::vector<BEVSimpleRowScan>& rows,
                                          const port::RuntimeParameters& params,
                                          const BEVElementRasterFrame* element_raster = nullptr);

port::ReferenceHoldState MakeReferenceHoldState(const port::BEVReferencePath& current_visual_reference,
                                                const port::RuntimeParameters& params);

port::ReferenceContinuityResult BuildReferenceHoldCandidate(const port::ReferenceHoldState& prior_hold,
                                                            const port::RuntimeParameters& params);

BEVSimpleImage BuildDebugDenseBevImage(const port::LegacyCameraFrameView& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector);

BEVSimplePerceptionResult RunBEVSimplePerception(const port::LegacyCameraFrameView& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const BEVProjector& projector,
                                                 BEVSampleProjectionLut* lut,
                                                 const BEVElementRasterFrame* element_raster = nullptr);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_SIMPLE_PERCEPTION_HPP
