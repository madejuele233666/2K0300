## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `valid_pass_requirements`
  - `partial_scope_rule`
- Subject binding:
  - docs-first review binds to `openspec/changes/add-steering-media-live-web-viewer/{proposal.md,design.md,tasks.md,specs/**/spec.md}`
  - source-first review binds to changed host tooling, tests, and directly impacted host capture docs
- Routing target for blocking findings:
  - `openspec-repair-change`
- Findings semantics:
  - authoritative findings JSON follows `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`
  - blocking, auto-fixable source findings route back to repair; non-blocking findings may remain as documented residual risk
- Artifact-completion gate ownership:
  - this task list completes the schema's `applyRequires` set
  - the active artifact-creation caller runs docs-first `openspec-artifact-verify` before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: Read-Only Live Viewer From The Existing Media Stream

- [x] 1.1 Add host-side live web serving deliverables under `new/user/` using Python standard library components only.
- [x] 1.2 Add an optional `host_capture.py` live-view flag surface that starts the live server, prints the viewer URL, and leaves default capture behavior unchanged when omitted.
- [x] 1.3 Wire decoded steering media events to sibling sinks so evidence recording and live fan-out are independent consumers of the same accepted media stream.
- [x] 1.4 Implement browser rendering for `gray8` frames using declared `width`, `height`, `source_width`, `source_height`, `downsample`, and read-only steering snapshot metadata.
- [x] 1.5 Ensure slow or disconnected live clients cannot block media ingest or evidence persistence; stale live frames may be dropped.

## 2. Vertical Slice: Evidence Compatibility And Local Feedback

- [x] 2.1 Extend `new/verification/tests/run_host_capture_selftest.sh` or add a focused companion test to start host capture with live viewing enabled.
- [x] 2.2 In the test, send synthetic `config_snapshot` and `image_frame` envelopes, assert existing evidence files still exist, and assert a browser-like client receives live frame metadata and payload.
- [x] 2.3 In the test, simulate a slow or disconnected live client while assistant telemetry continues and the steering media peer reconnects; assert control capture continuity, media reconnect receive behavior, and evidence persistence are not blocked by the live client.
- [x] 2.4 In the test, send browser-originated text and binary input to the live endpoint and assert the live viewer ignores or rejects it without producing assistant commands, ACKs, parameter writes, or state mutations.
- [x] 2.5 Run `rtk python3 -m py_compile new/user/host_capture.py` plus any new Python helper modules.
- [x] 2.6 Run `rtk bash new/verification/tests/run_host_capture_selftest.sh` and preserve the command result in the implementation summary.

## 3. Verification and Review

- [x] 3.1 Run `rtk openspec validate add-steering-media-live-web-viewer --strict` or the repository-equivalent validation command and repair any artifact or spec format issues.
- [x] 3.2 [Checkpoint] Run docs-first verifier-subagent review using `verify-sequence/default` for the completed proposal/specs/design/tasks bundle. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `review/review-runs/add-steering-media-live-web-viewer/docs-first/findings.json`, verifier evidence JSON at `review/review-runs/add-steering-media-live-web-viewer/docs-first/verifier-evidence.json`, and caller/orchestrator-maintained `review/review-runs/add-steering-media-live-web-viewer/agent-table.json`.
- [x] 3.3 [Checkpoint] After implementation, run source-first verifier-subagent review using `verify-sequence/default` for changed host tooling, tests, and directly impacted code. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require `review_goal=implementation_correctness`, fields from `verifier_evidence_required`, valid-pass requirements, authoritative findings JSON at `review/review-runs/add-steering-media-live-web-viewer/source-first/findings.json`, verifier evidence JSON at `review/review-runs/add-steering-media-live-web-viewer/source-first/verifier-evidence.json`, and current-state-only `agent-table.json`.

## 4. Cleanup and Decision Capture

- [x] 4.1 Update `new/user/README.md` or equivalent host workflow docs only if new CLI flags need durable operator documentation.
- [x] 4.2 Confirm no board `runtime/`, `platform/`, `port/`, `legacy/`, `config/`, or steering media protocol files gained browser/HTTP/WebSocket/canvas/live UI concepts.
- [x] 4.3 Mark completed tasks only after their command evidence or review output exists.
- [x] 4.4 Sync the delta spec into `openspec/specs/steering-tuning-media-observability/spec.md` after implementation verification passes, then archive the change.
