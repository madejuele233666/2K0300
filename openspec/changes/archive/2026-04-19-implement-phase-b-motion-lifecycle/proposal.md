## Why

`new/docs/race-finish-series.zh-CN/02-phase-b-low-speed-vehicle-motion.md` now defines Phase B as "low-speed, controllable, recoverable real-vehicle motion", but the current runtime still treats "control loop is running" as almost the same thing as "vehicle may drive". That gap makes startup, spinup, stopping, fail-safe latching, and restart behavior too implicit to validate cleanly on a real car, even though `old_2/` already proved those lifecycle semantics must be treated as first-class control behavior.

## What Changes

- Introduce a project-owned Phase B motion lifecycle for `new/` that separates process lifecycle, safety veto, motion intent, and actuator output policy instead of burying all motion semantics inside `ControlLoop::Tick()`.
- Add a dedicated motion-supervisor layer that owns `DISARMED -> START_REQUESTED -> SPINUP -> RUNNING -> STOPPING -> FAIL_SAFE_LATCHED` transitions, controlled re-arm behavior, and phase-aware speed/output shaping.
- Keep `control.veto.*` reserved for real fail-safe causes and add separate observability for "gate is clear but motion is intentionally held disarmed", so verification can distinguish "not started yet" from "fault-stopped".
- Extend runtime parameters and runtime state with Phase B motion contracts, including spinup timing, stop timing, output slew limiting, turn limiting during spinup, and fault re-arm timing.
- Add explicit controller reset boundaries for stop/fault/restart transitions so PID and attitude carry-over state does not pollute the next Phase B run.
- Update `main.cpp` and runtime ownership so product behavior only guarantees "request stop, wait for controlled stop, then exit", while test-only auto-start/auto-stop/fault-reset automation remains optional harness behavior rather than a permanent runtime contract.
- Add bounded, whitelist-based Phase B fault-injection hooks for drop-frame, IMU invalid, encoder invalid, and forced low-voltage verification without creating a second runtime entrypoint.
- Align Phase B documentation, smoke-wrapper expectations, and evidence markers with the new motion lifecycle so `new/docs/race-finish-series.zh-CN/02-phase-b-low-speed-vehicle-motion.md` becomes implementable as written.

## Capabilities

### New Capabilities
- `phase-b-motion-lifecycle`: Defines the project-owned motion supervisor, operator-intent boundary, controlled spinup/stop behavior, fail-safe latch/re-arm rules, Phase B observability, and test-only automation/fault-injection hooks for low-speed vehicle motion.

### Modified Capabilities
- `tc264-to-true-ls2k0300-adapter-layer`: Tightens the runtime contract so safety veto, motion hold-disarmed behavior, controller reset, and actuator-application observability remain project-owned rather than being inferred from vendor-facing motor calls alone.
- `true-ls2k0300-port-workspace`: Updates the accepted Phase B verification flow so board smoke, bounded runtime automation, and phase documents reflect the new motion lifecycle while keeping test harness behavior separate from long-lived product semantics.

## Risk Tier

- `STRICT`: This change modifies hardware-facing runtime behavior on a real vehicle, including when actuators may begin driving, how stop/fail-safe/restart are sequenced, how bounded smoke automation interacts with the product loop, and how Phase B evidence is interpreted. Incorrect design would create unsafe motion behavior or misleading verification evidence, so design/spec/tasks and docs-first review must be explicit.

## Impact

- Affected code: `new/code/runtime/control_loop.*`, `new/code/runtime/runtime_state.hpp`, new motion-supervisor runtime files, `new/code/legacy/pid_control.*`, `new/code/legacy/attitude_logic.*`, `new/code/runtime/perception_frontend.*`, `new/code/platform/imu_adapter.cpp`, `new/code/platform/encoder_adapter.cpp`, `new/code/platform/power_adapter.cpp`, `new/code/port/control_types.hpp`, `new/code/platform/param_store.cpp`, `new/user/main.cpp`, and `new/user/run_remote_smoke.sh`.
- Affected artifacts and docs: `new/docs/race-finish-series.zh-CN/02-phase-b-low-speed-vehicle-motion.md`, related roadmap/progress references, and Phase B verification evidence under `new/verification/` or change-local verification bundles.
- Affected runtime contracts: motion intent vs process exit, spinup/stop shaping, hold-disarmed vs fail-safe semantics, controller reset boundaries, fault-injection hooks, and marker names used to interpret Phase B runs.
- Dependencies and systems: direct-match IMU/encoder/motor paths, project-owned diagnostics, runtime parameter loading, board smoke automation, and Phase B real-vehicle verification workflows.
- Participating skills: `openspec-propose`, `openspec-architect`, `openspec-artifact-verify`, `openspec-apply-change`, and later `openspec-verify-change` once implementation begins.
