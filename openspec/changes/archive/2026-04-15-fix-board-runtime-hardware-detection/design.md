## Context

Board-side smoke testing on April 4, 2026 established three concrete mismatches between the current implementation and the accepted true-LS2K0300 contracts.

- `openspec/changes/archive/2026-04-04-retarget-port-to-true-ls2k0300-library/verification/runtime-smoke.log` shows camera startup succeeding on the board, followed by IMU startup failing because the code attempts to open `/sys/bus/iio/devices/iio:device1/name` while the tested board exposed only `iio:device0`.
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`, `motor_bridge.cpp`, and `adc_bridge.cpp` currently convert bridge operations into unconditional success or always-valid data shapes even when underlying file/device I/O can fail.
- `new/user/run_remote_smoke.sh` uses `|| true` around the remote execution path, which preserves log collection but discards the runtime process exit status. That makes script success weaker than actual board-runtime success.

Reference alignment mode is `adapt`. The change does not replace the project-owned adapter boundary; it tightens that boundary so direct-match mode only claims support when the board-visible resource is real and the bridge can tell success from failure.

### Reference Inventory

- `new/user/run_remote_smoke.sh`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/power_adapter.cpp`
- `new/code/platform/true_ls2k0300/vendor_paths.hpp`
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
- `new/code/platform/true_ls2k0300/motor_bridge.cpp`
- `new/code/platform/true_ls2k0300/adc_bridge.cpp`
- `new/code/runtime/startup.cpp`
- `openspec/specs/tc264-to-true-ls2k0300-adapter-layer/spec.md`
- `openspec/specs/true-ls2k0300-port-workspace/spec.md`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu_core.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_file.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_adc.cpp`
- `openspec/changes/archive/2026-04-04-retarget-port-to-true-ls2k0300-library/verification/runtime-smoke.log`

### Extracted Contracts

- IMU vendor contract:
  - current vendor helper uses a fixed sysfs path in `zf_device_imu_core.cpp`
  - the project bridge currently trusts that helper and exposes only `bool InitializeImu()`
  - failure behavior today: fixed-path miss produces `DEV_NO_FIND`, which blocks startup but does not explain whether the board lacks IMU support or only uses a different node
- Encoder vendor contract:
  - `encoder_get_count(path)` reads a device node through the file helper
  - failure behavior today: the bridge always reports `counts.valid = true`, so downstream code cannot distinguish actual counts from read failure
- Motor vendor contract:
  - PWM/GPIO writes happen via free functions and current bridge initialization always returns success
  - failure behavior today: apply/shutdown do not propagate device-write failures
- ADC vendor contract:
  - `adc_convert(path)` parses a string read from sysfs
  - failure behavior today: a failed read collapses to `0`, which is indistinguishable from an actual low raw reading unless the higher layer adds its own guard
- Smoke-runner contract:
  - the helper must upload config, launch the board binary, retrieve logs, and communicate a truthful runtime verdict
  - failure behavior today: it preserves evidence but discards the remote process exit code

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `zf_device_imu_core.cpp` fixed `iio:device1` lookup | `new/code/platform/true_ls2k0300/imu_bridge.cpp` + project-owned diagnostics | Adapt | Keep vendor globals hidden, but resolve the active IMU node from real board-visible inventory or explicit override. |
| `zf_driver_encoder.cpp` + `zf_driver_file.cpp` raw node reads | `new/code/platform/true_ls2k0300/encoder_bridge.cpp` + `new/code/platform/encoder_adapter.cpp` | Adapt | Bridge must carry read failure explicitly so adapter readiness and delta validity stay truthful. |
| `zf_driver_pwm.cpp` / `zf_driver_gpio.cpp` free-function writes | `new/code/platform/true_ls2k0300/motor_bridge.cpp` + `new/code/platform/motor_adapter.cpp` | Adapt | Preserve project-owned actuator boundary but surface write/init failure instead of unconditional success. |
| `zf_driver_adc.cpp` raw parse semantics | `new/code/platform/true_ls2k0300/adc_bridge.cpp` + `new/code/platform/power_adapter.cpp` | Adapt | Treat missing ADC input as unavailable, not as a synthetic low-voltage sample. |
| `new/user/run_remote_smoke.sh` current remote launch | same file + verification outputs under this change | Adapt | Keep best-effort log retrieval, but expose the board process result as the smoke verdict. |

### Coverage Report

- ✅ Camera direct-match path is already board-usable on the tested hardware and can remain fixed to `/dev/video0` until a concrete board mismatch is observed.
- ⚠️ IMU support is only partially covered because startup fails closed but the direct-match mapping still assumes a fixed sysfs node that is not stable across boards.
- ⚠️ Encoder and motor paths are only partially covered because node names exist on the tested board, but failure propagation and readiness semantics are not truthful.
- ⚠️ ADC low-voltage checks are only partially covered because the tested path exists, yet the bridge cannot distinguish read failure from a real `0` sample.
- ❌ Board smoke verdict coverage is missing because the helper can return success while the runtime itself fails.

### Unresolved Alignment Risks

- The real board may expose IMU identity through a kernel path or naming convention different from the tested board; the change must keep an explicit override path so the project does not replace one hidden invariant with another.
- Motor and encoder device nodes exist on the tested board, but implementation must avoid active actuation during verification unless the task explicitly calls for it.

## Goals / Non-Goals

**Goals:**

- Make direct-match readiness truthful for IMU, encoder, motor, and ADC-backed low-voltage monitoring.
- Resolve the tested IMU startup failure mode by removing fixed-node assumptions and moving resource resolution into a project-owned bridge contract; if no supported runtime-facing IMU resource is visible, startup remains fail-safe with explicit diagnostics.
- Preserve fail-safe startup and runtime veto behavior when hardware discovery or bridge I/O fails.
- Make board smoke logs and exit codes suitable as implementation-verification evidence.

**Non-Goals:**

- Rewriting the preserved runtime/control architecture outside the affected hardware bridges and smoke helper.
- Replacing the project-owned adapter boundary with direct vendor calls in runtime or legacy code.
- Generalizing every vendor path into a configurable subsystem when the board contract can stay explicit and reviewable.
- Adding mandatory Gemini dual-gating for a `STANDARD` change.

## Decisions

### Decision 1: Resolve IMU direct-match resources inside the project-owned bridge

Problem being solved:
The current IMU path fails on the real board because the vendor helper assumes `/sys/bus/iio/devices/iio:device1`, while the tested board only exposed `iio:device0`.

Alternatives considered:

- Option A: Keep the fixed vendor helper path and switch the project profile to disabled/adaptation mode on boards that do not match.
  - Strength: smallest code change.
  - Weakness: preserves a false direct-match contract and turns a board-enumeration issue into permanent lost capability.
- Option B: Move IMU node discovery into `new/code/platform/true_ls2k0300/imu_bridge.cpp`, with explicit override support and diagnostics naming the resolved node.
  - Strength: keeps vendor globals hidden while making direct-match behavior truthful on real boards.
  - Weakness: requires the bridge to own a little more discovery logic and string/path state.

Why this option was chosen:
Option B keeps the accepted project boundary intact and fixes the exact board mismatch observed during testing without promoting hidden sysfs numbering assumptions into the spec.

Stack equivalent:
- project-owned bridge result type such as resolved node path + IMU type + validity flag
- adapter readiness driven by the bridge result, not by a vendor-global fallback

Named deliverables:
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`
- `new/code/platform/true_ls2k0300/bridge.hpp`
- `new/code/platform/imu_adapter.cpp`
- `openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.log`

