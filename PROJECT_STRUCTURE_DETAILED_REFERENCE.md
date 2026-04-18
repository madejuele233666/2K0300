# Project Structure Detailed Reference

This document is the detailed companion to `PROJECT_STRUCTURE_REFERENCE.md`. It is source-oriented and interaction-oriented: the main goal is to answer three questions precisely.

1. What does each part of the repository own.
2. What does each file in `new/` contain.
3. How do functions, interfaces, data structures, and runtime stages interact.

## 1. Repository Top-Level Map

| Path | Role | Practical meaning |
|---|---|---|
| `old/` | Original TC264 project | Historical source system. Bare-metal, dual-core, ISR-driven. Used as reference, not active runtime. |
| `LS2K0300_Library/` | Vendor LS2K0300 library and stock project | Provides the concrete device/driver libraries that `new/` links against. |
| `new/` | Migrated LS2K0300 workspace | Main application workspace. Current runtime and main maintenance target. |
| `openspec/` | Change-management artifacts | Proposal, specs, design, tasks, and verification evidence for the migration. |
| `docx/` | Human-facing setup/reference docs | Environment setup and repository usage notes. |
| `.codex/` | Local automation/skill support | Tooling support, not runtime code. |

## 2. `new/` Layered Architecture

`new/` is intentionally split into layers:

| Layer | Path | Owns | Must not own |
|---|---|---|---|
| Composition/build layer | `new/user/` and `new/build.sh` | process entry, build, deploy, smoke orchestration | algorithm details, raw device logic |
| Contract layer | `new/code/port/` | project-owned data types, abstract interfaces, diagnostics contract, hardware-profile contract | vendor-specific headers and device calls |
| Platform layer | `new/code/platform/` | LS2K device adaptation, JSON-backed parameter store, timer backend | control policy and high-level runtime sequencing |
| Legacy logic layer | `new/code/legacy/` | migrated control/perception logic rewritten against project-owned types | raw hardware access |
| Runtime layer | `new/code/runtime/` | startup, perception scheduling, timer-driven control, shutdown, shared state | vendor-heavy drivers and raw platform implementation details |
| Config layer | `new/config/` | runtime parameters and compatibility profile | code |
| Local docs/assets/output | `new/docs/`, `new/model/`, `new/out/` | reference docs, retained model assets, generated build output | core runtime logic |

### 2.1 Main Runtime Threads And Data Flow

```text
main thread
  main()
    -> load profile + parameters
    -> create adapters
    -> RunStartup()
    -> ControlLoop::Start()
    -> loop: PerceptionFrontend::ProcessOneFrame()
    -> ControlLoop::Stop()
    -> RunShutdown()

timer callback thread or PIT callback
  TimerAdapter
    -> ControlLoop::Tick()
       -> read IMU + encoder
       -> read latest perception from RuntimeState
       -> if veto clear, run PID + motor mix
       -> if veto set, keep controller internals frozen and emit emergency stop
       -> IMotorAdapter::Apply()
```

### 2.2 Decoupling Summary

The intended decoupling boundary is mostly real:

| Boundary | Current state |
|---|---|
| `legacy/` against vendor libraries | Good. `legacy/` depends on `port/` types only. |
| `runtime/` against vendor libraries | Good. low-voltage sensing is now abstracted behind `IPowerMonitorAdapter` and implemented in `platform/power_adapter.cpp`. |
| `port/` against platform details | Good. `port/` is pure contracts and value types. |
| `main.cpp` against concrete platform code | Good. `main.cpp` is composition only. |
| Raw device access confinement | Good. UVC, IMU, encoder, motor, PIT, and low-voltage ADC/file logic all live in `platform/`. |

## 3. `new/` Detailed File Reference

## 3.1 Build And Entry Layer

### `new/build.sh`

Role: thin workspace-level wrapper that delegates the real build to `new/user/build.sh`.

Contains:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `SCRIPT_DIR` | shell variable | resolves current directory | compared against `new` for guard |
| top-level guard | script logic | ensures script is run from inside `new/` | exits on mismatch |
| top-level delegation | script logic | invokes `./user/build.sh` if present | `new/user/build.sh` |

Interaction notes:

- No shell functions are defined.
- This file exists mainly so higher-level automation can discover a workspace-level build entry.

### `new/user/cross.cmake`

Role: toolchain-selection file included by CMake before target definition.

Contains:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `CROSS_COMPILE` | CMake variable | toggles LS2K cross-build vs host build | read immediately by `IF(CROSS_COMPILE)` |
| `CMAKE_SYSTEM_NAME` | CMake variable | declares Linux target system | CMake toolchain logic |
| `CMAKE_SYSTEM_PROCESSOR` | CMake variable | declares `loongson` processor | CMake toolchain logic |
| `TOOLCHAIN_DIR` | CMake variable | stores LoongArch toolchain path | compiler variable setup |
| `CMAKE_CXX_COMPILER` | CMake variable | points to `loongarch64-linux-gnu-g++` | CMake compile stage |
| `CMAKE_C_COMPILER` | CMake variable | points to `loongarch64-linux-gnu-gcc` | CMake compile stage |
| `CMAKE_FIND_ROOT_PATH*` | CMake variables | restrict library/include search to toolchain root | dependency resolution |

Interaction notes:

- Included by `new/user/CMakeLists.txt`.
- This is a build-time interface, not a runtime interface.

### `new/user/CMakeLists.txt`

Role: full build graph for the migrated executable.

Contains:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `include(cross.cmake)` | CMake directive | loads cross-build config | `new/user/cross.cmake` |
| `CURRENT_DIR`, `NEW_DIR`, `REPO_ROOT`, `LS2K_LIB_ROOT`, `PROJ_NAME` | CMake variables | path and project discovery | later source/include configuration |
| `COMMON_FLAGS` | CMake variable | shared compiler flags | C and C++ flag setup |
| `find_package(OpenCV REQUIRED)` | CMake directive | resolves OpenCV dependency | `param_store.cpp` via `cv::FileStorage` |
| `include_directories(...)` | CMake directive | adds user/code/vendor include roots | all compiled translation units |
| `NEW_SRCS` | source list | all hand-written migration sources | `add_executable` |
| `LS2K_COMMON_SRCS` | source list | vendor common helpers | `add_executable` |
| `LS2K_DEVICE_SRCS` | source list | vendor device backends | `CameraAdapter`, `ImuAdapter` |
| `LS2K_DRIVER_SRCS` | source list | vendor drivers | timer, ADC, encoder, PWM/GPIO backends |
| `add_executable(${PROJ_NAME} ...)` | build target | builds the `new` executable | all source lists |
| `set_target_properties(... INSTALL_RPATH ...)` | target property | bakes OpenCV runtime search path | deployed executable |
| `target_link_libraries(... ${OpenCV_LIBS} pthread m)` | link rule | links OpenCV, pthread, libm | final executable |

Interaction notes:

- This file is the build-time bridge from migration-owned code to vendor libraries.
- The concrete vendor source files compiled into `new` are exactly the ones listed here.

### `new/user/build.sh`

Role: active build-and-upload script.

Functions and logic:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `log_info()` | shell function | emits informational log lines | top-level build flow |
| `log_error()` | shell function | emits error log lines to stderr | top-level build flow |
| `WORK_DIR`, `OUT_DIR`, `USER_DIR` | shell variables | derive local paths | `cmake`, `make` |
| `REMOTE_IP`, `REMOTE_USER`, `REMOTE_PATH` | shell variables | define deployment target | `scp` upload |
| `MAKE_JOBS` | shell variable | defines parallel build job count | `make -j` |
| top-level clean step | script logic | deletes previous generated output except `本文件夹作用.txt` | `new/out/` |
| top-level configure step | script logic | runs `cmake "${USER_DIR}"` | `new/user/CMakeLists.txt` |
| top-level compile step | script logic | runs `make -j` | generated Makefiles |
| artifact existence check | script logic | verifies `${OUT_DIR}/new` exists | built executable |
| upload step | script logic | copies artifact to board unless `SKIP_UPLOAD=1` | remote board |

External interface:

- `BOARD_IP`
- `BOARD_USER`
- `BOARD_PATH`
- `MAKE_JOBS`
- `SKIP_UPLOAD`

### `new/user/build.sh.bak`

Role: historical backup of the build script. Not in the active path.

Functions and logic:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `error_exit()` | shell function | colored error output and `exit 1` | entire script |
| `info_echo()` | shell function | colored info output | entire script |
| top-level clean/configure/make/upload sequence | script logic | earlier version of the current build flow | same toolchain/deployment path |

Interaction notes:

- Kept as reference only.
- Current automation should use `new/user/build.sh`.

### `new/user/run_remote_smoke.sh`

Role: runtime smoke helper for verification.

Functions and logic:

