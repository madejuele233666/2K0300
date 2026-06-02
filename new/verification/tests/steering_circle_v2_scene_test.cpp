#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include "runtime/detail/steering_circle_v2_internal.hpp"
#include "runtime/runtime_state.hpp"
#include "runtime/steering_circle_v2_reference_adapter.hpp"
#include "runtime/steering_circle_v2_scene.hpp"

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "steering_circle_v2_scene_test failed: " << message << '\n';
        std::exit(1);
    }
}

ls2k::legacy::BEVSimpleRowScan Row(float forward,
                                   float left_a,
                                   float left_b,
                                   float right_a,
                                   float right_b) {
    ls2k::legacy::BEVSimpleRowScan row{};
    row.valid = true;
    row.forward_m = forward;
    row.sampleable_count = 32;
    row.white_count = 8;
    row.black_count = 24;
    ls2k::legacy::BEVSimpleWhiteInterval left{};
    left.forward_m = forward;
    left.left_m = left_a;
    left.right_m = left_b;
    left.center_m = 0.5F * (left_a + left_b);
    left.width_m = left_b - left_a;
    ls2k::legacy::BEVSimpleWhiteInterval right{};
    right.forward_m = forward;
    right.left_m = right_a;
    right.right_m = right_b;
    right.center_m = 0.5F * (right_a + right_b);
    right.width_m = right_b - right_a;
    row.intervals.push_back(left);
    row.intervals.push_back(right);
    return row;
}

ls2k::legacy::BEVSimpleRowScan SingleIntervalRow(float forward, float left_m, float right_m) {
    ls2k::legacy::BEVSimpleRowScan row{};
    row.valid = true;
    row.forward_m = forward;
    row.sampleable_count = 65;
    row.white_count = 32;
    row.black_count = 33;
    ls2k::legacy::BEVSimpleWhiteInterval interval{};
    interval.forward_m = forward;
    interval.left_m = left_m;
    interval.right_m = right_m;
    interval.center_m = 0.5F * (left_m + right_m);
    interval.width_m = right_m - left_m;
    row.intervals.push_back(interval);
    return row;
}

void SetSampleableSpan(ls2k::legacy::BEVSimpleRowScan& row,
                       float left_m,
                       float right_m) {
    row.sampleable_left_m = left_m;
    row.sampleable_right_m = right_m;
    row.sampleable_width_m = right_m - left_m;
    row.sampleable_count = 65;
}

std::vector<ls2k::legacy::BEVSimpleRowScan> RowsFromReach(
    const std::vector<float>& left_reach_near_to_far,
    const std::vector<float>& right_reach_near_to_far) {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows;
    const std::size_t count = std::min(left_reach_near_to_far.size(),
                                       right_reach_near_to_far.size());
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        ls2k::legacy::BEVSimpleRowScan row{};
        row.valid = true;
        row.forward_m = 0.06F + static_cast<float>(index) * 0.06F;
        row.sampleable_count = 65;
        row.white_count = 32;
        row.black_count = 33;
        ls2k::legacy::BEVSimpleWhiteInterval interval{};
        interval.forward_m = row.forward_m;
        interval.left_m = -left_reach_near_to_far[index];
        interval.right_m = right_reach_near_to_far[index];
        interval.center_m = 0.5F * (interval.left_m + interval.right_m);
        interval.width_m = interval.right_m - interval.left_m;
        row.intervals.push_back(interval);
        rows.push_back(row);
    }
    return rows;
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftCircleRows() {
    std::vector<float> left{
        0.20F, 0.20F, 0.20F, 0.20F, 0.42F, 0.50F,
        0.58F, 0.64F, 0.68F, 0.70F, 0.68F, 0.64F,
        0.58F, 0.50F, 0.44F, 0.42F, 0.46F, 0.52F,
        0.58F, 0.62F, 0.58F, 0.52F, 0.48F, 0.44F};
    std::vector<float> right(24U, 0.20F);
    return RowsFromReach(left, right);
}

