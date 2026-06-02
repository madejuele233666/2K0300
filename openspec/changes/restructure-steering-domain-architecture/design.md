## Context

The current active steering code still reflects an earlier migration topology:

- `new/code/legacy/` owns migrated but now active vision, reference, and control algorithms.
- `new/code/runtime/` owns lifecycle and state, but also owns CircleV2 scene logic, reference time alignment, motion/control domain types, tuning state, safety gate decisions, low-voltage sampling, and observability contracts.
- `new/code/platform/` owns hardware adapters, but also owns assistant and steering-media protocol/link code that is host communication rather than hardware adaptation.

The current README and workflow define the active chain as:

```text
camera frame
-> Otsu threshold
-> sparse BEV row facts / ROI metric sampler
-> element evidence
-> visual reference candidate
-> visual reference orchestration
-> selected BEVReferencePath
-> reference continuity
-> reference usability
-> reference tracking geometry
-> reference-control readiness
-> safety gate
-> yaw / turn output
-> actuator
-> debug transport
```

The migration should make the code tree express that chain. It is an architecture ownership change, not an algorithm or behavior change.

## Goals / Non-Goals

**Goals:**

- Remove active use of `new/code/legacy/` by moving files into domain layers.
- Move runtime-owned algorithm, safety, observability, and transport-like code into correct layers.
- Preserve current logic, formulas, state transitions, parameter semantics, JSON wire formats, and safety-gate priority.
- Keep `RuntimeState` and runtime shared-state locking semantics intact.
- Produce a staged plan that can be implemented and verified one slice at a time.
- Use `ai-enforced-workflow` artifact and implementation verification through `verify-sequence/default`.

**Non-Goals:**

- No BEV calibration, sampling, classification, CircleV2 algorithm, yaw control, wheel PID, mixer, actuator, safety-gate priority, low-voltage threshold, assistant command, steering-media payload, or runtime parameter behavior changes.
- No board powered-drive validation unless explicitly requested during apply.
- No `ParamStore`/configuration-layer extraction in this change.
- No deep rewrite of `RuntimeState` locking or frame-store ownership.
- No cleanup of archive/superseded documentation beyond active-path routing notes required by this migration.

## Decisions

### Decision 1: Use explicit domain layers rather than only deleting `legacy/`

**Problem being solved:** `legacy/` deletion alone would hide domain coupling in `runtime/` and `platform/`.

**Chosen option:** Introduce and populate:

```text
new/code/
  port/
  platform/
  transport/
  vision/
  reference/
  control/
  safety/
  observability/
  runtime/
```

**Alternatives considered:**

- Only move `legacy/*` into `vision/reference/control`: lower initial churn, but leaves CircleV2, safety, debug, and host protocol ownership wrong.
- Keep `assistant_*` and `steering_media_*` in `platform`: simpler includes, but confuses host protocol/link ownership with hardware adapters.
- Put all steering code under one `steering/` directory: fewer top-level directories, but weaker dependency boundaries and less testable ownership.

**Why this option was chosen:** It matches the active README chain and allows dependency checks to enforce that debug is not authority, safety is the safety owner, and runtime is orchestration rather than domain logic.

**Stack Equivalent:** Domain-oriented application core: `port` is contracts, `platform` is concrete hardware/OS adapters, `transport` is host protocol/link, domain layers compute facts/decisions, and `runtime` orchestrates state and services.

**Named Deliverables:** New directories, moved source/header files, updated includes/namespaces, CMake source-list updates, verification script updates, and active documentation routing.

**Failure Semantics:** A misplaced file is a review failure even if the code builds, because this change is about ownership and dependency direction. A behavior change is also a failure unless explicitly justified and separately specified.

**Boundary Examples:**

