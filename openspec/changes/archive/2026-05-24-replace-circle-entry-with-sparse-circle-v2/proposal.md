## Why

The current circle-entry path is still shaped as a single-frame visual element candidate and uses rear / side-rear black frontier support for entry geometry. The V2 design requires a stateful sparse-BEV circle scene interpreter that preserves the existing Phase1 circle cue while replacing rear-black entry logic with explicit Approach, InnerTrace, and ExitTrace behavior.

## What Changes

- Add `CircleV2Scene` as the only runtime owner of circle semantics: Phase1 cue, entry gate, direction lock, state transitions, telemetry, and circle reference plan creation.
- Introduce a stable scene input surface: `SceneFrameView`, `OrdinaryRoadModel`, non-null `MotionArcView`, `CaptureStamp`, `CircleV2Memory`, and `CircleV2Params`.
- Implement the minimal V2 FSM: `Idle -> Approach -> InnerTrace -> ExitTrace -> Idle`, with `Approach` only entered from `Idle` and `ExitTrace` hold serving as the cooldown.
- Preserve existing Phase1 circle direction semantics by migrating the cue into `CircleV2EventObserver` / `ObserveCirclePhase1Cue`, with golden parity coverage against the old Phase1 result.
- Remove rear / side-rear black frontier logic from runtime circle reference construction and stop producing old `circle_entry` candidates, diagnostics, and evidence records.
- Generate B/InnerTrace references from the nearest locked-direction inner edge offset by road half width, and C/ExitTrace references from the opposite-side straight outer edge offset by road half width.
- Route yaw through a composed `MotionArcView` and direction-normalize yaw delta before B -> C; the FSM reads only the resulting event.
- Add `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, and `CIRCLE_V2_EXIT_HOLD_FRAMES`, while removing the old `CIRCLE_ENTRY_*` runtime parameter surface.
- Adapt `CircleV2ReferencePlan` to the existing `VisualReferenceCandidate` arbitration surface through a small reference adapter.
- Update steering snapshots, steering media, probes, and tests to expose V2 telemetry and no longer depend on old circle evidence / circle_entry runtime semantics.

## Capabilities

### New Capabilities

- `sparse-circle-v2-scene`: stateful sparse-BEV circle scene interpretation, reference plan generation, lifecycle memory, motion-arc exit detection, and candidate adaptation.

### Modified Capabilities

- `bev-visual-element-evidence`: remove runtime circle ownership from the visual element pipeline; it remains responsible for cross and non-circle element evidence only.
- `steering-tuning-media-observability`: replace old circle_entry observability expectations with CircleV2 telemetry, V2 parameter snapshot fields, and V2 reference source reporting.

## Risk Tier

- `STRICT`: this changes active steering reference selection, runtime state memory, perception pipeline ownership, IMU/yaw integration boundaries, public runtime parameters, steering media/probe observability, and multiple regression tests. It also removes old circle-entry runtime semantics instead of preserving backward-compatible behavior.

## Impact

- Affected layers: `port` reference/evidence/state contracts, `legacy` visual element circle evidence cleanup, `runtime` perception pipeline and scene memory, `platform` parameter loading and steering media serialization, `config` default parameters, `user` probes/selftests, and `verification` tests.
- Public interfaces: new `CIRCLE_V2_*` parameters, CircleV2 telemetry fields, CircleV2 reference source names, and removal of old `CIRCLE_ENTRY_*` / `circle_entry` runtime observability.
- Dependencies: uses existing sparse BEV row scans, ordinary BEV reference paths, visual-reference arbitration, runtime motion history/yaw integration, and steering media snapshot serialization.
- No new external library dependency is expected.
