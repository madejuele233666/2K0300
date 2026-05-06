## Why

The active steering runtime still treats dense BEV as a debug artifact, while circle, roadblock, ML, and future connectivity checks need a runtime BEV raster fact surface that is separate from debug output and control policy. `perception_frontend.cpp` also directly wires sparse line perception, cross detection, candidate aggregation, orchestration, hold, and debug publication, which will make each new element collide in the same runtime file.

## What Changes

- Add a runtime `BEVElementRaster` input layer derived from the current camera frame, threshold, projector, and runtime raster parameters.
- Split single-frame steering perception work out of `PerceptionFrontend` so the frontend owns capture/publish lifecycle while a dedicated frame pipeline owns thresholding, sparse scans, raster creation, element aggregation, orchestration, hold, usability, lateral error, and readiness.
- Move cross-exit element code from the generic element evidence file into a cross-specific module while preserving existing cross behavior and default-off takeover semantics.
- Add a `VisualElementPipeline` aggregation entry point so runtime code calls element evidence once and future elements register behind the pipeline instead of editing frontend orchestration.
- Extend element evidence with a backward-compatible generic record list while keeping the existing typed `cross_exit` fields stable.
- Replace repeated hand-written element-evidence protocol/debug serialization with shared serialization/copy helpers that keep `cross_exit` first and append generic records for future elements.
- Add runtime parameters for `BEV_ELEMENT_RASTER` and performance evidence for the runtime raster stage.

## Capabilities

### New Capabilities
- `bev-element-raster-pipeline`: Runtime BEV element raster facts, steering frame pipeline ownership, and element-pipeline aggregation boundaries.

### Modified Capabilities
- `bev-visual-element-evidence`: element evidence keeps typed `cross_exit` and gains backward-compatible generic records for future elements.
- `assistant-telemetry-sidecar`: telemetry mirrors the shared element-evidence serializer without changing JSON-line framing or command behavior.
- `steering-tuning-media-observability`: steering snapshots and media headers mirror the shared element-evidence serializer and include raster parameter context.
- `runtime-speed-tuning-surface`: the read-only parameter snapshot includes the new `BEV_ELEMENT_RASTER` runtime parameter group.

## Risk Tier

- `STANDARD`: the change touches `port`, `legacy`, `runtime`, `config`, `platform` protocol/debug surfaces, tests, and public observability JSON. It does not intentionally change actuator behavior, ordinary line selection, safety, yaw, hold policy, or cross takeover defaults.

## Impact

- Affected layers: `port`, `legacy`, `runtime`, `platform`, `config`, `verification`, and root README/main specs.
- Public interfaces: `RuntimeParameters`, `PerceptionResult`, `ControlDebugSnapshot`, assistant telemetry JSON, steering media JSON, debug snapshots, and runtime performance stages.
- No new external dependencies.
- Project-specific safety constraints: no circle detector, no ordinary path black-barrier recovery, no lateral-jump removal, no motor behavior change, and board smoke remains no-motion unless separately requested through accepted workflows.