std::vector<ls2k::legacy::BEVSimpleRowScan> RightCircleRows() {
    std::vector<float> left(24U, 0.20F);
    std::vector<float> right{
        0.20F, 0.20F, 0.20F, 0.20F, 0.42F, 0.50F,
        0.58F, 0.64F, 0.68F, 0.70F, 0.68F, 0.64F,
        0.58F, 0.50F, 0.44F, 0.42F, 0.46F, 0.52F,
        0.58F, 0.62F, 0.58F, 0.52F, 0.48F, 0.44F};
    return RowsFromReach(left, right);
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftEntryRows() {
    return RowsFromReach({0.20F, 0.30F, 0.36F, 0.42F, 0.42F, 0.42F},
                         {0.22F, 0.22F, 0.22F, 0.22F, 0.22F, 0.22F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> RightEntryRows() {
    return RowsFromReach({0.22F, 0.22F, 0.22F, 0.22F, 0.22F, 0.22F},
                         {0.20F, 0.30F, 0.36F, 0.42F, 0.42F, 0.42F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftEntryRowsWithBentOpposite() {
    return RowsFromReach({0.20F, 0.30F, 0.36F, 0.42F, 0.42F, 0.42F},
                         {0.20F, 0.42F, 0.12F, 0.34F, 0.20F, 0.20F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftEntryRowsWithBottomGap() {
    return {
        SingleIntervalRow(0.06F, -0.20F, 0.22F),
        SingleIntervalRow(0.12F, -0.30F, 0.22F),
        SingleIntervalRow(0.68F, -0.42F, 0.22F),
        SingleIntervalRow(0.74F, -0.50F, 0.22F),
    };
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftEntryRowsWithoutNearSupport() {
    return {
        SingleIntervalRow(0.30F, -0.20F, 0.22F),
        SingleIntervalRow(0.36F, -0.30F, 0.22F),
        SingleIntervalRow(0.42F, -0.36F, 0.22F),
        SingleIntervalRow(0.48F, -0.42F, 0.22F),
    };
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftEntryRowsWithExpansionAfterRoiStart() {
    return RowsFromReach({0.20F, 0.20F, 0.20F, 0.20F,
                          0.20F, 0.30F, 0.36F, 0.42F},
                         {0.22F, 0.22F, 0.22F, 0.22F,
                          0.22F, 0.22F, 0.22F, 0.22F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> LeftInnerTraceRows() {
    return RowsFromReach({0.20F, 0.20F, 0.35F, 0.35F, 0.35F, 0.20F},
                         {0.20F, 0.20F, 0.20F, 0.20F, 0.20F, 0.20F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> RightInnerTraceRows() {
    return RowsFromReach({0.20F, 0.20F, 0.20F, 0.20F, 0.20F, 0.20F},
                         {0.20F, 0.20F, 0.35F, 0.35F, 0.35F, 0.20F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> StraightRows() {
    return RowsFromReach(std::vector<float>(24U, 0.20F),
                         std::vector<float>(24U, 0.20F));
}

std::vector<ls2k::legacy::BEVSimpleRowScan> FalseRightBendRows() {
    const float forward[] = {0.061F,    0.123565F, 0.18613F, 0.248696F,
                             0.311261F, 0.373826F, 0.436391F, 0.498957F,
                             0.561522F, 0.624087F, 0.686652F, 0.749217F,
                             0.811783F, 0.874348F, 0.936913F, 0.999478F,
                             1.06204F,  1.12461F,  1.18717F,  1.24974F,
                             1.3123F,   1.37487F,  1.43744F,  1.5F};
    const float left[] = {-0.14F, -0.12F, -0.10F, -0.0600001F, -0.000000119209F,
                          0.0599999F, 0.0999999F, 0.12F, 0.14F, 0.12F,
                          0.0999999F, 0.04F, -0.0400001F, -0.08F, -0.14F,
                          -0.18F, -0.20F, -0.22F, -0.24F, -0.24F,
                          -0.26F, -0.26F, -0.24F, 0.82F};
    const float right[] = {0.22F, 0.30F, 0.36F, 0.42F, 0.48F, 0.52F,
                           0.54F, 0.56F, 0.56F, 0.56F, 0.56F, 0.54F,
                           0.52F, 0.50F, 0.44F, 0.38F, 0.30F, 0.24F,
                           0.22F, 0.20F, 0.20F, 0.24F, 0.26F, 1.60F};
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows;
    constexpr std::size_t count = sizeof(forward) / sizeof(forward[0]);
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        rows.push_back(SingleIntervalRow(forward[index], left[index], right[index]));
    }
    return rows;
}

std::vector<ls2k::legacy::BEVSimpleRowScan> StraightRightExpansionRows() {
    return RowsFromReach({0.20F, 0.20F, 0.20F, 0.20F, 0.20F, 0.20F},
                         {0.22F, 0.30F, 0.34F, 0.38F, 0.42F, 0.46F});
}

std::vector<ls2k::legacy::BEVSimpleRowScan> RightDetachedArtifactRows() {
    const std::vector<float> main_right{
        0.20F, 0.16F, 0.12F, 0.08F, 0.04F, 0.00F,
        -0.04F, -0.08F, -0.12F, -0.16F, -0.20F, -0.24F,
    };
    const std::vector<float> artifact_left{
        0.28F, 0.34F, 0.40F, 0.46F, 0.52F, 0.58F,
        0.64F, 0.70F, 0.76F, 0.82F, 0.88F, 0.94F,
    };
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows;
    rows.reserve(main_right.size());
    for (std::size_t index = 0; index < main_right.size(); ++index) {
        const float forward = 0.06F + static_cast<float>(index) * 0.06F;
        rows.push_back(Row(forward,
                           -0.22F,
                           main_right[index],
                           artifact_left[index],
                           artifact_left[index] + 0.08F));
    }
    return rows;
}

ls2k::legacy::BEVSimpleRowScan InvalidRow(float forward) {
    ls2k::legacy::BEVSimpleRowScan row{};
    row.valid = false;
    row.forward_m = forward;
    return row;
}

ls2k::port::BEVReferencePath CenterPath() {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    for (std::size_t index = 0; index < 3U; ++index) {
        ls2k::port::BEVPathSample& sample = path.sampled_path[index];
        sample.present = true;
        sample.point.forward_m = 0.3F + 0.1F * static_cast<float>(index);
        sample.point.lateral_m = 0.0F;
        sample.confidence = 0.9F;
        sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    }
    return path;
}

ls2k::port::BEVReferencePath EntryCenterPath() {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    for (std::size_t index = 0; index < 3U; ++index) {
        ls2k::port::BEVPathSample& sample = path.sampled_path[index];
        sample.present = true;
        sample.point.forward_m = 0.06F + 0.06F * static_cast<float>(index);
        sample.point.lateral_m = 0.0F;
        sample.confidence = 0.9F;
        sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    }
    return path;
}

ls2k::port::BEVReferencePath FirstIntervalCenterPath(
    const std::vector<ls2k::legacy::BEVSimpleRowScan>& rows) {
    ls2k::port::BEVReferencePath path{};
    path.mode = ls2k::port::ReferenceMode::kIntervalCenter;
    const std::size_t count =
        std::min(rows.size(), path.sampled_path.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index].intervals.empty()) {
            continue;
        }
        const ls2k::legacy::BEVSimpleWhiteInterval& interval =
            rows[index].intervals.front();
        ls2k::port::BEVPathSample& sample = path.sampled_path[index];
        sample.present = true;
        sample.point.forward_m = rows[index].forward_m;
        sample.point.lateral_m = interval.center_m;
        sample.confidence = 0.9F;
        sample.source = ls2k::port::BEVPathPointSource::kIntervalCenter;
    }
    return path;
}

struct YawContext {
    float yaw = 0.0F;
};

bool QueryYaw(void* context, uint64_t, uint64_t, float& out_delta_rad) {
    out_delta_rad = static_cast<YawContext*>(context)->yaw;
    return true;
}

bool QueryYawUnavailable(void*, uint64_t, uint64_t, float&) {
    return false;
}

ls2k::runtime::SceneFrameView FrameWithCenterPath(
    std::vector<ls2k::legacy::BEVSimpleRowScan>& rows,
    float yaw,
    const ls2k::port::BEVReferencePath& center_path,
    float road_half_width_m = 0.2F) {
    static YawContext yaw_context{};
    yaw_context.yaw = yaw;
    ls2k::runtime::SceneFrameView frame{};
    frame.rows.rows = ls2k::runtime::ConstArrayView<ls2k::legacy::BEVSimpleRowScan>(
        rows.data(), rows.size());
    ls2k::runtime::OrdinaryRoadModel ordinary_road{};
    ordinary_road.center_path = center_path;
    ordinary_road.half_width.value_m = road_half_width_m;
    frame.ordinary_road = ordinary_road;
    frame.motion_arc = ls2k::runtime::MotionArcView(&yaw_context, QueryYaw);
    frame.stamp.capture_time_ms = 200;
    return frame;
}

ls2k::runtime::SceneFrameView Frame(std::vector<ls2k::legacy::BEVSimpleRowScan>& rows,
                                    float yaw) {
    return FrameWithCenterPath(rows, yaw, CenterPath());
}

ls2k::runtime::SceneFrameView FrameWithoutOrdinaryRoad(
    std::vector<ls2k::legacy::BEVSimpleRowScan>& rows,
    float yaw) {
    ls2k::runtime::SceneFrameView frame = Frame(rows, yaw);
    frame.ordinary_road.reset();
    return frame;
}

ls2k::runtime::detail::CircleV2Events EventsFor(
    const ls2k::runtime::SceneFrameView& frame,
    const ls2k::runtime::CircleV2Memory& prior,
    const ls2k::runtime::CircleV2Params& params) {
    const ls2k::runtime::detail::CircleSideExpansionObservation expansion =
        ls2k::runtime::detail::ObserveCircleSideExpansion(frame, params);
    return ls2k::runtime::detail::ObserveCircleV2Events(frame, expansion, prior, params);
}

void TestPhase1CueParity() {
    ls2k::runtime::CircleV2Memory idle{};
    ls2k::runtime::CircleV2Params params{};

    std::vector<ls2k::legacy::BEVSimpleRowScan> left_rows = LeftCircleRows();
    const ls2k::runtime::detail::CircleV2Events left_events =
        EventsFor(Frame(left_rows, 0.0F), idle, params);
    Expect(left_events.detected_dir == ls2k::runtime::CircleDir::kLeft,
           "Idle Phase1 cue must preserve old left-circle direction");

    std::vector<ls2k::legacy::BEVSimpleRowScan> right_rows = RightCircleRows();
    const ls2k::runtime::detail::CircleV2Events right_events =
        EventsFor(Frame(right_rows, 0.0F), idle, params);
    Expect(right_events.detected_dir == ls2k::runtime::CircleDir::kRight,
           "Idle Phase1 cue must preserve old right-circle direction");

    std::vector<ls2k::legacy::BEVSimpleRowScan> straight_rows = StraightRows();
    const ls2k::runtime::detail::CircleV2Events straight_events =
        EventsFor(Frame(straight_rows, 0.0F), idle, params);
    Expect(straight_events.detected_dir == ls2k::runtime::CircleDir::kNone,
           "Idle Phase1 cue must preserve old none result");
}

void TestFalseRightBendDoesNotPassOppositeStraightGate() {
    ls2k::runtime::CircleV2Memory idle{};
    ls2k::runtime::CircleV2Params params{};
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = FalseRightBendRows();

    const ls2k::runtime::detail::CircleSideExpansionObservation expansion =
        ls2k::runtime::detail::ObserveCircleSideExpansion(Frame(rows, 0.0F), params);
    Expect(expansion.right_phase1_open,
           "false right bend fixture must preserve the right-side Phase1 expansion cue");
    Expect(expansion.detected_dir == ls2k::runtime::CircleDir::kNone,
           "bent opposite boundary must block right circle Phase1 cue");

    const ls2k::runtime::detail::CircleV2Events events =
        EventsFor(Frame(rows, 0.0F), idle, params);
    Expect(events.detected_dir == ls2k::runtime::CircleDir::kNone,
           "Idle must not enter Approach on a normal right bend");
}

void TestStraightSameSideExpansionDoesNotCreateCircleCue() {
    ls2k::runtime::CircleV2Memory idle{};
    ls2k::runtime::CircleV2Params params{};
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows =
        StraightRightExpansionRows();

    const ls2k::runtime::detail::CircleSideExpansionObservation expansion =
        ls2k::runtime::detail::ObserveCircleSideExpansion(Frame(rows, 0.0F), params);
    Expect(expansion.right_phase1_open,
           "straight right expansion fixture must preserve the right-side Phase1 expansion cue");
    Expect(expansion.detected_dir == ls2k::runtime::CircleDir::kNone,
           "same-side straight expansion must not become a right circle Phase1 cue");

    const ls2k::runtime::detail::CircleV2Events events =
        EventsFor(Frame(rows, 0.0F), idle, params);
    Expect(events.detected_dir == ls2k::runtime::CircleDir::kNone,
           "Idle must reject a straight same-side expansion");
}

void TestDisconnectedFarSideArtifactDoesNotCreateCircleCue() {
    ls2k::runtime::CircleV2Memory idle{};
    ls2k::runtime::CircleV2Params params{};
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows =
        RightDetachedArtifactRows();
    const ls2k::port::BEVReferencePath center_path =
        FirstIntervalCenterPath(rows);
    const ls2k::runtime::SceneFrameView frame =
        FrameWithCenterPath(rows, 0.0F, center_path);

    const ls2k::runtime::detail::CircleSideExpansionObservation expansion =
        ls2k::runtime::detail::ObserveCircleSideExpansion(frame, params);
    Expect(!expansion.right_phase1_open,
           "disconnected far-side white artifacts must not create right-side Phase1 opening");

    const ls2k::runtime::detail::CircleV2Events events =
        EventsFor(frame, idle, params);
    Expect(events.detected_dir == ls2k::runtime::CircleDir::kNone,
           "road-connected boundary observation must reject detached artifact as right circle");
}

void TestApproachWaitsForBottomEntryGate() {
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kApproach;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};

    std::vector<ls2k::legacy::BEVSimpleRowScan> phase1_rows = LeftCircleRows();
    const ls2k::runtime::detail::CircleSideExpansionObservation phase1_expansion =
        ls2k::runtime::detail::ObserveCircleSideExpansion(Frame(phase1_rows, 0.0F), params);
    Expect(phase1_expansion.left_phase1_open,
           "left Phase1 fixture must still expose full-trace expansion");
    Expect(!phase1_expansion.left_entry_gate_reached,
           "far-side Phase1 expansion must not be treated as bottom entry gate");

    const ls2k::runtime::detail::CircleV2Events phase1_events =
        EventsFor(Frame(phase1_rows, 0.0F), prior, params);
    Expect(!phase1_events.entry_gate_reached,
           "Approach must wait when only the Phase1 cue remains visible");
}

void TestApproachConsumesOnlyLockedDirectionExpansion() {
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kApproach;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};

    std::vector<ls2k::legacy::BEVSimpleRowScan> left_rows = LeftEntryRows();
    const ls2k::runtime::detail::CircleV2Events left_events =
        EventsFor(Frame(left_rows, 0.0F), prior, params);
    Expect(left_events.entry_gate_reached,
           "left Approach must consume left-side bottom expansion with a straight opposite edge");

    std::vector<ls2k::legacy::BEVSimpleRowScan> bent_opposite_rows =
        LeftEntryRowsWithBentOpposite();
    const ls2k::runtime::detail::CircleV2Events bent_opposite_events =
        EventsFor(Frame(bent_opposite_rows, 0.0F), prior, params);
    Expect(!bent_opposite_events.entry_gate_reached,
           "left Approach must reject bottom expansion when the bottom opposite edge is not straight");

    std::vector<ls2k::legacy::BEVSimpleRowScan> right_rows = RightEntryRows();
    const ls2k::runtime::detail::CircleV2Events wrong_side_events =
        EventsFor(Frame(right_rows, 0.0F), prior, params);
    Expect(!wrong_side_events.entry_gate_reached,
           "left Approach must ignore right-side expansion");

    std::vector<ls2k::legacy::BEVSimpleRowScan> gapped_rows =
        LeftEntryRowsWithBottomGap();
    const ls2k::runtime::detail::CircleV2Events gapped_events =
        EventsFor(Frame(gapped_rows, 0.0F), prior, params);
    Expect(!gapped_events.entry_gate_reached,
           "Approach bottom gate must require enough rows inside the configured ROI");

    std::vector<ls2k::legacy::BEVSimpleRowScan> far_rows =
        LeftEntryRowsWithoutNearSupport();
    const ls2k::runtime::detail::CircleV2Events far_events =
        EventsFor(Frame(far_rows, 0.0F), prior, params);
    Expect(!far_events.entry_gate_reached,
           "Approach bottom gate must not consume rows outside the configured ROI");

    std::vector<ls2k::legacy::BEVSimpleRowScan> shifted_roi_rows =
        LeftEntryRowsWithExpansionAfterRoiStart();
    const ls2k::runtime::detail::CircleV2Events default_roi_events =
        EventsFor(Frame(shifted_roi_rows, 0.0F), prior, params);
    Expect(!default_roi_events.entry_gate_reached,
           "Approach bottom gate must ignore expansion outside the configured bottom ROI");
    params.entry_bottom_forward_min_m = 0.29F;
    params.entry_bottom_forward_max_m = 0.50F;
    const ls2k::runtime::detail::CircleV2Events shifted_roi_events =
        EventsFor(Frame(shifted_roi_rows, 0.0F), prior, params);
    Expect(shifted_roi_events.entry_gate_reached,
           "Approach bottom gate must use the configured bottom forward ROI");
    params.entry_bottom_forward_min_m = 0.0F;
    params.entry_bottom_forward_max_m = 0.25F;

    prior.dir = ls2k::runtime::CircleDir::kRight;
    const ls2k::runtime::detail::CircleV2Events right_events =
        EventsFor(Frame(right_rows, 0.0F), prior, params);
    Expect(right_events.entry_gate_reached,
           "right Approach must consume right-side expansion");

    const ls2k::runtime::detail::CircleV2Events other_wrong_side_events =
        EventsFor(Frame(left_rows, 0.0F), prior, params);
    Expect(!other_wrong_side_events.entry_gate_reached,
           "right Approach must ignore left-side expansion");
}

void TestReducerSequenceAndHold() {
    ls2k::runtime::CircleV2Params params{};
    params.exit_hold_frames = 3;
    ls2k::runtime::CircleV2Memory memory{};
    ls2k::runtime::detail::CircleV2Events events{};
    events.detected_dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::detail::CircleV2Decision decision =
        ls2k::runtime::detail::ReduceCircleV2(memory, events, {10}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kApproach,
           "Idle must enter Approach on detected dir");
    Expect(decision.next_memory.dir == ls2k::runtime::CircleDir::kLeft,
           "Approach must lock left dir");

    memory = decision.next_memory;
    events = {};
    events.entry_gate_reached = true;
    decision = ls2k::runtime::detail::ReduceCircleV2(memory, events, {20}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "Approach must enter InnerTrace on entry gate");

    memory = decision.next_memory;
    events = {};
    events.exit_gate_reached = true;
    decision = ls2k::runtime::detail::ReduceCircleV2(memory, events, {30}, params);
    Expect(decision.reference.role == ls2k::runtime::CircleV2ReferenceRole::kExitTrace,
           "B->C frame must expose ExitTrace reference role");
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kExitTrace,
           "exit_hold_frames=3 must not hide C after B->C frame");

    memory = decision.next_memory;
    memory.clock.phase_frame_index = 2;
    events = {};
    decision = ls2k::runtime::detail::ReduceCircleV2(memory, events, {40}, params);
    Expect(decision.reference.role == ls2k::runtime::CircleV2ReferenceRole::kExitTrace,
           "final C hold frame must still expose ExitTrace role");
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kIdle,
           "final C hold frame must write next Idle memory");
    Expect(decision.next_memory.dir == ls2k::runtime::CircleDir::kNone,
           "EnterIdle must clear dir");
}

void TestDefaultExitHoldProvidesCooldownWindow() {
    ls2k::runtime::CircleV2Params params{};
    Expect(params.exit_hold_frames == 60,
           "default ExitTrace hold must provide a real cooldown window");

    ls2k::runtime::CircleV2Memory memory{};
    memory.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    memory.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::detail::CircleV2Events events{};
    events.exit_gate_reached = true;
    ls2k::runtime::detail::CircleV2Decision decision =
        ls2k::runtime::detail::ReduceCircleV2(memory, events, {30}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kExitTrace,
           "default hold must keep memory in ExitTrace after the B->C frame");

    memory = decision.next_memory;
    memory.clock.phase_frame_index = 58;
    events = {};
    decision = ls2k::runtime::detail::ReduceCircleV2(memory, events, {40}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kExitTrace,
           "default hold must not release before the 60th ExitTrace frame");

    memory = decision.next_memory;
    memory.clock.phase_frame_index = 59;
    decision = ls2k::runtime::detail::ReduceCircleV2(memory, events, {50}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kIdle,
           "default hold must release after the 60th ExitTrace frame");
}

void TestDirectedYaw() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 5.0F;
    const ls2k::runtime::SceneFrameView frame = Frame(rows, -5.5F);
    const ls2k::runtime::detail::CircleV2Events events =
        EventsFor(frame, prior, params);
    Expect(events.exit_gate_reached, "left circle negative yaw must satisfy directed exit");

    const ls2k::runtime::SceneFrameView wobble = Frame(rows, 5.5F);
    const ls2k::runtime::detail::CircleV2Events wobble_events =
        EventsFor(wobble, prior, params);
    Expect(!wobble_events.exit_gate_reached, "left circle positive yaw must not pass by abs");

    prior.dir = ls2k::runtime::CircleDir::kRight;
    const ls2k::runtime::SceneFrameView right_frame = Frame(rows, 5.5F);
    const ls2k::runtime::detail::CircleV2Events right_events =
        EventsFor(right_frame, prior, params);
    Expect(right_events.exit_gate_reached,
           "right circle positive yaw must satisfy directed exit");
}

void TestInnerTraceYawStallFallbackEvent() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftEntryRows();
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 5.0F;
    params.inner_trace_stall_timeout_ms = 4000;
    params.inner_trace_stall_yaw_min_rad = 0.3F;

    ls2k::runtime::SceneFrameView stalled_frame = Frame(rows, -0.1F);
    stalled_frame.stamp.capture_time_ms = 4100;
    const ls2k::runtime::detail::CircleV2Events stalled_events =
        EventsFor(stalled_frame, prior, params);
    Expect(stalled_events.inner_trace_stalled,
           "InnerTrace must report stalled after timeout with yaw below configured minimum");

    ls2k::runtime::SceneFrameView turning_frame = Frame(rows, -0.5F);
    turning_frame.stamp.capture_time_ms = 4100;
    const ls2k::runtime::detail::CircleV2Events turning_events =
        EventsFor(turning_frame, prior, params);
    Expect(!turning_events.inner_trace_stalled,
           "InnerTrace stall fallback must respect configured yaw minimum");

    prior.clock.max_directed_turn_angle_rad = 2.8F;
    ls2k::runtime::SceneFrameView yaw_drop_frame = Frame(rows, -0.1F);
    yaw_drop_frame.stamp.capture_time_ms = 6100;
    const ls2k::runtime::detail::CircleV2Events yaw_drop_events =
        EventsFor(yaw_drop_frame, prior, params);
    Expect(!yaw_drop_events.inner_trace_stalled,
           "InnerTrace stall fallback must respect the max yaw progress already reached");
    ls2k::runtime::detail::CircleV2Decision yaw_drop_decision =
        ls2k::runtime::detail::ReduceCircleV2(prior, yaw_drop_events, {6100}, params);
    Expect(yaw_drop_decision.next_memory.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "InnerTrace must stay active after net yaw drops from prior progress");
    Expect(yaw_drop_decision.next_memory.clock.max_directed_turn_angle_rad >= 2.8F,
           "InnerTrace memory must retain max yaw progress after net yaw drops");

    prior.clock.max_directed_turn_angle_rad = 0.0F;
    ls2k::runtime::detail::CircleV2Decision decision =
        ls2k::runtime::detail::ReduceCircleV2(prior, stalled_events, {4100}, params);
    Expect(decision.next_memory.phase == ls2k::runtime::CirclePhase::kIdle,
           "InnerTrace yaw stall must return to Idle");
    Expect(decision.next_memory.dir == ls2k::runtime::CircleDir::kNone,
           "InnerTrace yaw stall must clear locked dir");
    Expect(decision.reason ==
               ls2k::runtime::CircleV2TelemetryReason::kInnerTraceYawStalled,
           "InnerTrace yaw stall reason must be explicit");
}

void TestMotionHistoryCoversDefaultInnerTraceStallWindow() {
    constexpr int kDefaultControlPeriodMs = 5;
    constexpr int kDefaultInnerTraceStallTimeoutMs = 4000;
    constexpr int kRequiredMarginMs = 1000;
    const std::size_t covered_ms =
        ls2k::runtime::MotionHistory::kCapacity *
        static_cast<std::size_t>(kDefaultControlPeriodMs);
    Expect(covered_ms >=
               static_cast<std::size_t>(kDefaultInnerTraceStallTimeoutMs +
                                        kRequiredMarginMs),
           "motion history must cover the default InnerTrace stall window");
}

void TestActivePhasesSurviveMissingOrdinaryRoad() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = StraightRows();
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    ls2k::runtime::CircleV2Memory approach{};
    approach.phase = ls2k::runtime::CirclePhase::kApproach;
    approach.dir = ls2k::runtime::CircleDir::kLeft;
    approach.clock.enter_capture_time_ms = 100;
    const ls2k::runtime::CircleV2StepResult approach_result =
        ls2k::runtime::CircleV2Scene{}.Step(FrameWithoutOrdinaryRoad(rows, 0.0F),
                                            approach,
                                            params);
    Expect(approach_result.next_memory.phase == ls2k::runtime::CirclePhase::kApproach,
           "Approach must not reset when ordinary road is unavailable");
    Expect(approach_result.next_memory.dir == ls2k::runtime::CircleDir::kLeft,
           "Approach must preserve locked dir when ordinary road is unavailable");
    Expect(!approach_result.reference_plan.has_value(),
           "Approach must not produce a circle reference plan");

    ls2k::runtime::CircleV2Memory inner{};
    inner.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    inner.dir = ls2k::runtime::CircleDir::kLeft;
    inner.clock.enter_capture_time_ms = 100;
    const ls2k::runtime::CircleV2StepResult inner_result =
        ls2k::runtime::CircleV2Scene{}.Step(FrameWithoutOrdinaryRoad(rows, 0.0F),
                                            inner,
                                            params);
    Expect(inner_result.next_memory.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "InnerTrace must not reset when ordinary road is unavailable");
    Expect(inner_result.reference_plan.has_value(),
           "InnerTrace may use raw row edge geometry when ordinary road is unavailable");
    Expect(inner_result.telemetry.reason ==
               ls2k::runtime::CircleV2TelemetryReason::kNone,
           "InnerTrace raw-row geometry must not report unavailable geometry");
}

void TestInnerTraceSurvivesUnavailableMotionArc() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftEntryRows();
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 0.1F;
    ls2k::runtime::SceneFrameView frame =
        FrameWithCenterPath(rows, 0.0F, EntryCenterPath());
    frame.motion_arc = ls2k::runtime::MotionArcView(nullptr, QueryYawUnavailable);
    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(frame, prior, params);
    Expect(result.next_memory.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "InnerTrace must not reset when yaw delta is unavailable");
    Expect(result.reference_plan.has_value(),
           "InnerTrace geometry can still produce a reference plan when only motion is unavailable");
    Expect(result.reference_plan->role == ls2k::runtime::CircleV2ReferenceRole::kInnerTrace,
           "InnerTrace unavailable motion plan role mismatch");
}

void TestReferenceHoldResetPreservesCircleV2Memory() {
    ls2k::port::SteeringPerceptionMemory memory{};
    memory.reference_hold.hold_cycles = 7;
    memory.circle_v2.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    memory.circle_v2.dir = ls2k::runtime::CircleDir::kLeft;
    memory.circle_v2.clock.enter_capture_time_ms = 100;
    memory.circle_v2.clock.phase_frame_index = 3;

    ls2k::runtime::ResetSteeringReferenceHoldMemory(memory);

    Expect(memory.reference_hold.hold_cycles == 0,
           "reference reset must clear reference hold");
    Expect(memory.circle_v2.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "reference reset must preserve CircleV2 phase");
    Expect(memory.circle_v2.dir == ls2k::runtime::CircleDir::kLeft,
           "reference reset must preserve CircleV2 dir");
    Expect(memory.circle_v2.clock.enter_capture_time_ms == 100,
           "reference reset must preserve CircleV2 enter time");
    Expect(memory.circle_v2.clock.phase_frame_index == 3,
           "reference reset must preserve CircleV2 frame index");
}

void TestExitTraceRejectsNonStraightOuterEdge() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.4F, -0.5F, -0.4F, 0.6F, 0.7F),
        Row(0.5F, -0.5F, -0.4F, 0.2F, 0.3F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};
    params.exit_hold_frames = 2;
    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(Frame(rows, 0.0F), prior, params);
    Expect(!result.reference_plan.has_value(),
           "ExitTrace must reject a non-straight opposite outer edge");
    Expect(result.telemetry.reason == ls2k::runtime::CircleV2TelemetryReason::kGeometryUnavailable,
           "non-straight ExitTrace geometry must report geometry unavailable");
}

void TestExitTraceUsesOrdinaryRoadHalfWidthFact() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.4F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.5F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};
    params.exit_hold_frames = 2;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, CenterPath(), 0.12F),
            prior,
            params);
    Expect(result.reference_plan.has_value(),
           "ExitTrace must produce a plan from straight role-specific geometry");
    const float lateral =
        result.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    Expect(std::fabs(lateral - 0.38F) < 1.0e-5F,
           "ExitTrace must use OrdinaryRoadModel.half_width instead of row-derived width");
}

void TestExitTraceAcceptsNonSelectedBoundaryClippedInterval() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        SingleIntervalRow(0.3F, 0.20F, 0.50F),
        SingleIntervalRow(0.4F, 0.20F, 0.50F),
        SingleIntervalRow(0.5F, 0.20F, 0.50F),
    };
    for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
        SetSampleableSpan(row, 0.20F, 0.70F);
    }
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};
    params.exit_hold_frames = 2;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, CenterPath(), 0.12F),
            prior,
            params);
    Expect(result.reference_plan.has_value(),
           "ExitTrace must accept an interval whose non-selected edge is clipped");
    const float lateral =
        result.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    Expect(std::fabs(lateral - 0.38F) < 1.0e-5F,
           "ExitTrace must offset from the selected visible outer edge");
}

