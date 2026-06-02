## Context

`new/docs/visual-element-sparse-circle-v6.zh-CN.md` defines a control-chain change, not a visual element or path-generation change. The current runtime selects or holds a `BEVReferencePath`, computes `ReferenceUsability`, computes a weighted future `ReferenceLateralErrorEstimate`, and feeds that single float into `SteeringYawController::ComputeTurnOutputTarget()`.

That single value blends current offset and future path shape. V6 introduces one neutral geometry layer after selected/held/time-aligned reference selection so the controller can consume same-layer facts: `lateral_offset_m`, `heading_error_rad`, and `curvature_m_inv`.

## Goals / Non-Goals

**Goals:**

- Add `ReferenceTrackingGeometry` as a port-owned neutral fact.
- Compute tracking geometry from the final control reference path after usability, both in perception-frame and control-time-aligned paths.
- Move reference-control readiness from lateral-error authority to tracking-geometry authority.
- Move yaw target computation from a single weighted lateral-error input to lateral, heading, and curvature terms.
- Expose tracking geometry and yaw-term decomposition in debug/media/assistant evidence.
- Keep the change minimal and parameterized: three control gains plus fit minimum samples.

**Non-Goals:**

- No CircleV2 FSM, event observer, geometry observer, composer, or candidate generation change.
- No ordinary/cross/single-boundary/path-connectivity helper change.
- No visual reference arbitration, hold policy, safety gate, wheel mixer, wheel PID, PWM, or motor adapter change.
- No gyro desired-yaw-rate redesign in this change.
- No forward-window or anchor parameter in the first release.

## Decisions

### Decision 1: Add a neutral reference tracking geometry helper

**Problem:** The control layer needs path geometry facts without making path generators, selectors, or scenes know steering-control gains.

**Chosen approach:** Add `new/code/port/reference_tracking_geometry_types.hpp` and `new/code/legacy/steering_reference_tracking_geometry.hpp/.cpp`. The helper consumes only `BEVReferencePath`, `ReferenceUsability`, and `BEVControlModelParameters`. It uses the leading usable prefix and a simple quadratic fit to populate `ReferenceTrackingGeometry`.

**Alternatives considered:** Extending `ReferenceLateralErrorEstimate` would keep the misleading weighted-lateral authority. Computing curvature inside CircleV2 or ordinary path builders would couple scene/path ownership to control behavior. Computing terms directly in `SteeringYawController` would hide geometry failure semantics inside control law.

**Stack Equivalent:** C++17 port DTO plus legacy free-function helper, parallel to existing reference-usability/lateral-error helper layering.

**Named Deliverables:** `reference_tracking_geometry_types.hpp`, `steering_reference_tracking_geometry.hpp/.cpp`, focused tracking-geometry tests.

**Failure Semantics:** If usability is false, samples are insufficient, the fit is degenerate, or outputs are non-finite, return `computed=false` with a deterministic `reason`; do not invent fallback curvature or silently reuse prior geometry.

**Boundary Examples:** The helper does not accept `VisualReferenceCandidate`, CircleV2 memory, cross evidence, selector result metadata, control gate state, wheel targets, or PWM values.

**Contrast Structure:** This is not another path generator; it is an interpreter of the already selected reference path.

**Verification Hook:** Local geometry tests cover straight, offset straight, curved, and insufficient-sample paths. On-board hook is steering snapshot/media evidence showing `tracking_geometry` for the same selected reference.

**Feedback Loop:** Focused C++ tests plus steering-media selftest ensure the helper compiles into both runtime and evidence surfaces.

### Decision 2: Treat tracking geometry as readiness authority

**Problem:** Reference control can currently become ready because lateral error was computed even though V6's required control facts are absent.

**Chosen approach:** Update `ReferenceControlReadiness` to consume `ReferenceTrackingGeometry` and require `computed=true` plus finite geometry values. Keep stale/hold/alignment and other existing readiness checks in their current owners.

**Alternatives considered:** Readiness could check both old lateral error and new geometry, but that keeps two authorities. The yaw controller could reject uncomputed geometry, but that moves readiness failure into the control law.

