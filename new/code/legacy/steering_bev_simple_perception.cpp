#include "legacy/steering_bev_simple_perception.hpp"

// Simple BEV perception pipeline:
// frame view -> virtual BEV sparse row scan -> row white intervals -> reference path.
// Debug dense BEV remains output-only; runtime element raster is built by
// steering_bev_element_raster.* and is not read back from debug media.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ls2k::legacy {
namespace {

constexpr std::uint8_t kInvalidGray = 0U;

float LateralAtIndex(std::size_t index, float lateral_limit, float lateral_step) {
    return -lateral_limit + static_cast<float>(index) * lateral_step;
}

std::size_t ComputeLateralSampleCount(float lateral_limit, float lateral_step) {
    if (lateral_limit <= 0.0F || lateral_step <= 1.0e-5F) {
        return 0;
    }
    return static_cast<std::size_t>(std::floor((2.0F * lateral_limit) / lateral_step + 1.0e-4F)) + 1U;
}

bool SameCalibration(const port::BEVProjectorCalibration& lhs,
                     const port::BEVProjectorCalibration& rhs) {
    if (lhs.valid != rhs.valid ||
        lhs.debug_grid_width != rhs.debug_grid_width ||
        lhs.debug_grid_height != rhs.debug_grid_height ||
        lhs.projector_id != rhs.projector_id ||
        lhs.projector_hash != rhs.projector_hash) {
        return false;
    }
    for (std::size_t index = 0; index < port::kBevCalibrationPointCount; ++index) {
        if (lhs.source_points[index].row_px != rhs.source_points[index].row_px ||
            lhs.source_points[index].col_px != rhs.source_points[index].col_px ||
            lhs.target_points[index].forward_m != rhs.target_points[index].forward_m ||
            lhs.target_points[index].lateral_m != rhs.target_points[index].lateral_m) {
            return false;
        }
    }
    return true;
}

bool SameForwardSamples(const std::array<float, port::kBevReferenceSampleCount>& lhs,
                        const std::array<float, port::kBevReferenceSampleCount>& rhs) {
    for (std::size_t index = 0; index < port::kBevReferenceSampleCount; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

port::ReferenceGeometryIdentity MakeReferenceGeometryIdentity(const port::RuntimeParameters& params) {
    port::ReferenceGeometryIdentity identity{};
    identity.initialized = true;
    identity.forward_samples_m = params.bev_geometry.forward_samples_m;
    identity.search_lateral_limit_m = params.bev_geometry.search_lateral_limit_m;
    identity.lateral_step_m = params.bev_geometry.lateral_step_m;
    return identity;
}

bool SameReferenceGeometryIdentity(const port::ReferenceGeometryIdentity& lhs,
                                   const port::ReferenceGeometryIdentity& rhs) {
    return lhs.initialized && rhs.initialized &&
           lhs.search_lateral_limit_m == rhs.search_lateral_limit_m &&
           lhs.lateral_step_m == rhs.lateral_step_m &&
           SameForwardSamples(lhs.forward_samples_m, rhs.forward_samples_m);
}

void InitializeReferencePath(port::BEVReferencePath& reference,
                             const port::RuntimeParameters& params,
                             port::ReferenceMode mode) {
    reference.mode = mode;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        port::BEVPathSample& sample = reference.sampled_path[index];
        sample.present = false;
        sample.point.forward_m = params.bev_geometry.forward_samples_m[index];
        sample.point.lateral_m = 0.0F;
        sample.confidence = 0.0F;
        sample.source = port::BEVPathPointSource::kNone;
    }
}

bool LutMatches(const BEVSampleProjectionLut& lut,
                const port::LegacyCameraFrameView& frame,
                const port::RuntimeParameters& params,
                const BEVProjector& projector,
                std::size_t lateral_count,
                float lateral_limit,
                float lateral_step) {
    return lut.valid &&
           lut.frame_width == frame.width &&
           lut.frame_height == frame.height &&
           lut.frame_stride == frame.stride &&
           lut.lateral_sample_count == lateral_count &&
           lut.lateral_limit_m == lateral_limit &&
           lut.lateral_step_m == lateral_step &&
           SameForwardSamples(lut.forward_samples_m, params.bev_geometry.forward_samples_m) &&
           SameCalibration(lut.calibration, projector.Calibration()) &&
           lut.entries.size() == port::kBevReferenceSampleCount * lateral_count;
}

}  // namespace

bool SampleFrameBilinear(const port::LegacyCameraFrameView& frame,
                         float row_px,
                         float col_px,
                         std::uint8_t& out_gray) {
    if (!frame.Valid()) {
        return false;
    }
    if (row_px < 0.0F || col_px < 0.0F || row_px > static_cast<float>(frame.height - 1) ||
        col_px > static_cast<float>(frame.width - 1)) {
        return false;
    }

    const int row0 = static_cast<int>(std::floor(row_px));
    const int col0 = static_cast<int>(std::floor(col_px));
    const int row1 = std::min(row0 + 1, frame.height - 1);
    const int col1 = std::min(col0 + 1, frame.width - 1);
    const float row_frac = row_px - static_cast<float>(row0);
    const float col_frac = col_px - static_cast<float>(col0);

    const auto gray_at = [&frame](int row, int col) -> float {
        const std::size_t index =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.stride) +
            static_cast<std::size_t>(col);
        return static_cast<float>(frame.gray[index]);
    };

