## Context

The current BEV runtime has two emerging input surfaces in the root README: sparse BEV row scans and BEV element raster. Sparse rows are already active and feed line reference and cross-exit evidence. The dense BEV builder is explicitly debug-only and is not a valid runtime authority. Future circle, roadblock, ML, and ordinary path connectivity checks need a runtime-owned raster with projection/sample availability rather than a debug image.

`PerceptionFrontend` currently performs camera capture, fallback, thresholding, sparse BEV perception, cross evidence, cross candidate push, visual-reference orchestration, hold, usability, lateral error, readiness, and result publication in one file. Adding more elements there would make `perception_frontend.cpp` the integration hotspot and would blur ownership between frontend lifecycle, frame-level perception, element aggregation, and downstream reference-control checks.

## Goals / Non-Goals

**Goals:**

- Establish runtime `BEVElementRaster` as a first-class visual fact input with projection/sample availability.
- Keep debug/overlay/media output observational and unable to feed back into runtime decisions.
- Split single-frame perception work out of `PerceptionFrontend` while keeping existing selected-reference and control-readiness semantics unchanged.
- Introduce a `VisualElementPipeline` aggregation entry point that currently registers only cross-exit.
- Move cross implementation to cross-specific files with behavior-zero-change.
- Add generic, backward-compatible element evidence records for future elements without pre-adding typed circle/roadblock/ML fields.
- Preserve `element_evidence.cross_exit.*` JSON shape and ordering for old consumers.

**Non-Goals:**

- No circle, roadblock, ML detector, or candidate builder implementation.
- No ordinary path connectivity recovery, lateral-jump removal, path interpolation, or sparse-line selection behavior change.
- No cross-derived control behavior change and no default change to `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED`.
- No hold, safety, yaw, actuator, calibration, or longitudinal scale changes.
- No archive topology/opening/memory/policy restoration.

## Decisions

### Decision 1: Runtime Raster Is A New Authority, Debug Dense BEV Remains Output

- Problem: future elements need a runtime input surface, but current dense BEV is documented as debug-only.
- Alternatives: reuse debug dense image, let each detector project pixels independently, or create a shared raster fact layer.
- Choice: add `BEVElementRaster` and `BEVElementRasterLut` in `legacy`, with `port` contracts for cell class and sample state. Debug/media may render or serialize raster-derived facts but must not become authority.
- Stack Equivalent: camera frame + threshold + projector + raster parameters -> precomputed projection LUT -> per-frame class buffer -> element detectors.
- Failure Semantics: cells that cannot project or sample are `invalid`/`unknown`; outside-frame and projection-failed states never become white, black, path, opening, or boundary facts.
- Boundary Examples: element detectors may read raster cells and segment helpers; control readiness, safety, yaw, and actuator code may not read raster internals.
- Verification Hook: raster LUT, classifier, segment-touch tests, residual check, no-upload build, and board cadence evidence.

### Decision 2: Use Aggressive But Simple Runtime Raster Performance

- Problem: dense raster is now runtime work and cannot be a slow debug side effect.
- Alternatives: recompute projections per frame, use OpenCV-like image transforms, or precompute a compact project/sample LUT.
- Choice: build a `BEVElementRasterLut` when projector/raster params change. Each cell stores projection state, source pixel indices, and fixed-point bilinear weights. Each frame builds a threshold-based `class_table[256]`, samples gray using the LUT, classifies by table, and reuses persistent buffers.
- Stack Equivalent: config-time LUT -> frame-time table lookup and fixed-point blend -> class buffer.
- Failure Semantics: disabled raster returns an unavailable raster with no sampleable cells; missing raster params use `RuntimeParameters{}` defaults; malformed or out-of-range raster params follow existing parameter parse-failure fallback instead of silent clamping; invalid LUT cells stay invalid without attempting fallback sampling.
- Boundary Examples: raster width is runtime-configured; height is derived from BEV metric aspect ratio so the cell grid remains metric-consistent.
- Verification Hook: performance stage `kPerceptionElementRaster` and tests that compare representative raster classifications with existing classification semantics.

### Decision 3: Split Frontend Lifecycle From Single-Frame Perception

- Problem: direct element integration in `PerceptionFrontend` makes every element touch capture/publish code and invites duplicated orchestration.
- Alternatives: keep all wiring in `PerceptionFrontend`, make `steering_visual_element_pipeline` own all perception, or introduce a single-frame steering perception pipeline.
- Choice: `PerceptionFrontend` owns camera capture, fault injection, empty-frame fallback, state publication, memory reset, and diagnostics. `SteeringFramePerceptionPipeline` owns threshold, sparse rows, raster, line candidate, element pipeline, orchestration, hold, usability, lateral error, and readiness for one frame.
- Stack Equivalent: frontend lifecycle -> frame pipeline -> `PerceptionResult`.
- Failure Semantics: frame pipeline returns the same invalid/default perception facts for empty/unhealthy input that the current frontend returns today.
- Boundary Examples: `VisualElementPipeline` does not compute selected reference, hold, readiness, safety, or actuator state.
- Verification Hook: existing BEV simple perception and visual-reference orchestration tests, plus no-upload build and runtime smoke.