void TestExitTraceRejectsSelectedBoundaryClippedEdgePath() {
    {
        std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
            SingleIntervalRow(0.3F, 0.20F, 0.50F),
            SingleIntervalRow(0.4F, 0.20F, 0.50F),
            SingleIntervalRow(0.5F, 0.20F, 0.50F),
        };
        for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
            SetSampleableSpan(row, 0.00F, 0.50F);
        }
        ls2k::runtime::CircleV2Memory prior{};
        prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
        prior.dir = ls2k::runtime::CircleDir::kLeft;
        ls2k::runtime::CircleV2Params params{};
        params.exit_hold_frames = 2;

        const ls2k::runtime::CircleV2StepResult result =
            ls2k::runtime::CircleV2Scene{}.Step(
                FrameWithCenterPath(rows, 0.0F, CenterPath(), 0.12F),
                prior,
                params);
        Expect(!result.reference_plan.has_value(),
               "left ExitTrace must reject a clipped selected right outer edge");
    }
    {
        std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
            SingleIntervalRow(0.3F, -0.50F, -0.20F),
            SingleIntervalRow(0.4F, -0.50F, -0.20F),
            SingleIntervalRow(0.5F, -0.50F, -0.20F),
        };
        for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
            SetSampleableSpan(row, -0.50F, 0.00F);
        }
        ls2k::runtime::CircleV2Memory prior{};
        prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
        prior.dir = ls2k::runtime::CircleDir::kRight;
        ls2k::runtime::CircleV2Params params{};
        params.exit_hold_frames = 2;

        const ls2k::runtime::CircleV2StepResult result =
            ls2k::runtime::CircleV2Scene{}.Step(
                FrameWithCenterPath(rows, 0.0F, CenterPath(), 0.12F),
                prior,
                params);
        Expect(!result.reference_plan.has_value(),
               "right ExitTrace must reject a clipped selected left outer edge");
    }
}