- `transport/assistant_protocol.*` may define JSON command/telemetry encoding.
- `platform/true_ls2k0300/assistant_bridge.*` may own concrete socket bridge calls.
- `runtime/assistant_service.*` may poll a link and mutate `RuntimeState`.
- `observability/assistant_telemetry_view.hpp` may map debug snapshots to protocol views, but must not recompute telemetry facts.

**Contrast Structure:** `platform` talks to devices/OS/vendor bridges; `transport` talks to external host protocols; `runtime` decides when services tick; domain layers compute facts or decisions without owning service lifecycle.

**Verification Hook:** Source scan for stale `new/code/legacy/` active references, forbidden domain `runtime/*` includes, and `platform/assistant_*` / `platform/steering_media_*` active ownership after migration. On-board hook: `new/user/debug.sh build` verifies board build wiring after CMake path updates.

**Feedback Loop:** Each slice moves one ownership group, updates direct tests, runs its focused tests, then records residual stale-path search output before moving on.

### Decision 2: Move shared motion-history facts out of `runtime` before moving reference time alignment

**Problem being solved:** `reference_time_alignment` belongs to reference, but today it includes `runtime/runtime_state.hpp` only to access `MotionHistory`.

**Chosen option:** Create a non-runtime motion-history contract, preferably in `port/`, and make `RuntimeState`, `SteeringFramePerceptionPipeline`, CircleV2 motion arc query, and `reference_time_alignment` consume that contract.

**Alternatives considered:**

- Move `reference_time_alignment.*` into `reference/` while still including `runtime/runtime_state.hpp`: quick but violates the target dependency graph.
- Keep `reference_time_alignment.*` in `runtime/`: preserves build stability but leaves a known reference algorithm in runtime.
- Duplicate a small history type in reference: avoids dependency but risks divergent ring-buffer semantics.

**Why this option was chosen:** It preserves a single motion-history type while avoiding `reference -> runtime`.

**Stack Equivalent:** A domain event/history DTO exposed by contracts rather than by the runtime state owner.

**Named Deliverables:** `port/motion_history_types.hpp` or equivalent, include updates in `runtime_state.hpp`, `steering_frame_perception_pipeline.*`, `steering_reference_time_alignment.*`, and tests.

**Failure Semantics:** If any non-runtime domain includes `runtime/runtime_state.hpp` for motion history after this slice, the slice is incomplete.

**Boundary Examples:**

- `RuntimeState` may contain a `port::MotionHistory`.
- `reference/reference_time_alignment.*` may accept `const port::MotionHistory&`.
- `reference/` must not call runtime reset helpers or lock runtime mutexes.

**Contrast Structure:** Runtime owns storage and locking; `port` owns the motion-history value shape; reference consumes a snapshot.

**Verification Hook:** `run_reference_time_alignment_test.sh` plus include scan for `new/code/reference` containing `runtime/`.

**Feedback Loop:** First migrate the type and test alignment behavior, then move the time-alignment source path.

### Decision 3: Treat `steering_frame_perception_pipeline` as runtime pipeline orchestration, not pure vision

**Problem being solved:** The pipeline executes Otsu, BEV, element evidence, visual reference selection, hold, usability, lateral error, tracking geometry, and readiness; moving it to `vision/` would make vision depend on reference/control readiness concepts.

**Chosen option:** Rename or move it to `runtime/pipelines/steering_frame_pipeline.*`, keeping `perception_frontend.*` as the runtime scheduler.

**Alternatives considered:**

- Move the full file into `vision/`: wrong because it orchestrates reference/readiness stages.
- Split the entire file into separate orchestrators immediately: architecturally appealing, but larger risk for a no-logic-change migration.
- Leave the current name/path unchanged: lower churn, but keeps an inaccurate runtime root name and hides the pipeline role.

**Why this option was chosen:** It improves naming and organization without changing pipeline order or memory semantics.

**Stack Equivalent:** Application-service pipeline coordinator: a runtime service object composes domain components without owning their algorithms.

