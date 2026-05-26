## Context

The current steering media path is already a read-only board-to-host side channel. Board runtime publishes `config_snapshot` and `image_frame` envelopes through `SteeringMediaService`, the platform bridge actively connects to `assistant_tcp.host:steering_media_port`, and `new/user/host_capture.py` records the host-side evidence bundle. The missing capability is live inspection: operators can only inspect raw frame files after capture, even though the same incoming stream has enough information for immediate browser rendering.

Reference alignment scope:

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `new/code/platform/steering_media_protocol.cpp` | host decoder/live sender | Adapt | Keep the 8-byte length prefix, JSON header, and raw `gray8` payload contract. |
| `new/code/runtime/steering_media_service.cpp` | live viewer input model | Adapt | Consume `width`, `height`, `source_width`, `source_height`, `downsample`, `motion_phase`, and `steering_snapshot`; do not change publish gating. |
| `new/code/platform/true_ls2k0300/steering_media_bridge.cpp` | live fan-out failure behavior | Adapt | Preserve non-blocking, latest-frame style semantics on the host viewer side. |
| `new/user/host_capture.py` | evidence sink and CLI integration | Adapt | Preserve existing output files and add optional live viewing as another sink. |

Coverage report:

- Fully covered: accepted envelope framing, config/image frame families, payload size validation, evidence bundle files.
- Fully covered: board remains unaware of browser/HTTP/WebSocket concepts.
- Partially covered until implementation: host ingest fan-out and slow-client drop semantics need a focused selftest.
- Intentionally not copied: board-side send buffering internals stay in C++; host live broadcast only needs latest-frame/drop-tolerant semantics.

## Goals / Non-Goals

**Goals:**

- Add an optional host-side browser viewer for live steering media frames.
- Keep board runtime and the accepted steering media protocol unchanged.
- Make TCP ingest, evidence persistence, and live web fan-out separable mechanisms.
- Preserve `host_capture.py` as the canonical evidence workflow.
- Verify the feature locally without board hardware and leave a board-run hook for later real-device confirmation.

**Non-Goals:**

- No board runtime HTTP, WebSocket, JPEG, MJPEG, H.264, or base64 image publisher.
- No browser-originated assistant commands, motion commands, or parameter writes.
- No replacement of `assistant_tcp` control capture.
- No dependency on OpenCV, Pillow, FastAPI, Flask, or Node tooling for the first release.

## Decisions

### Decision: Keep The Board Protocol As The Source Contract

**Problem being solved**

The browser needs a convenient stream, but changing the board protocol would couple runtime, transport, and UI concerns and would risk the accepted evidence path.

**Chosen approach**

The board continues to send the accepted steering media envelope. The host live server adapts decoded media events to browser delivery.

**Alternatives considered**

- Add JPEG/MJPEG encoding on the board. Rejected because it adds board CPU work, codec choices, and a second image contract.
- Add a second JSON-line base64 stream. Rejected because the accepted design already chose binary length-prefix framing and raw payloads.

**Stack Equivalent**

- Board contract: existing `config_snapshot` and `image_frame` envelope.
- Host adapter: Python decoder plus live web sink.
- Browser rendering: canvas `ImageData` from raw `gray8`.

**Named Deliverables**

- Host live server integration in `new/user/host_capture.py` and/or a helper module under `new/user/`.
- CLI flags for enabling the live viewer and selecting the HTTP/WebSocket bind endpoint.
- Selftest fixture that sends config and image envelopes to the host listener.

**Failure Semantics**

- Malformed media envelope remains a host receiver error.
- Browser delivery errors close or skip the affected client only.
- Absence of browser clients does not change recording or TCP receive behavior.

**Boundary Examples**

- Allowed: host capture records `frames/frame-*.raw` while also broadcasting latest frames to browsers.
- Forbidden: board runtime includes web UI, WebSocket, canvas, or browser-specific fields.

**Contrast Structure**

