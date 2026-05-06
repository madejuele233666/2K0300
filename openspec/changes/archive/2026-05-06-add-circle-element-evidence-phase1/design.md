## Context

The completed `establish-bev-element-raster-pipeline` change introduced the runtime `BEVElementRasterFrame`, `steering_visual_element_pipeline.*`, and generic `VisualElementEvidenceRecord` extension records. The active README now defines the circle branch boundary: add `steering_circle_element_evidence.*`, derive `circle_left` and `circle_right` from `BEVElementRasterFrame`, keep Phase 1 evidence/debug only, and perform cross suppression only in the visual element pipeline.

The user requirement for Phase 1 is intentionally small: `cross_exit` is bilateral opening plus strict wide-white-row support; one side opening plus the opposite side straight is circle evidence; one side opening plus opposite shrink or non-straight support is bend/non-circle; neither side opening is ordinary straight or curve evidence. Phase 2 line following is explicitly out of scope.

Alignment reference scope:

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `README.md` circle + ML boundary | `steering_circle_element_evidence.*`, `steering_visual_element_pipeline.*` | Adapt | Use current documented ownership and suppression rules as the authority. |
| `new/code/legacy/steering_bev_element_raster.hpp` | `steering_circle_element_evidence.*` | Adapt | Use raster classes, projection states, metric conversion, and sampleability as detector input facts. |
| `new/code/port/visual_element_evidence_types.hpp` | circle generic records | Replicate | Reuse `VisualElementEvidenceRecord` shape instead of typed circle fields. |
| `new/code/legacy/steering_visual_element_pipeline.*` | circle registration and suppression | Adapt | Add circle aggregation beside cross while preserving cross candidate behavior. |
| `new/code/runtime/steering_frame_perception_pipeline.cpp` | runtime circle input path | Replicate | Runtime already builds raster and passes it to the element pipeline. |
| `new/code/platform/param_store.cpp` and `new/config/default_params.json` | circle parameter loading/defaults | Adapt | Extend the existing optional `BEV_ELEMENT` parameter pattern. |
| `new/user/scene_overlay_probe.cpp` | offline circle evidence observation | Adapt | Bring probe input parity up to runtime by constructing the element raster before calling the pipeline. |

Coverage report:

| Contract | Coverage |
|---|---|
| Runtime raster input exists before circle detector work | Covered by existing frame pipeline and raster contracts. |
| Generic records can carry new element evidence | Covered by existing `VisualElementEvidenceRecord`; this change only adds circle ids and semantics. |
| Cross/circle mutual ignorance | Covered by adding suppression only in the pipeline, not in either detector. |
| Phase 1 no circle candidate takeover | Covered by fixed candidate summary and no `kCircleLeft`/`kCircleRight` push. |
| Offline raw-frame verification | Partially covered today; this change must add raster construction to `scene_overlay_probe`. |

Archive topology code is intentionally not a reference contract. It may explain historical behavior, but this change does not copy opening score, scene FSM, inner island memory, trusted path logic, or archive policy state.

## Goals / Non-Goals

**Goals:**

- Add raster-backed circle evidence detection for `circle_left_raw` and `circle_right_raw`.
- Publish effective `circle_left` and `circle_right` records that future line-following work can consume.
- Preserve raw facts when cross suppresses effective circle records.
- Add append-only circle evidence parameters under `BEV_ELEMENT`.
- Update offline probe coverage so authority-baseline raw images can exercise the same element raster input path as runtime.
- Keep all circle output evidence-only in Phase 1.

**Non-Goals:**

- No Phase 2 circle entry, path generation, or line-following modification.
- No `VisualReferenceCandidate` push for circle and no visual-reference arbitration behavior change.
- No changes to `steering_bev_simple_perception.*`, lateral jump handling, ordinary far/near path recovery, `BEVReferencePath` continuity, hold, usability, lateral error, readiness, safety, yaw, or actuator logic.
- No re-projection inside the circle detector, no debug/overlay/media feedback as detector input, and no archive topology state restoration.
- No ML, roadblock, speed, motor, or board tuning behavior change.

## Decisions

### Decision 1: Circle Detector Is A Raster-Only Evidence Module

- Problem: circle needs a visual fact source without coupling to line following, cross internals, or control state.
- Alternatives: reuse sparse row scans, use current line candidate geometry, restore archive topology logic, or read `BEVElementRasterFrame`.
- Choice: add `steering_circle_element_evidence.*` that reads only `BEVElementRasterFrame` plus `RuntimeParameters.bev_element`.
- Stack Equivalent: `BEVElementRasterFrame` cell classes/projection states/metric conversion -> detector side summaries -> `VisualElementEvidenceRecord` raw circle facts.
- Named Deliverables: `new/code/legacy/steering_circle_element_evidence.hpp`, `new/code/legacy/steering_circle_element_evidence.cpp`, focused circle evidence tests, and CMake source registration.
- Failure Semantics: absent, disabled, invalid, or insufficiently sampleable raster returns absent raw circle records with deterministic reasons; it does not fall back to line, hold, or debug images.
- Boundary Examples: detector may call raster metric/cell helpers; detector may not read `CrossExitElementEvidence`, `VisualReferenceCandidate`, hold memory, safety, IMU, encoder, yaw, actuator, or `PerceptionResult`.
- Contrast Structure: this adapts the new raster pipeline and explicitly rejects archive scene state and opening-score inheritance.
- Verification Hook: unit tests for valid/invalid raster cases and board no-motion telemetry/media evidence showing circle records appear without selected-reference or actuator changes.

