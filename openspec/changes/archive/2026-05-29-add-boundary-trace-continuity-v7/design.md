## Context

`new/docs/path-evaluation-boundary-continuity-v7.zh-CN.md` records the field evidence and design intent for this change. The observed failure is not in frontend rendering and not in the steering controller: meter-scale lateral samples already appear in `steering_snapshot.visual_reference.path_candidates[0].samples`. Values beyond `BEV_GEOMETRY.SEARCH_LATERAL_LIMIT_M` cannot be produced by ordinary double-edge midpoint generation, and the current single-edge ordinary path path builds a two-point boundary trace from the current row edge and the nearest adjacent same-kind row edge before calling `BuildSingleBoundaryOffsetReference()`.

Current code surfaces:

- `new/code/legacy/steering_bev_interval_edges.hpp` owns screen/sampleable-edge endpoint visibility.
- `new/code/legacy/steering_bev_simple_perception.cpp` owns ordinary row interval interpretation, midpoint candidates, single-edge candidates, and strict leading extraction.
- `new/code/legacy/steering_single_boundary_offset.cpp` owns pure signed-normal-offset geometry and already rejects malformed traces, but it does not know row intervals or boundary association.
- `new/code/legacy/steering_reference_connectivity.cpp` owns current-frame path black-barrier clipping after candidates are generated.
- `new/code/runtime/steering_frame_perception_pipeline.cpp` owns candidate aggregation, selection, continuity, tracking geometry, readiness, and downstream control.

The V7 fix belongs before ordinary candidate generation consumes edge facts. It should remove discontinuous raw boundary points, then let existing single-edge/midpoint semantics interpret the remaining facts.

## Goals / Non-Goals

**Goals:**

- Add explicit `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` and publish it through the existing parameter/config evidence surfaces.
- Add a neutral boundary-trace clipping helper that deletes individual outlier boundary points while allowing later points to be evaluated against the last kept point with row-gap-scaled distance.
- Apply clipped low/high edge facts before ordinary double-edge midpoint and single-boundary offset candidate generation.
- Ensure one clipped side naturally degrades to existing single-edge semantics and two clipped sides naturally remove the row candidate.
- Keep acceptance tests behavior-level through public ordinary reference generation and parameter/config surfaces while allowing small helper-level coverage for the V7 clipping rule itself.
- Run docs-first and source-first OpenSpec verifier checkpoints, then extra source-first verifier rounds until there are two consecutive valid source-first passes before sync and archive.

**Non-Goals:**

- No `REFERENCE_LATERAL_JUMP_GATE_M`, `NOMINAL_ROAD_HALF_WIDTH_M`, `LATERAL_STEP_M`, projector, threshold, row-scan, or control tuning.
- No screen-edge semantic change; unknown screen-edge handling remains in `EvaluateIntervalEdgeVisibility()`.
- No change to `BuildSingleBoundaryOffsetReference()` geometry or caller-neutral ownership.
- No selector, connectivity gate, hold-last, reference-control readiness, CircleV2 FSM, cross recognition, wheel mixer, wheel PID, PWM, or motor logic change.
- No path clamp to the screen/sampleable span; valid single-boundary offset paths may still leave the visible span.

## Decisions

### Decision 1: Add an explicit BEV geometry distance parameter

**Problem:** V7 requires a boundary-trace continuity threshold, and the user explicitly rejected deriving it from `NOMINAL_ROAD_HALF_WIDTH_M`, `LATERAL_STEP_M`, or any quantization tolerance.

**Chosen approach:** Add `boundary_trace_max_adjacent_distance_m` to `port::BEVGeometryParameters`, parse `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`, document it in `new/config/default_params.md`, set its default to `0.45`, and publish it in steering media config snapshots. Ordinary candidate generation must read this configured value and pass it to `BoundaryTraceClipOptions`; it must not construct a replacement threshold from other BEV geometry fields at the call site.

**Alternatives considered:** Constructing the threshold at the call site from nominal half-width and lateral step was rejected because it hides the boundary continuity policy inside unrelated geometry parameters. Reusing `REFERENCE_LATERAL_JUMP_GATE_M` was rejected because that parameter is the disabled old path/candidate jump gate, not a raw boundary-point association distance.

**Stack Equivalent:** Existing BEV geometry parameter pattern: `BEVGeometryParameters` field, JSON parse/validation, default JSON, default parity tests, parameter load tests, media config snapshot output.

