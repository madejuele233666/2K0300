## Context

The source system remains the TC264 bare-metal application under `old/`, with the same legacy control split already identified in the superseded change:

- `old/user/cpu0_main.c`
- `old/user/isr.c`
- `old/code/All_init.c`
- `old/code/camera.c`
- `old/code/PID.c`
- `old/code/Motor.c`
- `old/code/ZiTaiJieSuan.c`
- `old/code/key.c`
- `old/code/FUZZY_PID_UCAS.c`

What changed is the target baseline. The completed change `port-old-to-new-ls2k0300-library` aligned against `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library`, but the actual vendor baseline is `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`.

Current vs target:

- Wrong baseline:
  - `.hpp` headers
  - object-style `zf_driver_*` / `zf_device_*` classes
  - extra file-driver wrappers such as `zf_driver_file_buffer.cpp`, `zf_driver_file_string.cpp`
  - timer, encoder, UVC, and IMU wrapped behind stronger C++ abstractions
- Real baseline:
  - `.h` headers
  - wrapper-light, mixed C/C++ APIs rather than the superseded wrapper-heavy object layer
  - many free functions and process-global/device-global state
  - `zf_device_imu_core + per-device imu660ra/imu660rb/imu963ra` split
  - `zf_device_uvc` exposed as `uvc_camera_init`, `wait_image_refresh`, `rgay_image`, and `cv::Mat` image globals
  - `pit_ms_init` plus `Pit_timer` / `std::function` callback wiring, but still without a project-owned symmetric stop contract

Reference alignment mode is `adapt`, not `replicate`. Legacy control/perception logic is still the asset to preserve, but the real LS2K0300 stack offers materially different lifecycle and device contracts than the superseded wrapper-heavy baseline.

### Reference Inventory

- `old/user/cpu0_main.c`
- `old/user/isr.c`
- `old/code/All_init.c`
- `old/code/camera.c`
- `old/code/PID.c`
- `old/code/Motor.c`
- `old/code/ZiTaiJieSuan.c`
- `old/code/key.c`
- `old/code/FUZZY_PID_UCAS.c`
- `new/user/CMakeLists.txt`
- `new/code/platform/bootstrap.cpp`
- `new/code/platform/camera_adapter.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/power_adapter.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/build.sh`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/CMakeLists.txt`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/main.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/cross.cmake`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_headfile.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_gpio.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pwm.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_adc.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_uvc.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_uvc.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu_core.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu_core.cpp`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu660ra.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu660rb.h`
- `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu963ra.h`

### Extracted Contracts

- Timer contract:
  - `Pit_timer` and `pit_ms_init(uint32_t ms, std::function<void()> callback)`
  - concrete issue: callback-thread lifecycle exists, but no documented project-owned stop API
- Encoder contract:
  - `encoder_get_count(const char *path)`
  - concrete issue: no documented clear/reset primitive in the public contract
- GPIO/PWM/ADC contract:
  - `gpio_set_level(path, dat)`, `pwm_set_duty(path, duty)`, `adc_convert(path)`
  - concrete issue: direct path strings and free-function ownership
- Camera contract:
  - `uvc_camera_init(path)`, `wait_image_refresh()`, globals `rgay_image`, `frame_rgay`, `frame_rgb`
  - concrete issue: global image ownership, `cv::Mat` exposure in vendor globals, and no public exposure-control API
- IMU contract:
  - `imu_get_dev_info()`, global `imu_type`, per-device getters `imu660ra_get_acc()` and peers, global axis values
  - concrete issue: detection and sample collection are split across globals and device-specific procedures

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `new/user/CMakeLists.txt` bound to wrong root | `new/user/CMakeLists.txt` + `new/user/build.sh` | Adapt | Re-anchor the workspace to `true_LS2K0300_Library/.../project/user` semantics only. |
| `old/user/isr.c` | `new/code/runtime/control_loop.cpp` + `new/code/platform/true_ls2k0300/timer_bridge.cpp` | Adapt | Control-loop timing must preserve stop ownership even though vendor PIT does not. |
| `old/code/camera.c` | `new/code/legacy/camera_logic.cpp` + `new/code/platform/true_ls2k0300/camera_bridge.cpp` | Adapt | Legacy logic consumes copied grayscale snapshots, never vendor globals. |
| `old/code/ZiTaiJieSuan.c` | `new/code/legacy/attitude_logic.cpp` + `new/code/platform/true_ls2k0300/imu_bridge.cpp` | Adapt | Vendor IMU globals are normalized into project-owned snapshots. |
| `old/code/Motor.c` | `new/code/legacy/motor_logic.cpp` + `new/code/platform/true_ls2k0300/motor_bridge.cpp` | Adapt | Free-function actuator calls are hidden behind a project-owned adapter. |
| `old/code/PID.c` | `new/code/legacy/pid_control.cpp` | Replicate | Core math stays reusable once IO semantics are normalized. |
| `old/code/key.c` | `new/code/platform/param_store.cpp` + `new/code/platform/true_ls2k0300/camera_bridge.cpp` | Adapt | Parameter load remains file-backed; unsupported `exp_light` behavior must be surfaced explicitly. |
| superseded change evidence under `openspec/changes/port-old-to-new-ls2k0300-library/` | new change verification and archive notes | Adapt | Historical evidence is retained, but not accepted as proof for the real target. |

