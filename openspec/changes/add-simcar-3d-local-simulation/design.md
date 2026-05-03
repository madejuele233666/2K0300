## Context

This change adds a host-local simulator for the LS2K0300 line-following runtime. The implementation must be possible from the perspective of `true_LS2K0300_Library/` plus this OpenSpec change: facts learned from the current target runtime are embedded here as a shadow contract, and implementation should not require rereading or modifying `new/code`.

Target shadow contract:

- The opaque target entrypoint is `new/user/main.cpp`; it is part of the read-only target.
- Target source under `new/user/main.cpp` and `new/code/*` must not be edited by this change.
- The target process reads `LS2K_PROFILE_PATH`, `LS2K_PARAMS_PATH`, `LS2K_AUTO_START`, `LS2K_AUTO_STOP_AFTER_MS`, and `LS2K_AUTO_RESET_FAULT`.
- Default target config paths are `new/config/hardware_profile.json` and `new/config/default_params.json`, but simulation runs must pass generated run-local copies through environment variables.
- The first simulation parameter copy must set `assistant_enabled=0` and `steering_media_enabled=0`; later changes may enable those sidecars through mocks or real receivers.
- The replacement bridge namespace is `ls2k::platform::true_ls2k0300`.
- Required bridge behavior groups are camera initialize/capture/shutdown, IMU initialize/read, encoder initialize/read counts, motor initialize/apply/disable, battery raw ADC read, timer start/stop/running, and assistant/steering-media stubs sufficient for disabled-first operation.
- Required devices for MVP simulation are UVC grayscale camera, IMU, left/right encoder, left/right motor PWM/GPIO, battery ADC, timer, and file persistence through run-local JSON files.
- Future-only hardware reservations are TFLite, Flash/file persistence, and TCP/UDP/wireless assistant. Other hardware families are intentionally out of scope.
- Camera frames crossing the bridge are target-compatible `320x240` grayscale frames with exactly `76800` bytes.
- The first-stage runtime mode is wall-clock real time and seed-stable, but not bit-for-bit deterministic, because the target time source uses `std::chrono::steady_clock`.

## Goals / Non-Goals

**Goals:**

- Run the existing target runtime as an opaque read-only process against simulated LS2K0300 devices.
- Discover image algorithm failures locally before board or track runs.
- Use mature tools for hard parts: gRPC/Protobuf for IPC, MuJoCo for live 3D simulation, and BlenderProc later for offline high-fidelity generation.
- Produce complete run artifacts for failure diagnosis, replay, and regression.
- Keep production board deployment behavior unchanged.

**Non-Goals:**

- Do not implement deterministic stepping in MVP.
- Do not implement a hand-written socket protocol.
- Do not modify target runtime source or tracked target config files.
- Do not enable assistant or steering media sidecars in MVP.
- Do not add display, button, buzzer, servo, or extra GPIO simulation reservations.

## Decisions

### Decision 1: Treat the target runtime as an opaque read-only process

Problem solved: the simulator must validate the real runtime path without allowing simulator code to drift into a special runner-only behavior.

Alternatives considered:

- Link individual runtime components into a custom test runner.
- Modify target code to expose simulator hooks.
- Launch the existing target main process with a simulation bridge.

Chosen option: launch the existing target main process from `simcar run`, while simulation builds link replacement bridge implementations for `ls2k::platform::true_ls2k0300`.

Stack Equivalent: this is a hardware-in-the-loop adapter boundary, except the hardware process is replaced by a local simulator server and the target binary is otherwise treated as the firmware/application under test.

Named Deliverables:

- `simcar run` orchestration command.
- A simulation build target that compiles the opaque target entrypoint and links the simulator replacement bridge instead of the production bridge implementation.
- `simcar/docs/new_shadow_contract.md` containing this contract in implementation-facing form.

Failure Semantics:

- If the target process exits non-zero, `summary.json` records failure and the captured stdout/stderr paths.
- If the target process mutates tracked files under `new/`, static guard checks fail the run or verification task.
- If required environment variables are missing, `simcar run` fails before launching the target.

Boundary Examples:

- Allowed: pass `LS2K_PARAMS_PATH=simcar/runs/<run_id>/effective_params.json`.
- Allowed: compile target sources as read-only inputs.
- Disallowed: edit `new/code/platform/*` to add simulator conditionals.

Contrast Structure: the production board path remains a direct-match LS2K0300 runtime; the simulator path is a host-only launch wrapper plus replacement bridge at link time.

Verification Hook:

- Host: run `git diff -- new` after simulator runs and expect no tracked target changes.
- Host: run a short `straight` simulation and verify the target process starts from the existing main entrypoint.
- On-board: build and launch the unchanged production target path with the normal hardware profile to confirm direct-match bridge behavior is still selected.

### Decision 2: Use gRPC/Protobuf over a run-scoped Unix domain socket

Problem solved: high-quality 3D rendering is dependency-heavy and benefits from process isolation, while the bridge needs a stable typed protocol.

Alternatives considered:

- In-process `pybind11/embed`.
- Hand-written JSON or binary socket protocol.
- ZeroMQ with custom schemas.
- gRPC/Protobuf with generated C++ and Python bindings.

Chosen option: use generated gRPC/Protobuf over `simcar/runs/<run_id>/sim.sock`. ZeroMQ remains a fallback only if gRPC proves infeasible.

Stack Equivalent: this is a local device-server RPC boundary, similar to an external simulator API, but constrained to Unix domain sockets and generated schemas.

Named Deliverables:

- `simcar/proto/simcar.proto`.
- Generated C++ and Python bindings as build artifacts.
- C++ simulator client used by replacement bridge code.
- Python gRPC server used by `simcar run`.

Failure Semantics:

- If the socket is unavailable at startup, bridge initialization returns not-ready status and the run fails closed.
- If an RPC times out, the bridge returns invalid sensor data or failed motor apply according to the target bridge behavior group.
- If frame size is not exactly `320 * 240`, camera capture returns invalid and the run records a protocol failure.

Boundary Examples:

- `CaptureCameraFrame` returns `valid`, `width`, `height`, `gray_bytes`, `frame_id`, and `sim_time_ms`.
- `ApplyMotorCommand` sends logical left/right PWM values and receives an apply status.
- `ReadBatteryRaw` returns a valid raw ADC value above threshold unless the scenario injects low voltage.

Contrast Structure: generated protobuf definitions are the only wire contract; simulator code must not duplicate ad hoc message parsing in C++ or Python.

Verification Hook:

- Host: fixed-response server smoke test validates C++ client can receive a `76800` byte grayscale frame.
- Host: protocol test forces wrong frame size and expects camera invalid plus failure artifact.
- On-board: production build excludes simulator gRPC linkage and still uses the direct hardware bridge.

### Decision 3: Use MuJoCo for live 3D simulation and reserve BlenderProc for offline fidelity

Problem solved: hand-written 2D perspective generation can create systematic camera geometry artifacts; the simulator needs a camera model that naturally produces perspective from 3D scene geometry.

Alternatives considered:

- Hand-written 2D BEV-to-image warping.
- PyBullet live rendering.
- MuJoCo live rendering.
- Blender/BlenderProc as the live closed-loop renderer.
- CARLA/Gazebo/Webots/AirSim.

Chosen option: use MuJoCo for the live closed-loop backend because it is Python-friendly, supports physics stepping and offscreen camera rendering, and is lighter than full autonomous-driving simulators. Reserve BlenderProc for later offline high-fidelity sample generation, texture generation, and calibration assets.

Stack Equivalent: MuJoCo provides the physics/render loop normally implemented by a simulator engine; `simcar` owns scene configuration, sensor extraction, artifact writing, and bridge semantics.

Named Deliverables:

- Python simulator backend abstraction with a MuJoCo live implementation.
- First scene assets for `bend`, `cross`, `circle`, `straight`, `lost-line`, `overexposure`, and `shadow`.
- A documented placeholder for BlenderProc offline generation.

Failure Semantics:

- If MuJoCo is unavailable, `simcar run` fails before launching the target and records dependency failure.
- If rendering fails for a frame, the simulator returns an invalid camera frame and records a failure event.
- If a scenario requests unsupported high-fidelity features, MVP reports unsupported scenario fields rather than silently ignoring them.