Boundary example:
- Caller: `new/code/platform/imu_adapter.cpp`
- Payload in: direct-match `HardwareProfile` plus optional override env/path
- Forbidden leak: fixed `/sys/bus/iio/devices/iio:device1/*` assumptions outside the bridge
- Returned form: project-owned resolved-device result and project-owned `ImuBridgeSample`

Failure semantics:
- If no acceptable IMU node can be resolved, initialization returns not-ready and startup stays fail-safe.
- If a node resolves but per-sample reads fail later, samples return invalid and control-loop veto remains active.
- If an explicit override path is invalid, diagnostics must name that override as the failing input.

Verification hook:
- Re-run board smoke and require logs/evidence to show either the resolved IMU node or an explicit discovery failure that names the attempted path/resource.
- In the same evidence bundle, record camera path resolution and the encoder/motor/ADC direct-match resource evidence needed to explain success or fail-safe veto behavior.

### Decision 2: Make bridge I/O results explicit and drive readiness from them

Problem being solved:
Encoder, motor, and ADC bridges currently convert low-level file/device operations into unconditional success or always-valid results, which hides real hardware failures from startup and runtime safety logic.

Alternatives considered:

- Option A: Leave vendor helpers untouched and rely on downstream plausibility checks only.
  - Strength: minimal patching.
  - Weakness: cannot tell the difference between missing hardware, malformed I/O, and real device values.
- Option B: Add project-owned result structs/booleans around vendor read/write calls and let adapters translate those into readiness, valid-sample, and fail-safe diagnostics.
  - Strength: preserves vendor boundary while making startup/runtime state machine truthful.
  - Weakness: slightly increases bridge surface area.

Why this option was chosen:
Option B is the smallest change that aligns implementation with the existing fail-safe spec intent. The runtime cannot meet the current adapter-layer contract if bridge failures are indistinguishable from valid data.

