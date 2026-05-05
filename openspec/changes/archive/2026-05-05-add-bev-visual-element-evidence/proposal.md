## Why

The active BEV runtime currently exposes only line-derived reference facts, while future cross/circle/roadblock/ML work needs a facts-first element layer that can be observed and tested without letting element internals leak into control. This change adds the first narrow slice: cross-exit visual evidence with takeover disabled by default.

## What Changes

- Add a BEV visual element evidence capability for `cross_exit` facts derived from current sparse BEV row scans.
- Expose `element_evidence.cross_exit` through the public steering snapshot, assistant telemetry, and steering media image-frame header.
- Add a default-off runtime parameter for `cross_exit` candidate takeover so evidence can be collected before any element candidate affects arbitration.
- Keep downstream continuity, usability, lateral-error, readiness, safety, yaw, and actuator logic consuming only selected reference facts.

## Capabilities

### New Capabilities

- `bev-visual-element-evidence`: BEV metric visual element facts and disabled-by-default cross-exit candidate construction.

### Modified Capabilities

- `assistant-telemetry-sidecar`: telemetry includes the public element-evidence facts without changing command framing.
- `steering-tuning-media-observability`: steering snapshots and media headers include the public element-evidence facts.

## Risk Tier

- `STANDARD`: the change touches port/runtime/config/protocol/test surfaces and adds a new public observability contract, but default runtime control behavior remains line-only unless the new takeover parameter is explicitly enabled.

## Impact

- Affected layers: `port`, `legacy`, `runtime`, `platform`, `config`, `verification`, `user` tooling.
- Public interfaces: runtime parameters, `PerceptionResult`, `ControlDebugSnapshot`, assistant telemetry JSON, steering media JSON header.
- No new external dependencies.
- Project-specific safety constraints: no motor behavior change by default, no board auto-start, and board smoke verification remains no-motion unless explicitly requested by the existing workflow.
