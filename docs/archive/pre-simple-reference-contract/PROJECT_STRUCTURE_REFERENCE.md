# Project Structure Reference

This document summarizes the repository layout, the role of each major directory, and the internal layering of the migrated `new/` workspace. It is intended as a long-term orientation document for reading, modifying, and verifying the project.

## 1. Repository Overview

The repository contains four main code domains plus supporting documents:

| Path | Role | Notes |
|---|---|---|
| `old/` | Original TC264 codebase | The historical source system. Bare-metal style, MCU-centric, dual-core / interrupt-driven. |
| `LS2K0300_Library/` | Vendor LS2K0300 library and stock project skeleton | The migration target platform and driver stack. |
| `new/` | Migrated LS2K0300 application workspace | The main working runtime. This is the most important code area now. |
| `openspec/` | Change artifacts, verification logs, and workflow metadata | Design/spec/tasks and Codex/Gemini verification evidence live here. |
| `docx/` | Environment and usage documents | WSL / board setup and repository usage notes. |
| `.codex/` | Local Codex skills and workflow helpers | Tooling support, not product runtime code. |

At a high level:

```text
2K0300/
├── old/                 # Original TC264 project
├── LS2K0300_Library/    # Vendor LS2K0300 library + stock project
├── new/                 # Migrated LS2K0300 workspace
├── openspec/            # Structured change-management artifacts
├── docx/                # Environment/setup reference docs
└── .codex/              # Local Codex workflow/skill support
```

## 2. What `new/` Is

`new/` is the migration-owned workspace. It is not just a copy of the vendor project:

- It keeps the vendor-compatible build entry layout (`user/`, `model/`, `out/`) so existing build discovery still works.
- It introduces migration-owned source layers under `code/`.
- It keeps product configuration in `config/`.
- It keeps human-facing migration and debugging notes in `docs/`.

The main design rule inside `new/` is layered separation:

1. `user/` is the composition root and build/deploy entry.
2. `code/port/` defines project-owned contracts and shared types.
3. `code/platform/` implements those contracts using LS2K0300 drivers and Linux-side files/devices.
4. `code/legacy/` retains or rewrites reusable control/perception logic from the old system.
5. `code/runtime/` orchestrates startup, foreground perception, periodic control, and shutdown.

## 3. `new/` Directory Map

```text
new/
├── build.sh
├── code/
│   ├── legacy/
│   ├── platform/
│   ├── port/
│   ├── runtime/
│   └── 本文件夹作用.txt
├── config/
│   ├── default_params.json
│   └── hardware_profile.json
├── docs/
├── model/
├── out/
└── user/
```

### 3.1 `new/build.sh`

Wrapper entry for building the `new/` workspace from the workspace root.

Role:

- Provides a build entry that can be discovered by higher-level build automation.
- Delegates real work to `new/user/build.sh`.

Use this when:

- You want a workspace-level build entry rather than invoking `user/build.sh` directly.

### 3.2 `new/user/`

This is the entry/build/deploy layer.

| File | Role |
|---|---|
| `new/user/main.cpp` | Runtime composition root. Loads config, creates platform bundle, runs startup, starts control loop, runs foreground perception loop, handles signals, and shuts down cleanly. |
| `new/user/build.sh` | Cross-compiles the `new` executable and uploads it to the board. |
| `new/user/run_remote_smoke.sh` | Smoke-test helper: copies config to the board, executes the uploaded binary remotely, retrieves logs, and falls back to local execution when possible. |
| `new/user/CMakeLists.txt` | Build graph for the migrated application plus selected LS2K vendor libraries. |
| `new/user/cross.cmake` | Toolchain/cross-compile configuration. |
| `new/user/build.sh.bak` | Historical backup script. Not part of the active build path. |

Key point:

- `main.cpp` is intentionally narrow. It wires the program together but does not directly implement hardware logic or algorithm logic.

### 3.3 `new/code/port/`

This is the project-owned contract layer. It is the architectural boundary between the runtime/legacy logic and the platform-specific implementations.

