#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "legacy/steering_reference_usability.hpp"

namespace {

struct TestFailure {
    std::string message;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void SetPixel(ls2k::port::LegacyCameraFrame& frame, int row, int col, std::uint8_t value) {
    if (row < 0 || col < 0 || row >= frame.height || col >= frame.width) {
        return;
    }
    frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) +
               static_cast<std::size_t>(col)] = value;
}

void DrawPatch(ls2k::port::LegacyCameraFrame& frame, float row_px, float col_px, std::uint8_t value) {
    const int row = static_cast<int>(std::lround(row_px));
    const int col = static_cast<int>(std::lround(col_px));
    for (int dr = -2; dr <= 2; ++dr) {
        for (int dc = -2; dc <= 2; ++dc) {
            SetPixel(frame, row + dr, col + dc, value);
        }
    }
}

ls2k::port::LegacyCameraFrame MakeFrame(std::uint8_t fill = 0U) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = ls2k::port::kCompiledCameraFrameWidth;
    frame.height = ls2k::port::kCompiledCameraFrameHeight;
    frame.gray.fill(fill);
    return frame;
}

ls2k::legacy::BEVProjector MakeProjector(const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::BEVProjector projector{};
    Expect(projector.Configure(params.bev_projector), "default projector must configure");
    return projector;
}

void DrawVehicleStripe(ls2k::port::LegacyCameraFrame& frame,
                       const ls2k::legacy::BEVProjector& projector,
                       const ls2k::port::RuntimeParameters& params,
                       float lateral_min,
                       float lateral_max,
                       std::uint8_t value) {
    const float lateral_step = std::max(0.01F, params.bev_geometry.lateral_step_m);
    for (const float forward : params.bev_geometry.forward_samples_m) {
        for (float lateral = -params.bev_geometry.search_lateral_limit_m;
             lateral <= params.bev_geometry.search_lateral_limit_m + lateral_step * 0.5F;
             lateral += lateral_step) {
            if (lateral < lateral_min || lateral > lateral_max) {
                continue;
            }
            ls2k::port::ImagePoint image{};
            if (projector.ProjectVehicleToImage({forward, lateral}, image)) {
                DrawPatch(frame, image.row_px, image.col_px, value);
            }
        }
    }
}

int CountClass(const ls2k::legacy::BEVSimpleImage& image, ls2k::legacy::BEVSimplePixelClass klass) {
    return static_cast<int>(std::count(image.classes.begin(), image.classes.end(), klass));
}

int CountRasterClass(const ls2k::legacy::BEVElementRasterFrame& raster,
                     ls2k::port::BEVElementRasterCellClass klass) {
    return static_cast<int>(std::count(raster.classes.begin(), raster.classes.end(), klass));
}

int CountPresentPathPoints(const ls2k::port::BEVReferencePath& reference,
                           ls2k::port::BEVPathPointSource source) {
    int count = 0;
    for (const ls2k::port::BEVPathSample& sample : reference.sampled_path) {
        if (sample.present && sample.source == source) {
            ++count;
        }
    }
    return count;
}

int CountPresentPathPoints(const ls2k::port::BEVReferencePath& reference) {
    int count = 0;
    for (const ls2k::port::BEVPathSample& sample : reference.sampled_path) {
        if (sample.present) {
            ++count;
        }
    }
    return count;
}

ls2k::legacy::BEVElementRasterFrame MakeWhiteElementRaster(
    const ls2k::port::RuntimeParameters& params) {
    ls2k::legacy::BEVElementRasterFrame raster{};
    raster.valid = true;
    raster.enabled = true;
    raster.width = 101;
    raster.lateral_limit_m = params.bev_geometry.search_lateral_limit_m;
    raster.forward_max_m = params.bev_geometry.forward_samples_m.back();
    const float metric_width = std::max(1.0e-4F, raster.lateral_limit_m * 2.0F);
    const float scale_px_per_m = static_cast<float>(raster.width) / metric_width;
    raster.height = std::max(2, static_cast<int>(std::lround(raster.forward_max_m * scale_px_per_m)));
    const std::size_t cell_count = static_cast<std::size_t>(raster.width * raster.height);
    raster.classes.assign(cell_count, ls2k::port::BEVElementRasterCellClass::kWhite);
    raster.gray.assign(cell_count, 255U);
    raster.projection_states.assign(cell_count,
                                    ls2k::port::BEVElementRasterProjectionState::kSampleable);
    return raster;
}

