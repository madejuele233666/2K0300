## Context

The source system is a TC264 bare-metal project under `old/`. Its application logic is spread across:

- `old/user/cpu0_main.c`: top-level foreground loop, frame-ready driven thresholding and lane extraction, threshold/low-voltage emergency veto, image display, menu interaction, and oscilloscope/debug output.
- `old/user/isr.c`: periodic interrupt-driven control loop, key scan, PID updates, IMU update, and motor output.
- `old/code/All_init.c`: hardware initialization for IMU, camera, TFT, ADC, encoder, PWM, GPIO, and PIT.
- `old/code/camera.c` and `old/code/camera.h`: lane extraction, thresholding, edge tracking, event flags, and steering error generation.
- `old/code/PID.c`: camera and gyro steering loops plus speed target shaping.
- `old/code/Motor.c`: encoder sampling and PWM/GPIO motor control.
- `old/code/ZiTaiJieSuan.c`: IMU bias initialization and attitude helpers.
- `old/code/key.c`: parameter menu and flash-backed runtime tuning.

The target system is `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library`, which exposes Linux device-node and file-backed C++ drivers:

- `zf_driver_pit` for periodic callbacks instead of MCU PIT interrupts.
- `zf_driver_encoder` for encoder count sampling from `/dev/zf_encoder_*`.
- `zf_driver_pwm` and GPIO device nodes for actuator control.
- `zf_device_uvc` for camera input instead of `mt9v03x_*`.
- `zf_device_imu` for IMU data via IIO files instead of `mpu6050_*`.

The migration cannot assume the source and target hardware are identical. Some deployments may keep equivalent cameras, IMUs, motors, and encoders; others may swap one or more peripherals or use different electrical/polarity/range conventions. The adapter layer therefore has to treat hardware sameness as a detected or configured profile, not as an unstated invariant.

Reference alignment mode is `adapt`, not `replicate`, because the logic should survive while MCU-specific lifecycle assumptions do not.

### Reference Inventory

