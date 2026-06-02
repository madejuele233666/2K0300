## Context

`new/docs/visual-element-sparse-circle-v5.zh-CN.md` defines a focused reference-path fix: disable the old lateral-jump rejection path and add a zero-copy black-barrier connectivity check for current-frame visual candidates. The current code already has sparse rows, ordinary/cross/CircleV2 candidate generation, and `SelectVisualReference()` as a single aggregation point, but it still accepts candidates without checking whether adjacent samples cross black pixels in the current frame.

## Goals / Non-Goals

**Goals:**

- Add a neutral connectivity helper that reads the current grayscale frame without constructing a dense BEV raster.
- Apply the helper once at the runtime visual-candidate aggregation boundary.
- Keep ordinary, cross, CircleV2, selector, hold-last, and safety gates mutually unaware.
- Add `REFERENCE_LATERAL_JUMP_GATE_M` as an explicit disabled-by-default gate.
- Preserve `SPARSE_ROW_COUNT` prefix semantics and publish both V5 geometry controls in config snapshots.
- Verify with focused local tests, build, and OpenSpec docs/source review.

**Non-Goals:**

- No CircleV2 FSM/event/reducer changes.
- No cross detector or arbitration priority changes.
- No hold-last connectivity gate in V5.
- No dense BEV control raster or copied grayscale frame.
- No new confidence model or per-scene connectivity policy.

## Decisions

### Decision 1: Put connectivity in a neutral legacy helper

**Problem:** Ordinary, cross, and CircleV2 all output BEV reference paths, but none should learn another scene's path validation details.

**Chosen approach:** Add `new/code/legacy/steering_reference_connectivity.hpp/.cpp` with `ReferenceConnectivityFrameView` and `ReferencePathHasNoBlackSegments()`. The helper consumes only `LegacyCameraFrameView`, `BEVProjector`, threshold, classification parameters, and `BEVReferencePath`.

**Alternatives considered:** Putting checks inside CircleV2, cross, or ordinary builders would duplicate logic and couple generators to connectivity policy. Putting it in `SelectVisualReference()` would make the selector know camera/projector facts.

**Stack Equivalent:** C++17 free function in `legacy/`, using existing `BEVProjector` and `ClassifyBevPixel`.

**Named Deliverables:** `steering_reference_connectivity.hpp/.cpp`, focused helper tests.

**Failure Semantics:** A black pixel on any adjacent leading segment rejects the path. Projection failed, out-of-frame, invalid, and unknown are not black barriers in V5.

**Boundary Examples:** The helper receives no candidate kind, confidence, circle direction, cross evidence, or FSM state.

**Contrast Structure:** This is not a scene validator; it is a path segment black-barrier checker.

**Verification Hook:** Local helper tests construct synthetic grayscale frames and assert black-barrier rejection and white-segment acceptance. On-board hook is steering-media candidate path overlay after deployment; blocked candidates should be absent from selector-visible candidate paths.

**Feedback Loop:** `run_bev_simple_perception_test.sh` exercises helper behavior in the same compile unit family as row scanning.

### Decision 2: Gate candidates at the runtime aggregation boundary

**Problem:** Current-frame candidates are assembled in `SteeringFramePerceptionPipeline::ProcessFrame()` before selection. Gating any earlier leaks policy into generators; gating later lets invalid candidates enter selector reasoning.

**Chosen approach:** Build a `ReferenceConnectivityFrameView` in `steering_frame_perception_pipeline.cpp`, filter `line_candidate`, `element_result.candidates`, and optional `circle_candidate`, append only connected candidates to `candidate_paths`, then call `SelectVisualReference()`.

**Alternatives considered:** Calling the helper in each adapter is simple locally but creates multiple call sites and likely drift. Calling it in reference usability is too late because it changes selected-reference health rather than candidate admission.

**Stack Equivalent:** One small lambda or helper in the runtime pipeline around the existing candidate vector.

**Named Deliverables:** Updated `steering_frame_perception_pipeline.cpp` and pipeline-facing tests/probe coverage.

**Failure Semantics:** Blocked current-frame visual candidates are omitted from selector input. Hold-last remains available later through the existing continuity path if selected visual reference is unusable.

**Boundary Examples:** `CircleV2Scene` still outputs only `CircleV2ReferencePlan`; `AdaptCircleV2ReferencePlan()` still only packages it; `SelectVisualReference()` still only arbitrates the candidates it receives.

**Contrast Structure:** This is a current-frame visual output gate, not element recognition, not FSM reset, and not safety control.

**Verification Hook:** A runtime compile/build plus scene overlay probe confirms selector-visible candidate paths still serialize and CircleV2/cross behavior compiles through the central gate. Board hook is no-motion or supervised steering media capture if hardware is used.

