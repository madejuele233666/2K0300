## Why

Phase A documentation has fallen behind the actual board/runtime baseline: the project now has direct-match startup evidence for IMU, encoder, motor, and control-loop bring-up, but the docs still describe that path as unresolved. At the same time, the public actuator contract still carries a nonexistent servo path, and the current `control.veto` diagnostics are too coarse to support clean Phase A closure or elegant follow-on decoupling.

## What Changes

- Align `race-finish-series.zh-CN` Phase A progress and roadmap documents with the accepted direct-match board evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`, `runtime-smoke-retry-2026-04-15.exit`, and `hardware-discovery-retry-2026-04-15.log`, using `runtime-smoke-execution-evidence.md` as the supersession note for any earlier direct-match artifact.
- Remove the legacy-only `servo_pwm` branch from the public actuator contract so the runtime and legacy control path express pure differential steering only.
- Fix the `turn_output` contract and documentation so it is consistently defined as a differential steering adjustment, not a hidden servo-angle surrogate.
- Introduce project-owned control observability for veto, arming, and applied-output decisions so Phase A evidence can distinguish stale perception, perception veto, invalid sensors, low voltage, and actuator-application outcomes without leaking vendor details.
- Tighten Phase A evidence expectations around direct-match runtime smoke, actuator topology, and control-loop unlock diagnostics.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `tc264-to-true-ls2k0300-adapter-layer`: the public actuator/control contract changes from servo-compatible legacy carryover to explicit differential-drive-only semantics, and runtime diagnostics gain finer-grained project-owned observability for veto and actuator application state.
- `true-ls2k0300-port-workspace`: the `new/` documentation and verification evidence contract changes to reflect the accepted direct-match board baseline named above, Phase A evidence naming, and the requirement that runtime smoke and follow-on logs explain why control remains vetoed or becomes armed.

## Risk Tier

- `STANDARD`: this change crosses public port contracts, runtime diagnostics, and execution/verification documentation, but it stays within the existing Phase A runtime surface and does not introduce a new product capability, persistence model, or external interface.

## Impact

- Affected code: `new/code/port/control_types.hpp`, `new/code/legacy/motor_logic.*`, `new/code/runtime/control_loop.*`, `new/code/runtime/perception_frontend.*`, and related diagnostics/runtime-state files.
- Affected docs: `new/docs/race-finish-series.zh-CN/*` Phase A progress/roadmap documents and phase-scoped evidence references.
- Affected systems: Phase A board smoke review, actuator topology interpretation, control-loop unlock diagnostics, and follow-on IMU/encoder/motor evidence collection.
- Dependencies: the accepted direct-match runtime evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.{log,exit}` plus `hardware-discovery-retry-2026-04-15.log`, current phase-scoped smoke commands, and current OpenSpec specs `tc264-to-true-ls2k0300-adapter-layer` and `true-ls2k0300-port-workspace`.
- Expected skills: `openspec-propose`, `openspec-apply-change`, and `openspec-verify-change`.