**Stack Equivalent:** Existing readiness helper signature changes from lateral-error estimate to tracking geometry estimate.

**Named Deliverables:** Updated `steering_reference_control_readiness.hpp/.cpp` and readiness tests.

**Failure Semantics:** Uncomputed geometry yields `ready=false` and a reason such as `tracking_geometry_uncomputed`; it does not reset reference selection, scene state, or hold memory.

**Boundary Examples:** Readiness does not read lateral/heading/curvature gains, gyro gains, wheel mixer state, or PWM limits.

**Contrast Structure:** This is a fact-availability gate, not a controller and not a visual candidate filter.

**Verification Hook:** Tests assert insufficient geometry vetoes reference control while valid geometry allows the existing path through. On-board hook is `reference_control.ready=false` with a tracking-geometry reason in snapshots.

**Feedback Loop:** Existing reference-control-not-ready regressions plus new focused readiness tests.

### Decision 3: Feed yaw control from geometry terms

**Problem:** A single `weighted_lateral_error_m` control input cannot tune lateral correction and curvature feedforward independently.

**Chosen approach:** Update `SteeringYawController::ComputeTurnOutputTarget()` to accept `ReferenceTrackingGeometry`. Compute `lateral_term`, `heading_term`, and `curvature_term` from separate gains, then limit the sum through the existing raw turn-output limit.

**Alternatives considered:** Adding curvature to the old weighted lateral error is smaller but double-counts path shape and keeps the old semantic problem. Creating separate lateral and curvature controllers is overbuilt and would duplicate saturation/memory concerns.

**Stack Equivalent:** One existing controller method with a richer input DTO and a richer computation result.

**Named Deliverables:** Updated yaw controller header/source, BEV control-model parameters, default config/docs, and yaw-controller tests.

**Failure Semantics:** The controller assumes readiness has provided computed finite geometry. Term calculation is deterministic and bounded by the existing turn output limit.

**Boundary Examples:** `WheelTargetMixer` still receives only the applied turn output. Wheel PID and PWM layers never see geometry facts.

**Contrast Structure:** This is one control law with decomposed inputs, not scene-specific steering policy.

**Verification Hook:** Tests verify individual gains affect only their corresponding terms and that the final target is limited as before. On-board hook is yaw-control term decomposition in media/control snapshots.

**Feedback Loop:** Controller tests and steering-media selftest show the computation and serialization agree.

### Decision 4: Publish geometry and term decomposition as evidence

**Problem:** Curvature-aware steering cannot be tuned if telemetry only exposes the final turn target or old weighted lateral error.

**Chosen approach:** Add `tracking_geometry` to control debug snapshots and media/assistant serialization. Extend `yaw_control` serialization with `lateral_term`, `heading_term`, `curvature_term`, and `turn_output_target`.

**Alternatives considered:** Keeping telemetry unchanged reduces code churn but makes field tuning opaque. Recomputing telemetry in serializers risks drift from control behavior.

**Stack Equivalent:** Runtime-owned snapshot fields serialized by existing steering media and assistant protocol adapters.

**Named Deliverables:** Updated `control_debug_snapshot`, `control_debug_reporter`, steering media protocol/service, assistant protocol, scene overlay probe, and media selftest.

**Failure Semantics:** Telemetry serializes the facts already computed by runtime/control; it does not recompute geometry and does not alter readiness.

**Boundary Examples:** Host UI/probe can display values, but the board runtime protocol remains one-way evidence publication.

**Contrast Structure:** This is observability, not control ownership and not a new command channel.

**Verification Hook:** Selftests parse config/snapshot JSON and assert the new groups/fields exist. On-board hook is captured steering media `image_frame.steering_snapshot`.

**Feedback Loop:** `run_steering_media_selftest.sh` and scene overlay probe baseline.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `new/docs/visual-element-sparse-circle-v6.zh-CN.md`, `openspec/specs/steering-tuning-media-observability/spec.md`, `openspec/specs/dual-wheel-motion-control/spec.md`, `new/code/legacy/steering_reference_lateral_error.cpp`, `new/code/legacy/steering_yaw_controller.cpp`, `new/code/runtime/control_loop.cpp`
- Primary feedback loop:
  focused C++ tests, media/probe tests, local build, then OpenSpec docs-first/source-first verification.