void TestExitTraceIgnoresEntryBottomForwardRoi() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.4F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.5F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    ls2k::runtime::CircleV2Params params{};
    params.exit_hold_frames = 2;
    params.entry_bottom_forward_min_m = 0.70F;
    params.entry_bottom_forward_max_m = 0.80F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, CenterPath(), 0.12F),
            prior,
            params);
    Expect(result.reference_plan.has_value(),
           "entry bottom forward ROI must not limit ExitTrace edge geometry");
    Expect(std::fabs(result.reference_plan->reference_path.sampled_path[0]
                         .point.forward_m - 0.30F) < 1.0e-5F,
           "ExitTrace edge path must preserve observed row forward coordinates");
}

void TestInnerTraceUsesLockedSideInnerEdgePath() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftInnerTraceRows();
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, EntryCenterPath()),
            prior,
            params);
    Expect(result.next_memory.phase == ls2k::runtime::CirclePhase::kInnerTrace,
           "InnerTrace path composition must not reset state");
    Expect(result.reference_plan.has_value(),
           "InnerTrace must output a direct inner-edge reference plan");
    Expect(result.reference_plan->role == ls2k::runtime::CircleV2ReferenceRole::kInnerTrace,
           "InnerTrace reference role mismatch");
    const float first_lateral =
        result.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    const float third_lateral =
        result.reference_plan->reference_path.sampled_path[2].point.lateral_m;
    Expect(std::fabs(first_lateral - (-0.2F)) < 1.0e-5F,
           "left InnerTrace first sample must sit on the locked-side inner edge");
    Expect(std::fabs(third_lateral - (-0.35F)) < 1.0e-5F,
           "left InnerTrace expanded sample must follow the inner edge without half-width offset");

    const ls2k::runtime::CircleV2StepResult raw_row_result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithoutOrdinaryRoad(rows, 0.0F),
            prior,
            params);
    Expect(raw_row_result.reference_plan.has_value(),
           "InnerTrace must still observe inner edge when ordinary center is unavailable");
    Expect(std::fabs(raw_row_result.reference_plan->reference_path.sampled_path[0]
                         .point.lateral_m - (-0.2F)) < 1.0e-5F,
           "raw-row InnerTrace fallback must keep the observed inner edge");

    params.inner_trace_path_offset_m = 0.05F;
    const ls2k::runtime::CircleV2StepResult offset_result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, EntryCenterPath()),
            prior,
            params);
    Expect(offset_result.reference_plan.has_value(),
           "InnerTrace offset path must still produce a reference plan");
    const float offset_first_lateral =
        offset_result.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    const float offset_third_lateral =
        offset_result.reference_plan->reference_path.sampled_path[2].point.lateral_m;
    Expect(std::fabs(offset_first_lateral - (-0.15F)) < 1.0e-5F,
           "left InnerTrace positive offset must move from inner edge toward road interior");
    const float left_slope_delta = 0.05F * std::sqrt(1.0F + 2.5F * 2.5F);
    Expect(std::fabs(offset_third_lateral - (-0.35F + left_slope_delta)) < 1.0e-5F,
           "left InnerTrace positive offset must follow local inner-edge direction");
}