Stack equivalent:
- `EncoderCounts.valid` reflects actual read success
- motor init performs a non-actuating capability probe over the configured GPIO/PWM resources before `ready=true`, and init/apply/disable return status instead of `void`-only success assumptions
- ADC read result distinguishes `unavailable` from `raw=0`

Named deliverables:
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/true_ls2k0300/motor_bridge.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/true_ls2k0300/adc_bridge.cpp`
- `new/code/platform/power_adapter.cpp`

Boundary examples:
- Caller: `new/code/platform/encoder_adapter.cpp`
  - Payload in: direct-match encoder request
  - Forbidden leak: implicit `counts.valid = true` after a failed file read
  - Returned form: project-owned count result with validity and optional diagnostic context
- Caller: `new/code/platform/motor_adapter.cpp`
  - Payload in: direct-match motor request
  - Forbidden leak: reporting `ready=true` before the configured PWM/GPIO nodes are verified reachable without actuation
  - Returned form: project-owned motor init/apply/disable results with probe diagnostics and write-failure status
- Caller: `new/code/platform/power_adapter.cpp`
  - Payload in: ADC path candidate
  - Forbidden leak: treating file-open failure as a real battery raw reading
  - Returned form: `LowVoltageSample` with `valid=false` when acquisition fails

Failure semantics:
- Encoder read failure yields `valid=false`, resets delta trust, and keeps actuator veto in place.
- Motor initialization performs deterministic resource-existence and access checks against the configured PWM/GPIO paths without driving the actuator; if that probe fails, the motor path stays not-ready and diagnostics name the failing resource.
- Motor write failure marks apply failure so control-loop arming/disarm state is not falsely advanced, and shutdown failure is reported instead of being treated as a clean disarm.
- ADC read failure yields `valid=false` and preserves emergency veto through the existing fail-safe branch.

Verification hook:
- Local review checks that no direct-match bridge reports unconditional success without a real I/O outcome, that motor readiness is gated by a non-actuating capability probe rather than a later write, and implementation verification exercises at least one failing-path log for IMU or ADC.
- Implementation evidence must also identify camera/encoder/motor/ADC runtime-facing resources (or documented unresolved resources) used during the same direct-match smoke run.

### Decision 3: Preserve board logs but stop masking board-runtime failure in the smoke helper

Problem being solved:
The smoke helper currently optimizes for evidence retrieval, but it discards the remote runtime exit code and can therefore report local success even when the board process failed during startup.

Alternatives considered:

- Option A: Return failure immediately on remote non-zero exit and skip log retrieval.
  - Strength: truthful exit code.
  - Weakness: loses the only evidence needed to diagnose board startup problems.
- Option B: Capture the remote runtime exit status, attempt log retrieval regardless, then return the preserved runtime verdict.
  - Strength: keeps evidence and makes the smoke result truthful.
  - Weakness: requires a slightly more explicit shell contract.

Why this option was chosen:
Option B matches the verification need uncovered by testing: evidence and verdict are both required.

Stack equivalent:
- shell helper state variables for remote runtime status and retrieval status
- verification log that records both the command target and the runtime verdict

Named deliverables:
- `new/user/run_remote_smoke.sh`
- `openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.log`
- `openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-execution-evidence.md`

Boundary example:
- Caller: implementation verification
- Payload in: board IP/user/path, uploaded config files, board binary
- Forbidden leak: `ssh` success treated as equivalent to runtime success
- Returned form: local shell exit status matching board runtime outcome, plus copied local log evidence

Failure semantics:
- If remote runtime exits non-zero but log retrieval succeeds, the helper still returns failure.
- If remote runtime exits non-zero and log retrieval also fails, the helper returns failure and writes the retrieval failure into local evidence.
- If SSH transport fails before runtime launch, the helper returns failure and may fall back locally only when the operator explicitly requests local-only smoke.

Verification hook:
- Implementation verification must demonstrate one board-side failing run where the helper returns non-zero and still leaves a local smoke log for diagnosis.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`

Two-stage flow:
- Stage 1: read-only verifier subagent (`.codex/agents/verify-reviewer.toml`) local review
- Stage 2: Gemini second opinion through logical runner contract `gemini-capture` only when required (`STRICT` or explicit dual gate)

Runtime profile policy:
- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml` by default.

Loop rule:
- Each verify/fix iteration MUST spawn a fresh verifier instance with no inherited verifier memory.

Minimal verification bundle (reuse exactly):
- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`
- `retry_policy`

### Artifact Verification