- Prototype question, if any:
  none; the first release deliberately uses a simple leading-prefix fit and leaves gyro desired-yaw-rate redesign for a later change.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass requirements, subject binding, and current-state `agent-table.json`
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when present, but do not create auxiliary review gates for them

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
- source-first checkpoints use changed code, changed tests, and directly impacted code as the primary surface
- approved docs remain reference material when source-first review runs
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in `continuation_probe`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume, spawn, repair, or terminate

Shared field groups from `verification-cycle-core-v1.json` and
`verification-cycle-openspec-adapter-v1.json`:

- `invocation_common_required`
- `output_paths_required`
- `verifier_evidence_required`
- `valid_pass_requirements`
- `partial_scope_rule`
- `subject_required_any_of`
- `findings_required`
- `finding_object_required`
- `finding_semantics`
- `repair_routing_rules`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path:
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`
- Invocation:
  built-in subagent API with `fork_context=false` and a minimal verification bundle
- Invocation template id: `verify-reviewer-inline-v3`
- Docs-first verifier-subagent findings JSON path:
  `review/review-runs/add-reference-tracking-geometry-v6/docs-first-findings.json`
- Docs-first verifier execution evidence JSON path:
  `review/review-runs/add-reference-tracking-geometry-v6/docs-first-verifier-evidence.json`
- Source-first verifier-subagent findings JSON path:
  `review/review-runs/add-reference-tracking-geometry-v6/findings.json`
- Source-first verifier execution evidence JSON path:
  `review/review-runs/add-reference-tracking-geometry-v6/verifier-evidence.json`
- Agent table path:
  `review/review-runs/add-reference-tracking-geometry-v6/agent-table.json`
- Continuation target on pass:
  sync specs and archive this change after implementation and the requested extra pass

Checkpoint-specific primary surfaces:

- docs-first review: `openspec/changes/add-reference-tracking-geometry-v6/proposal.md`, `design.md`, `tasks.md`, and `specs/**/*.md`
- source-first review: changed control geometry, readiness, yaw controller, parameter, telemetry, config, and test files, plus `new/docs/visual-element-sparse-circle-v6.zh-CN.md`

## Migration Plan

1. Create and verify OpenSpec artifacts.
2. Add neutral tracking geometry type/helper and focused tests.
3. Thread tracking geometry through `PerceptionResult`, perception pipeline, and control-time alignment.
4. Update readiness and yaw controller to consume tracking geometry.
5. Add control-model parameters and publish them in config/media snapshots.
6. Add snapshot/media/assistant fields for geometry and yaw-term decomposition.
7. Run focused tests, local build, OpenSpec verify, and the requested extra verify pass.
8. Sync delta specs into main specs and archive the change.

Rollback:

- Revert the helper, parameter surface, readiness/yaw-controller signature changes, and telemetry additions as one control-chain change. Path generation, visual element recognition, selector, wheel mixer, wheel PID, and PWM layers are unaffected by rollback.

Board deployment/smoke consideration:

- This change affects steering behavior. Local verification is the required completion gate for this change; a later supervised board smoke can inspect steering-media `tracking_geometry` and yaw-term fields before motion testing.

## Open Questions

- None for the first release. Forward-window/anchor fitting and desired-yaw-rate gyro feedback are intentionally later changes.

## Risks / Trade-offs

- A quadratic leading-prefix fit is simple and tuneable but may not be ideal for all path shapes. The first release accepts this to avoid scene-specific policy and hidden local-window semantics.
- Keeping legacy lateral-error fields for comparison can confuse readers if telemetry does not clearly expose tracking geometry as the V6 control input; selftests must cover the new primary fields.
- Curvature feedforward changes steering behavior and may require parameter tuning. Separate gains and published term decomposition are the intended tuning surface.
