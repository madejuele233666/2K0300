## Why

The current `new/code/legacy/` directory still mixes vision, reference, and control algorithms, while `runtime/` also owns CircleV2 scene logic, reference time alignment, safety gate code, low-voltage sampling, and observability contracts. Removing `legacy/` alone would leave the same coupling under different directory names, so this change reorganizes the active steering code into explicit domain layers without changing runtime logic, algorithms, parameter semantics, JSON wire formats, or board behavior.

## What Changes

- Replace the current `legacy/` catch-all with domain directories under `new/code/`: `vision/`, `reference/`, and `control/`.
- Move CircleV2 scene/detail/reference-adapter code from `runtime/` into `vision/elements/circle_v2/`.
- Move reference time alignment into `reference/` after placing the shared motion-history contract somewhere that does not force `reference -> runtime`.
- Split `runtime/control_decision.*` into safety gate and control apply observation ownership under `safety/`.
- Move low-voltage sampler ownership into `safety/` while preserving `RuntimeState` write semantics through an explicit runtime-facing boundary.
- Move control debug snapshot/reporting and assistant telemetry view mapping into `observability/`.
- Move assistant and steering-media protocol/link code from `platform/` into `transport/`, while keeping board/socket bridge implementations in `platform/true_ls2k0300/`.
- Organize remaining runtime lifecycle, capture, pipelines, loops, and services under `runtime/` subdirectories without changing shared-state locking or service semantics.
- Update `new/user/CMakeLists.txt`, verification helper source lists, test includes, residual checks, and active documentation to use the new ownership paths.
- Preserve current behavior by limiting implementation to file moves, include updates, namespace migration where explicitly staged, build wiring, tests, and documentation.

## Capabilities

### New Capabilities

- `steering-domain-architecture`: Defines the target domain-layer structure, dependency direction, allowed temporary compromises, and migration invariants for the active steering runtime.
- `steering-architecture-migration-workflow`: Defines the staged execution plan, verification checkpoints, artifact gate, and ai-enforced workflow expectations for performing the architecture migration.

### Modified Capabilities

- None. This change is intended to preserve existing runtime behavior and contracts while reorganizing ownership and paths.

## Risk Tier

- `STRICT`: This is a cross-cutting architecture migration across `new/code/legacy/`, `new/code/runtime/`, `new/code/platform/`, `new/user/CMakeLists.txt`, active verification scripts, and active documentation. It touches safety-gate ownership, low-voltage sampling paths, assistant/steering-media transport ownership, control loop dependencies, and board build wiring. The change must use independent artifact and implementation verification because a path-only mistake can break board builds or silently weaken safety/observability boundaries even without algorithm changes.

## Impact

- Affected layers:
  - `port/`: may receive shared motion-history contract types and updated README ownership documentation.
  - `platform/`: loses host protocol/link ownership, keeps hardware adapters and `true_ls2k0300` bridge implementations.
  - `transport/`: new layer for assistant and steering-media protocol/link code plus visual-element-evidence JSON encoding helpers.
  - `vision/`: new layer for image, BEV, element evidence, visual element pipeline, and CircleV2 scene code.
  - `reference/`: new layer for visual reference orchestration, continuity/hold, usability, lateral-error debug facts, tracking geometry, readiness, and reference time alignment.
  - `control/`: new layer for motion types/supervisor, tuning state, yaw controller, wheel PID, wheel target mixer, and actuator command builder.
  - `safety/`: new layer for control gate, apply observation, and low-voltage sampler ownership.
  - `observability/`: new layer for debug snapshots, debug reporter, and telemetry view mapping.
  - `runtime/`: remains lifecycle/state/service/pipeline orchestration and keeps `RuntimeState` ownership.
- Affected build and tests:
  - `new/user/CMakeLists.txt`
  - `new/verification/tests/*.cpp`
  - `new/verification/tests/run_*.sh`
  - `new/verification/tests/common_steering_test_build.sh`
  - `new/verification/tests/run_bev_simple_residual_check.sh`
- Affected active docs:
  - `README.md`
  - `docs/WORKFLOW.md` if routing needs a new architecture entry
  - `new/code/port/README.md`
  - `new/docs/visual-element-sparse-circle-v1.zh-CN.md`
  - any active user/build docs that name source paths
- Participating skills and workflow gates:
  - `openspec-propose`
  - `openspec-artifact-verify`
  - `openspec-apply-change`
  - `openspec-verify-change`
  - `openspec-repair-change` if verifier findings require artifact or implementation repair
  - `verify-sequence/default` through the ai-enforced workflow verification-cycle contracts