### Decision 2: The Recognition Rule Stays Minimal And Symmetric

- Problem: Phase 1 must decide what counts as left/right circle without dragging in turn-entry strategy.
- Alternatives: use a richer topology classifier, require inner-island memory, require line-candidate drift, or use the agreed visual fact rule.
- Choice: `circle_left` means left opening plus right straight support; `circle_right` means right opening plus left straight support. Bilateral strict wide-white-row support is cross-like and not circle; fragmented/non-straight bilateral opening is bend/non-circle; neither-open remains ordinary straight or curve evidence.
- Stack Equivalent: per-side raster opening support + opposite-side line fit + opposite-shrink rejection + strict wide-white-row saturation guard + confidence threshold.
- Named Deliverables: side-summary helper local to circle detector, configured thresholds for minimum support rows, sampleable count, opening expansion, opening expansion ratio, opposite-straight drift, opposite-shrink ratio, strict cross wide-row white ratio, and present confidence.
- Failure Semantics: saturated wide-white rows, ambiguous both-open, bend-like double opening, no-open, insufficient support, excessive opposite-side drift, opposite shrink, or low confidence fails closed as absent circle evidence.
- Boundary Examples: opposite straight is computed from raster geometry only, not from the line candidate; bends that do not expose one-side opening plus opposite straight remain negative with deterministic absent reasons such as `bend` or `opposite_straight_drift_exceeded`.
- Contrast Structure: the detector recognizes a current-frame visual fact and does not decide whether the car should enter the circle.
- Verification Hook: synthetic raster tests for left, right, both-open, no-open, sampleability failure, drift failure, and low-confidence failure; offline authority-baseline probe checks for circle/cross/bend samples.

### Decision 3: Raw And Effective Records Make Cross Suppression Explicit

- Problem: cross must suppress circle when both could appear, while debug still needs to show the raw circle detector facts.
- Alternatives: overwrite one `circle_left` record, let cross detector know about circle, let circle detector know about cross, or publish raw plus effective records from the pipeline.
- Choice: publish `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right` from `steering_visual_element_pipeline.*`. Raw records preserve detector output. Effective records mirror raw records unless `cross_exit.present=true`, in which case effective records are absent with `reason=suppressed_by_cross_exit`.
- Stack Equivalent: cross detector output + circle detector raw records -> pipeline suppression combiner -> ordered generic records.
- Named Deliverables: pipeline registration/suppression helper and tests that assert record order, raw preservation, effective suppression, and candidate list absence.
- Failure Semantics: suppression never deletes raw records; missing raster or disabled circle still publishes all four records as absent/not-evaluated facts.
- Boundary Examples: cross-specific files remain unaware of circle; circle-specific files remain unaware of cross; ML records are not suppressed by this circle/cross rule.
- Contrast Structure: aggregation owns relationships between elements; detectors own only isolated facts.
- Verification Hook: visual element pipeline tests and board no-motion evidence proving `cross_exit.present` can coexist with raw circle records while effective circle records are suppressed.

### Decision 4: Circle Evidence Is Append-Only Configuration And Wire Shape

- Problem: circle needs thresholds without breaking existing parameters or creating typed-field churn.
- Alternatives: hard-code thresholds, add typed circle fields to every protocol/debug surface, or extend existing `BEV_ELEMENT` and generic records.
- Choice: add optional `BEV_ELEMENT` circle fields and use existing `element_evidence.records` for all circle output.
- Stack Equivalent: `default_params.json` + `param_store.cpp` optional nested reads + `RuntimeParameters.bev_element` defaults -> detector thresholds -> generic record serializer.
- Named Deliverables: new fields in `BEVElementParameters`, default JSON entries, parameter parser tests, runtime parameter default tests, and documentation updates.
- Failure Semantics: missing fields use runtime defaults; malformed or out-of-range fields follow the existing optional-parameter parse-failure fallback; `CIRCLE_EVIDENCE_ENABLED=false` still emits absent records and no candidates.
- Boundary Examples: `CROSS_EXIT_TAKEOVER_ENABLED` semantics remain unchanged; no new assistant/media command is introduced; old consumers can ignore unknown records.
- Contrast Structure: appending records preserves compatibility better than adding per-element typed DTO fields.
- Verification Hook: parameter load/default tests, serializer/telemetry/media regressions, and board no-motion config snapshot evidence showing the new `BEV_ELEMENT` values are observable.