void PaintRasterMetricCell(ls2k::legacy::BEVElementRasterFrame& raster,
                           const ls2k::port::BEVPoint& point,
                           ls2k::port::BEVElementRasterCellClass klass) {
    int x = 0;
    int y = 0;
    Expect(raster.MetricToCell(point, x, y), "test metric cell must be inside raster");
    raster.classes[raster.Index(x, y)] = klass;
}

void TestBevClassificationAndRowIntervals() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.18F, 0.18F, 255U);

    ls2k::legacy::BEVSampleProjectionLut lut{};
    const ls2k::legacy::BEVSimplePerceptionResult result =
        ls2k::legacy::RunBEVSimplePerception(frame.View(1, 1), 100, params, projector, &lut);
    const ls2k::legacy::BEVSimpleImage debug_bev =
        ls2k::legacy::BuildDebugDenseBevImage(frame.View(1, 1), 100, params, projector);

    Expect(debug_bev.valid, "debug API must generate a BEV image");
    Expect(CountClass(debug_bev, ls2k::legacy::BEVSimplePixelClass::kWhite) > 0,
           "drawn BEV stripe must classify as white in the debug BEV image");
    Expect(CountClass(debug_bev, ls2k::legacy::BEVSimplePixelClass::kBlack) > 0,
           "background must classify as black in the BEV image");
    Expect(result.rows.size() == ls2k::port::kBevReferenceSampleCount,
           "row scanner must scan the configured BEV forward samples");

    bool saw_interval = false;
    bool saw_row_support_stats = false;
    for (const ls2k::legacy::BEVSimpleRowScan& row : result.rows) {
        saw_interval = saw_interval || !row.intervals.empty();
        saw_row_support_stats = saw_row_support_stats ||
                                (row.sampleable_count > 0U &&
                                 row.sampleable_width_m > 0.0F &&
                                 row.white_count + row.black_count + row.unknown_count ==
                                     row.sampleable_count);
    }
    Expect(saw_interval, "drawn BEV stripe must expose white interval facts");
    Expect(saw_row_support_stats,
           "row scanner must expose sample support stats without changing reference facts");
    Expect(ls2k::legacy::EvaluateReferenceUsability(result.reference_path, params).usable,
           "continuous white intervals must produce usable current facts");
    Expect(CountPresentPathPoints(result.reference_path,
                                  ls2k::port::BEVPathPointSource::kIntervalCenter) >= 3,
           "reference white points must explicitly come from interval centers");
}

void TestUnknownBandUsesCenterBrightnessOnly() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(112U);

    ls2k::legacy::BEVSampleProjectionLut lut{};
    const ls2k::legacy::BEVSimplePerceptionResult result =
        ls2k::legacy::RunBEVSimplePerception(frame.View(1, 1), 100, params, projector, &lut);
    const ls2k::legacy::BEVSimpleImage debug_bev =
        ls2k::legacy::BuildDebugDenseBevImage(frame.View(1, 1), 100, params, projector);

    Expect(debug_bev.valid, "uniform frame must still produce a debug BEV image");
    Expect(CountClass(debug_bev, ls2k::legacy::BEVSimplePixelClass::kUnknown) > 0,
           "near-threshold BEV pixels must classify as unknown");
    Expect(!ls2k::legacy::EvaluateReferenceUsability(result.reference_path, params).usable,
           "unknown pixels must not be promoted into white interval reference points");
}

void TestElementRasterClassificationAndCoordinates() {
    ls2k::port::RuntimeParameters params{};
    params.bev_element_raster.width = 320;
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.18F, 0.18F, 255U);

    ls2k::legacy::BEVElementRasterLut lut{};
    const ls2k::legacy::BEVElementRasterFrame raster =
        ls2k::legacy::BuildBEVElementRaster(frame.View(1, 1), 100, params, projector, &lut);

    Expect(raster.valid, "enabled element raster must build from a valid projector and frame");
    Expect(raster.width == params.bev_element_raster.width,
           "element raster width must follow BEV_ELEMENT_RASTER.WIDTH");
    Expect(raster.height > 2, "element raster height must be derived from metric aspect ratio");
    Expect(CountRasterClass(raster, ls2k::port::BEVElementRasterCellClass::kWhite) > 0,
           "drawn BEV stripe must classify as white in the element raster");
    Expect(CountRasterClass(raster, ls2k::port::BEVElementRasterCellClass::kBlack) > 0,
           "background must classify as black in the element raster");

    int cell_x = 0;
    int cell_y = 0;
    Expect(raster.MetricToCell({0.60F, 0.0F}, cell_x, cell_y),
           "metric point inside raster must map to a cell");
    const ls2k::port::BEVPoint round_trip = raster.CellToMetric(cell_x, cell_y);
    Expect(std::abs(round_trip.lateral_m) < 0.01F,
           "center metric point must round-trip near raster center");

    params.bev_element_raster.enabled = false;
    const ls2k::legacy::BEVElementRasterFrame disabled =
        ls2k::legacy::BuildBEVElementRaster(frame.View(1, 1), 100, params, projector, &lut);
    Expect(!disabled.valid, "disabled element raster must be unavailable");
    Expect(disabled.classes.empty(), "disabled element raster must expose no cells");
}

