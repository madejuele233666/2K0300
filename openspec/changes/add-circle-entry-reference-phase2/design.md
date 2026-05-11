## Context

Phase 1 already gives the runtime four circle generic records in the visual element pipeline: `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right`. Raw records preserve detector facts, effective records are suppressed by `cross_exit` only in `steering_visual_element_pipeline.*`, and circle records currently remain evidence-only with no `kCircleLeft` or `kCircleRight` candidate push.

The user requirement for Phase 2 is limited to circle entry reference generation. The car should not be forced into a turn by an artificial gradual lateral offset. Instead, when effective `circle_left/right` is present, the runtime should use the observed inner-circle black-white frontier as a road edge, infer the road centerline from the current near-connected white component width, and produce a default-off circle `VisualReferenceCandidate`. Phase 2 does not implement complete roundabout traversal, exit recognition, actuator logic, state memory, or a scene FSM.

Alignment reference scope:

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `README.md` steering architecture | Phase2 visual element boundary | Adapt | Preserve "大道至简" and "互不知晓": detector facts, candidate builder, arbitration, control gates stay separated. |
| `new/code/legacy/steering_circle_element_evidence.*` | typed circle entry facts | Adapt | Extend current raster-only circle detector output instead of adding a second detector path. |
| `new/code/legacy/steering_visual_element_pipeline.*` | circle candidate build/push | Adapt | Pipeline continues to own cross suppression and element candidate aggregation. |
| `new/code/port/visual_element_evidence_types.hpp` | evidence records and params | Adapt | Preserve generic record wire shape; add internal typed facts and append-only parameters. |
| `new/code/port/visual_reference_orchestration_types.hpp` and `new/code/legacy/steering_visual_reference_orchestration.cpp` | circle candidate kind/validation | Replicate | Reuse existing `kCircleLeft/kCircleRight` kinds, special-candidate priority, and validation rules. |
| `new/code/runtime/steering_frame_perception_pipeline.cpp` | runtime candidate flow | Replicate | Keep line candidate + element candidates -> `SelectVisualReference` -> hold/usability/lateral-error/readiness chain. |
| `new/user/scene_overlay_probe.cpp` and authority-baseline tests | offline evidence observation | Adapt | Add circle entry fact/candidate observability without making probe output a runtime authority. |

Coverage report:

| Contract | Coverage |
|---|---|
| Effective circle records exist and are cross-suppressed in pipeline | Covered by Phase 1; Phase 2 must consume only effective records. |
| Circle reference candidate kinds exist | Covered by existing orchestration types; Phase 2 must build structurally valid candidates. |
| Circle detector isolation from cross/line/control owners | Partially covered by Phase 1; Phase 2 typed facts must preserve this boundary. |
| Candidate builder layer exists for cross | Covered by `BuildCrossExitVisualReferenceCandidate`; Phase 2 should follow the same summary/takeover pattern for circle. |
| Authority-baseline raw fixtures exercise runtime raster input | Covered for Phase 1 evidence; Phase 2 must extend expected candidate observability. |

## Goals / Non-Goals

**Goals:**

- Derive circle entry typed facts from the current `BEVElementRasterFrame` without finding or classifying an "inner black block".
- Identify a rear-side black-white frontier belonging to the near-connected white component, with left-circle direction toward upper-left and right-circle direction toward upper-right.
- Infer road half-width from the median of near-connected white component row widths near the vehicle and generate a centerline from the frontier edge.
- Build `VisualReferenceCandidateKind::kCircleLeft` or `VisualReferenceCandidateKind::kCircleRight` only from effective circle evidence and typed entry facts.
- Keep takeover default-off and append-only under `BEV_ELEMENT`.
- Preserve existing selected-reference, hold, usability, lateral-error, readiness, safety, yaw, and actuator boundaries.

**Non-Goals:**

- No full roundabout state machine, memory of the inner island, exit detection, or complete circle traversal policy.
- No actuator writes, steering PID changes, speed changes, yaw-rate steering changes, or safety bypass.
- No artificial gradual lateral offset relative to the current line candidate.
- No candidate built from `circle_left_raw` or `circle_right_raw` after cross suppression.
- No use of archive topology FSM, trusted path memory, ML, debug overlay pixels, or probe-rendered images as detector authority.
- No generic interpolation behavior for ordinary line reference construction; interpolation is circle-chain-local only.

## Decisions

### Decision 1: Add Typed Entry Facts To The Circle Detector, Not To The Generic Wire Shape