### Coverage Report

- ✅ PWM/GPIO and ADC have direct true-baseline equivalents through vendor free functions.
- ✅ UVC and IMU functionality exist on the real baseline, but they are exposed through global-state-heavy, mixed C/C++ contracts that require normalization.
- ⚠️ PIT timing exists, but the public vendor contract does not provide the lifecycle symmetry required by fail-safe shutdown.
- ⚠️ Encoder count access exists, but the public contract does not provide the clear-after-read semantics assumed by the superseded implementation.
- ⚠️ Camera capture exists, but the public contract does not expose manual exposure control; `exp_light` cannot be assumed to apply directly.
- ❌ Prior build and runtime verification artifacts do not validate the real target baseline and cannot be reused as acceptance evidence.

## Goals / Non-Goals

**Goals:**

- Retarget the accepted migration contract to `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`.
- Preserve the architectural goal of project-owned port contracts and decoupled legacy logic.
- Introduce an implementation-grade normalization layer for the real vendor API shape, especially where the vendor surface is wrapper-light, mixed C/C++, and global-state-driven.
- Define explicit failure semantics for timer stop, encoder delta derivation, unsupported exposure control, and superseded evidence handling.
- Make verification evidence unambiguous about which vendor baseline was exercised.

**Non-Goals:**

- Treating the superseded `LS2K0300_Library/LS2K300_Library/...` tree as an accepted alias of the real target.
- Syncing the superseded change's specs as the accepted contract for the real port.
- Rewriting the preserved legacy algorithms into an idiomatic from-scratch LS2K0300 application.
- Forking vendor driver code just to recreate the missing C++ wrappers from the wrong baseline.

## Decisions

### Decision 1: Treat the new work as a superseding retarget, not an in-place rewrite of the completed change

Problem being solved:
The earlier change is complete, but it completed against the wrong target baseline. Rewriting that artifact pack in place would blur accepted history, verification evidence, and the reason the retarget is needed.

Alternatives considered:

- Option A: Reopen and edit `port-old-to-new-ls2k0300-library` as if the target path had only been misnamed.
  - Strength: fewer OpenSpec directories.
  - Weakness: invalidates the meaning of its completed tasks and verification logs.
  - Migration cost: low on paper, high in audit clarity.
  - Verification impact: poor, because old evidence appears falsely portable.
- Option B: Create a new change that explicitly supersedes the prior target selection.
  - Strength: preserves historical traceability and makes the retarget auditable.
  - Weakness: duplicates some narrative context.
  - Migration cost: moderate.
  - Verification impact: strong, because new evidence is clearly attributable to the real baseline.

Chosen approach:
Option B. This change is the canonical retarget. The prior change remains historical evidence of the wrong baseline choice and SHALL NOT be synced as the accepted contract for the true library target.

Stack equivalent:
The concrete constructs are:

- `openspec/changes/retarget-port-to-true-ls2k0300-library/`
- explicit supersession notes in proposal, design, and tasks
- verification artifacts that name the true vendor root and reject old-root evidence reuse

Named deliverables:

- `openspec/changes/retarget-port-to-true-ls2k0300-library/proposal.md`
- `openspec/changes/retarget-port-to-true-ls2k0300-library/design.md`
- `openspec/changes/retarget-port-to-true-ls2k0300-library/tasks.md`
- `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/`

Failure semantics:

- If implementation or verification still points to `LS2K0300_Library/LS2K300_Library/...`, the retarget is blocked.
- If archive/sync handling would promote the superseded change as the accepted spec source for the real target, the workflow must stop and route back to artifact repair.

Verification hook:
Artifact verification scans proposal, design, specs, and tasks for explicit supersession handling and rejects any wording that treats the wrong baseline as equivalent.