void TestInnerTraceRejectsInsufficientRowGeometry() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(Frame(rows, 0.0F), prior, params);
    Expect(!result.reference_plan.has_value(),
           "one observed row must not be reused into a three-sample plan");
    const std::optional<ls2k::port::VisualReferenceCandidate> candidate =
        ls2k::runtime::AdaptCircleV2ReferencePlan(result.reference_plan);
    Expect(!candidate.has_value(),
           "adapter must not produce a candidate from insufficient row geometry");
}

void TestInnerTraceAcceptsNonSelectedBoundaryClippedInterval() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        SingleIntervalRow(0.06F, -0.50F, -0.20F),
        SingleIntervalRow(0.12F, -0.50F, -0.20F),
        SingleIntervalRow(0.18F, -0.50F, -0.20F),
    };
    for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
        SetSampleableSpan(row, -0.50F, 0.50F);
    }
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithCenterPath(rows, 0.0F, EntryCenterPath()),
            prior,
            params);
    Expect(result.reference_plan.has_value(),
           "InnerTrace must accept an interval whose non-selected edge is clipped");
    Expect(std::fabs(result.reference_plan->reference_path.sampled_path[0]
                         .point.lateral_m - (-0.20F)) < 1.0e-5F,
           "InnerTrace must use the selected visible inner edge");
}