**Named Deliverables:** `runtime/pipelines/steering_frame_pipeline.*`, updated `perception_frontend.*`, CMake entries, scene overlay probe includes, and pipeline-related tests.

**Failure Semantics:** If the migration changes pipeline stage order, reference hold behavior, or `PerceptionResult` fields, it violates the behavior-preservation goal.

**Boundary Examples:**

- `vision` owns `RunVisualElementPipeline`.
- `reference` owns usability/tracking/readiness functions.
- `runtime/pipelines` invokes them in the current order and publishes the result.

**Contrast Structure:** Domain functions do what to compute; runtime pipeline decides when and in what sequence within one frame.

**Verification Hook:** `run_bev_simple_perception_test.sh`, `run_visual_element_evidence_test.sh`, `run_visual_reference_orchestration_test.sh`, `run_scene_overlay_probe_authority_baseline_test.sh`, and residual check for old pipeline path.

**Feedback Loop:** Move domain functions first; move/rename pipeline only after called headers have stable new paths.

### Decision 4: Split safety gate from apply observation

**Problem being solved:** `runtime/control_decision.*` currently combines safety gate evaluation and control-cycle apply observation.

**Chosen option:** Create `safety/control_gate.*` for `ControlGateInputs`, `ControlGateDecision`, `ControlVetoReason`, `EvaluateControlGate`, and veto string/diagnostic mapping. Create `safety/control_apply_observation.*` for `ControlCycleInputs`, `ControlCycleObservation`, `ControlApplyOutcome`, `ObserveControlCycle`, and nonzero command observation.

**Alternatives considered:**

- Move the whole file to `safety/control_decision.*`: lower churn but keeps mixed naming.
- Move apply observation to `observability`: tempting because it describes results, but it is still a safety/control-cycle fact used by runtime state and snapshots.

**Why this option was chosen:** It preserves behavior while making the fixed safety-gate priority auditable.

**Stack Equivalent:** Guard policy plus post-apply fact extraction.

**Named Deliverables:** Two safety source/header pairs, updated `RuntimeState`, `control_loop`, `control_debug_snapshot`, tests, and residual checks.

**Failure Semantics:** Any change to veto priority order is a safety regression.

**Boundary Examples:**

- `safety/control_gate.*` decides low voltage before perception stale.
- `safety/control_apply_observation.*` describes whether an already composed command was applied, held, suppressed, or failed.

**Contrast Structure:** Gate decides whether output is allowed; observation records what happened when runtime attempted output.

**Verification Hook:** `run_reference_usability_lateral_error_test.sh`, `run_assistant_telemetry_selftest.sh`, `run_steering_media_selftest.sh`, residual safety priority checks in `run_bev_simple_residual_check.sh`, and no-motion board startup safety reason capture if board access is used.

**Feedback Loop:** Split the header/API without formula changes, then run control and telemetry tests before continuing.

### Decision 5: Move low-voltage safety ownership without changing runtime state semantics

**Problem being solved:** `LowVoltageSampler` is safety input ownership, but it currently writes `RuntimeState` directly.

**Chosen option:** Move safety decision/sample maintenance into `safety/`, while preserving the current runtime-facing state update boundary. The low-risk implementation may temporarily keep a runtime-facing adapter or explicit dependency note, but it must not silently make safety a runtime implementation layer.

**Alternatives considered:**

- Leave sampler in `runtime/`: low churn but leaves safety gate input outside safety.
- Fully extract an independent safety state object now: cleaner, but changes ownership/locking semantics beyond the no-logic-change goal.

**Why this option was chosen:** It improves ownership while respecting the explicit non-goal of changing `RuntimeState` locks and atomics.

**Stack Equivalent:** Safety input service with runtime-owned persistence.

**Named Deliverables:** `safety/low_voltage_sampler.*` or split safety/runtime boundary files, updated `main.cpp`, startup/low-voltage tests, and docs.

**Failure Semantics:** Invalid samples must still fail safe; startup sample reuse and transition diagnostics must remain intact.