| Symbol | Kind | Responsibility | Interacts with |
|---|---|---|---|
| `write_header()` | shell function | initializes verification log with timestamp, board, binary path | `VERIFY_LOG` |
| `run_remote()` | shell function | uploads config, runs board-side smoke, retrieves board log | board via `scp` and `ssh` |
| `run_local()` | shell function | local fallback smoke path when remote execution fails | local `new/out/new` |
| `WORK_DIR`, `REPO_ROOT` | shell variables | path resolution | config/log paths |
| `VERIFY_LOG` | shell variable | output verification log path | `openspec/.../runtime-smoke.log` by default |
| `BOARD_*` variables | shell variables | remote board path/auth settings | remote smoke execution |
| `LOCAL_BIN` | shell variable | local executable path | local fallback |
| `SMOKE_MAX_FRAMES` | shell variable | bounded smoke-loop frame cap | runtime env `LS2K_MAX_FRAMES` |
| top-level dispatch | script logic | chooses remote smoke unless `SMOKE_LOCAL_ONLY=1`; falls back to local on failure | remote/local execution |

External interface:

- `VERIFY_LOG_PATH`
- `BOARD_IP`
- `BOARD_USER`
- `BOARD_PATH`
- `BOARD_BIN`
- `SMOKE_MAX_FRAMES`
- `SMOKE_LOCAL_ONLY`

Interaction notes:

- This script is the main consumer of the runtime’s bounded smoke mode.
- It injects `LS2K_PARAMS_PATH`, `LS2K_PROFILE_PATH`, and `LS2K_MAX_FRAMES`.

### `new/user/main.cpp`

Role: composition root and process lifetime owner.

Functions and globals:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `g_stop_signal` | global flag | async signal bridge from signal handler to main loop | written by `HandleStopSignal`, read by `main()` |
| `ReadIntEnv(const char*, int)` | helper function | parses integer env var with fallback | `main()` for `LS2K_MAX_FRAMES` |
| `ReadStringEnv(const char*, const char*)` | helper function | resolves string env var with fallback | `main()` for config paths |
| `HandleStopSignal(int)` | signal handler | sets `g_stop_signal=1` | registered by `main()` |
| `main()` | process entry | loads config, creates platform bundle, runs startup, starts control loop, runs foreground perception loop, performs shutdown | entire runtime stack |

Detailed `main()` flow:

1. Registers `SIGINT` and `SIGTERM` handlers.
2. Creates `StdoutDiagnostics`, `HardwareProfile`, `RuntimeParameters`, and `RuntimeState`.
3. Creates a parameter store through `platform::MakeParamStore()`.
4. Resolves profile and parameter JSON paths from environment.
5. Loads `hardware_profile.json` first.
6. Rejects non-`direct-match` persistence before loading parameters.
7. Loads `default_params.json`.
8. Creates `PlatformBundle` via `CreatePlatformBundle()`.
9. Replaces `platform.params` with the already-used `param_store`.
10. Logs subsystem profile modes/hooks.
11. Calls `runtime::RunStartup()`.
12. Creates `runtime::ControlLoop` and calls `Start()`.
13. Creates `runtime::PerceptionFrontend`.
14. Runs a foreground loop:
    - stop on signal
    - stop on `LS2K_MAX_FRAMES`
    - otherwise call `PerceptionFrontend::ProcessOneFrame(params)`
15. Calls `control_loop.Stop()`.
16. Calls `RunShutdown()`.
17. Emits `main.exit`.

Environment interface:

- `LS2K_PROFILE_PATH`
- `LS2K_PARAMS_PATH`
- `LS2K_MAX_FRAMES`

Main interactions:

- Configuration owner: `IParamStore`.
- Adapter factory owner: `platform::CreatePlatformBundle`.
- Lifecycle owner: `RunStartup`, `ControlLoop`, `PerceptionFrontend`, `RunShutdown`.

### `new/code/本文件夹作用.txt`

Role: directory note file stating that `new/code/` stores user code.

Contains:

| Item | Meaning |
|---|---|
| plain-text note | marks `new/code/` as the hand-written code area |

## 3.2 Contract Layer: `new/code/port/`

### `new/code/port/control_types.hpp`

Role: project-owned data model shared across legacy logic, runtime orchestration, and platform adapters.

Constants and types:

| Symbol | Kind | Responsibility | Produced by / Consumed by |
|---|---|---|---|
| `kLegacyFrameWidth` | constant | legacy grayscale frame width (`160`) | camera/perception code |
| `kLegacyFrameHeight` | constant | legacy grayscale frame height (`128`) | camera/perception code |
| `kPhase1UvcWidth` | constant | expected phase-1 UVC width (`160`) | camera adapter |
| `kPhase1UvcHeight` | constant | expected phase-1 UVC height (`120`) | camera adapter |
| `CameraGeometryMarker` | enum | camera capture outcome classification | `CameraAdapter::Capture`, `PerceptionFrontend::ProcessOneFrame` |
| `LegacyCameraFrame` | struct | grayscale buffer in legacy processing layout | camera adapter -> `legacy::AnalyzeFrame` |
| `CameraCapture` | struct | camera adapter output envelope | `ICameraAdapter::Capture` -> `PerceptionFrontend` |
| `PerceptionResult` | struct | perception output + veto metadata | `AnalyzeFrame` or fallback path -> `ControlLoop::Tick` |
| `ImuSample` | struct | IMU sample | `IImuAdapter::Read` -> `ControlLoop::Tick`, `LegacyAttitudeLogic` |
| `EncoderDelta` | struct | per-cycle wheel count delta | `IEncoderAdapter::ReadDelta` -> `ControlLoop::Tick` |
| `LowVoltageSample` | struct | runtime low-voltage sample envelope | `IPowerMonitorAdapter::SampleLowVoltage` -> startup/perception |
| `ActuatorCommand` | struct | motor command envelope | `LegacyMotorLogic::Mix` -> `IMotorAdapter::Apply` |
| `RuntimeParameters` | struct | loaded runtime configuration and load-state flags | `ParamStore` -> startup/perception/control/camera |

Important `PerceptionResult` fields:

| Field | Meaning | Main consumer |
|---|---|---|
| `published` | whether any result was published | `ControlLoop::Tick` stale logic |
| `fresh` | whether current publication came from a real frame | `ControlLoop::Tick` stale logic |
| `emergency_veto` | high-level veto bit | `ControlLoop::Tick` |
| `low_voltage_veto` | low-voltage propagated veto | diagnostics and future policy extensions |
| `threshold_veto` | threshold-derived veto | diagnostics and `emergency_veto` composition |
| `geometry_veto` | geometry/capture-path veto | fallback perception path |
| `lateral_error` | lane-center error | `LegacyPidControl::ComputeTurnTarget` |
| `threshold` | frame threshold result | diagnostics/fail-safe logic |
| `highest_line` | nearest detected row | `FuzzyPidUcas::DuoJiGetP` input |
| `perception_tag` | source/tag string | diagnostics/debugging |

Important `RuntimeParameters` fields and current consumers:

| Field | Current consumer |
|---|---|
| `Speed_base` | `LegacyPidControl::ComputeMeanSpeedPwm` |
| `pid_turn_camera_d` | `LegacyPidControl::Configure` |
| `pid_turn_gyro_camera_d` | `LegacyPidControl::Configure` |
| `P_Mode` | `FuzzyPidUcas::InitMH`, startup-critical validation |
| `exp_light` | `CameraAdapter::Initialize`, startup-critical validation |
| `emergency_threshold` | `legacy::AnalyzeFrame` |
| `control_period_ms` | `ControlLoop::Start`, `ControlLoop::Tick` |
| `perception_stale_ms` | `ControlLoop::Tick` |
| `pwm_limit` | `ComputeMeanSpeedPwm`, `LegacyMotorLogic::Mix` |
| `startup_critical_applied`, `loaded_from_defaults`, `parse_failure` | startup and diagnostics |

Loaded but not materially used by current phase-1 control path:

- `JWJC`
- `circle_k`
- `circle_b`
- `road_k`
- `road_b`
- `see_max`
- `Straight_permit`
- `island_point`
- `island_delay`
- `circle_k_err`

### `new/code/port/platform_adapter.hpp`

Role: abstract adapter contract layer.

Interfaces:

| Symbol | Kind | Responsibility | Implemented by / Used by |
|---|---|---|---|
| `ICameraAdapter` | interface | camera initialize/capture/shutdown/readiness contract | implemented in `camera_adapter.cpp`, used by `PerceptionFrontend`, `RunStartup` |
| `IImuAdapter` | interface | IMU initialize/read/shutdown/readiness contract | implemented in `imu_adapter.cpp`, used by `ControlLoop`, `RunStartup` |
| `IEncoderAdapter` | interface | encoder initialize/read-delta/shutdown/readiness contract | implemented in `encoder_adapter.cpp`, used by `ControlLoop`, `RunStartup` |
| `IMotorAdapter` | interface | motor initialize/apply/disable/shutdown/readiness contract | implemented in `motor_adapter.cpp`, used by `ControlLoop`, `RunStartup`, `RunShutdown` |
| `ITimerAdapter` | interface | periodic callback start/stop/running contract | implemented internally in `bootstrap.cpp`, used by `ControlLoop`, `RunShutdown` |
| `IPowerMonitorAdapter` | interface | low-voltage initialization and sampling contract | implemented in `power_adapter.cpp`, used by `RunStartup`, `PerceptionFrontend` |
| `IParamStore` | interface | parameter/profile loading and startup-critical application contract | implemented in `param_store.cpp`, used by `main()`, `RunStartup` |
| `PlatformBundle` | struct | bundles owned adapter instances | built by `CreatePlatformBundle`, passed into runtime |

