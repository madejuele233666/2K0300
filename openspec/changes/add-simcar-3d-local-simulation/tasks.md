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
  - `subject_required_any_of`
  - `findings_required`
  - `finding_object_required`
  - `finding_semantics`
  - `repair_routing_rules`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, the active artifact-creation caller (`openspec-propose` or `openspec-continue-change`) runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate
- Agent lifecycle:
  - follow `cycle_rules` in `verification-cycle-core-v1.json`
  - keep `agent-table.json` current-state-only
  - continue a usable active agent first and prefer `send_input` while the same active agent is open
  - route `send_input` returning `agent not found` to resume/recovery checks, not immediate spawn
  - use `continuation_probe` to distinguish resume from recovery spawn
  - spawn only when `agent-table.json` has no usable `active` agent
  - expose recovery/spawn reason codes such as `active_agent_missing`, `active_agent_not_resumable`, and `no_usable_active_agent`
  - only `block -> pass` marks an agent `non_active`
  - termination requires a valid active pass with complete and exhaustive review coverage

## 1. Shadow Contract And Runtime Mode Documentation

- [ ] 1.1 Create `simcar/docs/new_shadow_contract.md` with the embedded opaque-target contract from `design.md`, including entrypoint, environment variables, bridge namespace, required behavior groups, first-stage sidecar policy, and future-only hardware reservations.
- [ ] 1.2 Create `simcar/docs/runtime_modes.md` documenting wall-clock MVP behavior, seed-stable but non-bit-for-bit deterministic semantics, and the deferred deterministic time-source hook.
- [ ] 1.3 Create `simcar/docs/architecture.md` describing the `simcar run` process, C++ replacement bridge, generated gRPC/Protobuf transport, Python MuJoCo server, artifact directory, and production-path isolation.
- [ ] 1.4 Add a static guard script or documented check that fails implementation verification if tracked files under `new/` are modified by simulator work.
- [ ] 1.5 Audit documentation to ensure simulator implementation instructions do not require reading `new/code`; references to `new/` must be limited to opaque launch/build paths and the shadow contract.

## 2. Protobuf Transport And Fixed-Response Simulator Skeleton

- [ ] 2.1 Add `simcar/proto/simcar.proto` defining run-scoped RPCs for run lifecycle, camera capture, IMU read, encoder read, motor apply/disable, battery ADC read, and shutdown.
- [ ] 2.2 Add build wiring that generates C++ and Python gRPC/Protobuf bindings into build/generated locations, with no generated source committed unless the project build policy explicitly requires it.
- [ ] 2.3 Implement a Python fixed-response gRPC server that listens on `simcar/runs/<run_id>/sim.sock` and returns valid startup-safe camera, IMU, encoder, battery, motor, and timer-related responses.
- [ ] 2.4 Implement a C++ simulator client wrapper with deadlines, status translation, frame-size validation, and no target-source dependencies beyond the shadow-contract bridge surface.
- [ ] 2.5 Add a host smoke test that starts the fixed-response server and verifies C++ can receive a valid `320x240` grayscale frame of exactly `76800` bytes.

## 3. C++ Replacement Bridge And Simulation Build Path

- [ ] 3.1 Implement simulation replacement bridge sources under `simcar/` for the `ls2k::platform::true_ls2k0300` camera, IMU, encoder, motor, battery ADC, and timer behavior groups.
- [ ] 3.2 Implement disabled-first assistant and steering-media bridge stubs that link successfully and stay inert when `assistant_enabled=0` and `steering_media_enabled=0`.
- [ ] 3.3 Add a simulation CMake/build target that links the replacement bridge and excludes the production `true_ls2k0300` bridge implementation, preventing duplicate symbols.
- [ ] 3.4 Add bridge failure mapping tests for unavailable socket, RPC timeout, wrong frame size, invalid battery sample, motor apply failure, and shutdown behavior.
- [ ] 3.5 Add a production-path build or smoke check showing the normal board build still uses the real direct-match bridge and does not link simulator gRPC dependencies.

## 4. `simcar run` Orchestration And Artifact Layout

