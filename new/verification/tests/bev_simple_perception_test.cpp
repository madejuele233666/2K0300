#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "legacy/steering_bev_projector.hpp"
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
    for (const ls2k::legacy::BEVSimpleRowScan& row : result.rows) {
        saw_interval = saw_interval || !row.intervals.empty();
    }
    Expect(saw_interval, "drawn BEV stripe must expose white interval facts");
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

void TestReferencePathStartsAtIndexZeroAndStopsAtFirstGap() {
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
    const ls2k::port::BEVReferencePath no_near =
        ls2k::legacy::BuildReferencePath(rows, params);
    Expect(!ls2k::legacy::EvaluateReferenceUsability(no_near, params).usable,
           "far intervals must not be connected back when index zero is missing");
    Expect(CountPresentPathPoints(no_near, ls2k::port::BEVPathPointSource::kIntervalCenter) == 0,
           "strict builder must publish no visual points before a leading segment starts at index zero");

    for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
        row.intervals.clear();
    }
    add_interval(0, 0.0F);
    add_interval(1, 0.0F);
    add_interval(2, 0.0F);
    add_interval(5, 0.0F);
    const ls2k::port::BEVReferencePath stopped =
        ls2k::legacy::BuildReferencePath(rows, params);
    Expect(ls2k::legacy::EvaluateReferenceUsability(stopped, params).usable,
           "first three leading intervals satisfy the configured control minimum");
    Expect(CountPresentPathPoints(stopped, ls2k::port::BEVPathPointSource::kIntervalCenter) == 3,
           "strict builder must stop at the first gap and not publish far reappearing intervals");
    Expect(!stopped.sampled_path[5].present,
           "strict builder must not reconnect far points across a gap");
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
        TestBevGeometryControlsWideImageScan();
        TestHoldIsExplicitNonVisualSource();
        TestReferencePathStartsAtIndexZeroAndStopsAtFirstGap();
        TestProjectionLutMatchesUncachedSparseScanAndRebuildsOnIdentityChange();
    } catch (const TestFailure& failure) {
        std::cerr << "bev_simple_perception_test failed: " << failure.message << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "bev_simple_perception_test passed\n";
    return EXIT_SUCCESS;
}