Boundary Examples:

- The live backend produces RGB or grayscale output, then the simulator converts it to the `320x240` grayscale bridge frame.
- Exposure, shadow, blur, and lost-line are scenario-level perturbations applied through scene setup or post-processing.
- BlenderProc artifacts are not required for MVP runs.

Contrast Structure: MuJoCo is used for closed-loop speed and isolation; BlenderProc is not on the live critical path.

Verification Hook:

- Host: render one frame per first-stage scenario and verify dimensions, non-empty dynamic range, and artifact write.
- Host: run `overexposure` and `shadow` to confirm image perturbations reach output frames.
- On-board: camera hardware smoke remains unchanged; simulator image generation does not alter production camera initialization.

### Decision 4: Simulate device behavior at the target bridge boundary

Problem solved: the simulator must be compatible with both the public LS2K0300 library shape and the target runtime bridge shape without changing target code.

Alternatives considered:

- Emulate `/dev` and `/sys` files directly.
- Replace lower-level vendor functions only.
- Replace the project-owned `true_ls2k0300` bridge boundary.

Chosen option: replace the project-owned `ls2k::platform::true_ls2k0300` bridge for simulation builds, while keeping the simulator behavior faithful to `true_LS2K0300_Library/` device semantics.

Stack Equivalent: this is a HAL simulation layer at the board-adapter boundary, not a rewrite of perception/control logic.

Named Deliverables:

- C++ replacement bridge source files under `simcar/`.
- Simulator-side sensor and actuator model modules.
- Assistant and steering-media bridge stubs sufficient for disabled-first MVP.

Failure Semantics:

- Startup sensor unavailability is fail-closed unless explicitly configured as a scenario failure.
- Low voltage defaults to valid non-emergency to avoid blocking ordinary scenarios.
- Motor apply failures disable output and are recorded in artifacts.
- IMU defaults provide valid startup samples; zero-only gyro startup should be avoided unless a scenario explicitly tests calibration failure.

Boundary Examples:

- Camera returns `320x240` grayscale bytes.
- Encoder returns left/right raw counts, while target adapter normalization remains outside simulator control.
- Motor input is logical left/right PWM; the simulator records the logical command and updates vehicle state.
- Battery raw ADC returns a configured integer sample.

Contrast Structure: simulation does not create fake `/dev` nodes for MVP; it replaces the bridge in the simulation binary only.

Verification Hook:

- Host: fixed-response bridge smoke confirms startup receives camera, IMU, encoder, ADC, and motor readiness.
- Host: motor command changes later encoder and IMU samples in at least one smoke scenario.
- On-board: normal hardware bridge build and a no-simulator board smoke test confirm production bridge files still own hardware access.

### Decision 5: Store complete run artifacts under a run-scoped directory

Problem solved: local failures must be inspectable and replayable without guessing which sensor frame, control command, or image caused the failure.

Alternatives considered:

- Only print stdout diagnostics.
- Store only video frames.
- Store complete structured artifacts per run.

Chosen option: every `simcar run` writes a run directory with summary, scenario, effective parameters, replay manifest, structured JSONL timelines, raw/PNG frames, and video.

Stack Equivalent: this is a test-run evidence bundle similar to CI artifacts, but scoped to simulation and replay.

Named Deliverables:

- `simcar/runs/<run_id>/summary.json`.
- `scenario.yaml`, `effective_params.json`, `replay_manifest.json`.
- `timeline.jsonl`, `sensors.jsonl`, `actuators.jsonl`, `control_debug_snapshot.jsonl`, `failures.jsonl`.
- `frames/*.raw`, `frames/*.png`, and `video.mp4`.

Failure Semantics:

- Missing required artifacts fail the run.
- If video encoding is unavailable, the run records video failure but still preserves raw and PNG frames.
- Replay manifests must include scenario id, seed, socket path or transport info, target binary path, and effective parameter path.

Boundary Examples:

- First failure frame and reason appear in `summary.json`.
- Sensor and actuator logs are JSONL to support streaming writes.
- Raw frames remain byte-for-byte bridge frames for algorithm debugging.