- Problem: the builder needs frontier and centerline facts, while existing telemetry/media consumers only require stable generic records.
- Alternatives: add new public generic-record fields, make the builder rescan the raster, extend `CircleElementEvidenceResult` with typed internal facts only, or carry typed facts through a pipeline-owned debug surface.
- Choice: extend `CircleElementEvidenceResult` with left/right entry facts while keeping `VisualElementEvidenceRecord` unchanged. Raw/effective generic records stay as the public wire shape; typed facts are internal C++ data passed from detector to pipeline/builder, and `VisualElementPipelineResult` gets a debug-only circle-entry diagnostics carrier for `scene_overlay_probe` to print.
- Why this option: it keeps Phase 1 telemetry compatibility, avoids duplicate recognition in the builder, and preserves the detector/builder split.
- Stack Equivalent: `BEVElementRasterFrame` -> `CircleElementEvidenceResult::{left_raw,right_raw,left_entry,right_entry}` -> pipeline effective record selection -> circle candidate builder + `VisualElementPipelineResult.circle_entry_diagnostics` -> probe print fields.
- Named Deliverables: updated `steering_circle_element_evidence.hpp/.cpp`, updated `steering_visual_element_pipeline.hpp`, internal point/fact structs, debug diagnostics structs, candidate builder function, and tests that assert generic record JSON shape remains unchanged.
- Failure Semantics: absent or invalid typed entry facts do not change raw/effective Phase 1 presence; they only prevent candidate build, set candidate summary reason, and expose absent debug diagnostics to the probe.
- Boundary Examples: detector may read raster classes/projection states/metric conversion and `BEV_ELEMENT` params; it may not read cross evidence, line candidate, hold state, safety, IMU, encoder, yaw, actuator, or `PerceptionResult`.
- Contrast Structure: recognition remains in the detector; candidate construction remains in the builder; serialization remains generic.
- Verification Hook: unit tests for record shape stability plus board/probe evidence that `element_evidence.records[*]` keys are unchanged while probe-only circle entry diagnostics are still printable when entry facts are present.

### Decision 2: Identify The Inner Edge As A Rear-Side Frontier Of The Near-Connected White Component

- Problem: a circle image contains multiple black regions and clipped raster boundaries; choosing the open-side outer boundary or "largest black block" can confuse FOV/raster limits with the inner-circle edge.
- Alternatives: classify black regions, use the open-side interval boundary, track raster/FOV edge, or derive a black-white frontier adjacent to the near-connected white component.
- Choice: start from the nearest valid white run, build the connected white component, and extract frontier points only where that component touches sampleable black cells in a rear-side neighborhood. Unknown, invalid, unavailable, and raster/FOV boundary cells cannot become frontier support.
- Why this option: it uses only current-frame visual facts, rejects clipped boundaries naturally, and avoids naming or remembering a black object.
- Stack Equivalent: raster white cells -> near-connected component -> rear-side sampleable black adjacency -> frontier chain candidates -> selected side-specific chain.
- Named Deliverables: component labeling/flood-fill or row-connected helper local to circle evidence, rear-side adjacency helper, frontier chain extraction helper, synthetic tests for FOV/unknown/unavailable rejection.
- Failure Semantics: no near-connected component, no sampleable black adjacency, frontier touching only invalid/unavailable/FOV, or detached white island support returns entry facts absent.
- Boundary Examples: a black cell outside sampleable projection is not an edge; a white island not connected to near rows is ignored; a split interval can contribute only if it remains connected to the near component.
- Contrast Structure: this tracks a boundary fact, not a black object and not an imposed path offset.
- Verification Hook: synthetic raster tests plus authority-baseline overlay/probe output showing selected frontier points are interior sampleable black-white edges rather than the left/right raster limit.

### Decision 3: Use Net Direction, Not Monotonicity Or PCA, For The First Direction Gate

- Problem: the correct entry frontier should extend toward the opening-side upper direction, but real sampled points can wobble and should not be rejected for non-monotonic per-row changes.
- Alternatives: require every point to move farther left/right, fit a PCA/line model, or use start/end net displacement with a minimum lateral delta.
- Choice: accept a chain when its end is farther forward and its net lateral motion is toward the opening side by at least `BEV_ELEMENT.CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`, default `0.08m`.
- Why this option: it directly matches the user's chosen first version, tolerates sequences such as `0.10, 0.30, 0.29`, and keeps the rule explainable.
- Stack Equivalent: ordered frontier chain points in BEV metric space -> `direction_delta_lateral_m` and `direction_delta_forward_m` -> side-specific direction gate.
- Named Deliverables: direction helper, append-only direction parameter, tests for left/right pass and below-threshold fail.
- Failure Semantics: insufficient point count, non-forward net direction, or side-specific net lateral delta below threshold yields entry facts absent with deterministic reason.
- Boundary Examples: left circle requires net lateral movement to the left as forward increases; right circle requires net lateral movement to the right as forward increases; per-point monotonicity is not required.
- Contrast Structure: direction is a minimal visual fact gate, not a curve fit or path policy.
- Verification Hook: unit tests with wobbly but net-correct chains and board/probe logs exposing direction delta for static circle frames.