- Chosen: board media envelope -> host decoder -> evidence sink and live sink.
- Not chosen: board media envelope -> evidence files -> browser polling raw frame files.

**Verification Hook**

- Local: `rtk bash new/verification/tests/run_host_capture_selftest.sh` extended to cover live view delivery.
- On-board: `./debug.sh steering host-capture --live-web --duration-s 20` with board log `steering_media.summary.image_sent` and live page frame counters.

**Feedback Loop**

The local selftest proves protocol compatibility and sink independence before any board run. A later board run can confirm hotspot routing and real frame cadence without becoming a requirement for local implementation closure.

### Decision: Add Live Viewing As A Sibling Sink, Not A Recorder Dependency

**Problem being solved**

If the webpage reads `frames/frame-*.raw` from disk, UI correctness becomes tied to evidence directory layout and file polling latency. If the recorder writes through the webpage, evidence preservation becomes dependent on UI availability.

**Chosen approach**

Media ingest publishes decoded events to separate consumers. The evidence sink persists files and metadata. The live sink broadcasts the current frame/status to browser clients.

**Alternatives considered**

- Browser polls raw files from the evidence directory. Simple, but not live enough and couples UI to evidence layout.
- Replace recorder with a web server. Rejected because evidence capture is the canonical workflow and must not depend on UI state.

**Stack Equivalent**

- Event boundary: decoded `header` plus raw `payload`.
- Sink equivalent: recorder callback and live broadcaster callback.
- Fan-out equivalent: latest-frame buffer plus client queues.

**Named Deliverables**

- A decoded media event callback or frame hub in host tooling.
- `EvidenceSink` behavior preserving current files.
- `LiveWebSink` behavior serving the browser and live frame stream.

**Failure Semantics**

- Evidence write errors remain capture errors.
- Live sink errors are isolated to live status unless server startup itself was requested and failed.
- Slow live clients drop stale frames rather than blocking ingest.

**Boundary Examples**

- Allowed: a capture run with `--live-web` records evidence even with zero browser clients.
- Forbidden: live viewer state determines whether `config_snapshot.json` or `frame_metadata.jsonl` is written.

**Contrast Structure**

- Chosen: fan-out from decoded media events.
- Not chosen: UI-driven recording or file-system polling as the primary live path.

**Verification Hook**

- Local: a test peer sends one image envelope while a browser-like live client connects; assertions check both evidence files and live endpoint output.
- Local: a slow or disconnected live client is simulated while assistant telemetry continues and the media peer reconnects; assertions check control capture continuity, media reconnect receive behavior, and evidence persistence.
- On-board: compare `summary.json.media_summary.frame_count`, assistant telemetry rows, live page frame count, and board log `steering_media.summary.image_sent` during a passive capture.

**Feedback Loop**

The first passing selftest must show evidence artifacts exist even when live viewing is enabled, and must show a browser-like client can receive frame metadata and payload.

### Decision: Use A Minimal Standard-Library Local Web Server

**Problem being solved**

The repo currently avoids a Python web framework dependency for host capture. A local live viewer should be runnable on the same host setup used for board tuning.

**Chosen approach**

Use Python standard-library HTTP serving plus a minimal WebSocket implementation for live frame delivery. Serve one static HTML page and a binary WebSocket stream.

**Alternatives considered**

- FastAPI/uvicorn or Flask. Easier APIs, but adds dependencies to an otherwise dependency-light host workflow.
- Server-sent events plus HTTP fetch for payloads. Simpler text stream, but binary frame delivery becomes awkward or requires another polling endpoint.
- MJPEG. Browser-native image display, but requires converting gray frames to an image codec and drops structured steering snapshot metadata.

**Stack Equivalent**

- HTTP route: live viewer page and lightweight status JSON.
- WebSocket route: binary frame messages.
- Browser renderer: JavaScript canvas plus read-only status fields.

**Named Deliverables**

- Live server helper in `new/user/`.
- Inline or file-based viewer HTML/JS served by the helper.
- CLI output showing the local viewer URL.