- [ ] 4.1 Implement `simcar run` CLI to create `simcar/runs/<run_id>/`, copy or render the scenario, write `effective_params.json`, start the simulator server, launch the target binary, and capture stdout/stderr.
- [ ] 4.2 Ensure `effective_params.json` disables `assistant_enabled` and `steering_media_enabled` for MVP while preserving other runtime parameters unless a scenario explicitly overrides them.
- [ ] 4.3 Implement run lifecycle controls for `LS2K_AUTO_START`, `LS2K_AUTO_STOP_AFTER_MS`, timeout handling, controlled stop, and forced shutdown fallback.
- [ ] 4.4 Write required run artifacts: `summary.json`, `scenario.yaml`, `effective_params.json`, `replay_manifest.json`, `timeline.jsonl`, `sensors.jsonl`, `actuators.jsonl`, `control_debug_snapshot.jsonl`, `failures.jsonl`, `frames/*.raw`, `frames/*.png`, and `video.mp4`.
- [ ] 4.5 Add an artifact validator that checks required files, first failure metadata on failures, replay manifest fields, and raw frame byte sizes.

## 5. MuJoCo Live 3D Backend And Device Model

- [ ] 5.1 Add Python simulator interfaces for `reset`, wall-clock `step`, `render_gray`, sensor extraction, actuator application, scenario loading, and artifact callbacks.
- [ ] 5.2 Implement MuJoCo live backend initialization with camera resolution `320x240`, track surface, simplified vehicle pose, lighting, and offscreen rendering.
- [ ] 5.3 Implement logical left/right PWM to vehicle-state update, including speed, yaw rate, camera pose, encoder counts, IMU acc/gyro/mag raw values, and battery raw ADC.
- [ ] 5.4 Implement scenario perturbations for exposure, shadow, blur/noise, lost-line, startup-safe defaults, and explicit startup-failure injection.
- [ ] 5.5 Implement first scenarios: `bend`, `cross`, `circle`, `straight`, `lost-line`, `overexposure`, and `shadow`.
- [ ] 5.6 Add smoke tests that render at least one frame for each first scenario and validate dimensions, non-empty dynamic range unless intentionally overexposed, and artifact writes.

## 6. Replay, Assertions, And Output Evidence

- [ ] 6.1 Implement `replay_manifest.json` content with run id, scenario id, seed, target binary path, effective parameter path, simulator backend, socket path, dependency versions, and artifact paths.
- [ ] 6.2 Implement simulator assertions for empty frame, invalid frame size, low-voltage unexpected emergency, target process non-zero exit, missing artifacts, and no sensor progress.
- [ ] 6.3 Implement JSONL timeline events for RPC calls, sensor reads, actuator commands, frame captures, target lifecycle events, and assertion failures.
- [ ] 6.4 Implement raw/PNG frame writing and best-effort `video.mp4` creation; if video encoding is unavailable, preserve raw/PNG frames and record the video failure in `summary.json`.
- [ ] 6.5 Add failure-injection tests proving `failures.jsonl` and `summary.json` record first failure reason, scenario, seed, and frame id when available.

## 7. Verification And Closure

- [ ] 7.1 [Checkpoint] Run docs-first verifier-subagent review using `verify-sequence/default` for `proposal.md`, `design.md`, `tasks.md`, and `specs/local-simcar-3d-simulation/spec.md`. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`, including `subject_required_any_of`, `verifier_evidence_required`, `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/add-simcar-3d-local-simulation/verification/artifact-review-findings.json`, verifier evidence JSON at `openspec/changes/add-simcar-3d-local-simulation/verification/artifact-review-evidence.json`, and caller/orchestrator-maintained `agent-table.json`.
- [ ] 7.2 Run host static guardrails: no tracked `new/` file changes, no simulator implementation path that parses `new/code`, no committed generated gRPC output unless explicitly required, and no production build linkage to simulator gRPC dependencies.
- [ ] 7.3 Run host smoke tests for fixed-response bridge, `simcar run` on `straight`, effective parameter sidecar disablement, low-voltage non-emergency default, startup sensor availability, and required artifact files.
- [ ] 7.4 Run scenario tests for `bend`, `cross`, `circle`, `straight`, `lost-line`, `overexposure`, and `shadow`, verifying each writes summary, replay manifest, structured logs, raw frames, PNG frames, and video or recorded video-failure evidence.
- [ ] 7.5 Run or document a production board build/smoke check confirming direct-match board bridge behavior remains unchanged after host-only simulator additions.
- [ ] 7.6 [Checkpoint] Run source-first verifier-subagent review using `verify-sequence/default` for changed `simcar/` implementation, build/test files, static guards, and directly impacted build orchestration. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`, including `subject_required_any_of`, `verifier_evidence_required`, `valid_pass_requirements`, `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/add-simcar-3d-local-simulation/verification/source-review-findings.json`, verifier evidence JSON at `openspec/changes/add-simcar-3d-local-simulation/verification/source-review-evidence.json`, and caller/orchestrator-maintained `agent-table.json`.