**Named Deliverables:** `bev_geometry_types.hpp`, parameter loader, `default_params.json`, `default_params.md`, steering media protocol/config snapshot, `ordinary-bev-reference` spec delta, `steering-tuning-media-observability` spec delta, parameter/default/media tests.

**Failure Semantics:** Non-positive or non-finite configured values use existing parameter parse-failure behavior. The ordinary builder receives a finite positive distance or produces no new continuity-filtered candidates if the parameter surface fails to load.

**Boundary Examples:** The parameter is not used by `BuildSingleBoundaryOffsetReference()`, visual selector, connectivity gate, CircleV2, cross, control readiness, or yaw control.

**Contrast Structure:** This is a raw boundary association distance, not a path lateral jump gate and not a sampling-resolution tolerance. The helper comparison remains the direct `hypot(...) <= max_adjacent_distance_m * row_gap` rule, with no added quantization tolerance.

**Verification Hook:** Local parameter tests verify default parity, JSON parsing, invalid-value rejection, and steering media config snapshot serialization. On-board hook is `steering-media/steering_snapshot` or config snapshot showing `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` after upload.

**Feedback Loop:** `run_runtime_parameter_defaults_test.sh`, `run_param_store_load_runtime_parameters_test.sh`, `run_steering_media_selftest.sh`, and source-first verifier review.

### Decision 2: Keep boundary clipping as a neutral helper

**Problem:** Boundary discontinuity must be removed before ordinary path generation, but the clipping rule must not know single-edge, double-edge, screen-edge, or control semantics.

**Chosen approach:** Add a small helper, preferably header-only to avoid broad CMake/script churn:

```cpp
struct BEVBoundaryTracePoint {
  std::size_t row_index = 0;
  port::BEVPoint point{};
};

struct BEVBoundaryTraceClipOptions {
  float max_adjacent_distance_m = 0.0F;
};

std::vector<BEVBoundaryTracePoint> ClipBoundaryTraceOutliers(
    const std::vector<BEVBoundaryTracePoint>& raw_points,
    const BEVBoundaryTraceClipOptions& options);
```

The helper keeps the first point, then compares each later point to the last kept point. `row_gap` is the absolute row-index gap from the last kept point to the candidate point. A point is kept only when `hypot(delta_forward_m, delta_lateral_m) <= max_adjacent_distance_m * row_gap`. A rejected point is deleted alone; later points remain eligible against the last kept point.

**Alternatives considered:** Prefix truncation was rejected because a single bad row must not discard later valid boundary observations. Putting clipping into `BuildSingleBoundaryOffsetReference()` was rejected because that helper is deliberately pure geometry and is shared by ordinary/CircleV2 callers. Putting it in the selector or connectivity gate was rejected because those layers see generated paths, not raw boundary facts.

**Stack Equivalent:** C++17 legacy helper, parallel in ownership style to `steering_bev_interval_edges.hpp`: small DTOs, free function, no runtime/module state.

**Named Deliverables:** `new/code/legacy/steering_bev_boundary_trace_clip.hpp` and focused unit coverage in `bev_simple_perception_test.cpp`.

**Failure Semantics:** The helper only defines the normal V7 clipping rule for finite raw BEV boundary points and a positive configured distance supplied by validated runtime parameters. Parameter invalidity is handled by parameter parsing before the helper is called. Ordinary candidate generation still requires enough associated points before offset.

**Boundary Examples:** The helper receives no `BEVSimpleWhiteInterval`, no `BEVIntervalEdgeVisibility`, no `SingleEdgeKind`, no screen-edge facts, no candidate reason, no image frame, no CircleV2/cross state, and no control facts.

**Contrast Structure:** This is not a path smoother and not a reference selector; it is a one-dimensional same-side boundary point outlier clipper.

**Verification Hook:** Helper-level tests cover the V7 clipping rule for continuous trace, single outlier deletion with later retention at `2 * max`, consecutive outliers, and ordering. Behavior-level ordinary-reference tests cover candidate degradation, deletion, and offset behavior. On-board hook is future steering-media evidence showing the pathological single-edge offset samples are absent while valid single-edge rows still publish.

**Feedback Loop:** `run_bev_simple_perception_test.sh` plus docs/source verifier review.