    const float top = gray_at(row0, col0) * (1.0F - col_frac) + gray_at(row0, col1) * col_frac;
    const float bottom = gray_at(row1, col0) * (1.0F - col_frac) + gray_at(row1, col1) * col_frac;
    const float gray = top * (1.0F - row_frac) + bottom * row_frac;
    out_gray = static_cast<std::uint8_t>(std::lround(std::clamp(gray, 0.0F, 255.0F)));
    return true;
}

namespace {

float DecisionBandForThreshold(float threshold) {
    const float nearest_saturation =
        std::min(std::max(1.0F, threshold), std::max(1.0F, 255.0F - threshold));
    return std::clamp(nearest_saturation * 0.5F, 32.0F, 72.0F);
}

}  // namespace

BEVSimplePixelClass ClassifyBevPixel(std::uint8_t gray,
                                     int threshold,
                                     const port::BEVClassificationParameters& classification) {
    const float threshold_f = static_cast<float>(std::clamp(threshold, 0, 255));
    const float margin = std::abs(static_cast<float>(gray) - threshold_f);
    const float confidence = std::clamp(margin / DecisionBandForThreshold(threshold_f), 0.0F, 1.0F);
    if (confidence < classification.unknown_confidence_min) {
        return BEVSimplePixelClass::kUnknown;
    }
    if (gray > threshold) {
        return confidence >= classification.white_confidence_min ? BEVSimplePixelClass::kWhite
                                                                 : BEVSimplePixelClass::kUnknown;
    }
    return BEVSimplePixelClass::kBlack;
}

namespace {

port::BEVPoint PixelToBevPoint(int x,
                               int y,
                               int width,
                               int height,
                               float lateral_limit_m,
                               float forward_max_m) {
    const float normalized_x = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.5F;
    const float normalized_y = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 1.0F;
    port::BEVPoint point{};
    point.lateral_m = normalized_x * (2.0F * lateral_limit_m) - lateral_limit_m;
    point.forward_m = (1.0F - normalized_y) * forward_max_m;
    return point;
}

}  // namespace

BEVSimpleImage BuildDebugDenseBevImage(const port::LegacyCameraFrameView& frame,
                                       int threshold,
                                       const port::RuntimeParameters& params,
                                       const BEVProjector& projector) {
    BEVSimpleImage image{};
    if (!projector.Valid() || !frame.Valid()) {
        return image;
    }

    const float lateral_limit = std::max(0.1F, params.bev_geometry.search_lateral_limit_m);
    const float forward_max = params.bev_geometry.forward_samples_m.back();
    const int width = std::max(2, params.bev_projector.debug_grid_width * 2);
    const float scale_px_per_m = static_cast<float>(width) / std::max(1.0e-4F, lateral_limit * 2.0F);
    const int height = std::max(2, static_cast<int>(std::lround(forward_max * scale_px_per_m)));

    image.valid = true;
    image.width = width;
    image.height = height;
    image.lateral_limit_m = lateral_limit;
    image.forward_max_m = forward_max;
    image.gray.assign(static_cast<std::size_t>(width * height), kInvalidGray);
    image.classes.assign(static_cast<std::size_t>(width * height), BEVSimplePixelClass::kInvalid);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const port::BEVPoint bev_point = PixelToBevPoint(x, y, width, height, lateral_limit, forward_max);
            port::ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage(bev_point, image_point)) {
                continue;
            }
            std::uint8_t gray = 0;
            if (!SampleFrameBilinear(frame, image_point.row_px, image_point.col_px, gray)) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            image.gray[index] = gray;
            image.classes[index] = ClassifyBevPixel(gray, threshold, params.bev_classification);
        }
    }

    return image;
}