- `old/user/cpu0_main.c`
- `old/user/cpu1_main.c`
- `old/user/isr.c`
- `old/code/All_init.c`
- `old/code/camera.c`
- `old/code/camera.h`
- `old/code/PID.c`
- `old/code/Motor.c`
- `old/code/ZiTaiJieSuan.c`
- `old/code/key.c`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/main.cpp`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit.hpp`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.hpp`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pwm.hpp`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu.hpp`
- `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_uvc.hpp`

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `old/code/All_init.c` | `new/code/platform/bootstrap.cpp` | Adapt | Replace register-level init with driver object construction and startup checks. |
| `old/user/isr.c` | `new/code/runtime/control_loop.cpp` | Adapt | PIT ISR timing becomes bounded periodic callback plus shared runtime state. |
| `old/user/cpu0_main.c` | `new/user/main.cpp` + `new/code/runtime/perception_frontend.cpp` | Adapt | Foreground path remains responsible for frame-ready perception publication and emergency-veto ownership, while initialization, cleanup, and diagnostics become Linux process lifecycle code. |
| `old/user/cpu1_main.c` | none in phase 1 | Adapt | Dual-core split is intentionally collapsed into one process with one control timer. |
| `old/code/camera.c` | `new/code/legacy/camera_logic.cpp` | Replicate | Image-processing logic is preserved after replacing direct camera globals with frame adapters. |
| `old/code/PID.c` | `new/code/legacy/pid_control.cpp` | Replicate | Core PID math is preserved with adapter-fed inputs, target-safe saturation, and the retained `DuoJi_GetP` fuzzy steering dependency. |
| `old/code/Motor.c` | `new/code/legacy/motor_logic.cpp` + `new/code/platform/motor_adapter.cpp` | Adapt | Decision logic is reused, but direct PWM/GPIO calls are replaced by actuator adapters. |
| `old/code/ZiTaiJieSuan.c` | `new/code/legacy/attitude_logic.cpp` + `new/code/platform/imu_adapter.cpp` | Adapt | Bias and filter helpers survive; raw sensor access is replaced. |
| `old/code/key.c` | `new/code/platform/param_store.cpp` + `new/config/default_params.json` | Adapt | Flash/menu behavior becomes file-backed parameters with an explicit phase-1 schema and optional runtime tuning hooks. |
| `old/code/FUZZY_PID_UCAS.c` | `new/code/legacy/fuzzy_pid_ucas.cpp` + `new/code/legacy/fuzzy_pid_ucas.hpp` | Adapt | `InitMH`, `DuoJi_GetP`, and `P_Mode`-selected steering tables stay in phase 1 because `PID.c`, `All_init.c`, and `key.c` still depend on them. |
| `old/code/Servo.c` | deferred in phase 1 | Adapt | Servo-specific behavior is replaced by bounded actuator output fields first. |
| `old/code/All_init_core1.c` | deferred in phase 1 | Adapt | Dual-core init is intentionally collapsed into the single-process runtime. |

### Coverage Report

- ✅ Camera processing, PID computation, and steering/speed logic can be migrated with adapter-backed inputs.
- ✅ Encoder, PWM, IMU, and timer contracts have LS2K0300 stack equivalents in the existing library.
- ✅ The retained `FUZZY_PID_UCAS` steering-table path is in scope because the phase-1 PID, bootstrap, and parameter flow still calls it.
- ⚠️ Flash-backed menu tuning is only partially covered; the phase-1 replacement is file-backed parameters plus logs, not full button-driven TFT UI.
- ⚠️ The old dual-core split is intentionally collapsed into one process; timing equivalence must be verified under LS2K0300.
- ❌ Nested interrupt semantics from TC264 are not reproduced. Any logic depending on interrupt nesting must be refactored into explicit state progression.

## Goals / Non-Goals

**Goals:**

- Create `new/` as the isolated, buildable home for the migrated LS2K0300 application.
- Preserve reusable perception and control logic from `old/code/` while removing direct dependencies on TC264 headers, macros, and peripherals.
- Replace hardware access with LS2K0300 adapter classes for camera, IMU, encoder, motors, diagnostics, and parameter persistence.
- Define a predictable runtime model for startup, periodic control, foreground diagnostics, cleanup, and fail-safe shutdown.
- Make the migration reviewable and testable before code changes start.

**Non-Goals:**

- Bit-for-bit reproduction of TC264 timing, interrupt nesting, or dual-core execution.
- Porting `old/libraries/` into the target tree as a second driver stack.
- Preserving the original TFT menu UX, on-chip flash layout, or wireless assistant protocol in phase 1.
- Replacing or redesigning the LS2K0300 base library drivers.

## Decisions

### Decision 1: Use a copy-derived `new/` workspace instead of editing `project/` in place

Problem being solved:
The port needs isolation from vendor library updates and enough structure to stage C-to-C++ migration without polluting `LS2K0300_Library/.../project/` during early iterations, but it also needs to remain compatible with the current `build_all.sh` recursion over `user/build.sh`.

Stack equivalent:
The concrete construct is a copy-derived workspace at `new/` whose initial `user/`, `model/`, and `out/` scaffolding is copied from `project/`, while migration-owned sources and support files live under `new/code/port/`, `new/code/platform/`, `new/code/legacy/`, `new/code/runtime/`, `new/config/`, and `new/docs/`.

Alternatives considered:

- Option A: Edit `LS2K0300_Library/.../project/user` and `project/code` directly.
  - Strength: fastest path to a first build.
  - Weakness: mixes migration churn with vendor project files and makes rollback noisy.
  - Migration cost: low up front, higher once modules need reorganizing.
  - Verification impact: hard to separate platform drift from porting mistakes.
- Option B: Create `new/` as a dedicated workspace, copying the `project/` build skeleton and then adapting it to reference the LS2K0300 libraries through include and link paths.
  - Strength: clean ownership, easier traceability, easier staged migration.
  - Weakness: requires one more CMake layer and explicit dependency wiring.
  - Migration cost: moderate up front, lower for repeated iterations.
  - Verification impact: clearer diff boundary and simpler acceptance checks.

Chosen approach:
Option B. `new/` becomes the source-of-truth workspace for the migrated app. It is intentionally seeded from `project/` so that `build_all.sh` can discover `new/user/build.sh`, while the migrated code still stays isolated from the stock `project/` tree.

Named deliverables:

- `new/user/build.sh`
- `new/user/run_remote_smoke.sh`
- `new/user/CMakeLists.txt`
- `new/user/main.cpp`
- `new/code/port/`
- `new/code/platform/`
- `new/code/legacy/`
- `new/code/runtime/`
- `new/model/`
- `new/out/`
- `new/config/default_params.json`
- `new/docs/mapping.md`

Failure semantics:
If `new/` cannot build against the LS2K0300 toolchain without modifying vendor internals, the migration is blocked and must be repaired at the CMake boundary before algorithm code is moved.

Verification hook:
Codex local review checks that `new/` owns all new migration files and that no migrated source includes TC264-only headers. Implementation verification compiles the `new/` target with the LS2K0300 toolchain.

### Decision 2: Split the port into legacy logic modules and platform adapters

Problem being solved:
The old code mixes algorithm decisions with direct device access such as `pwm_set_duty`, `encoder_get_count`, `mt9v03x_*`, `mpu6050_*`, and flash APIs. Direct reuse would hard-wire TC264 assumptions into the new environment.

Stack equivalent:
The concrete construct is an adapter contract layer in `new/code/port/*.hpp` with LS2K0300-backed implementations in `new/code/platform/*.cpp`, while reused logic lives in `new/code/legacy/*.cpp` and runtime orchestration lives in `new/code/runtime/*.cpp`.

Alternatives considered:

- Option A: Full rewrite to idiomatic LS2K0300 C++ classes with no legacy compatibility layer.
  - Strength: cleaner final architecture.
  - Weakness: high behavior drift risk because every algorithm path is rewritten at once.
  - Migration cost: high.
  - Verification impact: hard to separate intended change from accidental regression.
- Option B: Compatibility-heavy port that emulates TC264 global functions and globals almost verbatim.
  - Strength: lowest initial code churn.
  - Weakness: keeps fragile globals, blurs ownership, and makes later cleanup expensive.
  - Migration cost: moderate.
  - Verification impact: faster initial bring-up but poor long-term maintainability.
- Option C: Preserve core logic but route all hardware interactions through explicit adapters and state structs.
  - Strength: controlled reuse with clear boundaries.
  - Weakness: requires extracting interfaces before copying modules.
  - Migration cost: moderate and predictable.
  - Verification impact: adapter mocks and contract checks become possible.

Chosen approach:
Option C. Legacy logic is preserved where it is computation-heavy and hardware-agnostic; every direct peripheral access is replaced with an adapter call or an input/output state object.

Port contract rule:

- `new/code/port/*.hpp` is part of the decoupling boundary, not an escape hatch.
- Public port headers SHALL expose only project-owned structs, enums, constants, and standard-library-friendly scalar containers.
- Public port headers SHALL NOT include or surface `zf_common_headfile.hpp`, `zf_driver_*`, `zf_device_*`, OpenCV headers, `cv::*` symbols, or LS2K0300 driver/device classes in public signatures.
- Translation from LS2K0300 or OpenCV objects into project-owned contract types happens only inside `new/code/platform/*.cpp`.
- `new/user/main.cpp` is the composition root. It MAY perform narrow concrete bootstrap or adapter-owner instantiation, but after wiring startup/runtime/shutdown ownership it SHALL interact with the rest of the application through project-owned contracts and SHALL NOT become a second public leak path for LS2K0300 or OpenCV concrete types.

Boundary examples:

- Caller: `new/code/runtime/control_loop.cpp`
  - Payload in: `ControlInputs{gray_frame, perception_result, imu_sample, encoder_left_delta, encoder_right_delta, params}`
  - Forbidden leak: no `P21_2`, `ATOM0_CH2_P21_4`, `mt9v03x_image`, TC264 ISR macros, `zf_common_headfile.hpp`, direct `zf_driver_*` / `zf_device_*` headers, or `cv::*` types cross into `new/code/legacy/` or public `new/code/port/*.hpp`
  - Returned form: `ControlOutputs{left_pwm, right_pwm, servo_pwm, emergency_stop, debug_metrics}`
- Caller: `new/code/platform/motor_adapter.cpp`
  - Payload in: normalized or saturated duty commands from legacy logic
  - Forbidden leak: legacy code cannot open `/dev/zf_pwm_*` or `/dev/zf_gpio_*` directly
  - Returned form: success/failure plus last applied actuator state
- Caller: `new/code/platform/camera_adapter.cpp`
  - Payload in: LS2K0300 UVC/OpenCV-owned frame objects
  - Forbidden leak: `cv::Mat`, `cv::VideoCapture`, and LS2K camera handles stop at platform implementation files and are converted into project-owned `LegacyCameraFrame` and `PerceptionFrameTicket`
  - Returned form: project-owned frame buffer plus freshness and geometry markers

Named deliverables:

- `new/code/port/control_types.hpp`
- `new/code/port/platform_adapter.hpp`
- `new/code/port/diagnostics.hpp`
- `new/code/platform/camera_adapter.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/param_store.cpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/legacy/fuzzy_pid_ucas.cpp`
- `new/code/legacy/motor_logic.cpp`
- `new/code/legacy/attitude_logic.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/runtime/control_loop.cpp`

Failure semantics:
If a migrated legacy file still requires direct TC264 register macros or driver calls after adapter extraction, that file is rejected from phase 1 until the dependency is replaced or the scope is reduced explicitly.
If a public `new/code/port/*.hpp` contract leaks LS2K0300 driver/device types or OpenCV-owned types, that contract is invalid and must be repaired before dependent legacy code is accepted.

Verification hook:
Local review checks include scans for forbidden headers and symbols from `old/libraries/`, `zf_common_headfile.hpp`, direct `zf_driver_*` / `zf_device_*` includes inside `new/code/legacy/`, LS2K/OpenCV type leakage in public `new/code/port/*.hpp`, and whether `new/user/main.cpp` stays a narrow composition root instead of becoming a second contract surface. Later implementation review verifies that legacy modules only depend on `new/code/port/*.hpp` and standard/library math headers, that public port headers remain project-owned, and that `main.cpp` only wires concrete platform owners before handing off to project-owned startup/runtime/shutdown contracts.

### Decision 2B: Represent hardware sameness or mismatch as an explicit adaptation profile

Problem being solved:
The source and target hardware may be identical, partially compatible, or materially different. If the port assumes identical peripherals, actuator ranges, and signal polarity, the migrated control path can compile while silently behaving incorrectly on the target board.

Stack equivalent:
The concrete construct is a hardware-profile contract declared in `new/code/port/hardware_profile.hpp`, configured by `new/config/hardware_profile.json`, and surfaced in runtime state plus diagnostics as an adaptation matrix plus named extension hooks.

Alternatives considered:

- Option A: Assume source and target hardware are identical unless proven otherwise.
  - Strength: minimal upfront work.
  - Weakness: unsafe default for a hardware-facing port.
  - Migration cost: low initially, high when mismatches surface late.
  - Verification impact: weak, because missing assumptions are not reviewable.
- Option B: Make every adapter infer behavior ad hoc at runtime with no shared profile.
  - Strength: flexible for individual drivers.
  - Weakness: fragmented rules and inconsistent failure behavior.
  - Migration cost: moderate.
  - Verification impact: hard to audit because compatibility logic is scattered.
- Option C: Use one explicit hardware adaptation profile that can describe direct-match cases and named adaptation-extension cases.
  - Strength: centralized assumptions, auditable behavior, and explicit implementation markers.
  - Weakness: requires profile definition work even for same-hardware deployments.
  - Migration cost: moderate and controlled.
  - Verification impact: strong, because reviewers can inspect one declared compatibility boundary.

Chosen approach:
Option C. The migration SHALL define one hardware profile that records whether each critical subsystem is direct-match or requires a named adaptation hook for the active target deployment. Adapters SHALL consume that profile and either pass values through or route behavior through explicit adaptation markers/interfaces that make the gap visible in code and diagnostics.

Current vs target:

- Current assumption risk: legacy code implicitly binds to one known TC264 hardware bundle.
- Target rule: the migrated runtime only trusts hardware behavior that is declared by profile and surfaced through adapters and named extension hooks.

Named deliverables:

- `new/code/port/hardware_profile.hpp`
- `new/config/hardware_profile.json`
- `new/docs/hardware-matrix.md`
- `new/code/port/diagnostics.hpp`

Failure semantics:

- If a subsystem requires non-trivial adaptation, the active hardware profile SHALL point to a named adapter hook or marker instead of silently falling back.
- If runtime-detected device traits contradict the configured hardware profile, the adapter SHALL emit a diagnosable marker and bind to the named extension path instead of auto-correcting silently.
- Hidden rejection or undocumented degradation for hardware mismatch is forbidden; the code must expose the gap through types, markers, or TODO-owned interfaces.

Verification hook:
Implementation verification must record the active hardware profile, the adaptation state of camera/IMU/encoder/actuator subsystems, and any invoked extension markers in runtime logs.

### Decision 2A: Freeze a legacy-compatible camera frame contract at the adapter boundary

Problem being solved:
The legacy vision path assumes MT9V03X grayscale buffers sized `160x128`, while the LS2K0300 UVC driver defaults to `160x120`. Leaving the height mismatch unspecified would make the adapter contract incomplete and let image geometry drift silently.

Stack equivalent:
The concrete construct is a `camera_adapter` contract in `new/code/platform/camera_adapter.cpp` that accepts only `160x120` UVC frames from `zf_device_uvc`, converts them to grayscale if needed, and exposes a row-major legacy buffer `uint8_t legacy_gray_160x128[128][160]` to migrated logic.

Alternatives considered:

- Option A: Resample `160x120` to `160x128`.
  - Strength: every output row is filled with sensor-derived data.
  - Weakness: changes every row index and can shift thresholding and edge-search behavior throughout the image.
  - Migration cost: moderate.
  - Verification impact: hard to compare legacy row assumptions.
- Option B: Center-pad or symmetrically pad the `160x120` image into `160x128`.
  - Strength: simple implementation.
  - Weakness: moves the bottom-of-frame geometry that the legacy control path uses most heavily.
  - Migration cost: low.
  - Verification impact: weak preservation of near-field row semantics.
- Option C: Bottom-anchor the `160x120` image into a `160x128` legacy buffer and synthesize only the top eight rows.
  - Strength: preserves bottom-row semantics and keeps `middle = 80` / row-index logic stable where control decisions are most sensitive.
  - Weakness: top rows are synthetic rather than sensor-native.
  - Migration cost: low.
  - Verification impact: deterministic and easy to assert.

Chosen approach:
Option C. In phase 1, `camera_adapter` SHALL accept `160x120` input only. It SHALL copy source rows `0..119` into destination rows `8..127`, preserve all 160 columns, and fill destination rows `0..7` by duplicating source row `0`. The exposed legacy buffer therefore stays `160x128` grayscale and bottom-aligned.

Boundary examples:

- Caller: `new/code/platform/camera_adapter.cpp`
  - Payload in: UVC frame with width `160`, height `120`, and MJPG/BGR-derived grayscale data
  - Forbidden leak: legacy modules do not inspect `cv::Mat`, `VideoCapture`, or UVC-specific enums
  - Returned form: `LegacyCameraFrame{width=160, height=128, stride=160, gray=legacy_gray_160x128}`
- Caller: `new/code/runtime/control_loop.cpp`
  - Payload in: camera adapter status plus the legacy-compatible frame
  - Forbidden leak: control logic cannot branch on raw UVC dimensions
  - Returned form: either a valid frame for the legacy path or an explicit camera-adaptation marker/interface result that identifies the missing geometry path

Named deliverables:

- `new/code/port/control_types.hpp`
- `new/code/platform/camera_adapter.cpp`
- `new/code/port/diagnostics.hpp`
- `new/docs/debugging.md`

Failure semantics:

- If the runtime camera frame is not exactly `160x120`, the adapter SHALL bind to a named geometry-extension marker/interface and emit a diagnosable message instead of silently cropping, silently resampling, or burying the mismatch in local logic.
- If grayscale conversion fails or the frame buffer is empty, the adapter SHALL return an explicit camera-input marker instead of a partially filled legacy buffer.
- Silent cropping, silent resampling, and undocumented dimension auto-acceptance are forbidden in phase 1.

Verification hook:
Implementation verification must exercise one accepted `160x120 -> 160x128` adaptation path and one non-`160x120` geometry-marker path, and it must write both outcomes to runtime verification logs.

### Decision 3: Replace ISR-driven control with a single periodic control-loop service

Problem being solved:
`old/user/isr.c` depends on MCU interrupts, priority configuration, and nested interrupt behavior, but the real runtime chain also depends on `old/user/cpu0_main.c` publishing fresh perception results and emergency-veto state before the ISR consumes them. LS2K0300 exposes a timer-thread abstraction (`zf_driver_pit`) rather than raw PIT ISR handlers.

Stack equivalent:
The concrete construct is `zf_driver_pit` driving `new/code/runtime/control_loop.cpp`, with `new/user/main.cpp` plus `new/code/runtime/perception_frontend.cpp` owning frame-ready perception work, emergency-veto publication, and foreground diagnostics.

Alternatives considered:

- Option A: Busy-loop control in `main()` using `system_delay_ms`.
  - Strength: trivial to implement.
  - Weakness: timing jitter, poor separation of periodic and foreground work.
  - Migration cost: low.
  - Verification impact: hard to claim control cadence.
- Option B: `zf_driver_pit` periodic callback with bounded work and shared runtime state.
  - Strength: closest stack equivalent to the existing periodic control behavior.
  - Weakness: requires careful state sharing and cleanup.
  - Migration cost: moderate.
  - Verification impact: explicit period and callback boundaries are testable.
- Option C: Multi-threaded pipeline with separate capture, perception, and control workers.
  - Strength: future scalability.
  - Weakness: too much concurrency change for phase 1.
  - Migration cost: high.
  - Verification impact: much larger state-space and race risk.

Chosen approach:
Option B. The first migration keeps one control loop thread at a fixed period, while frame-ready camera perception and emergency-veto publication run in the foreground or a narrowly scoped helper path instead of being silently moved into the PIT callback.

Current vs target:

- Current: `cpu0_main.c` waits on `mt9v03x_finish_flag`, runs `otsuThreshold_fast`, `eight_neighbor`, and threshold/low-voltage `Emergency_Stop` updates, then `isr.c` consumes the shared perception and veto state on PIT cadence.
- Target: a foreground or helper-owned perception stage consumes frame-ready events, publishes `PerceptionResult` plus freshness and emergency-veto markers into runtime state, and `zf_driver_pit` consumes only the published result on periodic cadence.

Named deliverables:

- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/startup.cpp`
- `new/code/runtime/shutdown.cpp`
- `new/code/port/control_types.hpp`

Failure semantics:

- If camera, IMU, encoder, or motor adapters fail during startup, the process SHALL enter a fail-safe mode with motor outputs disabled and a non-zero exit path.
- If the foreground perception path has not published a fresh frame result, or publishes an emergency-veto marker, the periodic loop SHALL veto actuator updates for that cycle and keep or force a safe output.
- If low-voltage monitoring or threshold-based image-health checks trigger the retained emergency path, that veto SHALL be visible in runtime state and diagnostics before actuator writes are attempted.
- Heavy frame processing such as thresholding and `eight_neighbor`-equivalent work SHALL NOT be silently moved into the PIT callback in phase 1.
- If the timer service cannot start, no foreground loop may arm actuators.
- Legacy carry-over values that previously relied on ISR execution order or free globals, such as `W_Target_last` and `bcount` in `old/user/isr.c` plus cross-module control state like `circle_find`, `zebra_flag`, and `cross_flag`, SHALL move into explicitly initialized members of `new/code/runtime/runtime_state.hpp` or typed adapter-owned state before first control-cycle use.

Verification hook:
Implementation verification confirms that startup failures disable motor writes, `zf_driver_pit` is initialized before control activation, foreground perception publishes freshness and veto markers before control consumption, carry-over control state is explicitly initialized before first use, and cleanup stops the timer before releasing actuator resources.

### Decision 4: Replace flash/menu tuning with file-backed parameters and explicit diagnostics

Problem being solved:
`old/code/key.c` assumes onboard keys, TFT screens, and flash pages. The target environment is Linux-based and already has file-oriented facilities, but the initial migration still needs tunable parameters and observability.

Stack equivalent:
The concrete construct is a structured JSON parameter file in `new/config/default_params.json`, loaded by `new/code/platform/param_store.cpp` into named runtime fields, plus stdout/log and optional frame overlay diagnostics. The phase-1 schema SHALL explicitly inventory `Speed_base`, `JWJC`, `circle_k`, `circle_b`, `road_k`, `road_b`, `see_max`, `PID_TURN_CAMERA.D`, `PID_TURN_GYRO_CAMERA.D`, `Straight_permit`, `island_point`, `island_delay`, `circle_k_err`, `P_Mode`, and `exp_light`, with any field renames documented in `new/docs/mapping.md`.

Alternatives considered:

- Option A: Rebuild the original key + TFT + flash UX immediately.
  - Strength: closest operator experience.
  - Weakness: large platform-specific surface unrelated to core migration.
  - Migration cost: high.
  - Verification impact: adds UI and persistence complexity before basic control is stable.
- Option B: Replace flash/menu with config files and structured debug output first.
  - Strength: minimal dependency surface and easy diff-based verification.
  - Weakness: loses on-device interactive tuning in phase 1.
  - Migration cost: moderate.
  - Verification impact: parameter load/save is straightforward to test.

Chosen approach:
Option B. Parameter persistence and diagnostics are simplified for phase 1; interactive tuning can be proposed later as a separate change if needed. Startup-critical values such as `P_Mode` and `exp_light` are treated as bring-up inputs, not late optional tuning.

Named deliverables:

- `new/config/default_params.json`
- `new/config/hardware_profile.json`
- `new/code/platform/param_store.cpp`
- `new/code/port/diagnostics.hpp`
- `new/docs/mapping.md`
- `new/docs/debugging.md`
- `new/docs/hardware-matrix.md`

Failure semantics:
- If parameter files are missing or malformed, the application SHALL either load documented defaults or refuse to arm actuators with a clear error. Silent fallback without logs is forbidden.
- If startup-critical fields such as `P_Mode` or `exp_light` cannot be applied before fuzzy-table initialization or camera-exposure setup, startup SHALL stop before actuator arming.
- Treating the phase-1 parameter file as an unstructured key bag is forbidden; missing required named fields SHALL trigger a logged default-or-stop path.

Verification hook:
Local review checks the presence of a documented default parameter source, explicit field inventory, and failure behavior. Implementation verification tests valid-load, malformed-file, and missing-file cases and records whether `P_Mode` and `exp_light` were applied before control enable.

### Decision 4A: Separate build/upload from board-side runtime smoke

Problem being solved:
The vendor `new/user/build.sh` workflow builds and uploads the artifact but does not execute it on the board or retrieve runtime logs. Treating that step alone as runtime smoke would leave the verification contract non-executable.

Stack equivalent:
The concrete construct is a two-step smoke path: `new/user/build.sh` performs clean build and artifact upload, and `new/user/run_remote_smoke.sh` uploads the required config files when needed, launches the uploaded `new` binary over SSH on the board, captures stdout/stderr to a remote log, and copies that log back to `openspec/changes/port-old-to-new-ls2k0300-library/verification/runtime-smoke.log`.

Alternatives considered:

- Option A: Treat successful `build.sh` completion as runtime smoke.
  - Strength: simplest command surface.
  - Weakness: proves only compilation and upload, not target execution or diagnostics.
  - Migration cost: low.
  - Verification impact: insufficient for this `STRICT` change.
- Option B: Leave board execution as an undocumented manual operator step.
  - Strength: flexible for bring-up.
  - Weakness: not reviewable, repeatable, or checkable by verification tasks.
  - Migration cost: low initially, high in repeated use.
  - Verification impact: weak because log provenance is undefined.
- Option C: Keep vendor build/upload intact and add a scripted board-side launch plus log-retrieval helper.
  - Strength: executable, reviewable, and aligned with the real vendor entry.
  - Weakness: requires one extra helper script and remote-path contract.
  - Migration cost: moderate.
  - Verification impact: strong because smoke evidence becomes reproducible.

Chosen approach:
Option C. `build.sh` remains the vendor-aligned build-and-upload entry point, and `run_remote_smoke.sh` becomes the mandatory board-side execution and log-retrieval helper for runtime verification.

Named deliverables:

- `new/user/build.sh`
- `new/user/run_remote_smoke.sh`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/runtime-smoke.log`
- `new/docs/debugging.md`

Failure semantics:

- If upload succeeds but remote launch does not happen, runtime smoke is incomplete and blocked.
- If remote launch starts but log retrieval fails, the smoke step is blocked because evidence is missing.
- If the target exits early because of parameter, hardware-profile, or adapter failure, `run_remote_smoke.sh` SHALL still retrieve the log so the failure is diagnosable.

Verification hook:
Implementation verification runs `new/user/build.sh` first and `new/user/run_remote_smoke.sh` second, then reviews the retrieved local `runtime-smoke.log` for startup, hardware profile, camera adaptation, and fail-safe evidence.

## Independent Verification Plan (STANDARD/STRICT)

Document verification as a two-stage flow:
- Stage 1: `Codex` local review over the concrete artifact/code set
- Stage 2: `Gemini CLI` independent verification using the Codex review output as input context

The local Codex review is mandatory. Gemini does not replace it.

### Artifact Verification

- Local reviewer: `Codex`
- Local review scope: `proposal.md`, `design.md`, `tasks.md`, `specs/**/*.md`, the reference inventory, the alignment mapping, and verification-path completeness for this `STRICT` change.
- Local review output: concrete findings or explicit `no findings` written to `openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-codex-review.md`
- Verifier source: `Gemini CLI` via repo-local Linux helper `./openspec/bin/gemini_capture.sh`
- Command: `./openspec/bin/gemini_capture.sh --prompt "<review openspec/changes/port-old-to-new-ls2k0300-library/proposal.md, openspec/changes/port-old-to-new-ls2k0300-library/design.md, openspec/changes/port-old-to-new-ls2k0300-library/tasks.md, openspec/changes/port-old-to-new-ls2k0300-library/specs/**/*.md, and openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-codex-review.md for completeness, correctness, coherence, missing migration guardrails, and spec-design-task alignment>" --raw-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-raw.json" --report-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-report.json" --max-attempts 2 --require-json-response`
- Prompt inputs:
  - `openspec/changes/port-old-to-new-ls2k0300-library/proposal.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/design.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/tasks.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/specs/**/*.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-codex-review.md`
- Output format: `gemini envelope json + normalized json`
- Raw report path: `openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-raw.json`
- Report path: `openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report is missing, rerun ./openspec/bin/gemini_capture.sh --input-raw-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-raw.json" --report-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/artifact-gemini-report.json" --require-json-response before blocking`
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Local reviewer: `Codex`
- Local review scope: all changed files under `new/`, any touched files under `LS2K0300_Library/...`, the final `tasks.md`, `runtime-smoke.log`, `parameter-failure.log`, `hardware-profile.log`, and behavioral alignment against the accepted specs.
- Local review output: concrete findings or explicit `no findings` written to `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-codex-review.md`
- Verifier source: `Gemini CLI` via repo-local Linux helper `./openspec/bin/gemini_capture.sh`
- Command: `./openspec/bin/gemini_capture.sh --prompt "<review the implementation for openspec/changes/port-old-to-new-ls2k0300-library using openspec/changes/port-old-to-new-ls2k0300-library/design.md, openspec/changes/port-old-to-new-ls2k0300-library/specs/**/*.md, openspec/changes/port-old-to-new-ls2k0300-library/tasks.md, openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-codex-review.md, openspec/changes/port-old-to-new-ls2k0300-library/verification/runtime-smoke.log, openspec/changes/port-old-to-new-ls2k0300-library/verification/parameter-failure.log, and openspec/changes/port-old-to-new-ls2k0300-library/verification/hardware-profile.log; focus on build integration, adapter boundaries, hardware-profile markers, camera-contract enforcement, parameter-failure handling, fail-safe behavior, and deferred scope correctness>" --raw-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-raw.json" --report-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-report.json" --max-attempts 2 --require-json-response`
- Prompt inputs:
  - `openspec/changes/port-old-to-new-ls2k0300-library/design.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/specs/**/*.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/tasks.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-codex-review.md`
  - `openspec/changes/port-old-to-new-ls2k0300-library/verification/runtime-smoke.log`
  - `openspec/changes/port-old-to-new-ls2k0300-library/verification/parameter-failure.log`
  - `openspec/changes/port-old-to-new-ls2k0300-library/verification/hardware-profile.log`
  - all changed code paths under `new/` and touched LS2K0300 project files
- Output format: `gemini envelope json + normalized json`
- Raw report path: `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-raw.json`
- Report path: `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-report.json`
- Fallback behavior: `retry once; if raw exists but report is missing, rerun ./openspec/bin/gemini_capture.sh --input-raw-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-raw.json" --report-path "openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-report.json" --require-json-response before blocking`
- Skill entry point: `openspec-verify-change`