**Failure Semantics**

- If `--live-web` is not requested, no web server starts.
- If the live bind endpoint is unavailable when requested, host capture exits with a clear error before pretending live viewing is enabled.
- Client disconnects are normal and do not mark the capture failed.

**Boundary Examples**

- Allowed: `--live-web --live-host 127.0.0.1 --live-port 8765`.
- Forbidden: adding a runtime parameter that tells the board about web endpoints.

**Contrast Structure**

- Chosen: optional host-side local web adapter.
- Not chosen: external web app stack or board-side web server.

**Verification Hook**

- Local: start host capture with live flags, connect a test WebSocket client, receive and validate one frame.
- Local: send browser-originated text and binary data to the live endpoint and assert those inputs are ignored or rejected without creating assistant ACKs, commands, parameter writes, or state mutations.
- On-board: human opens the emitted URL while host capture records real board media.

**Feedback Loop**

Implementation passes only when the browser-facing stream can be validated without manual UI inspection and without weakening existing evidence assertions.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `steering media sidecar`, `accepted media envelope`, `host evidence bundle`, `control.steering_snapshot`, and `host_capture.py` from existing specs and code.
- Primary feedback loop:
  local host-capture selftest with synthetic assistant/control and steering-media peers.
- Prototype question, if any:
  whether a standard-library WebSocket sender can deliver `gray8` frames without adding dependencies; answer must be captured in the implementation and selftest.
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

- docs-first checkpoints use changed `proposal/specs/design/tasks` as the primary surface
- source-first checkpoints use changed host tooling, tests, and directly impacted capture scripts as the primary surface
- approved docs remain reference material when source-first review runs
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same
  agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in
  `continuation_probe`
- valid `pass` requires
  `review_coverage.coverage_status=complete` and
  `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and
  it must not substitute its own judgment for verifier output

Shared field groups from `verification-cycle-core-v1.json` and
`verification-cycle-openspec-adapter-v1.json`:

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
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  resume `active` first; prefer `send_input` while that same `active` agent is still open; use `continuation_probe` to distinguish resume from dedicated recovery spawn; spawn when no usable `active` agent exists; repair follows `block`; only `block -> pass` marks `non_active`; final termination requires a valid `active` pass.
- Authoritative verifier-subagent findings JSON path:
  `review/review-runs/add-steering-media-live-web-viewer/docs-first/findings.json` for docs-first and `review/review-runs/add-steering-media-live-web-viewer/source-first/findings.json` for source-first.
- Verifier execution evidence JSON path:
  `review/review-runs/add-steering-media-live-web-viewer/docs-first/verifier-evidence.json` for docs-first and `review/review-runs/add-steering-media-live-web-viewer/source-first/verifier-evidence.json` for source-first.
- Agent table path:
  `review/review-runs/add-steering-media-live-web-viewer/agent-table.json`
- Continuation target on pass:
  continue to apply after docs-first pass; continue to sync/archive after source-first pass.

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed `new/user/host_capture.py`, any new `new/user/steering_media_*` helper or viewer asset, changed selftests, and directly impacted host capture docs

## Migration Plan

- Default behavior remains unchanged unless a live-view flag is provided.
- Existing capture output directories and file names remain stable.
- Live viewer URLs are local host-side endpoints and do not affect board configuration.
- Rollback is deleting the new live helper/assets and removing the live flags/callback wiring from host capture; existing evidence capture continues to work.

## Open Questions

- None blocking. The first implementation can choose the exact binary WebSocket message layout as long as the test client validates it and the board envelope contract remains unchanged.

## Risks / Trade-offs

- A standard-library WebSocket implementation is more code than a framework dependency; the trade-off is zero extra setup for board workflows.
- Binary browser rendering requires a small JavaScript canvas renderer; this is acceptable because it keeps raw frame semantics visible and avoids codec ambiguity.
- Live frame drop semantics mean the viewer is not an archival source; the evidence bundle remains the archival source.