### Decision 2: Keep `new/` as the migration workspace, but re-anchor it to the true vendor project skeleton only

Problem being solved:
The current `new/` workspace is already the migration boundary, but its build root and source list are pinned to the wrong vendor tree and to source files that do not exist in the real baseline.

Alternatives considered:

- Option A: Edit the vendor `project/` tree in place under `true_LS2K0300_Library/...`.
  - Strength: closest to vendor defaults.
  - Weakness: mixes migration churn with vendor-owned files and weakens ownership boundaries.
  - Migration cost: lower initially.
  - Verification impact: noisier diff surface.
- Option B: Keep `new/`, but update it to be copy-derived from the true vendor `project/user` skeleton and compile only against the true vendor libraries.
  - Strength: keeps migration ownership local while aligning to the real stack.
  - Weakness: requires a deliberate reseed of build assumptions.
  - Migration cost: moderate.
  - Verification impact: strong, because target-root assertions are reviewable.

Chosen approach:
Option B. `new/` remains the owned workspace, but every vendor-root assumption is reset to the true library tree. The workspace SHALL not compile source files that only exist in the superseded baseline.

Stack equivalent:
The concrete construct is a true-rooted build contract in `new/user/CMakeLists.txt`, `new/user/build.sh`, and related docs, with the vendor root resolved only from `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`.

Named deliverables:

- `new/user/CMakeLists.txt`
- `new/user/build.sh`
- `new/user/run_remote_smoke.sh`
- `new/docs/vendor-baseline.md`
- `new/docs/debugging.md`

Failure semantics:

- If the build graph references `zf_driver_file_buffer.cpp`, `zf_driver_file_string.cpp`, `zf_driver_pit_fd.cpp`, `zf_device_imu.cpp`, or other superseded-only source files, the build contract is invalid.
- If the true vendor project skeleton lacks an expected file, the migration must fail closed and record the missing dependency instead of silently falling back to the old tree.

Verification hook:
Implementation verification searches `new/` and build logs for the active vendor root and confirms that the compiled vendor source list exists under the true tree.

### Decision 3: Introduce an internal true-baseline normalization layer beneath the public platform adapters

Problem being solved:
The real baseline exposes device access through `.h`-named headers, many free functions and globals, plus some direct C++ types/classes such as `std::function`, `cv::Mat`, and `Pit_timer`. Directly binding those APIs inside public adapters would preserve the existing decoupling at the port boundary, but it would still make platform code brittle, hard to reason about, and difficult to verify for leak-free ownership.

Alternatives considered:

- Option A: Call vendor free functions and read vendor globals directly from each adapter implementation.
  - Strength: fastest rewrite.
  - Weakness: spreads vendor semantics, paths, and ownership rules across multiple files.
  - Migration cost: low short term, high long term.
  - Verification impact: harder to audit leakage and semantic drift.
- Option B: Add a thin internal bridge layer that is the only code allowed to include true vendor `.h` headers or touch vendor globals/direct vendor C++ types, while adapters expose project-owned snapshots upward.
  - Strength: stronger decoupling and cleaner failure semantics.
  - Weakness: one extra layer of code.
  - Migration cost: moderate.
  - Verification impact: strong, because the leak boundary becomes explicit.

Chosen approach:
Option B. `new/code/platform/true_ls2k0300/` becomes the internal normalization slice for true vendor APIs. Public adapters under `new/code/platform/` consume bridge-produced snapshots and never expose vendor globals, vendor free functions, direct vendor `cv::Mat` / callback types, or raw device nodes outside platform-owned code.

Stack equivalent:
The concrete constructs are internal bridge translation units and project-owned snapshot DTOs.

Boundary examples:

- Caller: `new/code/runtime/perception_frontend.cpp`
  - Payload in: `port::RuntimeParameters` and adapter-owned capture request
  - Forbidden leak: `rgay_image`, `frame_rgay`, `cv::Mat` globals, `uvc_camera_init`
  - Returned form: `port::CameraCapture` with owned grayscale bytes and geometry markers
- Caller: `new/code/runtime/control_loop.cpp`
  - Payload in: timer tick and project-owned runtime state
  - Forbidden leak: `imu_type`, `imu660ra_acc_x`, `encoder_get_count`, direct `/dev/zf_*` strings
  - Returned form: `port::ImuSample`, `port::EncoderDelta`, `port::ActuatorCommand`