### Decision 4: Element Evidence Uses Stable Typed Cross Plus Generic Extensions

- Problem: pre-adding typed fields for circle/roadblock/ML to every protocol/debug surface creates repeated shared-file churn and false implementation signals.
- Alternatives: add all expected typed fields now, use only untyped records, or keep cross typed and add a generic record list.
- Choice: keep `VisualElementEvidenceFrame::cross_exit` and add a generic `records` extension list. Shared JSON serializers write `element_evidence.cross_exit` first, then `element_evidence.records`. Each record has stable `id`, `present`, `confidence`, `reason`, `bounds`, `support`, and `candidate` keys. Future elements can append records without changing existing `cross_exit` consumers.
- Stack Equivalent: typed cross DTO + generic record DTO -> shared element-evidence serializer -> assistant/media/debug surfaces.
- Failure Semantics: unknown records are ignored by old consumers; absent record list means no extra element evidence.
- Boundary Examples: the base change does not add typed `circle_left` or `circle_right`, and does not add circle candidate stubs.
- Verification Hook: serializer tests that check old `cross_exit` fields and synthetic extension record output.

### Decision 5: Cross Split Is Mechanical

- Problem: `steering_visual_element_evidence.*` is named generically but currently contains only cross logic.
- Alternatives: leave cross in place, rename everything at once, or mechanically split cross into its own files and use the old name for aggregation.
- Choice: move cross detector/candidate builder to `steering_cross_exit_element_evidence.*` and introduce `steering_visual_element_pipeline.*` for aggregation. Keep cross constants, detection, candidate summary, and takeover default behavior unchanged.
- Stack Equivalent: sparse rows + line candidate -> cross detector/builder -> visual element pipeline result.
- Failure Semantics: the same absent/present/candidate reasons remain authoritative after the move.
- Boundary Examples: the cross split must not change ordinary line candidate selection or visual-reference arbitration inputs except through the existing explicit takeover parameter.
- Verification Hook: existing visual element evidence tests and visual-reference orchestration tests.

## Independent Verification Plan (STANDARD/STRICT)

- Shared sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`.
- Contracts:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Verifier profile: `.codex/agents/verify-reviewer.toml`.
- Invocation template id: `verify-reviewer-inline-v3`.
- Artifact-completion docs-first primary surface: changed `proposal.md`, `design.md`, `specs/**/*.md`, and `tasks.md`.
- Source-first primary surface: changed code, tests, configs, docs, and directly impacted serialization/protocol/runtime files.
- Authoritative evidence paths:
  - docs-first findings: `openspec/changes/establish-bev-element-raster-pipeline/verification/docs-first/attempt-<n>/findings.json`
  - docs-first evidence: `openspec/changes/establish-bev-element-raster-pipeline/verification/docs-first/attempt-<n>/verifier-evidence.json`
  - docs-first caller state: `openspec/changes/establish-bev-element-raster-pipeline/verification/docs-first/agent-table.json`
  - source-first findings: `openspec/changes/establish-bev-element-raster-pipeline/verification/source-first/attempt-<n>/findings.json`
  - source-first evidence: `openspec/changes/establish-bev-element-raster-pipeline/verification/source-first/attempt-<n>/verifier-evidence.json`
  - source-first caller state: `openspec/changes/establish-bev-element-raster-pipeline/verification/source-first/agent-table.json`
- Repository `.index/` remains optional background only and is not a gate.
- Valid pass requires complete review coverage and exhaustive review coverage evidence. Same active verifier is continued through repair until pass; a fresh challenger pass is required for implementation closure.

## Migration Plan

- Add `BEV_ELEMENT_RASTER` defaults and parser support so missing config falls back to enabled runtime raster defaults without changing existing `BEV_ELEMENT` takeover behavior.
- Introduce raster and pipeline behind existing runtime result contracts, then move `PerceptionFrontend` call sites onto the new single-frame pipeline.
- Replace protocol/debug element-evidence serialization with shared helpers while preserving existing `cross_exit` output shape and order.
- Board rollout remains no-motion first: build/upload, start normal no-motion, capture assistant/media evidence, inspect `cross_exit` fields and raster perf cadence.
- Rollback is disabling `BEV_ELEMENT_RASTER.ENABLED` or reverting the structural change; cross takeover remains default-off throughout.

## Open Questions

- None for this base change. Future changes will decide ordinary path black-barrier recovery, lateral-jump removal, circle evidence, cross-circle suppression, and ML raster consumers.

## Risks / Trade-offs

- Runtime raster adds per-frame CPU work; the LUT/table/buffer approach reduces per-frame cost, and board smoke must verify cadence before treating it as safe for follow-on detectors.
- Splitting `PerceptionFrontend` increases short-term diff size, but it reduces the long-term shared hotspot for circle/roadblock/ML.
- Generic element records are less type-safe than predeclared circle fields, but they better preserve backward compatibility and avoid false public contracts for unimplemented elements.
- Keeping ordinary line behavior unchanged means this base change does not yet deliver the desired far-path recovery; that is deliberate to keep raster/pipeline foundations reviewable.