**Feedback Loop:** `run_scene_overlay_probe_authority_baseline_test.sh` and full build catch integration regressions.

### Decision 3: Make the old lateral jump gate explicit and disabled by default

**Problem:** `ReferenceMaxJump()` currently derives business rejection from `LATERAL_STEP_M`, conflating sampling resolution with path continuity.

**Chosen approach:** Add `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M` with default `1000.0` and route existing jump checks through that parameter. Keep `LATERAL_STEP_M` strictly as sampling resolution.

**Alternatives considered:** Deleting jump checks immediately would be larger and risk touching more ordinary extraction code. Leaving the hidden formula would violate V5.

**Stack Equivalent:** New `float` in `BEVGeometryParameters`, parser validation, default JSON, docs, and media snapshot serialization.

**Named Deliverables:** Updated `bev_geometry_types.hpp`, `param_store.cpp`, `steering_media_protocol.cpp`, `default_params.json`, `default_params.md`, and tests.

**Failure Semantics:** Invalid parameter values follow existing parse-failure fallback behavior.

**Boundary Examples:** The connectivity helper does not read this parameter; ordinary reference builder reads it only where old jump checks already existed.

**Contrast Structure:** This is not a new continuity algorithm; it is an explicit kill switch for the old heuristic.

**Verification Hook:** Parameter tests verify parsing, defaults parity, and media snapshot output. Board hook is config snapshot inspection in steering media capture.

**Feedback Loop:** `run_runtime_parameter_defaults_test.sh`, `run_param_store_load_runtime_parameters_test.sh`, and `run_steering_media_selftest.sh`.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `new/docs/visual-element-sparse-circle-v5.zh-CN.md`,
  `openspec/specs/ordinary-bev-reference/spec.md`,
  `openspec/specs/sparse-circle-v2-scene/spec.md`,
  `openspec/specs/steering-tuning-media-observability/spec.md`,
  `new/code/runtime/steering_frame_perception_pipeline.cpp`
- Primary feedback loop:
  focused local C++ tests and `SKIP_UPLOAD=1 ./new/user/debug.sh build`, then OpenSpec docs-first/source-first verifier passes.
- Prototype question, if any:
  none; V5 already fixes the black-barrier and zero-copy constraints.
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
- source-first checkpoints use changed code, tests, and directly impacted code as the primary surface
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
  `review/review-runs/add-bev-reference-connectivity-v5/docs-first-findings.json`
- Docs-first verifier execution evidence JSON path:
  `review/review-runs/add-bev-reference-connectivity-v5/docs-first-verifier-evidence.json`
- Source-first verifier-subagent findings JSON path:
  `review/review-runs/add-bev-reference-connectivity-v5/findings.json`
- Source-first verifier execution evidence JSON path:
  `review/review-runs/add-bev-reference-connectivity-v5/verifier-evidence.json`
- Agent table path:
  `review/review-runs/add-bev-reference-connectivity-v5/agent-table.json`
- Continuation target on pass:
  apply implementation, sync specs, and archive this change

Checkpoint-specific primary surfaces:

- docs-first review: `openspec/changes/add-bev-reference-connectivity-v5/proposal.md`, `design.md`, `tasks.md`, and `specs/**/*.md`
- source-first review: changed code/tests/config/docs under `new/`, plus the V5 doc and change artifacts

## Migration Plan

1. Create OpenSpec proposal/design/tasks/spec deltas and pass strict validation.
2. Add the connectivity helper and focused tests.
3. Add `REFERENCE_LATERAL_JUMP_GATE_M` to parameters, docs, and media snapshots.
4. Gate current-frame visual candidates in the runtime pipeline.
5. Run focused tests, full no-upload build, and OpenSpec source-first review.
6. Sync delta specs into `openspec/specs/` and archive the change.

Board deployment/smoke consideration:

- Runtime candidate admission changes can affect steering behavior. Local verification is sufficient for this code turn; a later board smoke can inspect steering-media candidate paths after upload.

Rollback:

- Revert the helper and runtime gate to restore previous selector input behavior. The new parameter default is disabled and can remain harmless, but full rollback removes it from config snapshots.

## Open Questions

- None for V5. Hold-last gating is explicitly out of scope.

## Risks / Trade-offs

- Black-only blocking intentionally allows unknown/out-of-frame segments through. This matches V5's narrow requirement but is weaker than a full traversability check.
- Filtering before selection can remove all current-frame visual candidates and rely on hold-last; this is intended for disconnected white regions.
- The helper samples image-space segments, so projector calibration quality remains load-bearing just as it is for sparse row scanning.
