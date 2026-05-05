## Context

The current runtime builds sparse BEV row scans and a line visual reference candidate, then sends only that line candidate to visual-reference orchestration. The root README requires future elements to enter as BEV metric visual evidence first, then optional candidates, then the existing selected-reference chain.

## Goals / Non-Goals

**Goals:**

- Add the first BEV visual element evidence slice for `cross_exit`.
- Publish element evidence facts through existing public observability surfaces.
- Keep default control behavior unchanged by disabling element candidate takeover.

**Non-Goals:**

- No circle, roadblock, ML, dense BEV element raster, scene FSM, topology, memory score, or actuator policy.
- No BEV calibration or longitudinal scale changes.
- No default motor behavior change.

## Decisions

### Decision 1: Use Sparse Row Facts For Cross v1

- Problem: cross v1 needs a low-risk evidence input without introducing raster complexity.
- Alternatives: dense debug BEV, archive element evidence, or existing sparse rows.
- Choice: extend active sparse row facts with sample counts and detect `cross_exit` from contiguous wide white rows.
- Stack Equivalent: sparse row scan -> row support statistics -> cross evidence.
- Named Deliverables: row statistics in `BEVSimpleRowScan`, `VisualElementEvidenceFrame`, `DetectVisualElementEvidence`.
- Failure Semantics: absent rows or weak support publish `present=false` with deterministic reason.
- Boundary Examples: detector may read row intervals and row support counts; detector may not read safety, hold, yaw, actuator, or `PerceptionResult`.
- Verification Hook: `run_bev_simple_perception_test.sh` and new `run_visual_element_evidence_test.sh`.

### Decision 2: Separate Detector From Candidate Builder

- Problem: evidence detection must not fabricate a control path.
- Alternatives: detector returns `VisualReferenceCandidate`, or detector returns facts and a builder optionally constructs a candidate.
- Choice: detector returns facts; builder constructs candidate summary and candidate from existing line facts only.
- Stack Equivalent: evidence DTO -> builder -> optional candidate -> existing orchestration.
- Named Deliverables: `cross_exit` evidence, candidate summary, default-off inclusion flag.
- Failure Semantics: unsupported evidence or missing line geometry yields `built=false`; disabled takeover yields `included_in_arbitration=false`.
- Boundary Examples: builder may read current line candidate; builder may not perform hold, safety, yaw, or actuator decisions.
- Verification Hook: new element evidence tests plus existing visual-reference orchestration tests.

### Decision 3: Publish Evidence As Read-Only Observability

- Problem: field debugging needs aligned evidence without letting debug become authority.
- Alternatives: keep evidence only in offline probe, publish only media, or publish all public steering observability surfaces.
- Choice: publish `element_evidence.cross_exit` through control snapshot, assistant telemetry, steering media, and scene overlay probe.
- Stack Equivalent: `PerceptionResult` transport -> `ControlDebugSnapshot` -> protocol encoders.
- Named Deliverables: public snapshot fields, telemetry JSON fields, media JSON fields, probe text output.
- Failure Semantics: missing or absent evidence serializes defaults; transports do not recompute facts.
- Boundary Examples: protocol code copies facts; protocol code does not detect cross or choose references.
- Verification Hook: `run_assistant_telemetry_selftest.sh`, `run_steering_media_selftest.sh`, no-motion steering media capture.

## Independent Verification Plan (STANDARD/STRICT)

- Shared sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`.
- Contracts: `verification-cycle-core-v1.json`, `verification-cycle-openspec-adapter-v1.json`, `verification-cycle-agent-table-v1.json`.
- Verifier profile: `.codex/agents/verify-reviewer.toml`.
- Artifact-completion docs-first review primary surface: changed proposal/specs/design/tasks.
- Source-first review primary surface: changed code, tests, configs, and directly impacted protocol paths.
- Authoritative evidence paths:
  - docs-first: `openspec/changes/add-bev-visual-element-evidence/verification/docs-first/attempt-<n>/findings.json`
  - docs-first: `openspec/changes/add-bev-visual-element-evidence/verification/docs-first/attempt-<n>/verifier-evidence.json`
  - docs-first caller state: `openspec/changes/add-bev-visual-element-evidence/verification/docs-first/agent-table.json`
  - source-first: `openspec/changes/add-bev-visual-element-evidence/verification/source-first/attempt-<n>/findings.json`
  - source-first: `openspec/changes/add-bev-visual-element-evidence/verification/source-first/attempt-<n>/verifier-evidence.json`
  - source-first caller state: `openspec/changes/add-bev-visual-element-evidence/verification/source-first/agent-table.json`
- Repository `.index/` remains optional background only and is not a gate.

## Migration Plan

- Add defaults and parsers so missing `BEV_ELEMENT` keeps takeover disabled.
- Update protocol consumers and tests in the same change so new public fields are deterministic.
- Board rollout remains no-motion first: build/upload, start normal no-motion, capture assistant/media evidence, inspect `element_evidence.cross_exit`.
- Rollback is removal of the change or setting `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED=false`, which is already the default.

## Open Questions

- None for v1. Future changes will decide dense element raster, circle/roadblock/ML facts, and cross-specific path strategy.

## Risks / Trade-offs

- Publishing the public evidence surface increases protocol churn, but it gives aligned field evidence before takeover.
- The first candidate reuses line geometry, so it is not a full cross driving strategy. This keeps v1 safe and testable.
- Hard-coded detector constants reduce parameter complexity now; later tuning can add scoped parameters if field evidence shows the constants are insufficient.