### Decision 4: Infer The Centerline From Current Near-Component Width, Then Join Near Segment To Frontier-Derived Segment

- Problem: following the frontier itself would drive on the edge, while a fixed track-width parameter would make Phase 2 depend on a manually tuned lane width.
- Alternatives: track frontier directly, use a fixed road width parameter, offset the current line candidate gradually, or infer half-width from near component rows.
- Choice: compute `road_half_width_m` as the median of the first `CIRCLE_MIN_SUPPORT_ROWS` near-connected component row widths divided by two. For `circle_left`, centerline lateral is `frontier_lateral_m + road_half_width_m`; for `circle_right`, centerline lateral is `frontier_lateral_m - road_half_width_m`. Leading samples before the visible frontier use the near-connected component centerline when continuity and join-jump limits hold.
- Why this option: it follows the inner frontier as a road edge while producing a road centerline, and it uses the current frame's own road width evidence.
- Stack Equivalent: component row intervals -> median half-width -> near center samples + frontier-derived center samples -> `BEVReferencePath`.
- Named Deliverables: median half-width helper, centerline projection helper, circle-only join/interpolation helper, tests for left/right formulas and continuity from sample index 0.
- Failure Semantics: insufficient near widths, non-finite half-width, join jump above `CIRCLE_ENTRY_MAX_JOIN_JUMP_M`, interpolation gap above `CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`, or a gap across unknown/unavailable/FOV rejects candidate build.
- Boundary Examples: small gaps inside one selected frontier chain may be linearly interpolated; gaps across projection-unavailable cells and unrelated chains are not bridged; ordinary line builder behavior is unchanged.
- Contrast Structure: the car enters naturally by following a real edge-derived centerline, not by forcing a turn command.
- Verification Hook: visual element tests inspect path sample continuity, centerline lateral values, and rejection reasons; board no-motion evidence can compare selected source before/after enabling takeover.

### Decision 5: Build Circle Candidates Only From Effective Evidence, With Default-Off Takeover

- Problem: Phase 2 should be observable and testable before it can steer the vehicle, and cross suppression must remain authoritative.
- Alternatives: always push circle candidates, push raw circle candidates for debug, require a new runtime state machine, or follow the cross candidate summary/takeover pattern.
- Choice: add `BuildCircleEntryVisualReferenceCandidate` that consumes the effective circle record, side-specific entry facts, and params. It may set `candidate.built=true`; it pushes to arbitration only when `CIRCLE_ENTRY_TAKEOVER_ENABLED=true` and the candidate is valid. Cross-suppressed effective records do not build or push candidates.
- Why this option: it mirrors cross takeover semantics, keeps runtime default behavior line-only, and allows explicit static/board validation before motion.
- Stack Equivalent: effective `VisualElementEvidenceRecord` + `CircleEntryPathFacts` + `RuntimeParameters.bev_element` -> `VisualElementCandidateSummary` + optional `VisualReferenceCandidate`.
- Named Deliverables: builder declaration/implementation, pipeline integration, parameter defaults/loading/docs, tests for disabled/enabled/suppressed paths.
- Failure Semantics: evidence absent, entry facts absent, invalid path, disabled takeover, or validation failure produces a candidate summary reason and no arbitration inclusion unless explicitly enabled and valid.
- Boundary Examples: `circle_left_raw.present=true` but `circle_left.present=false` after cross suppression cannot build; `circle_left.present=true` may build but default-excluded; takeover enabled still flows through existing special-candidate validation and downstream control gates.
- Contrast Structure: Phase 2 creates a candidate, not a control command.
- Verification Hook: `run_visual_element_evidence_test.sh`, `run_visual_reference_orchestration_test.sh`, takeover-enabled authority-baseline probe, and board static evidence with takeover disabled before any motion run.

### Decision 6: Keep Observability In Probe And Existing Evidence Surfaces

- Problem: implementation needs offline and board-visible evidence for thresholds and selected paths without turning debug output into an input.
- Alternatives: rely only on unit tests, add a second debug-only detector, put typed fields into the generic evidence wire shape, or extend existing pipeline/probe diagnostics.
- Choice: extend `VisualElementPipelineResult` with debug-only circle entry diagnostics and update `scene_overlay_probe` to print those fields alongside the existing generic evidence and candidate summary. The probe still drives detection from runtime raster facts and does not read rendered overlay pixels.
- Why this option: it reuses the Phase 1 authority-baseline loop and makes threshold tuning inspectable.
- Stack Equivalent: raw fixture/current frame -> runtime raster -> visual element pipeline evidence/candidates/debug diagnostics -> printed record/candidate/source/entry fields -> test assertions.
- Named Deliverables: `VisualElementPipelineResult` debug diagnostics carrier, probe print fields for entry reason/direction/half-width/path summary, authority-baseline expectations, optional takeover-enabled probe parameter fixture, and `scene_overlay_probe.cpp` local parameter parser support for all `CIRCLE_ENTRY_*` keys.
- Failure Semantics: probe failures or absent entry facts do not alter runtime control; they only fail offline validation.
- Boundary Examples: overlay rendering may visualize frontier/path; detectors and builders do not read overlay pixels. Probe takeover-enabled checks must either parse `CIRCLE_ENTRY_TAKEOVER_ENABLED` through `LoadRuntimeParamsJson`/`ValidProbeBEVElementParameters` or explicitly route probe params through the shared runtime loader before asserting selected-source behavior.
- Contrast Structure: observability follows the runtime fact chain rather than creating a parallel authority.
- Verification Hook: authority-baseline probe for `circle-1/2/3`, `cross-1/2/3`, and `bend-1/2/3`; probe parameter override tests for the new `CIRCLE_ENTRY_*` fields; board media/assistant evidence after upload.