**Boundary Examples:**

- `platform/power_adapter.*` samples ADC/threshold facts.
- `safety/low_voltage_sampler.*` interprets and schedules low-voltage safety input.
- `runtime` remains allowed to persist the result in `RuntimeState`.

**Contrast Structure:** Hardware adapter obtains the sample; safety interprets it; runtime stores and schedules it.

**Verification Hook:** `run_power_adapter_threshold_test.sh`, `run_startup_low_voltage_order_test.sh`, and board no-motion startup smoke with low-voltage threshold diagnostics.

**Feedback Loop:** Move sampler after safety gate split so safety include paths stabilize first.

### Decision 6: Separate transport and observability from platform/runtime

**Problem being solved:** Assistant and steering-media protocol/link code is host communication, not hardware platform. Debug snapshots and telemetry view mapping are observability, not runtime control.

**Chosen option:** Move protocol/link helpers into `transport/`, move debug snapshot/reporter/telemetry mapping into `observability/`, and update runtime services to depend on those layers.

**Alternatives considered:**

- Keep protocol in platform because link `.cpp` calls board bridge functions: conflates the default transport implementation with the bridge dependency.
- Move debug snapshot into `port`: too much business-shaped debug structure for a contract-only layer.
- Keep debug snapshot in runtime: preserves paths but keeps observability in lifecycle code.

**Why this option was chosen:** It preserves the README rule that debug/transport serialize facts and must not become authority.

**Stack Equivalent:** Transport adapter layer plus telemetry/diagnostic presenter layer.

**Named Deliverables:** `transport/assistant_*`, `transport/steering_media_*`, `transport/visual_element_evidence_json.hpp`, `observability/control_debug_snapshot.hpp`, `observability/control_debug_reporter.*`, `observability/assistant_telemetry_view.hpp`, service include updates, tests.

**Failure Semantics:** Any assistant JSON field or steering-media envelope change is a protocol regression unless separately specified.

**Boundary Examples:**

- `observability/assistant_telemetry_view.hpp` maps `ControlDebugSnapshot` to `transport::AssistantTelemetryView`.
- `transport/steering_media_link.cpp` may instantiate a default transport backed by `platform/true_ls2k0300/steering_media_bridge.hpp`.
- `runtime/steering_media_service.*` decides when to publish and which frame to attach.

**Contrast Structure:** Observability shapes facts for display; transport encodes/sends; runtime schedules publishing.

**Verification Hook:** `run_assistant_telemetry_selftest.sh`, `run_steering_media_selftest.sh`, `common_steering_test_build.sh`, host steering-media capture selftest if applicable, and board no-motion assistant/media readiness logs.

**Feedback Loop:** Move transport before moving assistant telemetry view so observability can depend on `transport/assistant_protocol.hpp`.

### Decision 7: Use staged namespace migration

**Problem being solved:** File moves alone are path churn; namespace changes are broader API churn. Doing both everywhere at once makes failures harder to localize.

**Chosen option:** Prefer file path moves with namespace changes staged per domain group only when that group is already locally verified. If a namespace is retained temporarily, record the exception and remove it before deleting `legacy/`.

**Alternatives considered:**

- Change all namespaces in one sweep: cleaner final state but high blast radius.
- Never change namespaces: less churn but leaves `ls2k::legacy` in active code after deleting `legacy/`.

**Why this option was chosen:** It keeps each slice under a verifiable scope while still aiming for final domain names.

**Stack Equivalent:** Incremental API migration with compile/test gates per module.

**Named Deliverables:** Namespace updates for `vision`, `reference`, `control`, `safety`, `transport`, `observability`, and updated call sites/tests.

**Failure Semantics:** Final implementation cannot leave active public domain APIs under `ls2k::legacy` unless a task explicitly documents a remaining compromise and verifier accepts it.

**Boundary Examples:**