namespace {

BEVSimpleRowScan ScanSparseRow(const port::LegacyCameraFrameView& frame,
                               int threshold,
                               const port::RuntimeParameters& params,
                               const BEVSampleProjectionLut& lut,
                               std::size_t row_index) {
    BEVSimpleRowScan row{};
    if (!lut.valid || row_index >= port::kBevReferenceSampleCount || lut.lateral_sample_count == 0) {
        return row;
    }

    row.valid = true;
    row.forward_m = params.bev_geometry.forward_samples_m[row_index];
    row.row_px = static_cast<int>(row_index);
    const float min_width_m = std::max(0.02F, params.bev_geometry.lateral_step_m * 1.5F);
    int run_begin = -1;
    bool have_sampleable_lateral = false;
    for (std::size_t lateral_index = 0; lateral_index <= lut.lateral_sample_count; ++lateral_index) {
        bool white = false;
        if (lateral_index < lut.lateral_sample_count) {
            const BEVSampleProjectionEntry& entry =
                lut.entries[row_index * lut.lateral_sample_count + lateral_index];
            BEVSimplePixelClass pixel_class = BEVSimplePixelClass::kInvalid;
            if (entry.state == BEVSampleProjectionState::kSampleable) {
                std::uint8_t gray = 0;
                if (SampleFrameBilinear(frame, entry.image_row_px, entry.image_col_px, gray)) {
                    pixel_class = ClassifyBevPixel(gray, threshold, params.bev_classification);
                    const float lateral = LateralAtIndex(lateral_index,
                                                         lut.lateral_limit_m,
                                                         lut.lateral_step_m);
                    if (!have_sampleable_lateral) {
                        row.sampleable_left_m = lateral;
                        row.sampleable_right_m = lateral;
                        have_sampleable_lateral = true;
                    } else {
                        row.sampleable_left_m = std::min(row.sampleable_left_m, lateral);
                        row.sampleable_right_m = std::max(row.sampleable_right_m, lateral);
                    }
                }
            }
            if (pixel_class == BEVSimplePixelClass::kWhite) {
                ++row.white_count;
            } else if (pixel_class == BEVSimplePixelClass::kBlack) {
                ++row.black_count;
            } else if (pixel_class == BEVSimplePixelClass::kUnknown) {
                ++row.unknown_count;
            } else {
                ++row.unavailable_count;
            }
            if (pixel_class != BEVSimplePixelClass::kInvalid) {
                ++row.sampleable_count;
                white = pixel_class == BEVSimplePixelClass::kWhite;
            }
        }

        if (white && run_begin < 0) {
            run_begin = static_cast<int>(lateral_index);
        }
        if ((!white || lateral_index == lut.lateral_sample_count) && run_begin >= 0) {
            const int run_end = static_cast<int>(lateral_index) - 1;
            const float left =
                LateralAtIndex(static_cast<std::size_t>(run_begin), lut.lateral_limit_m, lut.lateral_step_m);
            const float right =
                LateralAtIndex(static_cast<std::size_t>(run_end), lut.lateral_limit_m, lut.lateral_step_m);
            const float width = std::max(0.0F, right - left);
            if (width >= min_width_m) {
                BEVSimpleWhiteInterval interval{};
                interval.forward_m = row.forward_m;
                interval.left_m = left;
                interval.right_m = right;
                interval.center_m = 0.5F * (left + right);
                interval.width_m = width;
                interval.left_px = run_begin;
                interval.right_px = run_end;
                row.intervals.push_back(interval);
            }
            run_begin = -1;
        }
    }
    if (have_sampleable_lateral) {
        row.sampleable_width_m = std::max(0.0F, row.sampleable_right_m - row.sampleable_left_m);
    }
    return row;
}

std::vector<BEVSimpleRowScan> ScanSparseRows(const port::LegacyCameraFrameView& frame,
                                             int threshold,
                                             const port::RuntimeParameters& params,
                                             const BEVSampleProjectionLut& lut) {
    std::vector<BEVSimpleRowScan> rows;
    rows.reserve(port::kBevReferenceSampleCount);
    for (std::size_t index = 0; index < port::kBevReferenceSampleCount; ++index) {
        rows.push_back(ScanSparseRow(frame, threshold, params, lut, index));
    }
    return rows;
}

const BEVSimpleWhiteInterval* ChooseInterval(const BEVSimpleRowScan& row,
                                             bool have_previous,
                                             float previous_lateral,
                                             const port::RuntimeParameters& params) {
    if (row.intervals.empty()) {
        return nullptr;
    }
    const float max_jump =
        std::clamp(params.bev_geometry.lateral_step_m * 6.0F, 0.08F, 0.14F);
    const BEVSimpleWhiteInterval* best = nullptr;
    float best_cost = 0.0F;
    for (const BEVSimpleWhiteInterval& interval : row.intervals) {
        const float target = have_previous ? previous_lateral : 0.0F;
        const float cost = std::abs(interval.center_m - target);
        if (have_previous && cost > max_jump) {
            continue;
        }
        if (best == nullptr || cost < best_cost) {
            best = &interval;
            best_cost = cost;
        }
    }
    return best;
}

}  // namespace