- Caller: `new/code/platform/camera_adapter.cpp`
  - Payload in: bridge request object
  - Forbidden leak: vendor global buffer pointers crossing into `new/code/legacy/`
  - Returned form: copied `LegacyCameraFrame` and diagnostics marker

Named deliverables:

- `new/code/platform/true_ls2k0300/camera_bridge.cpp`
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
- `new/code/platform/true_ls2k0300/timer_bridge.cpp`
- `new/code/platform/true_ls2k0300/motor_bridge.cpp`
- `new/code/platform/true_ls2k0300/adc_bridge.cpp`
- `new/code/platform/true_ls2k0300/vendor_paths.hpp`
- updated `new/code/platform/*.cpp` adapters

Failure semantics:

- Any include of `zf_common_headfile.h`, vendor `.h` headers, direct vendor globals, or direct vendor C++ types outside `new/code/platform/true_ls2k0300/` is a contract violation.
- Any adapter that returns pointers into vendor global buffers instead of copied project-owned state is invalid.

Verification hook:
Implementation review scans for vendor `.h` includes and vendor symbol usage outside the bridge slice, and checks that public `new/code/port/*.hpp` remains vendor-free.

### Decision 4: Re-spec semantic gaps explicitly instead of recreating the wrong baseline's stronger wrappers

Problem being solved:
The wrong baseline offered semantics that are not actually present in the true vendor surface, including explicit stop ownership, clear-after-read encoder access, wrapper-owned UVC state, and a more uniform device facade. Recreating those semantics by assumption would hide real incompatibilities.

Alternatives considered:

- Option A: Rebuild the wrong baseline's wrappers on top of the true vendor tree so the rest of `new/` barely changes.
  - Strength: minimal downstream churn.
  - Weakness: reintroduces a shadow platform and risks lying about true device semantics.
  - Migration cost: moderate.
  - Verification impact: weak, because the real target behavior stays obscured.
- Option B: Document and implement project-owned semantics for each gap, even when the result is stricter than the superseded code.
  - Strength: accurate contract and safer runtime behavior.
  - Weakness: forces explicit decisions for unsupported or ambiguous behavior.
  - Migration cost: moderate to high.
  - Verification impact: strong.

Chosen approach:
Option B. The retarget SHALL make the gaps first-class:

- Timer:
  - canonical behavior is an adapter-owned stoppable periodic executor
  - vendor `pit_ms_init` is usable only if the bridge can provide explicit lifecycle control; otherwise the runtime remains on the project-owned executor path
- Encoder:
  - adapter-owned state stores last sampled absolute counts and derives deltas explicitly
  - initialization, counter reset, and improbable wrap behavior emit diagnostics instead of being silently interpreted as valid deltas
- Camera:
  - direct-match path covers capture and grayscale conversion only
  - if `exp_light` must control real exposure and the true public API cannot honor it, the camera profile must route through a named adaptation hook or startup stays fail-closed/diagnostics-only according to the documented policy
- IMU:
  - bridge detects `imu_type`, calls the appropriate device-specific getters, and copies the resulting globals into one project-owned `ImuSample`
- Compatibility shims:
  - `__has_include(...hpp)` fallbacks, stub classes, and cross-baseline compatibility code are forbidden

Stack equivalent:
The concrete constructs are bridge-owned state tables plus explicit diagnostics markers for unsupported or degraded semantics.

Named deliverables:

- `new/code/platform/true_ls2k0300/timer_bridge.cpp`
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
- `new/code/platform/true_ls2k0300/camera_bridge.cpp`
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`
- `new/config/hardware_profile.json`
- `new/docs/hardware-matrix.md`

Failure semantics:

- If explicit stop ownership cannot be guaranteed, the timer backend must not claim vendor PIT direct-match success.
- If encoder delta derivation cannot be trusted on first sample or after reset, actuator updates are vetoed until the adapter regains a valid baseline.
- If `exp_light` is declared startup-critical for the active profile but no supported exposure path exists, startup blocks or remains diagnostics-only with actuators disarmed.
- If the codebase still depends on `.hpp` wrappers or `__has_include` stubs, the retarget is blocked.

Verification hook:
Implementation verification exercises timer shutdown, encoder delta initialization, camera unsupported-exposure handling, and IMU type detection with runtime logs tied to the true baseline.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`

Two-stage flow:
- Stage 1: read-only verifier subagent (`.codex/agents/verify-reviewer.toml`) local review
- Stage 2: Gemini second opinion through logical runner contract `gemini-capture` only when required (`STRICT` or explicit dual gate)