### Decision 5: Probe Input Parity Is Required For Authority-Baseline Validation

- Problem: current offline probe calls the visual element pipeline without an element raster, so raster-only circle evidence cannot be validated on raw fixtures.
- Alternatives: create a separate circle-only probe, hand-feed synthetic facts only, or make `scene_overlay_probe` match runtime input wiring.
- Choice: update `scene_overlay_probe` to build `BEVElementRasterFrame` with the same camera frame, threshold, params, and projector before calling `RunVisualElementPipeline`.
- Stack Equivalent: raw fixture -> Otsu threshold -> simple BEV rows + element raster -> visual element pipeline -> printed/serialized records.
- Named Deliverables: probe raster builder wiring, record output coverage, authority-baseline residual script updates, and documented expected sample roles.
- Failure Semantics: probe raster failures produce absent circle records with reasons; probe evidence cannot alter runtime control behavior.
- Boundary Examples: authority-baseline expectations are deterministic. `circle-1/2/3.raw` are left-circle positives with `cross_exit.present=false`, `circle_left_raw.present=true`, and effective `circle_left.present=true`. `cross-1/2/3.raw` are strict wide-white bilateral-opening cross samples with raw and effective circle records absent. `bend-1/2/3.raw` are circle negatives with raw and effective circle records absent.
- Contrast Structure: offline probe is observation parity, not a second detector pipeline.
- Verification Hook: `run_bev_simple_residual_check.sh` plus direct probe runs over authority-baseline raw images; board hook remains read-only evidence capture after upload, with no motion-specific behavior expected.

## Independent Verification Plan (STANDARD/STRICT)

- Shared sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`.
- Contracts:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Verifier runtime profile: `.codex/agents/verify-reviewer.toml`.
- Invocation: built-in subagent API with `fork_context=false`, template `verify-reviewer-inline-v3`.
- Agent lifecycle: follow `cycle_rules` in `verification-cycle-core-v1.json`; only the orchestrator may authorize resume, spawn, repair, or terminate.
- Docs-first primary surface: `openspec/changes/add-circle-element-evidence-phase1/proposal.md`, `design.md`, `specs/**/*.md`, and `tasks.md`.
- Source-first primary surface: changed circle detector, visual element pipeline, port parameter/evidence contracts, parameter loading/defaults, probe code, tests, CMake files, README/docs, and directly impacted serializers.
- Authoritative docs-first findings path: `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/attempt-<n>/findings.json`.
- Authoritative docs-first evidence path: `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/attempt-<n>/verifier-evidence.json`.
- Docs-first agent table path: `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/agent-table.json`.
- Authoritative source-first findings path: `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-<n>/findings.json`.
- Authoritative source-first evidence path: `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-<n>/verifier-evidence.json`.
- Source-first agent table path: `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/agent-table.json`.
- Valid pass requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`.
- Repository `.index/` material is optional background only; it is not a gate, verifier verdict, repair router, or closure authority.

### Review Checkpoints

- Artifact-completion docs-first review: run after `proposal`, `design`, `specs`, and `tasks` exist.
- Implementation source-first review: run after implementation and local verification settle.
- Fresh challenger requirement: source-first implementation closure requires a fresh challenger pass after the reusable working verifier reaches zero findings.

## External Repository Index Reference (Optional)

This change does not require a repository-index refresh for artifact creation. If `.index/` is used later, it remains non-authoritative background only and must not replace code search, OpenSpec validation, verifier findings, or implementation closure evidence.

## Migration Plan

- Create circle detector files and register them in build targets without changing runtime selected-reference behavior.
- Extend `BEV_ELEMENT` parameters and defaults, preserving missing-field compatibility and existing cross takeover behavior.
- Wire circle records through `steering_visual_element_pipeline.*` and existing shared serializers.
- Update `scene_overlay_probe` so offline raw image checks can observe raster-backed circle evidence.
- Run focused unit tests, probe checks, regression tests, no-upload build, and `git diff --check`.
- Board rollout is no-motion evidence capture: upload/start normally, verify assistant/media/debug evidence carries circle records and params, and verify actuator behavior remains unchanged.
- Rollback is disabling `BEV_ELEMENT.CIRCLE_EVIDENCE_ENABLED` or reverting the detector/pipeline registration; cross takeover remains controlled by its existing parameter.

## Open Questions

- None for Phase 1. Phase 2 will separately define how effective `circle_left` and `circle_right` modify line following to enter a circle.

## Risks / Trade-offs

- Minimal visual rules are easier to verify and align with the user requirement, but they can miss complex circle appearances; Phase 1 prefers fail-closed evidence over speculative entry behavior.
- Generic records are less type-specific than dedicated DTO fields, but they preserve compatibility and match the existing extension contract.
- Always emitting four circle records increases debug verbosity, but it makes raw/effective suppression explicit and easier to test.
- Raster-only detection keeps detector boundaries clean, but offline validation depends on updating probe parity before fixture expectations are meaningful.