port::BEVReferencePath ExtractStrictLeadingReferenceSegment(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params) {
    port::BEVReferencePath reference{};
    InitializeReferencePath(reference, params, port::ReferenceMode::kNone);
    bool have_previous = false;
    float previous_lateral = 0.0F;

    for (std::size_t index = 0; index < rows.size() && index < reference.sampled_path.size(); ++index) {
        port::BEVPathSample& sample = reference.sampled_path[index];
        const BEVSimpleWhiteInterval* interval =
            ChooseInterval(rows[index], have_previous, previous_lateral, params);
        if (interval == nullptr) {
            break;
        }

        reference.mode = port::ReferenceMode::kIntervalCenter;
        sample.present = true;
        sample.point.forward_m = interval->forward_m;
        sample.point.lateral_m = interval->center_m;
        sample.confidence = 1.0F;
        sample.source = port::BEVPathPointSource::kIntervalCenter;
        previous_lateral = interval->center_m;
        have_previous = true;
    }
    return reference;
}

port::BEVReferencePath BuildReferencePath(const std::vector<BEVSimpleRowScan>& rows,
                                          const port::RuntimeParameters& params) {
    return ExtractStrictLeadingReferenceSegment(rows, params);
}

port::ReferenceHoldState MakeReferenceHoldState(const port::BEVReferencePath& current_visual_reference,
                                                const port::RuntimeParameters& params) {
    port::ReferenceHoldState state{};
    state.hold_cycles = 0;
    state.last_reference = current_visual_reference.sampled_path;
    state.geometry_identity = MakeReferenceGeometryIdentity(params);
    return state;
}

port::ReferenceContinuityResult BuildReferenceHoldCandidate(const port::ReferenceHoldState& prior_hold,
                                                            const port::RuntimeParameters& params) {
    port::ReferenceContinuityResult result{};
    result.next_hold_state = prior_hold;
    const port::ReferenceGeometryIdentity current_identity = MakeReferenceGeometryIdentity(params);
    const bool hold_allowed =
        prior_hold.hold_cycles < params.bev_classification.hold_last_max_cycles &&
        SameReferenceGeometryIdentity(prior_hold.geometry_identity, current_identity);
    if (!hold_allowed) {
        result.next_hold_state = {};
        return result;
    }

    InitializeReferencePath(result.reference_path, params, port::ReferenceMode::kHoldLast);
    std::size_t copied = 0;
    for (std::size_t index = 0; index < result.reference_path.sampled_path.size(); ++index) {
        port::BEVPathSample sample = prior_hold.last_reference[index];
        if (!sample.present ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            break;
        }
        sample.confidence *= 0.75F;
        sample.source = port::BEVPathPointSource::kHold;
        result.reference_path.sampled_path[index] = sample;
        ++copied;
    }
    if (copied == 0) {
        result.reference_path = {};
        result.next_hold_state = {};
        return result;
    }

    result.mode = port::ReferenceMode::kHoldLast;
    result.source = "hold";
    result.hold_selected = true;
    result.next_hold_state = prior_hold;
    result.next_hold_state.hold_cycles = prior_hold.hold_cycles + 1;
    return result;
}