| File | Role |
|---|---|
| `new/code/port/control_types.hpp` | Shared project-owned data structures: camera frames, perception results, IMU samples, encoder deltas, low-voltage samples, actuator commands, runtime parameters. |
| `new/code/port/platform_adapter.hpp` | Pure interface layer for camera, IMU, encoder, motor, timer, power monitor, and parameter store adapters. |
| `new/code/port/hardware_profile.hpp` | Hardware compatibility model: direct-match / adaptation-hook / disabled for each subsystem. |
| `new/code/port/diagnostics.hpp` | Diagnostic event model, stdout sink, and rate-limited diagnostic helper. |

Key point:

- This layer should remain free of LS2K concrete types and OpenCV concrete types in public interfaces.

### 3.4 `new/code/platform/`

This is the LS2K0300-backed implementation layer. It adapts vendor drivers and Linux-side resources into the `port/` interfaces.

| File | Role |
|---|---|
| `new/code/platform/bootstrap.hpp` | Factory declarations for platform adapters and `PlatformBundle` creation. |
| `new/code/platform/bootstrap.cpp` | Creates the runtime platform bundle and provides the timer adapter implementation. |
| `new/code/platform/hardware_profile.cpp` | String conversion helpers for `SubsystemMode`. |
| `new/code/platform/param_store.cpp` | Loads runtime parameters and hardware profile from JSON; applies startup-critical fields; enforces fail-closed profile behavior. |
| `new/code/platform/camera_adapter.cpp` | Wraps LS2K UVC camera access; adapts `160x120` UVC frames into legacy `160x128` grayscale layout; surfaces geometry/hook markers. |
| `new/code/platform/imu_adapter.cpp` | Wraps IMU access and surfaces samples through project-owned `ImuSample`. |
| `new/code/platform/encoder_adapter.cpp` | Wraps quadrature encoders; reads and clears per-cycle deltas. |
| `new/code/platform/motor_adapter.cpp` | Wraps PWM/GPIO motor output; applies fail-safe suppression for invalid or hook paths. |
| `new/code/platform/power_adapter.cpp` | Wraps low-voltage sensing (ADC or file/env fallback) behind `IPowerMonitorAdapter` and emits standardized diagnostics. |

Key responsibilities in this layer:

- Translate LS2K and Linux-side device operations into project-owned contracts.
- Enforce hardware-profile behavior.
- Contain the concrete driver knowledge so the rest of the app stays decoupled.

### 3.5 `new/code/legacy/`

This is the migrated algorithm/control logic layer. It preserves selected logic from the TC264 project, but no longer touches raw hardware directly.

| File | Role |
|---|---|
| `new/code/legacy/fuzzy_pid_ucas.cpp/.hpp` | Retained fuzzy steering table logic. |
| `new/code/legacy/pid_control.cpp/.hpp` | Camera-turn, gyro-turn, and speed PID logic. |
| `new/code/legacy/motor_logic.cpp/.hpp` | Mixes mean PWM and turn output into left/right actuator commands. |
| `new/code/legacy/attitude_logic.cpp/.hpp` | Attitude integration / IMU-derived control helper logic. |
| `new/code/legacy/camera_logic.cpp/.hpp` | Otsu-like thresholding and `eight_neighbor`-equivalent lane error extraction. |

Key point:

- This layer contains “what the car is trying to do,” not “how the board accesses devices.”

### 3.6 `new/code/runtime/`

This is the orchestration layer that binds perception, control, and lifecycle together.

| File | Role |
|---|---|
| `new/code/runtime/runtime_state.hpp` | Shared runtime state and carry-over control variables (`W_Target_last`, `bcount`, flags, perception, sensor snapshots, last command, counters). |
| `new/code/runtime/startup.hpp/.cpp` | Validates the platform bundle, enforces fail-closed profile rules, applies startup-critical params, samples low-voltage through the power adapter, initializes adapters, and marks startup complete. |
| `new/code/runtime/perception_frontend.hpp/.cpp` | Foreground perception path. Re-samples low-voltage state, pulls camera frames, handles empty/geometry/hook fallbacks, and publishes perception results to shared state. |
| `new/code/runtime/control_loop.hpp/.cpp` | Periodic control loop. Runs on the timer callback, consumes perception + sensor inputs, and keeps controller internals frozen whenever veto/fail-safe is active. |
| `new/code/runtime/shutdown.hpp/.cpp` | Stops timer, disables actuators, shuts down adapters, and records final shutdown diagnostics. |

Key point:

- This layer owns lifecycle and sequencing, not the low-level algorithms and not the driver details.