- `ls2k::control::WheelPidController` is acceptable final ownership.
- `ls2k::legacy::WheelPidController` under `new/code/control` is only a temporary migration bridge.

**Contrast Structure:** Path ownership proves source placement; namespace ownership proves API placement.

**Verification Hook:** ast-grep or `rg` namespace scans, CMake build, focused tests.

**Feedback Loop:** After each domain group compiles, run namespace residual checks and repair call sites.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `README.md`, `docs/WORKFLOW.md`, `new/code/port/README.md`, active `new/docs/visual-element-sparse-circle-v1.zh-CN.md`, current `new/code` files.
- Primary feedback loop:
  staged file/domain migration with focused tests after each slice, broad build/test after all slices, then independent source-first verification.
- Prototype question, if any:
  whether `MotionHistory` should live in `port/motion_history_types.hpp` or another port contract header. This must be answered in the first implementation slice before moving reference time alignment.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass requirements, subject binding, and current-state `agent-table.json`.
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when present, but do not create auxiliary review gates for them.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow:

- checkpoints use the same `active/non_active` verification cycle
- docs-first checkpoints use changed `proposal/specs/design/tasks` as the primary surface
- source-first checkpoints use changed code, tests, and directly impacted code as the primary surface
- approved docs remain reference material when source-first review runs
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.
- Invoke verifier reviews through the built-in subagent API, not shell or
  handwritten review summaries.
- Spawn verifier reviews with `fork_context=false` and pass only the minimal
  verification bundle, optional index context, and output paths.
- Use invocation template `verify-reviewer-inline-v3`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in `continuation_probe`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and it must not substitute its own judgment for verifier output

Shared field groups from `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`:

- `invocation_common_required`
- `output_paths_required`
- `verifier_evidence_required`
- `valid_pass_requirements`
- `partial_scope_rule`

Review completion contract:

- execution evidence MUST record:
  - `review_goal`
  - `review_phase`
  - `review_scope`
  - `review_coverage`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
- each checkpoint MUST maintain `agent-table.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path:
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`
- Invocation mechanism: built-in subagent API with `fork_context=false`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `continuation_probe` to distinguish resume from dedicated recovery spawn
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path:
  `review/review-runs/restructure-steering-domain-architecture/<phase>/attempt-<n>/findings.json`
- Verifier execution evidence JSON path:
  `review/review-runs/restructure-steering-domain-architecture/<phase>/attempt-<n>/verifier-evidence.json`
- Agent table path:
  `review/review-runs/restructure-steering-domain-architecture/agent-table.json`
- Continuation target on pass:
  docs-first pass allows implementation to begin; source-first pass allows archive/sync/closure steps.

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: `proposal.md`, `specs/steering-domain-architecture/spec.md`, `specs/steering-architecture-migration-workflow/spec.md`, `design.md`, `tasks.md`
- active-change source-first review: changed source paths under `new/code`, changed build files under `new/user`, changed verification tests/scripts, and changed active docs

## Migration Plan

### Phase 0: Preflight and guardrails

- Record current source/build/test/doc surfaces and dirty worktree boundaries.
- Run `openspec status --change restructure-steering-domain-architecture --json`.
- Complete docs-first artifact verification through `verify-sequence/default`.
- Decide and document the motion-history contract destination.

### Phase 1: Control domain

- Move `runtime/motion_types.*`, `runtime/motion_supervisor.*`, `runtime/tuning_state.*`, `legacy/steering_yaw_controller.*`, `legacy/wheel_pid.*`, `legacy/wheel_target_mixer.*`, and `legacy/actuator_command_builder.*` into `control/`.
- Update `control_loop.*`, assistant service tuning references, tests, and CMake.
- Verify wheel mixer, reference/usability/yaw tests, assistant telemetry test, and build source lists.

### Phase 2: Safety domain