bool EnsureBEVSampleProjectionLut(BEVSampleProjectionLut& lut,
                                  const port::LegacyCameraFrameView& frame,
                                  const port::RuntimeParameters& params,
                                  const BEVProjector& projector) {
    const float lateral_limit = std::max(0.1F, params.bev_geometry.search_lateral_limit_m);
    const float lateral_step = std::max(0.005F, params.bev_geometry.lateral_step_m);
    const std::size_t lateral_count = ComputeLateralSampleCount(lateral_limit, lateral_step);
    if (!projector.Valid() || !frame.Valid() || lateral_count == 0) {
        lut = {};
        return false;
    }
    if (LutMatches(lut, frame, params, projector, lateral_count, lateral_limit, lateral_step)) {
        return true;
    }

    BEVSampleProjectionLut rebuilt{};
    rebuilt.valid = true;
    rebuilt.calibration = projector.Calibration();
    rebuilt.frame_width = frame.width;
    rebuilt.frame_height = frame.height;
    rebuilt.frame_stride = frame.stride;
    rebuilt.forward_samples_m = params.bev_geometry.forward_samples_m;
    rebuilt.lateral_limit_m = lateral_limit;
    rebuilt.lateral_step_m = lateral_step;
    rebuilt.lateral_sample_count = lateral_count;
    rebuilt.entries.resize(port::kBevReferenceSampleCount * lateral_count);

    for (std::size_t row_index = 0; row_index < port::kBevReferenceSampleCount; ++row_index) {
        const float forward_m = params.bev_geometry.forward_samples_m[row_index];
        for (std::size_t lateral_index = 0; lateral_index < lateral_count; ++lateral_index) {
            BEVSampleProjectionEntry& entry =
                rebuilt.entries[row_index * lateral_count + lateral_index];
            entry.forward_m = forward_m;
            entry.lateral_m = LateralAtIndex(lateral_index, lateral_limit, lateral_step);
            port::ImagePoint image_point{};
            if (!projector.ProjectVehicleToImage({entry.forward_m, entry.lateral_m}, image_point)) {
                entry.state = BEVSampleProjectionState::kProjectionFailed;
                continue;
            }
            entry.image_row_px = image_point.row_px;
            entry.image_col_px = image_point.col_px;
            if (image_point.row_px < 0.0F || image_point.col_px < 0.0F ||
                image_point.row_px > static_cast<float>(frame.height - 1) ||
                image_point.col_px > static_cast<float>(frame.width - 1)) {
                entry.state = BEVSampleProjectionState::kOutsideFrame;
            } else {
                entry.state = BEVSampleProjectionState::kSampleable;
            }
        }
    }

    lut = std::move(rebuilt);
    return true;
}

const char* ToString(BEVSimplePixelClass class_kind) {
    switch (class_kind) {
        case BEVSimplePixelClass::kInvalid:
            return "invalid";
        case BEVSimplePixelClass::kUnknown:
            return "unknown";
        case BEVSimplePixelClass::kBlack:
            return "black";
        case BEVSimplePixelClass::kWhite:
            return "white";
    }
    return "invalid";
}

const char* ToString(BEVSampleProjectionState state) {
    switch (state) {
        case BEVSampleProjectionState::kSampleable:
            return "sampleable";
        case BEVSampleProjectionState::kOutsideFrame:
            return "outside_frame";
        case BEVSampleProjectionState::kProjectionFailed:
            return "projection_failed";
    }
    return "projection_failed";
}

const char* ToString(port::ReferenceMode mode) {
    switch (mode) {
        case port::ReferenceMode::kNone:
            return "none";
        case port::ReferenceMode::kIntervalCenter:
            return "interval_center";
        case port::ReferenceMode::kHoldLast:
            return "hold_last";
    }
    return "none";
}

const char* ToString(port::BEVPathPointSource source) {
    switch (source) {
        case port::BEVPathPointSource::kNone:
            return "none";
        case port::BEVPathPointSource::kIntervalCenter:
            return "interval_center";
        case port::BEVPathPointSource::kHold:
            return "hold";
    }
    return "none";
}

BEVSimplePerceptionResult RunBEVSimplePerception(const port::LegacyCameraFrameView& frame,
                                                 int threshold,
                                                 const port::RuntimeParameters& params,
                                                 const BEVProjector& projector,
                                                 BEVSampleProjectionLut* lut) {
    BEVSimplePerceptionResult result{};
    result.threshold = threshold;
    BEVSampleProjectionLut local_lut{};
    BEVSampleProjectionLut& active_lut = lut == nullptr ? local_lut : *lut;
    if (!EnsureBEVSampleProjectionLut(active_lut, frame, params, projector)) {
        return result;
    }

    result.rows = ScanSparseRows(frame, threshold, params, active_lut);
    result.reference_path = BuildReferencePath(result.rows, params);
    result.reference_mode = ToString(result.reference_path.mode);
    result.reference_source =
        result.reference_path.mode == port::ReferenceMode::kIntervalCenter ? "simple_interval_center" : "none";
    return result;
}

}  // namespace ls2k::legacy