Interface methods:

| Interface method | Responsibility |
|---|---|
| `ICameraAdapter::Initialize` | bring camera path up using profile and runtime parameters |
| `ICameraAdapter::Capture` | produce one `CameraCapture` |
| `ICameraAdapter::Shutdown` | release camera resources |
| `ICameraAdapter::Ready` | report usable state |
| `IImuAdapter::Initialize` | bring IMU path up |
| `IImuAdapter::Read` | read one IMU sample |
| `IImuAdapter::Shutdown` | release IMU resources |
| `IImuAdapter::Ready` | report usable state |
| `IEncoderAdapter::Initialize` | bring encoder path up |
| `IEncoderAdapter::ReadDelta` | read and clear encoder deltas |
| `IEncoderAdapter::Shutdown` | release encoder resources |
| `IEncoderAdapter::Ready` | report usable state |
| `IMotorAdapter::Initialize` | bring motor path up |
| `IMotorAdapter::Apply` | apply one actuator command |
| `IMotorAdapter::Disable` | hard-stop outputs without full destruction |
| `IMotorAdapter::Shutdown` | release motor resources |
| `IMotorAdapter::Ready` | report usable state |
| `ITimerAdapter::Start` | start periodic callback source |
| `ITimerAdapter::Stop` | stop periodic callback source |
| `ITimerAdapter::Running` | report timer running state |
| `IPowerMonitorAdapter::Initialize` | initialize low-voltage backend |
| `IPowerMonitorAdapter::SampleLowVoltage` | sample low-voltage state as `LowVoltageSample` |
| `IPowerMonitorAdapter::Ready` | report power monitor readiness |
| `IParamStore::LoadRuntimeParameters` | parse runtime params JSON or apply defaults |
| `IParamStore::LoadHardwareProfile` | parse hardware profile JSON in fail-closed mode |
| `IParamStore::ApplyStartupCritical` | validate and mark critical params before adapter bring-up |

### `new/code/port/hardware_profile.hpp`

Role: compatibility-contract model for each subsystem.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `SubsystemMode` | enum | `kDirectMatch`, `kAdaptationHook`, `kDisabled` | all adapters, startup validation, logging |
| `SubsystemProfile` | struct | mode + hook name for one subsystem | `HardwareProfile` |
| `HardwareProfile` | struct | full repository-wide runtime compatibility declaration | loaded by `ParamStore`, consumed by startup/adapters |
| `IsEnabled(const SubsystemProfile&)` | inline helper | returns `mode != kDisabled` | startup/adapters/timer |
| `ToString(SubsystemMode)` | declaration | converts enum to text | logging in `main.cpp` and elsewhere |

Current subsystem members in `HardwareProfile`:

- `camera`
- `imu`
- `encoder`
- `motor`
- `timer`
- `persistence`
- `display`

### `new/code/port/diagnostics.hpp`

Role: thread-safe diagnostics contract and utilities.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `DiagnosticLevel` | enum | log severity classification | all emitters |
| `DiagnosticEvent` | struct | log payload | all emitters |
| `NowMs()` | inline helper | monotonic timestamp in ms | diagnostics across project |
| `DiagnosticSink` | interface | sink contract | implemented by `StdoutDiagnostics` |
| `DiagnosticRateLimiter::ShouldEmit` | method | decides whether a keyed event may be emitted now | `EmitRateLimited` |
| `GlobalDiagnosticRateLimiter()` | inline helper | process-global limiter singleton | `EmitRateLimited` |
| `EmitRateLimited(...)` | inline helper | emits only when rate limit allows | camera/imu/encoder/motor/control paths |
| `StdoutDiagnostics::Emit` | method | thread-safe stdout/stderr log emission | default sink in `main()` |
| `StdoutDiagnostics::Info/Warn/Error/FailSafe` | convenience methods | create and emit `DiagnosticEvent` with severity | `main.cpp` primarily |
| `StdoutDiagnostics::LevelString` | private static method | converts `DiagnosticLevel` to output label text | `StdoutDiagnostics::Emit` |

Interaction notes:

- `StdoutDiagnostics` is thread-safe because both the foreground thread and timer thread emit logs.
- `EmitRateLimited` is essential for hook/fault paths that could otherwise log on every control cycle.

## 3.3 Platform Layer: `new/code/platform/`

### `new/code/platform/bootstrap.hpp`

Role: declarations for platform factories and bundle assembly.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `MakeCameraAdapter()` | factory | returns concrete `ICameraAdapter` | `CreatePlatformBundle` or direct callers |
| `MakeImuAdapter()` | factory | returns concrete `IImuAdapter` | `CreatePlatformBundle` |
| `MakeEncoderAdapter()` | factory | returns concrete `IEncoderAdapter` | `CreatePlatformBundle` |
| `MakeMotorAdapter()` | factory | returns concrete `IMotorAdapter` | `CreatePlatformBundle` |
| `MakePowerMonitorAdapter()` | factory | returns concrete `IPowerMonitorAdapter` | `CreatePlatformBundle` |
| `MakeParamStore()` | factory | returns concrete `IParamStore` | `main.cpp`, `CreatePlatformBundle` |
| `CreatePlatformBundle(...)` | factory | creates complete `PlatformBundle` | `main.cpp` |

### `new/code/platform/bootstrap.cpp`

Role: timer implementation and bundle assembly.

Internal class and functions:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `TimerAdapter` | concrete class | implements `ITimerAdapter` using `zf_driver_pit` when available, otherwise a fallback thread | `CreatePlatformBundle`, `ControlLoop` |
| `TimerAdapter::Start(...)` | method | stops previous timer, validates profile, chooses PIT or fallback thread, stores callback | `ControlLoop::Start` |
| `TimerAdapter::Stop(...)` | method | clears running flag, stops PIT if present, joins fallback thread if present | `ControlLoop::Stop`, `RunShutdown` |
| `TimerAdapter::Running()` | method | reports `running_` | not heavily used in current code |
| `TimerAdapter::PitDispatch()` | static method | static PIT trampoline into active timer instance | PIT driver callback |
| `CreatePlatformBundle(...)` | free function | instantiates all adapters and timer | `main.cpp` |

Important state inside `TimerAdapter`:

| Field | Meaning |
|---|---|
| `running_` | controls loop/thread liveness |
| `period_ms_` | callback period |
| `callback_` | stored control-loop callback |
| `worker_` | fallback thread handle |
| `pit_` | concrete PIT driver instance when available |
| `active_` | static pointer used by `PitDispatch()` |

Interaction notes:

- `CreatePlatformBundle(...)` currently ignores its `profile` and `diagnostics` parameters and always constructs the same adapter set. Profile-specific behavior starts later during adapter initialization.
- This file is the owner of timer backend selection.

### `new/code/platform/hardware_profile.cpp`

Role: text conversion for `SubsystemMode`.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `ToString(SubsystemMode)` | free function | converts profile mode to `direct-match`, `adaptation-hook`, or `disabled` | `main.cpp` and diagnostic messages |

### `new/code/platform/param_store.cpp`

Role: JSON-backed parameter store and hardware-profile loader.

Internal helpers:

| Symbol | Kind | Responsibility | Called by |
|---|---|---|---|
| `ReadText(const std::string&, std::string&)` | helper | loads whole file into string | both load methods |
| `ParseJsonObject(const std::string&, cv::FileStorage&)` | helper | opens in-memory JSON as OpenCV `FileStorage` and checks root is map | both load methods |
| `ReadNumberNode(const cv::FileNode&, double&)` | helper | validates numeric node and extracts value | numeric field readers |
| `ReadRequiredNumber(...)` | helper | reads required top-level numeric field | runtime parameter loader |
| `ReadIntegerValue(const cv::FileNode&, int&)` | helper | validates numeric integer-ness before cast | integer field readers |
| `ReadRequiredInt(...)` | helper | reads required top-level int field | runtime parameter loader |
| `ReadOptionalInt(...)` | helper | reads optional int and sets malformed flag on bad type/value | runtime parameter loader |
| `ReadRequiredNestedNumber(...)` | helper | reads nested numeric field like `PID_TURN_CAMERA.D` | runtime parameter loader |
| `ReadRequiredString(...)` | helper | reads required string field | profile loader |
| `ParseMode(const std::string&, SubsystemMode&)` | helper | parses mode text | profile loader |
| `ParseProfileBlock(...)` | helper | parses one subsystem block containing `mode` and `hook` | profile loader |
| `ProfileBlockError(const char*)` | helper | formats parse error text for a subsystem | profile loader |