### 3.7 `new/config/`

This is the runtime configuration layer.

| File | Role |
|---|---|
| `new/config/default_params.json` | Runtime control/perception parameters and policy values such as `Speed_base`, `P_Mode`, `exp_light`, `control_period_ms`, and safety thresholds. |
| `new/config/hardware_profile.json` | Declares whether each subsystem is `direct-match`, `adaptation-hook`, or `disabled`. |

Key point:

- `default_params.json` configures behavior.
- `hardware_profile.json` configures compatibility assumptions and allowed device paths.

### 3.8 `new/model/`

Retained model assets copied from the vendor project skeleton.

| File | Role |
|---|---|
| `new/model/loong_cnn_model_simple.h` | Embedded model header retained from the stock project. |
| `new/model/loong_cnn_model_simple.tflite` | TFLite model asset retained from the stock project. |
| `new/model/本文件夹作用.txt` | Vendor note file. |

Current status:

- These assets are retained for workspace completeness, but the current migrated runtime path does not actively use them.

### 3.9 `new/docs/`

This is the local reference/documentation area inside the migrated workspace.

The files fall into three groups:

| Group | Files | Role |
|---|---|---|
| Runtime/operator references | `debugging.md`, `hardware-matrix.md`, `mapping.md` | Explain runtime diagnostics, hardware profile behavior, and old-to-new mapping. |
| Local copies of OpenSpec artifacts | `proposal.md`, `design.md`, `tasks.md`, `workspace_spec.md`, `adapter_spec.md` | Convenient local copies/reference views of the migration plan and spec. |
| Verification outputs | `superseded/review-artifacts/artifact_codex_review.md`, `superseded/review-artifacts/artifact_gemini_raw.json`, `superseded/review-artifacts/artifact_gemini_report.json` | Archived artifact-level verification material kept only for historical reference. |

Practical note:

- For the canonical verification record of the live change, prefer `openspec/changes/port-old-to-new-ls2k0300-library/verification/`.

### 3.10 `new/out/`

Build output directory.

Typical contents:

- generated CMake files
- generated Makefiles
- the built `new` executable
- temporary/object output

Important:

- This is generated output and should generally not be treated as source code.

## 4. `new/` Runtime Flow

The runtime path is easier to understand as a sequence:

1. `new/user/main.cpp`
   - installs signal handlers
   - creates diagnostics
   - loads `hardware_profile.json`
   - loads `default_params.json`
   - creates the `PlatformBundle`

2. `new/code/runtime/startup.cpp`
   - checks the platform bundle is complete
   - validates profile rules (strict direct-match by default; degraded startup requires explicit opt-in)
   - applies startup-critical parameters (`P_Mode`, `exp_light`)
   - samples startup low-voltage state through the power adapter
   - initializes camera / IMU / encoder / motor adapters

3. `new/code/runtime/control_loop.cpp::Start`
   - configures PID state
   - starts the timer through the timer adapter
   - arms or intentionally disarms actuators depending on fail-safe conditions
   - in degraded diagnostics mode, allows `motor=disabled` startup while keeping actuators disarmed

4. Foreground loop in `new/user/main.cpp`
   - repeatedly calls `new/code/runtime/perception_frontend.cpp`
   - this is where heavy frame processing stays

5. `new/code/runtime/perception_frontend.cpp`
   - re-samples low-voltage state each frame
   - pulls a frame from the camera adapter
   - either publishes a fallback veto result or calls `legacy::AnalyzeFrame(...)`
   - updates shared perception state

6. Timer callback into `new/code/runtime/control_loop.cpp::Tick`
   - reads IMU and encoder deltas
   - reads the latest published perception result
   - checks freshness / emergency veto / sensor validity
   - runs legacy PID and motor mix only when veto is clear
   - applies the resulting actuator command through the motor adapter

7. `new/code/runtime/shutdown.cpp`
   - stops the timer
   - disables motors
   - shuts down adapters
   - emits the final shutdown marker

## 5. Data and Control Boundaries in `new/`

### 5.1 Contract Layer (`port/`)

The `port/` layer defines the vocabulary used between the rest of the modules:

- `LegacyCameraFrame`
- `CameraCapture`
- `PerceptionResult`
- `ImuSample`
- `EncoderDelta`
- `LowVoltageSample`
- `ActuatorCommand`
- `RuntimeParameters`
- `HardwareProfile`
- `PlatformBundle`

