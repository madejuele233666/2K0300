#ifndef LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_HPP
#define LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_HPP

#include <cstdint>
#include <optional>

#include "port/bev_reference_types.hpp"
#include "port/circle_v2_types.hpp"
#include "vision/elements/circle_v2/circle_v2_scene_frame_view.hpp"

namespace ls2k::vision {

using port::CircleDir;
using port::CirclePhase;
using port::CircleV2Memory;
using port::CircleV2Params;
using port::CircleV2ReferencePlan;
using port::CircleV2ReferenceRole;
using port::CircleV2StageClock;
using port::CircleV2Telemetry;
using port::CircleV2TelemetryReason;

struct CircleV2StepResult {
    CircleV2Memory next_memory{};
    std::optional<CircleV2ReferencePlan> reference_plan{};
    CircleV2Telemetry telemetry{};
};

const char* ToString(CircleDir dir);
const char* ToString(CirclePhase phase);
const char* ToString(CircleV2ReferenceRole role);
const char* ToString(CircleV2TelemetryReason reason);

void ResetCircleV2Memory(CircleV2Memory& memory);

class CircleV2Scene {
public:
    CircleV2StepResult Step(const SceneFrameView& frame,
                            const CircleV2Memory& prior,
                            const CircleV2Params& params) const;
};

}  // namespace ls2k::vision

#endif  // LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_HPP