Contrast Structure: stdout remains useful but is not the canonical artifact contract.

Verification Hook:

- Host: run artifact validator checks required files and minimal schema fields.
- Host: failure-injection scenario confirms `failures.jsonl` and first failure metadata.
- On-board: no board artifact path changes are required for this host-only feature.

## Independent Verification Plan (STANDARD/STRICT)

Verification uses shared sequence `verify-sequence/default` from `openspec/schemas/ai-enforced-workflow/verification-sequence.md` and these contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Runtime profile policy:

- Use verifier runtime profile `.codex/agents/verify-reviewer.toml`.
- Invocation template id: `verify-reviewer-inline-v3`.
- Review goal: `implementation_correctness`.

Review checkpoints:

- Artifact-completion docs-first review:
  - Primary surface: `proposal.md`, `design.md`, `tasks.md`, and `specs/local-simcar-3d-simulation/spec.md`.
  - Authoritative findings JSON: `openspec/changes/add-simcar-3d-local-simulation/verification/artifact-review-findings.json`.
  - Verifier evidence JSON: `openspec/changes/add-simcar-3d-local-simulation/verification/artifact-review-evidence.json`.
  - Agent table: `openspec/changes/add-simcar-3d-local-simulation/verification/agent-table.json`.
  - Continuation target on pass: implementation entry.
- Source-first implementation review:
  - Primary surface: changed `simcar/` implementation, build/test files, generated-code rules, and directly impacted build orchestration.
  - Authoritative findings JSON: `openspec/changes/add-simcar-3d-local-simulation/verification/source-review-findings.json`.
  - Verifier evidence JSON: `openspec/changes/add-simcar-3d-local-simulation/verification/source-review-evidence.json`.
  - Agent table: `openspec/changes/add-simcar-3d-local-simulation/verification/agent-table.json`.
  - Continuation target on pass: implementation closure.

Stage A flow:

- Checkpoints use the same `active/non_active` verification cycle.
- Approved docs remain reference material when source-first review runs.
- Repository-level `.index/` material is optional background only.
- Continue a usable `active` agent first; prefer `send_input` while that same active agent is open.
- Use `continuation_probe` to distinguish resume from recovery spawn.
- Spawn only when no usable active agent exists.
- Only `block -> pass` marks an agent `non_active`.
- Final termination requires a valid active pass.
- Valid pass requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`.
- Partial verification requires explicit `review_scope.scope`.

## External Repository Index Reference (Optional)

Repository index material is optional background only for this change.

- Canonical root: `.index/`
- Run contract path: `.index/contracts/repository-index-run-v1.json`
- Manifest contract path: `.index/contracts/repository-index-manifest-v1.json`
- Entry contract path: `.index/contracts/repository-index-entry-v1.json`
- Validator entrypoint: `.index/bin/validate_repository_index.py`
- Run modes: `full_refresh`, `scoped_refresh`
- Non-authority rule: `.index/` MUST NOT emit verifier verdicts, claim closure authority, define repair routing, or become required for verification completion.

## Migration Plan

- Add the OpenSpec artifacts first and run docs-first verification before implementation entry.
- Implement host-only `simcar/` code without modifying tracked target files under `new/`.
- Keep the production board build and bridge path unchanged; simulator replacement bridge is selected only by the simulation build/orchestration path.
- If simulator launch fails, no target files need rollback; remove run artifacts or the new `simcar/` implementation.
- Board smoke consideration: after implementation, run or document a production-path build/smoke check confirming the direct-match board path still initializes through the real bridge.

## Open Questions

- None blocking for MVP. Deterministic stepping, assistant/steering-media enablement, BlenderProc offline generation, and TFLite simulation are deferred follow-up changes.

## Risks / Trade-offs

- gRPC and MuJoCo add host dependency complexity, but they avoid maintaining custom IPC and custom 3D rendering.
- Wall-clock MVP runs improve time-to-first-simulation but cannot guarantee bit-for-bit deterministic replay.
- Replacing the bridge at link time is cleaner than modifying target code, but build rules must prevent accidentally linking both real and simulator bridge implementations.
- Storing full frame artifacts may consume disk space; run retention policy can be added later.