This keeps the runtime and legacy logic independent from:

- raw LS2K driver classes
- raw Linux device-node details
- OpenCV `cv::*` concrete types in public signatures

### 5.2 Platform Layer (`platform/`)

The `platform/` layer owns:

- concrete device access
- file-backed configuration parsing
- timer implementation details
- translation from vendor/platform specifics into project-owned contracts

### 5.3 Legacy Layer (`legacy/`)

The `legacy/` layer owns:

- perception math
- PID math
- steering/motor mixing
- attitude helpers

It should not:

- open device nodes
- include vendor driver headers in public-facing ways
- decide startup/shutdown sequencing

### 5.4 Runtime Layer (`runtime/`)

The `runtime/` layer owns:

- lifecycle sequencing
- shared state
- perception publication
- control cadence
- fail-safe decision flow

## 6. How `new/` Relates to `old/`

The migration is not a direct directory-by-directory copy. It is a functional remapping:

| Old source | New destination | Meaning |
|---|---|---|
| `old/user/cpu0_main.c` | `new/user/main.cpp` + `new/code/runtime/perception_frontend.cpp` | Foreground loop and frame-ready perception path moved into a Linux-process structure. |
| `old/user/isr.c` | `new/code/runtime/control_loop.cpp` | Periodic ISR logic became a timer-driven callback. |
| `old/code/camera.c` | `new/code/legacy/camera_logic.cpp` | Retained/re-expressed image-processing logic. |
| `old/code/PID.c` | `new/code/legacy/pid_control.cpp` | Retained PID logic. |
| `old/code/Motor.c` | `new/code/legacy/motor_logic.cpp` + `new/code/platform/motor_adapter.cpp` | Logic separated from hardware writes. |
| `old/code/ZiTaiJieSuan.c` | `new/code/legacy/attitude_logic.cpp` + `new/code/platform/imu_adapter.cpp` | Attitude computation separated from raw sensor access. |
| `old/code/key.c` | `new/code/platform/param_store.cpp` + `new/config/default_params.json` | Flash/menu tuning replaced by JSON-backed parameters. |

Scope intentionally deferred or collapsed:

- `old/user/cpu1_main.c`
- `old/code/All_init_core1.c`
- nested interrupt behavior
- full TFT/menu parity
- servo-specific parity beyond safe actuator field preservation

## 7. How `new/` Relates to `LS2K0300_Library/`

The vendor tree is large, but only part of it is directly relevant to the migrated runtime:

```text
LS2K0300_Library/
└── LS2K300_Library/
    └── Seekfree_LS2K0300_Opensource_Library/
        ├── libraries/
        │   ├── zf_common/
        │   ├── zf_device/
        │   ├── zf_driver/
        │   └── zf_components/
        └── project/
            ├── user/
            ├── code/
            ├── model/
            └── out/
```

What `new/` uses directly:

- `libraries/zf_common`
- `libraries/zf_device`
- `libraries/zf_driver`

What `new/` takes structurally from the stock project:

- the idea of a `project`-style `user/` + `model/` + `out/` workspace

What `new/` does not use as active business logic:

- stock `project/code/`
- stock `project/user/main.cpp` behavior beyond being a structural reference
- the giant `buildroot-*` and `linux-*` trees

## 8. Role of `old/`

`old/` is the source-of-truth reference for the original TC264 implementation.

Important subareas:

| Path | Role |
|---|---|
| `old/user/` | Main loops, interrupt handlers, ISR configuration |
| `old/code/` | Camera, PID, motor, attitude, init, key/menu, fuzzy logic |
| `old/libraries/` | Original TC264-side support libraries |

Use `old/` when:

- you need to understand original algorithm intent
- you need to trace what behavior was retained vs replaced
- you need to compare runtime semantics during migration work

Do not use `old/` as the active target runtime.

## 9. Role of `openspec/`

`openspec/` is the structured change-management and verification area.

Relevant parts:

| Path | Role |
|---|---|
| `openspec/changes/port-old-to-new-ls2k0300-library/` | Canonical change folder for this migration |
| `openspec/changes/port-old-to-new-ls2k0300-library/design.md` | Design rationale and architectural decisions |
| `openspec/changes/port-old-to-new-ls2k0300-library/specs/` | Requirement deltas for the migration |
| `openspec/changes/port-old-to-new-ls2k0300-library/tasks.md` | Planned/verified implementation task list |
| `openspec/changes/port-old-to-new-ls2k0300-library/verification/` | Codex/Gemini reviews, smoke logs, parameter logs, hardware-profile logs |
| `openspec/bin/` | Workflow helper scripts such as `gemini_capture.sh` |

Use `openspec/` when:

- you need the canonical requirement source
- you need verification evidence
- you need to understand what is supposed to be in or out of scope

## 10. Role of `docx/`

`docx/` is not runtime code. It contains human-facing operational notes:

| File | Role |
|---|---|
| `docx/README.md` | Local documentation index |
| `docx/guide.md` | Usage/process guide |
| `docx/ENV_SETUP_WSL_BOARD.md` | Environment and board setup notes |

## 11. Recommended Reading Order

If you are new to the project, read in this order:

1. `PROJECT_STRUCTURE_REFERENCE.md` (this file)
2. `new/user/main.cpp`
3. `new/code/port/*.hpp`
4. `new/code/runtime/startup.cpp`
5. `new/code/runtime/perception_frontend.cpp`
6. `new/code/runtime/control_loop.cpp`
7. `new/code/platform/*.cpp`
8. `new/code/legacy/*.cpp`
9. `new/config/*.json`
10. `new/docs/mapping.md`
11. `openspec/changes/port-old-to-new-ls2k0300-library/design.md`

## 12. Where To Modify What

| Goal | Primary place to edit |
|---|---|
| Change startup/shutdown sequence | `new/code/runtime/startup.cpp`, `new/code/runtime/shutdown.cpp`, `new/user/main.cpp` |
| Change control cadence or periodic decision flow | `new/code/runtime/control_loop.cpp`, timer portion of `new/code/platform/bootstrap.cpp`, `new/config/default_params.json` |
| Change camera adaptation behavior | `new/code/platform/camera_adapter.cpp`, `new/code/runtime/perception_frontend.cpp`, `new/code/legacy/camera_logic.cpp` |
| Change PID or steering logic | `new/code/legacy/pid_control.cpp`, `new/code/legacy/fuzzy_pid_ucas.cpp` |
| Change motor output behavior | `new/code/legacy/motor_logic.cpp`, `new/code/platform/motor_adapter.cpp` |
| Change hardware compatibility assumptions | `new/config/hardware_profile.json`, `new/code/port/hardware_profile.hpp`, affected adapter in `new/code/platform/` |
| Change parameter schema or parsing | `new/config/default_params.json`, `new/code/platform/param_store.cpp`, `new/code/port/control_types.hpp` |
| Change build/link behavior | `new/user/CMakeLists.txt`, `new/user/build.sh`, `new/user/cross.cmake` |
| Change board smoke workflow | `new/user/run_remote_smoke.sh` |
| Check whether behavior matches migration scope | `openspec/changes/port-old-to-new-ls2k0300-library/` and `new/docs/mapping.md` |

## 13. Generated, Historical, and Non-Primary Areas

These paths exist, but are not primary product source:

| Path | Meaning |
|---|---|
| `new/out/` | Generated build output |
| `new/user/build.sh.bak` | Historical backup script, not active source of truth |
| `new/docs/superseded/review-artifacts/artifact_*.json` and `new/docs/superseded/review-artifacts/artifact_*review.md` | Archived local copies of verification artifacts, useful for traceability but not runtime code |
| `LS2K0300_Library/buildroot-*` | Vendor system/buildroot sources |
| `LS2K0300_Library/linux-*` | Vendor kernel sources |
| `old/Debug/`, `old/.ads/`, `old/.settings/` | Legacy IDE/build artifacts |

## 14. Practical Summary

If you only remember one mental model, use this one:

- `old/` explains where the behavior came from.
- `LS2K0300_Library/` supplies the board/platform stack.
- `new/` is the active migrated application.
- `openspec/` explains what the migration is supposed to do and how it is verified.

Inside `new/`:

- `user/` starts the app,
- `port/` defines the contracts,
- `platform/` talks to the board,
- `legacy/` keeps the migrated logic,
- `runtime/` sequences everything,
- `config/` declares behavior and hardware assumptions.