## Independent Verification Plan (STANDARD/STRICT)

- Shared sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`.
- Contracts:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Verifier runtime profile: `.codex/agents/verify-reviewer.toml`.
- Invocation: built-in subagent API with `fork_context=false`, template `verify-reviewer-inline-v3`.
- Agent lifecycle: follow `cycle_rules` in `verification-cycle-core-v1.json`; only the orchestrator may authorize resume, spawn, repair, or terminate.
- Valid pass requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`.
- Docs-first primary surface: `openspec/changes/add-circle-entry-reference-phase2/proposal.md`, `design.md`, `specs/**/*.md`, and `tasks.md`.
- Source-first primary surface: changed circle detector, circle candidate builder, visual element pipeline, port evidence/reference/parameter contracts, parameter loading/defaults, probe code, tests, CMake files, docs/config, and directly impacted serializers.
- Authoritative docs-first findings path: `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/attempt-<n>/findings.json`.
- Authoritative docs-first evidence path: `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/attempt-<n>/verifier-evidence.json`.
- Docs-first agent table path: `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/agent-table.json`.
- Authoritative source-first findings path: `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/attempt-<n>/findings.json`.
- Authoritative source-first evidence path: `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/attempt-<n>/verifier-evidence.json`.
- Source-first agent table path: `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/agent-table.json`.
- Review checkpoints:
  - Artifact-completion docs-first review after `proposal`, `design`, `specs`, and `tasks` exist.
  - Implementation source-first review after code, tests, local regression, and code-index refresh settle.
  - Fresh challenger pass after a reusable working verifier reaches zero source-first findings.

## External Repository Index Reference (Optional)

This change does not require a repository-index refresh for artifact creation. If `.index/` is used during implementation, it remains non-authoritative background only and must not replace code search, OpenSpec validation, verifier findings, or implementation closure evidence.

## Migration Plan

- Extend circle evidence structs and detector internals with typed entry facts while preserving existing generic record output.
- Extend `VisualElementPipelineResult` with a debug-only circle entry diagnostics carrier that `scene_overlay_probe` can read without changing assistant/media generic evidence records.
- Add append-only `BEV_ELEMENT` entry parameters and keep takeover disabled by default.
- Extend both the shared runtime parameter loader and the probe-local `LoadRuntimeParamsJson`/`ValidProbeBEVElementParameters` path for all `CIRCLE_ENTRY_*` fields.
- Implement circle entry candidate builder and integrate it in `steering_visual_element_pipeline.*` after effective records are determined.
- Update offline probe output and authority-baseline tests for built/default-excluded and takeover-enabled behavior.
- Run unit tests, authority-baseline probe, visual-reference orchestration regression, runtime parameter regression, media regression, no-upload build, `git diff --check`, and `code-index refresh`.
- Board rollout starts with no-motion/static evidence capture and takeover disabled; any motion test requires an explicit parameter enable after static evidence is reviewed.
- Rollback is setting `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED=false`; if needed, `CIRCLE_EVIDENCE_ENABLED=false` disables circle evidence records as already supported by Phase 1.

## Open Questions

- No blocking design questions remain. The default direction threshold is `0.08m` per user decision and may be tuned later from authority-baseline or board evidence without changing the contract.

## Risks / Trade-offs

- The near-connected component rule is simpler and more robust than black-region classification, but it can fail closed if the visible white road is fragmented before the frontier becomes connected.
- Median half-width avoids a fixed track-width parameter, but poor BEV calibration or near-row occlusion can bias the inferred centerline; tests and board evidence must inspect the computed half-width.
- Net direction is easier to reason about than PCA, but it may accept a noisy chain with correct endpoints; minimum point count and sampleable-black adjacency are the first-line guards.
- Default-off takeover protects runtime behavior during validation, but it means offline/probe evidence must explicitly verify both built/default-excluded and takeover-enabled paths.