Concrete implementation:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `ParamStore` | concrete class | implements `IParamStore` | `MakeParamStore` |
| `ParamStore::LoadRuntimeParameters(...)` | method | loads `RuntimeParameters`, falls back to defaults on missing/parse failure, annotates flags | `main.cpp` |
| `ParamStore::LoadHardwareProfile(...)` | method | loads `HardwareProfile` in fail-closed mode; missing or malformed file is fatal | `main.cpp` |
| `ParamStore::ApplyStartupCritical(...)` | method | validates `P_Mode` and `exp_light`, sets `startup_critical_applied`, emits diagnostics | `RunStartup` |
| `MakeParamStore()` | factory | returns `std::make_unique<ParamStore>()` | `main.cpp`, `CreatePlatformBundle` |

Parameter-loading behavior:

| Situation | Behavior |
|---|---|
| runtime params file missing | return success, use defaults, set `loaded_from_defaults=true`, `parse_failure=true`, emit warning |
| runtime params invalid JSON | return success, use defaults, set flags, emit fail-safe |
| runtime params missing required fields | return success, use defaults, set flags, emit fail-safe |
| hardware profile missing | return failure, emit fail-safe |
| hardware profile invalid | return failure, emit fail-safe |
| hardware profile missing any subsystem block | return failure, emit fail-safe |

Interaction notes:

- `param_store.cpp` is the only place that knows JSON schema for both config files.
- `ApplyStartupCritical(...)` is the gate before actuators may ever be armed.

### `new/code/platform/camera_adapter.cpp`

Role: concrete UVC-backed camera adapter plus phase-1 geometry adaptation.

Concrete symbols:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `CameraAdapter` | concrete class | implements `ICameraAdapter` | `MakeCameraAdapter` |
| `CameraAdapter::Initialize(...)` | method | handles disabled mode, hook mode, or direct UVC initialization; sets exposure | `RunStartup` |
| `CameraAdapter::Capture(...)` | method | captures one frame or reports a marker path; performs `160x120 -> 160x128` copy/adaptation | `PerceptionFrontend::ProcessOneFrame` |
| `CameraAdapter::Shutdown(...)` | method | clears state and releases UVC device | `RunShutdown` |
| `CameraAdapter::Ready()` | method | reports readiness | `RunStartup`, runtime checks |
| `MakeCameraAdapter()` | factory | constructs adapter | `CreatePlatformBundle` |

Important adapter state:

| Field | Meaning |
|---|---|
| `enabled_` | whether camera subsystem is enabled by profile |
| `ready_` | whether capture path is ready |
| `adaptation_hook_` | whether camera is intentionally routed away from direct-match |
| `hook_name_` | selected hook name |
| `frame_id_` | monotonically increasing capture counter |
| `device_` | concrete `zf_device_uvc` object when vendor header is available |

Important capture branches:

| Branch | Result |
|---|---|
| subsystem disabled | marker `kAdapterNotReady`, no frame |
| adaptation hook mode | marker `kAdaptationHookRouted`, no frame, rate-limited log |
| forced geometry override with non-`160x120` | marker `kNonPhase1Geometry`, no frame |
| direct-match but device not ready | marker `kAdapterNotReady`, no frame |
| refresh failed or gray pointer missing | marker `kEmptyFrame`, no frame |
| direct-match success | `has_frame=true`, marker `kPhase1Adapted`, adapted legacy frame |

Phase-1 adaptation contract:

- source rows `0..119` are copied to destination rows `8..127`
- destination rows `0..7` duplicate source row `0`

External interface:

- `LS2K_FORCE_UVC_GEOMETRY`

### `new/code/platform/imu_adapter.cpp`

Role: concrete IMU adapter.

Concrete symbols:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `ImuAdapter` | concrete class | implements `IImuAdapter` | `MakeImuAdapter` |
| `ImuAdapter::Initialize(...)` | method | handles disabled, hook, or direct IMU initialization | `RunStartup` |
| `ImuAdapter::Read(...)` | method | returns `ImuSample`; hook mode reports unavailable sample with rate-limited warning | `ControlLoop::Tick` |
| `ImuAdapter::Shutdown(...)` | method | releases IMU backend | `RunShutdown` |
| `ImuAdapter::Ready()` | method | reports readiness | `RunStartup` |
| `MakeImuAdapter()` | factory | constructs adapter | `CreatePlatformBundle` |

Interaction notes:

- In hook mode, `Read(...)` intentionally returns `valid=false`, which forces `ControlLoop::Tick()` into veto.
- In direct-match mode, this file is the only code that touches raw IMU accessors.

### `new/code/platform/encoder_adapter.cpp`

Role: concrete quadrature encoder adapter.

Concrete symbols:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `EncoderAdapter` | concrete class | implements `IEncoderAdapter` | `MakeEncoderAdapter` |
| `EncoderAdapter::Initialize(...)` | method | handles disabled, hook, or direct encoder initialization | `RunStartup` |
| `EncoderAdapter::ReadDelta(...)` | method | reads left/right counts, clears hardware counters, or emits hook warning | `ControlLoop::Tick` |
| `EncoderAdapter::Shutdown(...)` | method | releases encoder objects | `RunShutdown` |
| `EncoderAdapter::Ready()` | method | reports readiness | `RunStartup` |
| `MakeEncoderAdapter()` | factory | constructs adapter | `CreatePlatformBundle` |

Interaction notes:

- Direct-match mode creates separate left/right encoder driver objects.
- `ReadDelta(...)` is intentionally destructive with respect to hardware counters because it clears after reading.

### `new/code/platform/motor_adapter.cpp`

Role: concrete PWM/GPIO motor adapter and output suppression gate.

Concrete symbols:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `MotorAdapter` | concrete class | implements `IMotorAdapter` | `MakeMotorAdapter` |
| `MotorAdapter::Initialize(...)` | method | handles disabled, hook, or direct motor backend initialization | `RunStartup` |
| `MotorAdapter::Apply(...)` | method | applies command, disables on emergency stop, suppresses output in hook/unready states | `ControlLoop::Tick` |
| `MotorAdapter::Disable(...)` | method | writes zero duty without full destruction | `Apply()` on emergency stop, `RunShutdown` |
| `MotorAdapter::Shutdown(...)` | method | disables outputs and releases backend objects | `RunShutdown` |
| `MotorAdapter::Ready()` | method | reports readiness | `RunStartup`, `ControlLoop::Start` |
| `MotorAdapter::WriteOne(...)` | private static helper | converts signed duty to direction GPIO + absolute PWM | `Apply()` |
| `MakeMotorAdapter()` | factory | constructs adapter | `CreatePlatformBundle` |

Important `Apply(...)` branches:

| Branch | Behavior |
|---|---|
| adapter disabled or unready | rate-limited fail-safe log, return `false` |
| `command.emergency_stop == true` | call `Disable()`, return `true` |
| adaptation hook mode | rate-limited fail-safe log, suppress output, return `false` |
| direct-match valid path | write left/right PWM and direction, return `true` |

Interaction notes:

- This is the final actuator gate. Even if upstream logic computed a non-zero command, hook/unready/emergency branches prevent actual motion.
- `servo_pwm` exists in `ActuatorCommand` but is currently ignored here.

### `new/code/platform/power_adapter.cpp`

Role: concrete low-voltage monitor adapter (`IPowerMonitorAdapter`).

Concrete symbols:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `PowerMonitorAdapter` | concrete class | implements low-voltage backend initialization and sampling | `MakePowerMonitorAdapter` |
| `PowerMonitorAdapter::Initialize(...)` | method | prepares backend and emits backend marker diagnostics | `RunStartup` |
| `PowerMonitorAdapter::SampleLowVoltage(...)` | method | resolves force/env/raw path, samples voltage, returns `LowVoltageSample` | `RunStartup`, `PerceptionFrontend` |
| `PowerMonitorAdapter::Ready()` | method | reports initialized readiness | runtime checks |
| `MakePowerMonitorAdapter()` | factory | constructs adapter | `CreatePlatformBundle` |

Interaction notes:

- Primary path uses vendor ADC when available; fallback path uses `LS2K_LOW_VOLTAGE_RAW_PATH`.
- If sampling is unavailable, adapter reports `valid=false` and runtime fails closed unless degraded startup is explicitly enabled.

## 3.4 Legacy Logic Layer: `new/code/legacy/`

### `new/code/legacy/fuzzy_pid_ucas.hpp`