void TestElementRasterSegmentTouchesBlack() {
    ls2k::legacy::BEVElementRasterFrame raster{};
    raster.valid = true;
    raster.enabled = true;
    raster.width = 5;
    raster.height = 5;
    raster.lateral_limit_m = 0.5F;
    raster.forward_max_m = 1.0F;
    raster.classes.assign(25U, ls2k::port::BEVElementRasterCellClass::kWhite);
    raster.gray.assign(25U, 255U);
    raster.projection_states.assign(25U, ls2k::port::BEVElementRasterProjectionState::kSampleable);
    raster.classes[raster.Index(2, 2)] = ls2k::port::BEVElementRasterCellClass::kBlack;

    Expect(raster.SegmentTouchesBlackCells(0, 0, 4, 4),
           "cell segment crossing a black cell must report black contact");
    Expect(!raster.SegmentTouchesBlackCells(0, 4, 1, 4),
           "cell segment without black cells must remain clear");
    Expect(raster.SegmentTouchesBlack({1.0F, -0.5F}, {0.0F, 0.5F}),
           "metric segment crossing a black cell must report black contact");
}

void TestBevGeometryControlsWideImageScan() {
    ls2k::port::RuntimeParameters params{};
    params.bev_geometry.search_lateral_limit_m = 0.85F;
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, 0.50F, 0.70F, 255U);

    ls2k::legacy::BEVSampleProjectionLut lut{};
    const ls2k::legacy::BEVSimplePerceptionResult result =
        ls2k::legacy::RunBEVSimplePerception(frame.View(1, 1), 100, params, projector, &lut);

    bool saw_wide_right_interval = false;
    for (const ls2k::legacy::BEVSimpleRowScan& row : result.rows) {
        for (const ls2k::legacy::BEVSimpleWhiteInterval& interval : row.intervals) {
            saw_wide_right_interval = saw_wide_right_interval || interval.center_m > 0.45F;
        }
    }
    Expect(saw_wide_right_interval,
           "BEV row scanning must use the configured BEV image extent");
}

void TestHoldIsExplicitNonVisualSource() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.18F, 0.18F, 255U);

    ls2k::legacy::BEVSampleProjectionLut lut{};
    const ls2k::legacy::BEVSimplePerceptionResult first =
        ls2k::legacy::RunBEVSimplePerception(frame.View(1, 1), 100, params, projector, &lut);
    const ls2k::port::ReferenceUsability first_usability =
        ls2k::legacy::EvaluateReferenceUsability(first.reference_path, params);
    Expect(first_usability.usable, "first frame must produce usable visual facts");
    const ls2k::port::ReferenceHoldState first_hold =
        ls2k::legacy::MakeReferenceHoldState(first.reference_path, params);

    ls2k::port::LegacyCameraFrame blank = MakeFrame(0U);
    const ls2k::legacy::BEVSimplePerceptionResult blank_facts =
        ls2k::legacy::RunBEVSimplePerception(blank.View(2, 2), 100, params, projector, &lut);
    const ls2k::port::ReferenceUsability blank_usability =
        ls2k::legacy::EvaluateReferenceUsability(blank_facts.reference_path, params);
    Expect(!blank_usability.usable, "blank current frame must be selected only if hold is unavailable");
    const ls2k::port::ReferenceContinuityResult held =
        ls2k::legacy::BuildReferenceHoldCandidate(first_hold, params);

    Expect(held.hold_selected, "blank frame may explicitly hold the previous reference");
    Expect(ls2k::legacy::EvaluateReferenceUsability(held.reference_path, params).usable,
           "held reference facts must still pass selected usability");
    Expect(held.reference_path.mode == ls2k::port::ReferenceMode::kHoldLast,
           "held reference must use hold mode");
    Expect(CountPresentPathPoints(held.reference_path, ls2k::port::BEVPathPointSource::kHold) >= 3,
           "held points must be marked as hold, not current-frame visual evidence");
    Expect(first_hold.last_reference[0].source == ls2k::port::BEVPathPointSource::kIntervalCenter,
           "hold output must not overwrite visual memory with hold source");

    ls2k::port::ReferenceHoldState source_free_memory = first_hold;
    for (ls2k::port::BEVPathSample& sample : source_free_memory.last_reference) {
        if (sample.present) {
            sample.source = ls2k::port::BEVPathPointSource::kNone;
        }
    }
    const ls2k::port::ReferenceContinuityResult held_from_geometry =
        ls2k::legacy::BuildReferenceHoldCandidate(source_free_memory, params);
    Expect(held_from_geometry.hold_selected,
           "hold continuity must use present finite geometry, not source metadata");
    Expect(CountPresentPathPoints(held_from_geometry.reference_path,
                                  ls2k::port::BEVPathPointSource::kHold) >= 3,
           "hold output must rewrite source metadata to hold");

    ls2k::port::ReferenceHoldState invalid_geometry_memory = first_hold;
    invalid_geometry_memory.last_reference[0].point.lateral_m =
        std::numeric_limits<float>::quiet_NaN();
    const ls2k::port::ReferenceContinuityResult rejected_invalid_geometry =
        ls2k::legacy::BuildReferenceHoldCandidate(invalid_geometry_memory, params);
    Expect(!rejected_invalid_geometry.hold_selected,
           "hold continuity must reject non-finite leading geometry");

    ls2k::port::RuntimeParameters changed_geometry = params;
    changed_geometry.bev_geometry.lateral_step_m *= 0.5F;
    const ls2k::port::ReferenceContinuityResult rejected =
        ls2k::legacy::BuildReferenceHoldCandidate(first_hold, changed_geometry);
    Expect(!rejected.hold_selected, "geometry identity change must reject hold output");
}