void TestInnerTraceRejectsSelectedBoundaryClippedEdgePath() {
    {
        std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
            SingleIntervalRow(0.06F, -0.50F, -0.20F),
            SingleIntervalRow(0.12F, -0.50F, -0.20F),
            SingleIntervalRow(0.18F, -0.50F, -0.20F),
        };
        for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
            SetSampleableSpan(row, -0.70F, -0.20F);
        }
        ls2k::runtime::CircleV2Memory prior{};
        prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
        prior.dir = ls2k::runtime::CircleDir::kLeft;
        prior.clock.enter_capture_time_ms = 100;
        ls2k::runtime::CircleV2Params params{};
        params.exit_yaw_threshold_rad = 10.0F;

        const ls2k::runtime::CircleV2StepResult result =
            ls2k::runtime::CircleV2Scene{}.Step(
                FrameWithCenterPath(rows, 0.0F, EntryCenterPath()),
                prior,
                params);
        Expect(!result.reference_plan.has_value(),
               "left InnerTrace must reject a clipped selected inner edge");
    }
    {
        std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
            SingleIntervalRow(0.06F, 0.20F, 0.50F),
            SingleIntervalRow(0.12F, 0.20F, 0.50F),
            SingleIntervalRow(0.18F, 0.20F, 0.50F),
        };
        for (ls2k::legacy::BEVSimpleRowScan& row : rows) {
            SetSampleableSpan(row, 0.20F, 0.70F);
        }
        ls2k::runtime::CircleV2Memory prior{};
        prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
        prior.dir = ls2k::runtime::CircleDir::kRight;
        prior.clock.enter_capture_time_ms = 100;
        ls2k::runtime::CircleV2Params params{};
        params.exit_yaw_threshold_rad = 10.0F;

        const ls2k::runtime::CircleV2StepResult result =
            ls2k::runtime::CircleV2Scene{}.Step(
                FrameWithCenterPath(rows, 0.0F, EntryCenterPath()),
                prior,
                params);
        Expect(!result.reference_plan.has_value(),
               "right InnerTrace must reject a clipped selected inner edge");
    }
}