Role: declaration of the retained fuzzy steering gain table logic.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `FuzzyPidUcas` | class | owns left/right `P` tables and computes steering proportional gain | `LegacyPidControl` |
| `FuzzyPidUcas::InitMH(int)` | method | selects one of the predefined `P_Mode` tables | `LegacyPidControl::Configure` |
| `FuzzyPidUcas::DuoJiGetP(int, int)` | method | computes blended proportional gain from view height and error | `LegacyPidControl::ComputeTurnTarget` |

### `new/code/legacy/fuzzy_pid_ucas.cpp`

Role: implementation of fuzzy steering gain lookup.

Internal helpers and methods:

| Symbol | Kind | Responsibility | Called by |
|---|---|---|---|
| `kDuojiMap` | lookup table | coarse fuzzy region-to-index mapping | `DuoJiGetP` |
| `Clamp(float, float, float)` | helper | clamps scalar into range | `ApproxIndex`, `ApproxError`, `DuoJiGetP` |
| `ApproxIndex(float, float)` | helper | maps height-like input to `[0,3]` index | `DuoJiGetP` |
| `ApproxError(float, float)` | helper | maps absolute error to `[0,3]` index | `DuoJiGetP` |
| `FuzzyPidUcas::InitMH(int)` | method | assigns table set for modes 1..4; defaults to mode 3 table | `LegacyPidControl::Configure` |
| `FuzzyPidUcas::DuoJiGetP(int, int)` | method | performs bilinear-style blend over coarse map, then interpolates inside left/right `P` tables | `LegacyPidControl::ComputeTurnTarget` |

Interaction notes:

- Sign of `view_e` selects left or right gain table.
- `P_Mode` is the only runtime knob that changes the gain-table family.

### `new/code/legacy/pid_control.hpp`

Role: declaration of the migrated control law bundle.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `LegacyPidControl` | class | camera-turn, gyro-turn, and mean-speed PID owner | `ControlLoop` |
| `Configure(const RuntimeParameters&)` | method | updates camera/gyro gains and fuzzy table selection | `ControlLoop::Start` |
| `ComputeTurnTarget(const PerceptionResult&, float&)` | method | computes smoothed turn target from perception error | `ControlLoop::Tick` |
| `ComputeGyroTurn(float, float)` | method | computes turn output from target vs measured gyro | `ControlLoop::Tick` |
| `ComputeMeanSpeedPwm(double, double, int)` | method | computes mean traction PWM from target vs measured speed | `ControlLoop::Tick` |

Persistent internal state:

| Field | Meaning |
|---|---|
| `fuzzy_` | fuzzy gain helper |
| `cam_d_` | camera derivative gain |
| `gyro_p_`, `gyro_i_`, `gyro_d_` | gyro PID gains |
| `speed_p_`, `speed_i_`, `speed_d_` | speed PID gains |
| `camera_error_last_` | previous perception error |
| `gyro_error_last_`, `gyro_i_count_` | gyro PID memory |
| `speed_error_last_`, `speed_i_count_` | speed PID memory |

### `new/code/legacy/pid_control.cpp`

Role: implementation of the control laws.

Methods:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `LegacyPidControl::Configure(...)` | method | copies selected config values into internal gains and initializes fuzzy tables | `ControlLoop::Start` |
| `LegacyPidControl::ComputeTurnTarget(...)` | method | uses `lateral_error`, `highest_line`, fuzzy `P`, camera `D`, and `W_Target_last` smoothing | `ControlLoop::Tick` |
| `LegacyPidControl::ComputeGyroTurn(...)` | method | computes gyro-domain turn correction and clamps to `[-9000,9000]` | `ControlLoop::Tick` |
| `LegacyPidControl::ComputeMeanSpeedPwm(...)` | method | computes speed-domain PWM and clamps to `[-pwm_limit,pwm_limit]` | `ControlLoop::Tick` |

Important interaction detail:

- `ComputeTurnTarget(...)` explicitly updates `RuntimeState::W_Target_last` through the reference parameter. This preserves legacy smoothing behavior without making `LegacyPidControl` itself depend on `RuntimeState`.

### `new/code/legacy/motor_logic.hpp`

Role: declaration of left/right motor mixing logic.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `LegacyMotorLogic` | class | mixes mean PWM and turn output into left/right command | `ControlLoop` |
| `Mix(int, float, bool, int)` | method | returns `ActuatorCommand` or emergency-stop default | `ControlLoop::Tick` |

### `new/code/legacy/motor_logic.cpp`

Role: implementation of left/right command mixing.

Methods:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `LegacyMotorLogic::Mix(...)` | method | if vetoed, returns default `ActuatorCommand{}`; otherwise computes left/right PWM, clamps by limit, leaves `servo_pwm=0` | `ControlLoop::Tick` |

Interaction notes:

- Default-constructed `ActuatorCommand` has `emergency_stop=true`, so veto here directly propagates into `MotorAdapter::Apply(...) -> Disable(...)`.

### `new/code/legacy/attitude_logic.hpp`

Role: declaration of retained attitude integration helper.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `LegacyAttitudeLogic` | class | integrates yaw estimate from IMU gyro-z | `ControlLoop` |
| `UpdateFromImu(const ImuSample&, float)` | method | updates yaw when sample is valid | `ControlLoop::Tick` |
| `yaw_deg() const` | method | returns integrated yaw estimate | currently not consumed elsewhere |

### `new/code/legacy/attitude_logic.cpp`

Role: implementation of yaw integration.

Methods:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `LegacyAttitudeLogic::UpdateFromImu(...)` | method | if IMU sample valid, integrates `gyro_z * dt_sec * 57.324841` into `yaw_deg_` | `ControlLoop::Tick` |

Interaction notes:

- `yaw_deg_` is maintained but not currently fed back into any later control stage.

### `new/code/legacy/camera_logic.hpp`

Role: declaration of frame-analysis entry point.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `AnalyzeFrame(...)` | free function | computes `PerceptionResult` from one `LegacyCameraFrame` | `PerceptionFrontend::ProcessOneFrame` |

### `new/code/legacy/camera_logic.cpp`

Role: implementation of thresholding and lane-error extraction.

Internal helpers and functions:

| Symbol | Kind | Responsibility | Called by |
|---|---|---|---|
| `OtsuThresholdFast(const LegacyCameraFrame&)` | helper | computes downsampled Otsu-style threshold | `AnalyzeFrame` |
| `EightNeighborEquivalentError(...)` | helper | extracts weighted lane center and returns lateral error; also returns highest valid row | `AnalyzeFrame` |
| `AnalyzeFrame(...)` | free function | fills `PerceptionResult`, computes threshold and error, applies threshold/low-voltage veto logic, tags algorithm path | `PerceptionFrontend::ProcessOneFrame` |

`AnalyzeFrame(...)` output behavior:

| Output field | Value source |
|---|---|
| `published` | always `true` |
| `fresh` | always `true` for real frame path |
| `frame_id`, `capture_time_ms` | copied from camera capture envelope |
| `perception_tag` | fixed to `otsu+eight-neighbor-equivalent` |
| `threshold` | `OtsuThresholdFast(...)` |
| `lateral_error`, `highest_line` | `EightNeighborEquivalentError(...)` |
| `low_voltage_veto` | copied from runtime startup result |
| `threshold_veto` | `threshold <= emergency_threshold` or `threshold >= 220` |
| `geometry_veto` | always `false` on real frame path |
| `emergency_veto` | `low_voltage_veto || threshold_veto` |

## 3.5 Runtime Layer: `new/code/runtime/`

### `new/code/runtime/runtime_state.hpp`

Role: shared cross-stage state owned by the runtime.

Fields:

| Field | Responsibility | Written by | Read by |
|---|---|---|---|
| `W_Target_last` | retained smoothed turn target memory | `ControlLoop::Tick` via `LegacyPidControl::ComputeTurnTarget` | same control path |
| `bcount` | retained control-side activity counter | `ControlLoop::Tick` | same control path, potentially future diagnostics |
| `circle_find`, `zebra_flag`, `cross_flag` | retained legacy flags | currently not actively updated in phase 1 | reserved for future migration steps |
| `shared_mutex` | protects shared sensor/perception/command channels | perception and control threads | perception and control threads |
| `perception` | latest perception publication | `PerceptionFrontend` | `ControlLoop::Tick` |
| `imu` | latest IMU sample snapshot | `ControlLoop::Tick` | diagnostics/future observers |
| `encoder` | latest encoder snapshot | `ControlLoop::Tick` | diagnostics/future observers |
| `last_command` | last applied or zeroed command | `ControlLoop::Tick` | diagnostics/shutdown observers |
| `startup_complete` | startup success flag | `RunStartup` | `ControlLoop::Start` |
| `timer_started` | timer running flag | `ControlLoop::Start`, `ControlLoop::Stop`, `RunShutdown` | main/shutdown/observers |
| `actuators_armed` | whether actuators are effectively armed | `ControlLoop::Start`, `ControlLoop::Tick`, `RunShutdown` | observers |
| `stop_requested` | foreground loop stop request | `main()` | `main()` loop condition |
| `low_voltage_emergency` | latest low-voltage emergency state (startup + runtime resample) | `RunStartup`, `PerceptionFrontend` | `PerceptionFrontend`, `ControlLoop::Start` |
| `control_cycle_count` | control-cycle counter | `ControlLoop::Tick` | observers |
| `perception_publish_count` | perception-publication counter | `PerceptionFrontend` | observers |