void TestReferencePathStartsAtIndexZeroAndUsesRasterConnectivity() {
    ls2k::port::RuntimeParameters params{};
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows(ls2k::port::kBevReferenceSampleCount);
    for (std::size_t index = 0; index < rows.size(); ++index) {
        rows[index].valid = true;
        rows[index].forward_m = params.bev_geometry.forward_samples_m[index];
    }

    auto add_interval = [&](std::size_t index, float center) {
        ls2k::legacy::BEVSimpleWhiteInterval interval{};
        interval.forward_m = params.bev_geometry.forward_samples_m[index];
        interval.left_m = center - 0.08F;
        interval.right_m = center + 0.08F;
        interval.center_m = center;
        interval.width_m = 0.16F;
        rows[index].intervals.push_back(interval);
    };

    add_interval(3, 0.0F);
    add_interval(4, 0.0F);
    add_interval(5, 0.0F);
    ls2k::legacy::BEVElementRasterFrame raster = MakeWhiteElementRaster(params);
    const ls2k::port::BEVReferencePath no_near =
        ls2k::legacy::BuildReferencePath(rows, params, &raster);
    Expect(!ls2k::legacy::EvaluateReferenceUsability(no_near, params).usable,
           "far intervals must not be connected back when index zero is missing");
    Expect(CountPresentPathPoints(no_near, ls2k::port::BEVPathPointSource::kIntervalCenter) == 0,
           "builder must publish no visual points before a leading segment starts at index zero");

    for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
        row.intervals.clear();
    }
    add_interval(0, 0.0F);
    add_interval(1, 0.0F);
    add_interval(2, 0.0F);
    add_interval(5, 0.0F);
    const ls2k::port::BEVReferencePath connected =
        ls2k::legacy::BuildReferencePath(rows, params, &raster);
    Expect(ls2k::legacy::EvaluateReferenceUsability(connected, params).usable,
           "first three leading intervals satisfy the configured control minimum");
    Expect(CountPresentPathPoints(connected) == 6,
           "clear raster connectivity must keep the far interval in the leading path");
    Expect(CountPresentPathPoints(connected,
                                  ls2k::port::BEVPathPointSource::kRasterConnected) == 2,
           "missing rows bridged by clear raster connectivity must be marked as raster connected");
    Expect(connected.sampled_path[5].present &&
               connected.sampled_path[5].source == ls2k::port::BEVPathPointSource::kIntervalCenter,
           "far observed interval remains an interval-center fact after connectivity succeeds");

    PaintRasterMetricCell(raster,
                          {0.5F * (params.bev_geometry.forward_samples_m[2] +
                                   params.bev_geometry.forward_samples_m[5]),
                           0.0F},
                          ls2k::port::BEVElementRasterCellClass::kBlack);
    const ls2k::port::BEVReferencePath blocked =
        ls2k::legacy::BuildReferencePath(rows, params, &raster);
    Expect(CountPresentPathPoints(blocked) == 3,
           "black raster cells on the connecting line must stop before the far interval");
    Expect(!blocked.sampled_path[5].present,
           "black-blocked far interval must not enter the leading path");

    for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
        row.intervals.clear();
    }
    raster = MakeWhiteElementRaster(params);
    add_interval(0, 0.0F);
    add_interval(1, 0.35F);
    const ls2k::port::BEVReferencePath lateral_jump =
        ls2k::legacy::BuildReferencePath(rows, params, &raster);
    Expect(lateral_jump.sampled_path[1].present,
           "lateral jump threshold must not reject a clear raster-connected interval");
}