- Split `runtime/control_decision.*` into control gate and apply observation files under `safety/`.
- Move or boundary-wrap `low_voltage_sampler.*` under safety ownership.
- Update `RuntimeState`, `control_loop`, `main.cpp`, observability, tests, and residual checks.
- Verify low-voltage order, power adapter threshold, control/safety tests, and no stale safety priority changes.

### Phase 3: Transport and observability

- Move assistant and steering-media protocol/link files into `transport/`.
- Move visual-element evidence JSON helper into `transport/`.
- Move debug snapshot, debug reporter, and assistant telemetry view into `observability/`.
- Update runtime services and selftests.
- Verify assistant telemetry and steering-media selftests plus transport residual scans.

### Phase 4: Reference contract and reference domain

- Move shared motion-history type to `port/`.
- Move `steering_reference_time_alignment.*` into `reference/`.
- Move visual reference orchestration, usability, lateral error, tracking geometry, readiness, and extracted reference continuity/hold into `reference/`.
- Verify reference time alignment, usability/lateral error, tracking geometry, visual reference orchestration, and residual scans.

### Phase 5: Vision domain

- Move Otsu, BEV projector, BEV simple perception, BEV raster, interval edges, single-boundary offset, reference connectivity, cross/circle element evidence, visual element evidence, and visual element pipeline into `vision/`.
- Move CircleV2 scene, scene frame view, reference adapter, and detail files into `vision/elements/circle_v2/`.
- Update `runtime/pipelines`, scene overlay probe, tests, and CMake.
- Verify BEV simple perception, visual element evidence, CircleV2 scene, visual reference orchestration, and scene overlay authority baseline.

### Phase 6: Runtime subdirectory organization

- Move startup/shutdown into `runtime/lifecycle/`.
- Move camera capture and frame store into `runtime/capture/`.
- Move `steering_frame_perception_pipeline.*` to `runtime/pipelines/steering_frame_pipeline.*`.
- Move `control_loop.*` to `runtime/loops/`.
- Move assistant and steering media services into `runtime/services/`.
- Update includes, CMake, tests, and active docs.

### Phase 7: Cleanup, docs, and final verification

- Delete active `legacy/` directory after residual checks pass.
- Update active docs and avoid rewriting archive/superseded history except routing notes.
- Run focused tests, broad helper builds, residual checks, and board-aware smoke if available.
- Run source-first independent verification through `verify-sequence/default` until valid active pass.

## Open Questions

- Should the shared motion-history contract be named `port/motion_history_types.hpp` or live in `port/sensor_sample_types.hpp`? The first implementation slice must decide this before moving `reference_time_alignment`.
- Should low-voltage sampler be implemented as a safety-owned class that still accepts `RuntimeState&`, or split into safety core plus runtime state writer? The preferred design is split or explicit runtime-facing boundary; preserving lock semantics is more important than cosmetic purity.
- Should namespace migration happen in each phase or after all path moves? The default is per-domain namespace migration once a domain slice compiles, with no final active `ls2k::legacy` symbols.

## Risks / Trade-offs

- **Path churn risk:** CMake and direct shell test scripts hardcode many source paths. Mitigation: update and test per slice.
- **Safety regression risk:** Safety gate priority and low-voltage invalid-sample behavior are fail-safe contracts. Mitigation: split safety first with focused tests and residual priority checks.
- **Protocol regression risk:** Assistant and steering-media wire formats must remain stable. Mitigation: selftests and no intentional JSON/envelope field changes.
- **Dependency inversion risk:** Moving files without first moving shared types can create forbidden `reference -> runtime` or `safety -> runtime` dependencies. Mitigation: motion-history preflight and explicit low-voltage boundary.
- **Dirty worktree risk:** Current checkout contains many unrelated modifications. Mitigation: implementation must isolate this change to relevant source/build/test/doc files and never revert unrelated user work.
- **Board evidence risk:** Local tests do not prove board runtime health. Mitigation: include board build/no-motion smoke tasks and clearly separate local evidence from board evidence.