### `new/code/runtime/startup.hpp`

Role: declaration of startup orchestration entry point.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `RunStartup(...)` | free function | validates configuration and adapter readiness, applies critical parameters, detects low voltage | `main.cpp` |

### `new/code/runtime/startup.cpp`

Role: implementation of startup sequencing, strict/degraded startup gating, and low-voltage initialization through the power adapter.

Internal helpers:

| Symbol | Kind | Responsibility | Called by |
|---|---|---|---|
| `ReadBoolEnv(const char*)` | helper | env bool reader returning `optional<bool>` | `RunStartup` |
| `RequireReady(...)` | helper | enforces adapter readiness, with explicit degraded-mode exception for disabled profiles | `RunStartup` |
| `RequireDirectMatch(...)` | helper | requires direct-match profile unless degraded startup is explicitly enabled | `ValidateProfileContracts` |
| `ValidateProfileContracts(...)` | helper | enforces phase-1 fail-closed profile policy for critical subsystems | `RunStartup` |
| `ApplyStartupLowVoltage(...)` | helper | initializes power adapter and captures startup low-voltage sample | `RunStartup` |
| `RunStartup(...)` | free function | full startup sequence | `main.cpp` |

Detailed `RunStartup(...)` flow:

1. Verifies every adapter pointer exists in `PlatformBundle`.
2. Reads `LS2K_ALLOW_DEGRADED_STARTUP` and emits degraded-mode marker when enabled.
3. Validates profile contracts:
   - persistence must be enabled
   - persistence must not be `adaptation-hook`
   - timer must be enabled
   - camera/imu/encoder/motor must be `direct-match` unless degraded startup is explicitly enabled
4. Calls `platform.params->ApplyStartupCritical(params, diagnostics)`.
5. Refuses startup if startup-critical application failed.
6. Initializes power monitor and samples startup low-voltage state.
7. Initializes camera, IMU, encoder, and motor in that order.
8. After each initialize, requires `Ready()` (or degraded-mode allowance for explicitly disabled subsystem).
9. Initializes runtime-state lifecycle fields.
10. Emits `startup.complete`.

External interface:

- `LS2K_FORCE_LOW_VOLTAGE`
- `LS2K_LOW_VOLTAGE_RAW_THRESHOLD`
- `LS2K_LOW_VOLTAGE_RAW_PATH`
- `LS2K_ALLOW_DEGRADED_STARTUP`

### `new/code/runtime/perception_frontend.hpp`

Role: declaration of foreground perception worker.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `PerceptionFrontend` | class | owns camera + power references and publishes `PerceptionResult` into shared state | `main.cpp` |
| constructor | method | stores adapter/state/diagnostics references | `main.cpp` |
| `RefreshLowVoltageState()` | method | samples low voltage and updates runtime emergency state with transition diagnostics | `ProcessOneFrame` |
| `ProcessOneFrame(const RuntimeParameters&)` | method | refreshes low voltage, captures frame, and publishes either analyzed perception or fail-safe fallback | main foreground loop |

### `new/code/runtime/perception_frontend.cpp`

Role: implementation of foreground camera processing.

Methods:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `PerceptionFrontend::PerceptionFrontend(...)` | constructor | reference wiring only | `main.cpp` |
| `PerceptionFrontend::ProcessOneFrame(...)` | method | pulls `CameraCapture`, converts no-frame markers into fallback `PerceptionResult`, or forwards real frames into `legacy::AnalyzeFrame`, then publishes result under `shared_mutex` | main loop |

Fallback publication tags:

| Capture marker | Published `perception_tag` |
|---|---|
| `kEmptyFrame` | `camera-empty` |
| `kAdapterNotReady` | `camera-not-ready` |
| `kNonPhase1Geometry` | `camera-bad-geometry` |
| `kAdaptationHookRouted` | `camera-hook-routed` |
| default/unknown | `camera-marker-unknown` |

Fallback publication behavior:

- `published=true`
- `fresh=false`
- `geometry_veto=true`
- `emergency_veto=true`

Main interaction:

- This file is the only writer of `RuntimeState::perception`.

### `new/code/runtime/control_loop.hpp`

Role: declaration of periodic control worker.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `ControlLoop` | class | owns timer-driven control sequence | `main.cpp` |
| constructor | method | stores references to platform/profile/state/diagnostics | `main.cpp` |
| `Start(const RuntimeParameters&)` | method | configures PID, starts timer, decides initial arming state | `main.cpp` |
| `Stop()` | method | stops timer and running flag | `main.cpp` |
| `Tick()` | private method | one periodic control cycle | timer callback |

Owned helper modules:

| Field | Meaning |
|---|---|
| `pid_` | `LegacyPidControl` owner |
| `motor_logic_` | `LegacyMotorLogic` owner |
| `attitude_` | `LegacyAttitudeLogic` owner |

### `new/code/runtime/control_loop.cpp`

Role: implementation of periodic control sequencing.

Methods:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `ControlLoop::ControlLoop(...)` | constructor | reference wiring only | `main.cpp` |
| `ControlLoop::Start(...)` | method | verifies startup and motor readiness (with degraded diagnostics exception for disabled motor profile), stores params, configures PID, starts timer callback, decides whether actuators may be armed | `main.cpp` |
| `ControlLoop::Stop()` | method | clears running flag and stops timer backend | `main.cpp` |
| `ControlLoop::Tick()` | method | reads sensors, checks stale/veto conditions, freezes controller state on veto, otherwise computes turn/speed commands and applies actuator output | timer callback |

Detailed `Tick()` sequence:

1. Return immediately if not `running_`.
2. Read IMU via `platform_.imu->Read(...)`.
3. Read encoder delta via `platform_.encoder->ReadDelta(...)`.
4. Copy latest perception under `state_.shared_mutex`.
5. Compute `stale` from publication flags and `perception_stale_ms`.
6. Compute `veto` if stale, perception emergency veto, invalid IMU, or invalid encoder.
7. Emit rate-limited `control.veto` when vetoed.
8. If `veto=true`, skip PID/target/integrator progression and keep `ActuatorCommand{}` (emergency stop).
9. If `veto=false`, update attitude and compute turn/speed command path.
10. Call `platform_.motor->Apply(command, diagnostics_)`.
11. Update `actuators_armed`.
12. Under lock, store `last_command` and increment `bcount` if non-emergency command applied.
13. Increment `control_cycle_count`.

Initial arming logic in `Start(...)`:

| Condition | Effect |
|---|---|
| startup incomplete | refuse start |
| motor adapter not ready | refuse start, except degraded diagnostics mode with `motor=disabled` |
| timer start failure | refuse start |
| low-voltage emergency active | start timer but keep `actuators_armed=false` |
| motor profile is `adaptation-hook` | start timer but keep `actuators_armed=false` |
| motor profile is `disabled` | start timer in diagnostics mode and keep `actuators_armed=false` |

Important interaction notes:

- `Tick()` no longer advances PID/controller internal state while `veto=true`; this prevents recovery spikes after long veto windows.
- `RuntimeState::last_command` becomes zero/default when motor apply fails.

### `new/code/runtime/shutdown.hpp`

Role: declaration of shutdown entry point.

Symbols:

| Symbol | Kind | Responsibility | Used by |
|---|---|---|---|
| `RunShutdown(...)` | free function | stops timer, disables actuators, shuts down adapters | `main.cpp` |

### `new/code/runtime/shutdown.cpp`

Role: implementation of shutdown sequencing.

Functions:

| Symbol | Kind | Responsibility | Called by / Uses |
|---|---|---|---|
| `RunShutdown(...)` | free function | stops timer, disables motor, shuts down camera/imu/encoder/motor, updates state flags, emits `shutdown.complete` | `main.cpp` |

Interaction notes:

- `RunShutdown(...)` is safe to call after failed startup because it checks each bundle pointer before use.

## 3.6 Configuration Layer: `new/config/`

### `new/config/default_params.json`

Role: runtime parameter file loaded by `ParamStore::LoadRuntimeParameters(...)`.

Current fields:

