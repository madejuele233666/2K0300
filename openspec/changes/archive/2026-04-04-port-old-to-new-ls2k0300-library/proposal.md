## Why

`old/` contains working control and perception logic, but it is tied to the TC264 bare-metal library, interrupt model, and C driver APIs. The target `LS2K0300_Library` environment is a Linux/LoongArch C++ application stack with different device abstractions, so direct copy-paste would mix platform code and algorithm code in a way that is hard to build, validate, and extend.

## What Changes

- Create a new migration target under `new/`, seeded from a copy of the existing `project/` build skeleton and kept vendor-aligned as `new/user + new/code + new/model + new/out`, so the ported application remains directly buildable by the current LS2K0300 workflow.
- Define a porting architecture that separates reusable logic from TC264-specific drivers, initialization, flash/menu persistence, and interrupt/timer control.
- Introduce LS2K0300 adapter interfaces for camera input, IMU/attitude data, encoder sampling, PWM/GPIO motor control, display/logging, and parameter persistence.
- Treat source and target hardware as potentially identical or different, and require the adapter layer to support direct mapping plus explicit adaptation markers and extension interfaces instead of assuming one fixed hardware profile.
- Keep migrated legacy logic decoupled from LS2K0300 umbrella and driver headers by routing platform access through adapter-owned interfaces under `new/code/`, not through `zf_common_headfile.hpp` or direct `zf_driver_*` / `zf_device_*` includes.
- Reorganize the phase-1 reusable logic subset from `old/code/camera.c`, `old/code/PID.c`, `old/code/Motor.c`, `old/code/ZiTaiJieSuan.c`, and the retained steering-fuzzy dependency in `old/code/FUZZY_PID_UCAS.c` into modules that can compile against the LS2K0300 C++ toolchain with minimal algorithm changes, while extracting the supporting startup constants and persisted parameter surface needed from `old/code/All_init.c` and `old/code/key.c`.
- Explicitly inventory and migrate the phase-1 parameter surface now persisted by `old/code/key.c` and consumed during bring-up or control, including `Speed_base`, `JWJC`, `circle_k`, `circle_b`, `road_k`, `road_b`, `see_max`, `PID_TURN_CAMERA.D`, `PID_TURN_GYRO_CAMERA.D`, `Straight_permit`, `island_point`, `island_delay`, `circle_k_err`, `P_Mode`, and `exp_light`.
- Explicitly defer `old/code/Servo.c`, `old/code/All_init_core1.c`, and the original dual-core / interactive-menu behaviors, but do not defer helper logic that the retained phase-1 control path still calls.
- Repair source undefined behavior and hidden initialization assumptions during migration by moving carry-over state into explicit runtime or adapter-owned initialization instead of preserving read-before-init behavior from `old/`.
- Define the validation workflow needed to prove the migrated code builds and uploads through the vendor entry, launches on the board, retrieves runtime logs, acquires sensor data, runs the main control loop, and can be debugged in the target environment.

## Capabilities

### New Capabilities
- `ls2k0300-new-port-workspace`: Define the required `new/` project-copy layout, build integration, and module boundaries for the migrated application.
- `tc264-to-ls2k0300-adapter-layer`: Define the runtime adapter contracts that replace TC264-specific drivers and let reused logic run in the LS2K0300 library environment.

### Modified Capabilities

None.

## Risk Tier

- `STRICT`: This change ports a hardware-facing, timing-sensitive control application across runtime models, languages, and device APIs. It affects sensor acquisition, control-loop execution, motor output, parameter storage, and validation on a new platform, so design and verification must be explicit.

## Impact

- Affected source areas: `old/user/`, `old/code/`, `old/libraries/` (reference only), `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/`, and `new/`.
- Affected interfaces: camera acquisition, IMU data access, encoder reading, PWM/GPIO motor control, timer scheduling, display/debug output, persistent parameter loading/saving, and hardware-profile compatibility selection.
- Affected build/runtime systems: LS2K0300 cross-compilation via CMake, Linux device-node based drivers, and the application main loop / periodic callback model.
- Participating skills: `openspec-propose` for proposal generation, `openspec-architect` reasoning embodied in the design artifact, and later `openspec-apply-change` plus `openspec-verify-change` during implementation and validation.
