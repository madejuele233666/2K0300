# Phase A Camera Exposure

- Evidence log: `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-camera-exposure.log`
- Supporting sample note: `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-camera-samples.md`
- Board run date: `2026-04-17`
- Result: `decision_closed`

## Decision

The direct-match camera path does not support arbitrary runtime exposure control in Phase A.

## Evidence

### Unsupported Non-Default Exposure

When `exp_light=66` was supplied on the live board:

1. `params.critical.exp_light` warned before adapter bring-up.
2. `camera.exposure.unsupported` was emitted by the camera adapter.
3. Startup failed closed at `startup.camera.init`.

### Forced Non-Phase1 Geometry

When `LS2K_FORCE_UVC_GEOMETRY=320x240` was supplied:

1. Startup still completed.
2. The camera path emitted `camera.geometry.override`.
3. The run stayed in explicit marker-driven degradation rather than silently pretending direct-match geometry was valid.

## Phase A Policy

1. Default `exp_light=65` remains the bounded direct-match mainline.
2. Non-default exposure requests require a named adaptation path or a diagnostics-only/degraded workflow.
3. Exposure therefore stops being an open-ended architectural concern in Phase A.

## Relation To Control Unlock

The `2026-04-17` control-unveto run was dominated by startup `perception_stale`, not `perception_emergency` or exposure behavior, so exposure did not need to be promoted ahead of the current control-unlock conclusion.