| Key | Current value | Main consumer |
|---|---|---|
| `Speed_base` | `77.0` | speed PWM control |
| `JWJC` | `1.0` | loaded, not materially used in phase 1 |
| `circle_k` | `1.1` | loaded, not materially used in phase 1 |
| `circle_b` | `35.0` | loaded, not materially used in phase 1 |
| `road_k` | `1.15` | loaded, not materially used in phase 1 |
| `road_b` | `55.0` | loaded, not materially used in phase 1 |
| `see_max` | `35.0` | loaded, not materially used in phase 1 |
| `PID_TURN_CAMERA.D` | `5.0` | `LegacyPidControl::Configure` |
| `PID_TURN_GYRO_CAMERA.D` | `9.0` | `LegacyPidControl::Configure` |
| `Straight_permit` | `1.0` | loaded, not materially used in phase 1 |
| `island_point` | `25.0` | loaded, not materially used in phase 1 |
| `island_delay` | `500.0` | loaded, not materially used in phase 1 |
| `circle_k_err` | `0.0` | loaded, not materially used in phase 1 |
| `P_Mode` | `3` | fuzzy gain mode and startup-critical validation |
| `exp_light` | `65` | camera exposure and startup-critical validation |
| `emergency_threshold` | `40` | perception threshold veto |
| `control_period_ms` | `5` | control timer period |
| `perception_stale_ms` | `40` | stale-perception cutoff |
| `pwm_limit` | `9000` | clamp for speed and motor mix |

### `new/config/hardware_profile.json`

Role: compatibility profile loaded by `ParamStore::LoadHardwareProfile(...)`.

Current subsystem declarations:

| Subsystem | Mode | Hook | Main consumers |
|---|---|---|---|
| `camera` | `direct-match` | `phase1-160x120-to-160x128` | `CameraAdapter`, startup diagnostics |
| `imu` | `direct-match` | `imu660ra-default` | `ImuAdapter` |
| `encoder` | `direct-match` | `quadrature-default` | `EncoderAdapter` |
| `motor` | `direct-match` | `pwm-gpio-default` | `MotorAdapter`, `ControlLoop::Start` |
| `timer` | `direct-match` | `zf_driver_pit` | `TimerAdapter`, startup validation |
| `persistence` | `direct-match` | `json-file-store` | `main.cpp`, startup validation, `ParamStore` |
| `display` | `disabled` | `phase1-deferred` | currently unused runtime-wise |

Interaction notes:

- `persistence` is special: `main.cpp` and `RunStartup()` both reject unsupported persistence modes before runtime continues.
- `display` is explicitly present even though phase-1 runtime does not use it.

## 3.7 Local Documentation Layer: `new/docs/`

These files are reference material, not runtime inputs.

| File | Role |
|---|---|
| `new/docs/superseded/race-finish-series-source/mapping.md` | archived old-to-new mapping of retained contracts and deferred scope |
| `new/docs/superseded/race-finish-series-source/hardware-matrix.md` | archived subsystem-mode and fail-closed profile contract reference |
| `new/docs/superseded/race-finish-series-source/debugging.md` | archived runtime diagnostics, smoke procedure, marker meanings, low-voltage test knobs |
| `new/docs/superseded/race-finish-series-source/design.md` | archived local copy of change design artifact |
| `new/docs/superseded/race-finish-series-source/proposal.md` | archived local copy of change proposal |
| `new/docs/superseded/race-finish-series-source/tasks.md` | archived local copy of change task list |
| `new/docs/superseded/race-finish-series-source/workspace_spec.md` | archived local copy of workspace-related spec |
| `new/docs/superseded/race-finish-series-source/adapter_spec.md` | archived local copy of adapter-layer spec |
| `new/docs/superseded/review-artifacts/artifact_codex_review.md` | archived artifact-verification review copy |
| `new/docs/superseded/review-artifacts/artifact_gemini_raw.json` | archived raw machine verification output |
| `new/docs/superseded/review-artifacts/artifact_gemini_report.json` | archived summarized machine verification report |

Most important operator-facing docs:

| File | What it helps answer |
|---|---|
| `mapping.md` | which old modules were retained, deferred, or collapsed |
| `hardware-matrix.md` | what each hardware-profile mode means |
| `debugging.md` | what logs to expect and how smoke testing works |

## 3.8 Retained Model Assets: `new/model/`

| File | Role | Runtime status |
|---|---|---|
| `new/model/loong_cnn_model_simple.h` | retained vendor model header | not actively used by current migrated runtime |
| `new/model/loong_cnn_model_simple.tflite` | retained TFLite model asset | not actively used by current migrated runtime |
| `new/model/本文件夹作用.txt` | explanatory note for this directory | documentation only |

## 3.9 Generated Output: `new/out/`

This subtree is generated build output. It is useful for build/debug operations, but it does not contain project-authored runtime interfaces.

Key root files:

| File | Meaning |
|---|---|
| `new/out/new` | built executable |
| `new/out/cmake_install.cmake` | generated install script |
| `new/out/CMakeCache.txt` | CMake cache |
| `new/out/Makefile` | generated top-level build rules |
| `new/out/本文件夹作用.txt` | explanatory note |

Generated metadata files:

- `new/out/CMakeFiles/Makefile2`
- `new/out/CMakeFiles/Makefile.cmake`
- `new/out/CMakeFiles/CMakeDirectoryInformation.cmake`
- `new/out/CMakeFiles/cmake.check_cache`
- `new/out/CMakeFiles/TargetDirectories.txt`
- `new/out/CMakeFiles/progress.marks`
- `new/out/CMakeFiles/CMakeConfigureLog.yaml`
- `new/out/CMakeFiles/new.dir/cmake_clean.cmake`
- `new/out/CMakeFiles/new.dir/compiler_depend.make`
- `new/out/CMakeFiles/new.dir/progress.make`
- `new/out/CMakeFiles/new.dir/link.txt`
- `new/out/CMakeFiles/new.dir/DependInfo.cmake`
- `new/out/CMakeFiles/new.dir/flags.make`
- `new/out/CMakeFiles/new.dir/depend.make`
- `new/out/CMakeFiles/new.dir/build.make`
- `new/out/CMakeFiles/new.dir/compiler_depend.ts`
- `new/out/CMakeFiles/3.28.3/CMakeDetermineCompilerABI_C.bin`
- `new/out/CMakeFiles/3.28.3/CMakeCXXCompiler.cmake`
- `new/out/CMakeFiles/3.28.3/CMakeCCompiler.cmake`
- `new/out/CMakeFiles/3.28.3/CMakeSystem.cmake`
- `new/out/CMakeFiles/3.28.3/CMakeDetermineCompilerABI_CXX.bin`
- `new/out/CMakeFiles/3.28.3/CompilerIdC/CMakeCCompilerId.c`
- `new/out/CMakeFiles/3.28.3/CompilerIdC/a.out`
- `new/out/CMakeFiles/3.28.3/CompilerIdCXX/a.out`
- `new/out/CMakeFiles/3.28.3/CompilerIdCXX/CMakeCXXCompilerId.cpp`

Generated object and dependency files for hand-written migration code:

- `new/out/CMakeFiles/new.dir/main.cpp.o`
- `new/out/CMakeFiles/new.dir/main.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/shutdown.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/shutdown.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/control_loop.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/control_loop.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/perception_frontend.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/perception_frontend.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/startup.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/runtime/startup.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/camera_adapter.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/camera_adapter.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/encoder_adapter.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/encoder_adapter.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/bootstrap.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/bootstrap.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/imu_adapter.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/imu_adapter.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/motor_adapter.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/motor_adapter.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/power_adapter.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/power_adapter.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/hardware_profile.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/hardware_profile.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/param_store.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/platform/param_store.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/pid_control.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/pid_control.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/camera_logic.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/camera_logic.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/motor_logic.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/motor_logic.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/fuzzy_pid_ucas.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/fuzzy_pid_ucas.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/attitude_logic.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/new/code/legacy/attitude_logic.cpp.o.d`

Generated object and dependency files for vendor sources linked into `new`:

- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_function.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_function.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_font.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_font.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_fifo.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common/zf_common_fifo.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_adc.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_adc.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_gpio.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_gpio.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_file_string.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_file_string.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit_fd.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit_fd.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pit.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_encoder.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pwm.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_pwm.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_delay.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_delay.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_file_buffer.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_driver/zf_driver_file_buffer.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_uvc.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_uvc.cpp.o.d`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu.cpp.o`
- `new/out/CMakeFiles/new.dir/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_device/zf_device_imu.cpp.o.d`

## 4. Cross-File Interaction Maps

### 4.1 Configuration And Startup Chain

```text
main()
  -> MakeParamStore()
  -> LoadHardwareProfile("hardware_profile.json")
  -> LoadRuntimeParameters("default_params.json")
  -> CreatePlatformBundle()
  -> RunStartup()
       -> ValidateProfileContracts()
       -> ApplyStartupCritical()
       -> power.Initialize()
       -> power.SampleLowVoltage()
       -> camera.Initialize()
       -> imu.Initialize()
       -> encoder.Initialize()
       -> motor.Initialize()
```

Critical interactions:

| Producer | Consumer | Data / effect |
|---|---|---|
| `hardware_profile.json` | `ParamStore::LoadHardwareProfile` | `HardwareProfile` |
| `default_params.json` | `ParamStore::LoadRuntimeParameters` | `RuntimeParameters` |
| `ApplyStartupCritical()` | `RunStartup()` | `startup_critical_applied` gate |
| `IPowerMonitorAdapter::SampleLowVoltage()` | `RuntimeState` | startup low-voltage state and fail-safe gating |
| adapter `Ready()` | `RequireReady()` | startup pass/fail decision |