void TestProjectionLutMatchesUncachedSparseScanAndRebuildsOnIdentityChange() {
    ls2k::port::RuntimeParameters params{};
    ls2k::legacy::BEVProjector projector = MakeProjector(params);
    ls2k::port::LegacyCameraFrame frame = MakeFrame(0U);
    DrawVehicleStripe(frame, projector, params, -0.12F, 0.16F, 255U);

    ls2k::legacy::BEVSampleProjectionLut lut{};
    const ls2k::legacy::BEVSimplePerceptionResult cached =
        ls2k::legacy::RunBEVSimplePerception(frame.View(7, 7), 100, params, projector, &lut);
    const ls2k::legacy::BEVSimplePerceptionResult uncached =
        ls2k::legacy::RunBEVSimplePerception(frame.View(7, 7), 100, params, projector, nullptr);

    Expect(lut.valid, "sparse projection LUT must be built for a valid frame/projector identity");
    Expect(lut.entries.size() ==
               ls2k::port::kBevReferenceSampleCount * lut.lateral_sample_count,
           "LUT entry count must be forward rows times lateral samples");
    bool saw_sampleable = false;
    bool saw_non_sampleable = false;
    for (const ls2k::legacy::BEVSampleProjectionEntry& entry : lut.entries) {
        saw_sampleable =
            saw_sampleable || entry.state == ls2k::legacy::BEVSampleProjectionState::kSampleable;
        saw_non_sampleable =
            saw_non_sampleable || entry.state != ls2k::legacy::BEVSampleProjectionState::kSampleable;
    }
    Expect(saw_sampleable, "LUT must mark in-frame projected samples as sampleable");
    Expect(saw_non_sampleable, "LUT must preserve out-of-frame or failed projection state separately");
    for (std::size_t index = 0; index < cached.rows.size(); ++index) {
        Expect(cached.rows[index].intervals.size() == uncached.rows[index].intervals.size(),
               "cached and uncached sparse scans must expose the same interval count");
        if (!cached.rows[index].intervals.empty()) {
            const ls2k::legacy::BEVSimpleWhiteInterval& lhs = cached.rows[index].intervals.front();
            const ls2k::legacy::BEVSimpleWhiteInterval& rhs = uncached.rows[index].intervals.front();
            const float center_tol = 0.5F * params.bev_geometry.lateral_step_m + 1.0e-4F;
            const float width_tol = 1.0F * params.bev_geometry.lateral_step_m + 1.0e-4F;
            Expect(std::abs(lhs.center_m - rhs.center_m) <= center_tol,
                   "LUT and non-LUT interval centers must match within sparse half-step tolerance");
            Expect(std::abs(lhs.width_m - rhs.width_m) <= width_tol,
                   "LUT and non-LUT interval widths must match within sparse one-step tolerance");
        }
    }

    const std::uint64_t previous_entry_count = static_cast<std::uint64_t>(lut.entries.size());
    params.bev_geometry.lateral_step_m *= 0.5F;
    Expect(ls2k::legacy::EnsureBEVSampleProjectionLut(lut, frame.View(7, 7), params, projector),
           "LUT must rebuild successfully after sampling identity changes");
    Expect(static_cast<std::uint64_t>(lut.entries.size()) != previous_entry_count,
           "lateral step identity change must rebuild LUT with a different entry count");
}

}  // namespace

int main() {
    try {
        TestBevClassificationAndRowIntervals();
        TestUnknownBandUsesCenterBrightnessOnly();
        TestElementRasterClassificationAndCoordinates();
        TestElementRasterSegmentTouchesBlack();
        TestBevGeometryControlsWideImageScan();
        TestHoldIsExplicitNonVisualSource();
        TestReferencePathStartsAtIndexZeroAndUsesRasterConnectivity();
        TestProjectionLutMatchesUncachedSparseScanAndRebuildsOnIdentityChange();
    } catch (const TestFailure& failure) {
        std::cerr << "bev_simple_perception_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "bev_simple_perception_test passed\n";
    return EXIT_SUCCESS;
}