Runtime profile policy:
- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml` by default.
- Optional per-checkpoint override MAY be documented when needed.
- Risk-tier runtime profile mapping is optional and not required.

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
- Invocation method: shell command entry in active workspace
- Shell template id: `verify-reviewer-shell-v1`
- Local review scope:
  - `openspec/changes/retarget-port-to-true-ls2k0300-library/proposal.md`
  - `openspec/changes/retarget-port-to-true-ls2k0300-library/design.md`
  - `openspec/changes/retarget-port-to-true-ls2k0300-library/specs/**/*.md`
  - `openspec/changes/retarget-port-to-true-ls2k0300-library/tasks.md`
  - selected reference files from `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/`
  - the superseded change artifacts under `openspec/changes/port-old-to-new-ls2k0300-library/` for contrast only
- Local review output path: `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/artifact-subagent-review.json`
- Verifier runtime profile (default or override, optional): default from `.codex/agents/verify-reviewer.toml`
- Gemini policy: `STRICT`
- Runner contract: `gemini-capture`
- Prompt inputs:
  - change artifacts
  - true vendor reference files
  - superseded change references that demonstrate the baseline mismatch
- Output format: `normalized local review json` (+ Gemini raw/report when enabled)
- Raw report path (when Gemini enabled): `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/artifact-gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/artifact-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report_path output is missing, run recovery using input_raw_path -> report_path before blocking`
- Loop behavior: `if auto-fixable implementation findings exist and budget remains, main flow fixes then reruns with a fresh verifier instance`
- Execution evidence path: `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/artifact-review-command.txt`
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Sequence reference: `verify-sequence/default`
- Mode: `implementation`
- Local reviewer: `verify-reviewer` (read-only subagent)
- Invocation method: shell command entry in active workspace
- Shell template id: `verify-reviewer-shell-v1`
- Local review scope:
  - all changed files under `new/`
  - any touched files under `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/`
  - build output and runtime evidence produced by this change
  - explicit scans for wrong-root references and forbidden fallback shims
- Local review output path: `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/implementation-subagent-review.json`
- Verifier runtime profile (default or override, optional): default from `.codex/agents/verify-reviewer.toml`
- Gemini policy: `STRICT`
- Runner contract: `gemini-capture`
- Prompt inputs:
  - changed implementation files
  - `design.md`
  - `specs/**/*.md`
  - runtime logs, build logs, and command-resolution evidence
- Output format: `normalized local review json` (+ Gemini raw/report when enabled)
- Raw report path (when Gemini enabled): `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/implementation-gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/implementation-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report_path output is missing, run recovery using input_raw_path -> report_path before blocking`
- Loop behavior: `if auto-fixable implementation findings exist and budget remains, main flow fixes then reruns with a fresh verifier instance`
- Execution evidence path: `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/implementation-review-command.txt`
- Skill entry point: `openspec-verify-change`

## Migration Plan

1. Freeze the superseded baseline contract:
   - do not sync `port-old-to-new-ls2k0300-library` specs
   - use it only as contrast evidence for the retarget
2. Repair the workspace root:
   - repoint `new/user/*` to the true vendor tree
   - remove old-root source files and wrapper assumptions from build wiring
3. Introduce the internal true-baseline bridge slice:
   - normalize timer, camera, IMU, encoder, motor, and ADC contracts
   - keep `new/code/port/*.hpp` and `new/code/legacy/*` project-owned
4. Rework platform adapters to consume bridge snapshots and explicit failure markers.
5. Re-run build, smoke, parameter-failure, and hardware-profile verification against the true baseline only.
6. Archive the superseded change later as historical evidence without syncing its specs.

## Open Questions

- Can manual exposure control for `exp_light` be supported on the true baseline without vendor forking, or must the direct-match path explicitly forbid that behavior?
- Do deployed encoder device nodes reset on read in the target kernel, or must delta derivation always be adapter-owned by previous-sample subtraction?
- Is there any safe way to wrap `pit_ms_init` with explicit shutdown control on the real board, or should the project-owned periodic executor remain the canonical phase-1 timer path?

## Risks / Trade-offs

- The internal bridge layer adds code and files, but it creates a much cleaner decoupling boundary than scattering vendor globals through adapters.
- Fail-closed handling for unsupported exposure or timer semantics may delay bring-up, but it avoids falsely claiming direct-match support.
- Keeping `new/` instead of moving into the vendor project preserves ownership and reviewability, but it requires more explicit build-root maintenance.
- Retaining legacy algorithms limits behavior drift, but hidden assumptions may still surface once the real vendor semantics replace the stronger wrapper contracts.