### 4.2 Perception Chain

```text
main loop
  -> PerceptionFrontend::ProcessOneFrame(params)
      -> power.SampleLowVoltage()
      -> ICameraAdapter::Capture()
      -> if frame:
           legacy::AnalyzeFrame(...)
         else:
           build fallback PerceptionResult
      -> publish RuntimeState::perception
```

Critical interactions:

| Producer | Consumer | Data / effect |
|---|---|---|
| `IPowerMonitorAdapter::SampleLowVoltage()` | `PerceptionFrontend` | current low-voltage emergency state |
| `CameraAdapter::Capture()` | `PerceptionFrontend` | `CameraCapture` with marker/frame |
| `legacy::AnalyzeFrame(...)` | `RuntimeState::perception` | `PerceptionResult` |
| `RuntimeState::low_voltage_emergency` | `AnalyzeFrame(...)` | propagated veto bit |
| `CameraGeometryMarker` | fallback publication | `perception_tag` and emergency/geometry veto |

### 4.3 Control Chain

```text
ControlLoop::Start()
  -> pid_.Configure(params)
  -> timer.Start(profile.timer, period, Tick)

Tick()
  -> imu.Read()
  -> encoder.ReadDelta()
  -> read RuntimeState::perception
  -> if veto: emergency-stop command (no PID progression)
  -> else:
       -> attitude_.UpdateFromImu()
       -> pid_.ComputeTurnTarget()
       -> pid_.ComputeGyroTurn()
       -> pid_.ComputeMeanSpeedPwm()
       -> motor_logic_.Mix()
  -> motor.Apply()
```

Critical interactions:

| Producer | Consumer | Data / effect |
|---|---|---|
| `PerceptionFrontend` | `ControlLoop::Tick()` | latest perception |
| `ImuAdapter::Read()` | `LegacyAttitudeLogic`, `LegacyPidControl::ComputeGyroTurn` | gyro sample |
| `EncoderAdapter::ReadDelta()` | `ComputeMeanSpeedPwm` | measured speed proxy |
| `LegacyPidControl::ComputeTurnTarget(...)` | `RuntimeState::W_Target_last` | smoothed target update |
| `LegacyMotorLogic::Mix(...)` | `MotorAdapter::Apply(...)` | final command or emergency stop |

### 4.4 Shutdown Chain

```text
main()
  -> ControlLoop::Stop()
      -> timer.Stop()
  -> RunShutdown()
      -> timer.Stop()
      -> motor.Disable()
      -> camera.Shutdown()
      -> imu.Shutdown()
      -> encoder.Shutdown()
      -> motor.Shutdown()
```

### 4.5 Build And Smoke Chain

```text
new/build.sh
  -> new/user/build.sh
      -> cmake
      -> make
      -> scp new/out/new

new/user/run_remote_smoke.sh
  -> scp config files
  -> ssh remote runtime with LS2K_MAX_FRAMES
  -> scp remote log back
  -> fallback to local execution if needed
```

## 5. Repository Structure Outside `new/`

## 5.1 `old/`

Role: original TC264 project retained as migration reference.

Main structure:

| Path | Role |
|---|---|
| `old/code/` | original application modules |
| `old/user/` | original process/core entry and ISR files |
| `old/libraries/` | TC264-side libraries and drivers |
| `old/Debug/` | original build outputs |

Important old application files:

| File | Meaning |
|---|---|
| `old/code/FUZZY_PID_UCAS.c/.h` | legacy fuzzy steering control source |
| `old/code/PID.c/.h` | legacy PID control source |
| `old/code/Motor.c/.h` | legacy motor mix/output logic |
| `old/code/Servo.c/.h` | servo-specific logic, deferred in phase 1 |
| `old/code/ZiTaiJieSuan.c/.h` | attitude integration logic |
| `old/code/camera.c/.h` | camera thresholding and feature extraction |
| `old/code/All_init.c/.h` | initialization |
| `old/code/All_init_core1.c/.h` | second-core initialization, deferred in phase 1 |
| `old/code/key.c/.h` | key/input handling |

Important old runtime files:

| File | Meaning |
|---|---|
| `old/user/cpu0_main.c` | main core foreground loop in old system |
| `old/user/isr.c` | periodic ISR path that maps to `ControlLoop::Tick()` |
| `old/user/cpu1_main.c` | second-core path, deferred in new phase-1 runtime |

Practical relationship to `new/`:

- `new/docs/superseded/race-finish-series-source/mapping.md` and the current migrated `legacy/` layer are derived primarily from this subtree.

## 5.2 `LS2K0300_Library/`

Role: vendor LS2K platform repository. `new/` links against a subset of it.

Most relevant subtree:

| Path | Role | Used by `new/` |
|---|---|---|
| `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_common` | vendor common helpers | yes |
| `.../libraries/zf_device` | higher-level device wrappers like UVC and IMU | yes |
| `.../libraries/zf_driver` | low-level drivers like ADC, PIT, encoder, PWM/GPIO | yes |
| `.../libraries/zf_components` | extra components like `ncnn`, `tflm`, assistant tools | not used by current `new/` build |
| `.../project/` | stock vendor project skeleton | structural reference only |

Relevant vendor files compiled into `new`:

- `zf_common/zf_common_fifo.cpp`
- `zf_common/zf_common_font.cpp`
- `zf_common/zf_common_function.cpp`
- `zf_device/zf_device_imu.cpp`
- `zf_device/zf_device_uvc.cpp`
- `zf_driver/zf_driver_adc.cpp`
- `zf_driver/zf_driver_delay.cpp`
- `zf_driver/zf_driver_encoder.cpp`
- `zf_driver/zf_driver_file_buffer.cpp`
- `zf_driver/zf_driver_file_string.cpp`
- `zf_driver/zf_driver_gpio.cpp`
- `zf_driver/zf_driver_pit.cpp`
- `zf_driver/zf_driver_pit_fd.cpp`
- `zf_driver/zf_driver_pwm.cpp`

Practical relationship to `new/`:

- `new/user/CMakeLists.txt` is the authoritative place where `new/` selects which vendor files to link.
- `new/code/platform/*.cpp` is the authoritative place where migration-owned code touches these vendor APIs.

## 5.3 `openspec/changes/port-old-to-new-ls2k0300-library/`

Role: canonical change record for this migration.

Structure:

| Path | Role |
|---|---|
| `proposal.md` | change intent |
| `design.md` | architecture and design rationale |
| `tasks.md` | implementation checklist |
| `specs/ls2k0300-new-port-workspace/spec.md` | workspace behavior spec |
| `specs/tc264-to-ls2k0300-adapter-layer/spec.md` | adapter-layer spec |
| `verification/` | artifact and implementation verification evidence |
| `.openspec.yaml` | change metadata |

Relationship to `new/docs/`:

- `new/docs/superseded/race-finish-series-source/design.md`, `proposal.md`, `tasks.md`, `workspace_spec.md`, `adapter_spec.md`, and artifact verification copies are archived local mirrors or convenience copies of material in this change record.

## 5.4 `docx/`

Role: environment and usage documentation.

| File | Role |
|---|---|
| `docx/README.md` | general repository guidance |
| `docx/guide.md` | usage/reference guide |
| `docx/ENV_SETUP_WSL_BOARD.md` | WSL and board environment setup |

## 6. Main Architectural Conclusions

### 6.1 What Is Cleanly Decoupled

| Area | Status |
|---|---|
| perception/control logic from hardware APIs | cleanly separated by `port/` and `platform/` |
| runtime orchestration from algorithm details | cleanly separated by `runtime/` vs `legacy/` |
| configuration loading from control logic | cleanly separated in `IParamStore` |
| build/deploy/smoke automation from runtime code | cleanly separated in shell scripts |

### 6.2 Current Boundary Exceptions Or Notable Couplings

| Area | Detail |
|---|---|
| parameter surface | several fields are loaded from JSON but not yet consumed by current phase-1 control path |
| servo path | `ActuatorCommand` still carries `servo_pwm`, but no active servo backend exists in phase 1 |
| retained legacy flags | `circle_find`, `zebra_flag`, `cross_flag`, and integrated yaw are preserved but mostly not yet used downstream |

### 6.3 Fast Mental Model

If only one short model is needed, it is this:

1. `main.cpp` loads config, creates adapters, and owns lifetime.
2. `PerceptionFrontend` runs on the foreground thread and publishes `PerceptionResult`.
3. `ControlLoop` runs on the timer callback, reads sensors plus latest perception, computes command, and calls `IMotorAdapter::Apply`.
4. `port/` defines the contract, `platform/` talks to LS2K hardware, `legacy/` holds migrated algorithms, and `runtime/` sequences them.