void TestInnerTraceRejectsGappedRowGeometry() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
        InvalidRow(0.4F),
        Row(0.5F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(Frame(rows, 0.0F), prior, params);
    Expect(!result.reference_plan.has_value(),
           "missing middle row must break CircleV2 inner-edge path observation");
}

void TestInnerTraceIgnoresEntryBottomForwardRoi() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.10F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.18F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.30F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.36F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.42F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;
    params.entry_bottom_forward_min_m = 0.70F;
    params.entry_bottom_forward_max_m = 0.80F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithoutOrdinaryRoad(rows, 0.0F),
            prior,
            params);
    Expect(result.reference_plan.has_value(),
           "entry bottom forward ROI must not limit InnerTrace edge geometry");
    Expect(std::fabs(result.reference_plan->reference_path.sampled_path[0]
                         .point.forward_m - 0.10F) < 1.0e-5F,
           "CircleV2 edge path must preserve observed row forward coordinates");
}

void TestInnerTraceDoesNotBridgeInvalidRows() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows{
        Row(0.30F, -0.5F, -0.4F, 0.4F, 0.5F),
        InvalidRow(0.36F),
        Row(0.42F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.48F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.54F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(
            FrameWithoutOrdinaryRoad(rows, 0.0F),
            prior,
            params);
    Expect(!result.reference_plan.has_value(),
           "CircleV2 edge geometry must not bridge invalid row holes");
}

void TestSceneGeometryAndAdapter() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = LeftInnerTraceRows();
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kLeft;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;
    const ls2k::runtime::SceneFrameView inner_frame =
        FrameWithCenterPath(rows, 0.0F, EntryCenterPath());
    const ls2k::runtime::CircleV2StepResult inner =
        ls2k::runtime::CircleV2Scene{}.Step(inner_frame, prior, params);
    const std::optional<ls2k::port::VisualReferenceCandidate> candidate =
        ls2k::runtime::AdaptCircleV2ReferencePlan(inner.reference_plan);
    Expect(inner.reference_plan.has_value(), "InnerTrace must directly produce reference plan");
    Expect(inner.reference_plan->role == ls2k::runtime::CircleV2ReferenceRole::kInnerTrace,
           "InnerTrace plan role mismatch");
    Expect(candidate.has_value(), "adapter must produce candidate for plan");
    Expect(candidate->kind == ls2k::port::VisualReferenceCandidateKind::kCircleLeft,
           "adapter kind must follow left dir");
    Expect(candidate->source == "circle_v2_inner", "adapter source must identify inner role");
    Expect(std::fabs(candidate->reference_path.sampled_path[0].point.lateral_m - (-0.2F)) <
               1.0e-5F,
           "left InnerTrace candidate must follow the inner edge directly");
    params.inner_trace_path_offset_m = 0.04F;
    const ls2k::runtime::CircleV2StepResult offset_inner =
        ls2k::runtime::CircleV2Scene{}.Step(inner_frame, prior, params);
    const std::optional<ls2k::port::VisualReferenceCandidate> offset_candidate =
        ls2k::runtime::AdaptCircleV2ReferencePlan(offset_inner.reference_plan);
    Expect(offset_candidate.has_value(), "adapter must produce offset inner candidate");
    Expect(std::fabs(offset_candidate->reference_path.sampled_path[0].point.lateral_m -
                     (-0.16F)) < 1.0e-5F,
           "left InnerTrace adapter must preserve parameterized inner offset");

    prior.phase = ls2k::runtime::CirclePhase::kExitTrace;
    prior.clock.phase_frame_index = 1;
    params.exit_hold_frames = 2;
    std::vector<ls2k::legacy::BEVSimpleRowScan> exit_rows{
        Row(0.3F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.4F, -0.5F, -0.4F, 0.4F, 0.5F),
        Row(0.5F, -0.5F, -0.4F, 0.4F, 0.5F),
    };
    const ls2k::runtime::CircleV2StepResult exit =
        ls2k::runtime::CircleV2Scene{}.Step(Frame(exit_rows, 0.0F), prior, params);
    Expect(exit.reference_plan.has_value(), "ExitTrace final frame must still produce plan");
    const float exit_lateral = exit.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    Expect(std::fabs(exit_lateral - 0.3F) < 1.0e-5F,
           "left ExitTrace must offset the right outer edge left by half width");
    Expect(exit.telemetry.frame_phase == ls2k::runtime::CirclePhase::kExitTrace,
           "ExitTrace final frame telemetry frame phase mismatch");
    Expect(exit.telemetry.next_phase == ls2k::runtime::CirclePhase::kIdle,
           "ExitTrace final frame telemetry next phase mismatch");
}