- Sequence reference: `verify-sequence/default`
- Mode: `artifact`
- Local reviewer: `verify-reviewer` (read-only subagent)
- Invocation method: built-in subagent API in main process
- Invocation template id: `verify-reviewer-inline-v1`
- Local review scope: `openspec/changes/fix-board-runtime-hardware-detection/proposal.md`, `design.md`, `tasks.md`, and both delta specs under `specs/`
- Local review output path: `openspec/changes/fix-board-runtime-hardware-detection/verification/artifact-local-review.json`
- Verifier runtime profile: from `.codex/agents/verify-reviewer.toml`
- Gemini policy: not tier-mandatory; run only if the checkpoint is explicitly dual-gated or local review returns unresolved blocker disagreement
- Runner contract: `gemini-capture`
- Prompt inputs: proposal/design/tasks/specs paths plus minimal bundle `change=fix-board-runtime-hardware-detection`, `mode=artifact`, `risk_tier=STANDARD`, `evidence_paths_or_diff_scope=artifact files under the change directory`, `findings_contract=shared-findings-v1`, `retry_policy=artifact_rerun_budget=1`
- Output format: `normalized local review json` (+ Gemini raw/report when enabled)
- Raw report path (when Gemini enabled): `openspec/changes/fix-board-runtime-hardware-detection/verification/artifact-gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/fix-board-runtime-hardware-detection/verification/artifact-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report_path output is missing, run recovery using input_raw_path -> report_path before blocking`
- Loop behavior: `if auto-fixable implementation findings exist and budget remains, main flow fixes then reruns with a fresh verifier instance`
- Execution evidence path: `openspec/changes/fix-board-runtime-hardware-detection/verification/artifact-execution-evidence.md` (record verifier invocation metadata `agent_id/start_at/end_at/final_state`; record Gemini resolved command when Gemini runs)
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Sequence reference: `verify-sequence/default`
- Mode: `implementation`
- Local reviewer: `verify-reviewer` (read-only subagent)
- Invocation method: built-in subagent API in main process
- Invocation template id: `verify-reviewer-inline-v1`
- Local review scope: bridge/adapter/smoke-helper diffs plus board smoke outputs under `openspec/changes/fix-board-runtime-hardware-detection/verification/`
- Local review output path: `openspec/changes/fix-board-runtime-hardware-detection/verification/implementation-local-review.json`
- Verifier runtime profile: from `.codex/agents/verify-reviewer.toml`
- Gemini policy: not tier-mandatory; enable only for explicit dual gate or unresolved safety-critical disagreement before archive
- Runner contract: `gemini-capture`
- Prompt inputs: changed implementation files, `new/user/build.sh`, `new/user/run_remote_smoke.sh`, local smoke evidence, and minimal bundle `change=fix-board-runtime-hardware-detection`, `mode=implementation`, `risk_tier=STANDARD`, `evidence_paths_or_diff_scope=changed bridge/adapter files plus verification logs`, `findings_contract=shared-findings-v1`, `retry_policy=implementation_auto_fix_budget=1`
- Output format: `normalized local review json` (+ Gemini raw/report when enabled)
- Raw report path (when Gemini enabled): `openspec/changes/fix-board-runtime-hardware-detection/verification/implementation-gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/fix-board-runtime-hardware-detection/verification/implementation-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report_path output is missing, run recovery using input_raw_path -> report_path before blocking`
- Loop behavior: `if auto-fixable implementation findings exist and budget remains, main flow fixes then reruns with a fresh verifier instance`
- Execution evidence path: `openspec/changes/fix-board-runtime-hardware-detection/verification/implementation-execution-evidence.md` (record verifier invocation metadata `agent_id/start_at/end_at/final_state`; record Gemini resolved command when Gemini runs)
- Skill entry point: `openspec-verify-change`

## Migration Plan

1. Update the change artifacts so the accepted contract requires truthful direct-match discovery, explicit I/O failure propagation, and truthful smoke verdicts.
2. Implement bridge/interface changes in `new/code/platform/true_ls2k0300/` and adapters in `new/code/platform/`.
3. Update `new/user/run_remote_smoke.sh` to preserve runtime exit status while keeping best-effort log retrieval.
4. Rebuild with `new/user/build.sh`, then rerun board smoke against the tested LS2K0300 board.
5. Record refreshed verification outputs under this change directory before implementation verification and archive/sync decisions.

Rollback:
- If the new discovery logic cannot identify a stable IMU resource and no reviewed override path exists, keep startup fail-safe and stop at diagnostics rather than reverting to a hard-coded guessed node.

## Open Questions

- Which exact sysfs naming rule is stable enough across supported LS2K0300 boards for IMU discovery, and does the project need one reviewed env override for bring-up labs?

## Risks / Trade-offs

- IMU discovery logic adds board-specific knowledge inside the bridge; that is acceptable only if the result remains project-owned and diagnosable.
- Tightening encoder/motor/ADC failure propagation may expose additional latent board issues that were previously hidden by fabricated success paths.
- Making smoke verdicts truthful may cause more CI/operator-visible failures at first, but that is preferable to accepting false board passes.