### Decision 3: Apply clipped edge facts before ordinary candidate interpretation

**Problem:** Current `AddSingleEdgeCandidates()` directly asks for the nearest same-kind adjacent interval using the disabled lateral jump gate, so a false edge association can feed a large slope into signed-normal offset. Midpoint generation also uses raw endpoint visibility and would not naturally degrade if one side is clipped unless the candidate layer receives clipped visibility facts.

**Chosen approach:** Inside `BuildOrdinaryCenterCandidates()`, build clipped low-edge and high-edge row facts for the active sparse rows from intervals that pass existing screen-edge visibility. Candidate interpretation then reads the clipped facts:

- both low and high facts kept for the same interval row: midpoint candidate;
- only low kept: existing low-edge single-boundary offset semantics;
- only high kept: existing high-edge single-boundary offset semantics;
- neither kept: no candidate for that interval row.

For single-boundary offset, find the next kept same-side point after the current row; if none exists, the row candidate is removed. If an adjacent outlier was deleted but a farther row remains within the row-gap-scaled distance, use the farther kept point as the boundary support.

**Alternatives considered:** Only guarding `AddSingleEdgeCandidates()` would address the amplification entrance but would leave midpoint/single-edge degradation split between two systems. Reintroducing a path-level lateral jump check would move the fix after candidate generation and revive the old V5 problem. Clamping generated center points to BEV search bounds was rejected because valid single-boundary offset may leave the current screen/sampleable span.

**Stack Equivalent:** Internal ordinary-builder data preparation in `steering_bev_simple_perception.cpp`, analogous to existing `CenterCandidateRows` construction. The output remains `BEVReferencePath` with ordinary-compatible mode/source.

**Named Deliverables:** Updated `steering_bev_simple_perception.cpp`, focused ordinary reference tests for single-edge rejection/retention, midpoint degradation, and screen-outside path preservation.

**Failure Semantics:** A row whose required boundary support is clipped away yields no current-frame ordinary candidate. Existing strict-leading extraction stops at the first unavailable row after the segment starts. Existing hold-last remains the continuity fallback after visual selection/usability.

**Boundary Examples:** `EvaluateIntervalEdgeVisibility()` still owns screen/sampleable-edge visibility. `AppendConnectedVisualReferenceCandidate()` still owns black-barrier path connectivity after candidate generation. `SelectVisualReference()` still only sees candidate paths.

**Contrast Structure:** This is boundary fact filtering before candidate creation, not candidate arbitration, not path connectivity, not control smoothing, and not a parameter tune.

**Verification Hook:** Synthetic row tests assert that a discontinuous single-edge neighbor is removed before offset, a farther associated row can support offset after an outlier, one clipped side degrades to single-edge, both clipped sides remove the row, and valid outside-sampleable single-edge paths still work. On-board hook is a steering-media capture comparing consecutive frames for absence of meter-scale ordinary candidate jumps.

**Feedback Loop:** `run_bev_simple_perception_test.sh`, `run_visual_reference_orchestration_test.sh`, `run_visual_element_evidence_test.sh`, `run_steering_circle_v2_scene_test.sh`, local no-upload build, and verifier review.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `new/docs/path-evaluation-boundary-continuity-v7.zh-CN.md`,
  `openspec/specs/ordinary-bev-reference/spec.md`,
  `openspec/specs/steering-tuning-media-observability/spec.md`,
  `openspec/specs/bev-reference-connectivity/spec.md`,
  `new/code/legacy/steering_bev_simple_perception.cpp`,
  `new/code/legacy/steering_bev_interval_edges.hpp`,
  `new/code/legacy/steering_single_boundary_offset.cpp`,
  `new/code/legacy/steering_reference_connectivity.cpp`
- Alignment mapping:

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `steering_bev_interval_edges.hpp` | edge visibility before clipping | Adapt | Screen-edge visibility remains the first gate; clipping does not reinterpret screen edges. |
| `steering_bev_simple_perception.cpp` | ordinary candidate generation | Adapt | Candidate generation consumes clipped facts while preserving strict-leading extraction. |
| `steering_single_boundary_offset.cpp` | single-boundary geometry helper | Preserve | Keep pure geometry contract and prevent discontinuous ordinary traces from reaching it. |
| `steering_reference_connectivity.cpp` | path connectivity gate | Preserve | Connectivity remains post-generation black-barrier clipping, not raw boundary association. |
| `steering-tuning-media-observability/spec.md` | steering media config snapshot | Adapt | Add V7 boundary distance to the public config snapshot contract that already owns BEV geometry evidence fields. |