void TestRightInnerTraceInnerEdgePath() {
    std::vector<ls2k::legacy::BEVSimpleRowScan> rows = RightInnerTraceRows();
    ls2k::runtime::CircleV2Memory prior{};
    prior.phase = ls2k::runtime::CirclePhase::kInnerTrace;
    prior.dir = ls2k::runtime::CircleDir::kRight;
    prior.clock.enter_capture_time_ms = 100;
    ls2k::runtime::CircleV2Params params{};
    params.exit_yaw_threshold_rad = 10.0F;
    params.inner_trace_path_offset_m = 0.05F;
    const ls2k::runtime::SceneFrameView frame =
        FrameWithCenterPath(rows, 0.0F, EntryCenterPath());

    const ls2k::runtime::CircleV2StepResult result =
        ls2k::runtime::CircleV2Scene{}.Step(frame, prior, params);
    Expect(result.reference_plan.has_value(),
           "right InnerTrace must directly produce reference plan");
    Expect(result.reference_plan->role == ls2k::runtime::CircleV2ReferenceRole::kInnerTrace,
           "right InnerTrace plan role mismatch");
    const float first_lateral =
        result.reference_plan->reference_path.sampled_path[0].point.lateral_m;
    const float third_lateral =
        result.reference_plan->reference_path.sampled_path[2].point.lateral_m;
    Expect(std::fabs(first_lateral - 0.15F) < 1.0e-5F,
           "right InnerTrace positive offset must move from inner edge toward road interior");
    const float right_slope_delta = 0.05F * std::sqrt(1.0F + 2.5F * 2.5F);
    Expect(std::fabs(third_lateral - (0.35F - right_slope_delta)) < 1.0e-5F,
           "right InnerTrace positive offset must follow local inner-edge direction");
    const std::optional<ls2k::port::VisualReferenceCandidate> candidate =
        ls2k::runtime::AdaptCircleV2ReferencePlan(result.reference_plan);
    Expect(candidate.has_value(), "adapter must produce right circle candidate");
    Expect(candidate->kind == ls2k::port::VisualReferenceCandidateKind::kCircleRight,
           "adapter kind must follow right dir");
}

}  // namespace

int main() {
    TestPhase1CueParity();
    TestFalseRightBendDoesNotPassOppositeStraightGate();
    TestStraightSameSideExpansionDoesNotCreateCircleCue();
    TestDisconnectedFarSideArtifactDoesNotCreateCircleCue();
    TestApproachWaitsForBottomEntryGate();
    TestApproachConsumesOnlyLockedDirectionExpansion();
    TestReducerSequenceAndHold();
    TestDefaultExitHoldProvidesCooldownWindow();
    TestDirectedYaw();
    TestInnerTraceYawStallFallbackEvent();
    TestMotionHistoryCoversDefaultInnerTraceStallWindow();
    TestActivePhasesSurviveMissingOrdinaryRoad();
    TestInnerTraceSurvivesUnavailableMotionArc();
    TestReferenceHoldResetPreservesCircleV2Memory();
    TestExitTraceRejectsNonStraightOuterEdge();
    TestExitTraceUsesOrdinaryRoadHalfWidthFact();
    TestExitTraceAcceptsNonSelectedBoundaryClippedInterval();
    TestExitTraceRejectsSelectedBoundaryClippedEdgePath();
    TestExitTraceIgnoresEntryBottomForwardRoi();
    TestInnerTraceUsesLockedSideInnerEdgePath();
    TestInnerTraceRejectsInsufficientRowGeometry();
    TestInnerTraceAcceptsNonSelectedBoundaryClippedInterval();
    TestInnerTraceRejectsSelectedBoundaryClippedEdgePath();
    TestInnerTraceRejectsGappedRowGeometry();
    TestInnerTraceIgnoresEntryBottomForwardRoi();
    TestInnerTraceDoesNotBridgeInvalidRows();
    TestSceneGeometryAndAdapter();
    TestRightInnerTraceInnerEdgePath();
    std::cout << "steering_circle_v2_scene_test passed\n";
    return 0;
}