## Migration Plan

1. Copy the existing `project/` build skeleton into `new/`, preserving `user/`, `model/`, and `out/` entry structure so the current LS2K0300 build workflow can discover it.
2. Define common runtime types and adapter contracts for frame input, perception publication, IMU samples, encoder deltas, motor commands, diagnostics, and parameters.
3. Define one hardware adaptation profile that declares direct-match and named adaptation-hook subsystem states for the active target hardware.
4. Port initialization and shutdown into LS2K0300 bootstrap code, including fail-safe startup checks.
5. Adapt the copied `new/user/CMakeLists.txt` so it compiles migration-owned files from `new/code/` and its `port/`, `platform/`, `legacy/`, and `runtime/` subfolders while preserving the vendor `new/user/build.sh` entry.
6. Migrate algorithm-heavy modules from `old/code/` into `.../new/code/legacy/`, including the required `FUZZY_PID_UCAS` steering helper, while removing TC264-only includes and replacing direct peripheral calls with adapter-backed inputs/outputs.
7. Implement platform adapters under `.../new/code/platform/`, foreground frame-ready perception publication under `.../new/code/runtime/perception_frontend.cpp`, and the periodic control loop under `.../new/code/runtime/`, then wire foreground startup, diagnostics, cleanup, and explicit runtime-state initialization in `.../new/user/main.cpp`.
8. Replace flash/menu configuration with file-backed parameters, document the reduced phase-1 operator workflow, and lock the explicit schema for startup-critical fields such as `P_Mode` and `exp_light`.
9. Validate build and upload through `new/user/build.sh`, then execute board-side smoke and log retrieval through `new/user/run_remote_smoke.sh`, alongside accepted camera-frame adaptation, non-`160x120` geometry markers, parameter load failure modes, hardware-profile adaptation behavior, sensor acquisition, control-loop cadence, and actuator disable-on-failure before enabling broader feature parity work.

## Open Questions

- Which concrete target hardware combination will be used first, and which subsystems are expected to be direct-match versus requiring explicit adaptation hooks?
- Are the LS2K0300 motor and servo duty ranges numerically equivalent to the TC264 values, or is an explicit scaling table required?
- Does the target hardware expose the same left/right encoder polarity as the TC264 wiring, or must the adapter normalize sign conventions?
- Is any part of the original button/TFT/assistant workflow required in phase 1, or can all tuning move to files and logs?

## Risks / Trade-offs

- Collapsing the old interrupt and dual-core model into one timer-driven process reduces migration complexity but may change timing behavior under load.
- Preserving legacy globals inside migrated logic can speed up porting, but it increases the chance of hidden shared-state bugs unless runtime state ownership is made explicit early.
- Using `new/` as an isolated workspace improves reviewability, but it introduces a second build boundary that must stay aligned with the vendor library tree.
- Replacing flash/menu tuning with config files improves reproducibility but reduces on-device convenience until a later tuning interface exists.
- Camera and actuator semantics are the highest-risk alignment points because they combine numeric assumptions with hardware-dependent behavior.
- Supporting both same-hardware and different-hardware targets increases adapter and verification scope, but it avoids unsafe hidden assumptions.