- Primary feedback loop:
  focused C++ tests for helper/candidate behavior and parameter tests, then local build, docs-first verifier, source-first verifier, and extra source-first verifier rounds until two consecutive valid source-first passes.
- Prototype question, if any:
  none; V7 already defines the clipping rule and parameter source.
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
- source-first checkpoints use changed code, tests, config, docs, and directly impacted code as the primary surface
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
- after the first valid source-first pass, run additional source-first verifier rounds and require two consecutive valid source-first passes before sync and archive

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
- Invocation:
  built-in subagent API with `fork_context=false` and a minimal verification bundle
- Invocation template id: `verify-reviewer-inline-v3`
- Docs-first verifier-subagent findings JSON path:
  `review/review-runs/add-boundary-trace-continuity-v7/docs-first-findings.json`
- Docs-first verifier execution evidence JSON path:
  `review/review-runs/add-boundary-trace-continuity-v7/docs-first-verifier-evidence.json`
- Source-first verifier-subagent findings JSON path:
  `review/review-runs/add-boundary-trace-continuity-v7/findings.json`
- Source-first verifier execution evidence JSON path:
  `review/review-runs/add-boundary-trace-continuity-v7/verifier-evidence.json`
- Extra source-first pass 1 findings/evidence paths:
  `review/review-runs/add-boundary-trace-continuity-v7/extra-pass-1-findings.json`,
  `review/review-runs/add-boundary-trace-continuity-v7/extra-pass-1-verifier-evidence.json`
- Extra source-first pass 2 findings/evidence paths:
  `review/review-runs/add-boundary-trace-continuity-v7/extra-pass-2-findings.json`,
  `review/review-runs/add-boundary-trace-continuity-v7/extra-pass-2-verifier-evidence.json`
- Agent table path:
  `review/review-runs/add-boundary-trace-continuity-v7/agent-table.json`
- Continuation target on docs-first pass:
  apply implementation
- Continuation target on source-first pass:
  run extra source-first passes until two consecutive valid passes, then sync specs and archive

Checkpoint-specific primary surfaces:

- docs-first review: `openspec/changes/add-boundary-trace-continuity-v7/proposal.md`, `design.md`, `tasks.md`, and `specs/**/*.md`
- source-first review: changed code/tests/config/docs under `new/`, changed OpenSpec artifacts, directly impacted ordinary reference, parameter parsing, media protocol, visual orchestration, and CircleV2 compile surfaces

## Migration Plan

1. Create proposal, spec delta, design, and tasks from V7 and pass docs-first review.
2. Add the explicit BEV geometry parameter, parser, default config, docs, and evidence serialization.
3. Add the neutral boundary-trace clipping helper and focused tests.
4. Apply clipped boundary facts in ordinary BEV candidate generation.
5. Run focused tests, local build, source-first verifier, and extra source-first verifier rounds until two consecutive valid passes.
6. Sync delta specs into `openspec/specs/ordinary-bev-reference/spec.md` and `openspec/specs/steering-tuning-media-observability/spec.md`.
7. Archive `add-boundary-trace-continuity-v7`.

Board deployment/smoke consideration:

- This change can alter selected ordinary visual reference facts. Local tests and source-first review are required in this change; after archive, a supervised board smoke can confirm the loaded config parameter and inspect steering-media candidate paths for absence of the known meter-scale ordinary jumps.

Rollback:

- Revert the boundary-trace helper, ordinary candidate clipping integration, and new parameter surface together. This restores previous ordinary candidate behavior while leaving selector, connectivity, hold-last, CircleV2, and control layers untouched.

## Open Questions

- None. The user specified that the distance comes from an explicit parameter and that no quantization tolerance is added.

## Risks / Trade-offs

- A too-small configured boundary distance can remove real rapidly changing boundary facts. This is expected parameter behavior and is visible in config snapshots.
- The first release clips raw boundary points using BEV Euclidean distance and row-index gap only. It does not inspect image texture or infer physical lane identity.
- The ordinary builder loses some current-frame candidates when boundary facts are discontinuous, which can reduce current visual sample count and rely on existing strict-leading/hold-last behavior.
