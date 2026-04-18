## Why

`port-old-to-new-ls2k0300-library` completed against the wrong vendor baseline: `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library` instead of the actual target `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`. The two trees are not path aliases; they differ in project layout, build entrypoints, header naming, and device-driver API shape, so the existing implementation and verification evidence cannot be treated as valid for the real target.

## What Changes

- Define `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library` as the only accepted phase-1 vendor baseline for the migrated `new/` workspace.
- Repair the `new/` build boundary so it compiles against the real target tree instead of the superseded `LS2K0300_Library/LS2K300_Library/...` path, and remove assumptions about the old `build_all.sh` discovery model where that model does not exist in the real target.
- Re-spec the platform adapter layer around the true library's exported `.h`-named, wrapper-light vendor surface, which mixes free functions, globals, and some direct C++ types/classes for PIT, UVC, IMU, encoder, PWM/GPIO, and ADC, while preserving project-owned contracts at the `new/code/port/` boundary.
- Preserve the existing migration goal of keeping legacy control and perception logic decoupled from vendor headers, but explicitly forbid vendor global variables, vendor free functions, and direct vendor C++/OpenCV types from leaking past `new/code/platform/`.
- Require a fresh semantic audit for places where the wrong baseline offered stronger abstractions than the real baseline, especially timer stop semantics, encoder delta-clear semantics, camera exposure control, IMU access shape, and file-driver ownership.
- Define how the new change supersedes `port-old-to-new-ls2k0300-library`: the earlier change remains historical evidence of the wrong target selection and SHALL NOT be synced into main specs as the accepted contract for the real port.
- **BREAKING**: any implementation or verification artifact that assumes `.hpp` driver headers, the superseded wrapper-heavy `zf_driver_*` / `zf_device_*` object model, or `LS2K0300_Library/LS2K300_Library/...` build roots becomes invalid for the real target and must be replaced or explicitly revalidated.

## Capabilities

### New Capabilities
- `true-ls2k0300-port-workspace`: Define the required `new/` workspace layout, vendor-root selection, build integration, supersession rules, and validation workflow for the true LS2K0300 library target.
- `tc264-to-true-ls2k0300-adapter-layer`: Define the runtime adapter contracts and implementation constraints needed to map TC264-era logic onto the real LS2K0300 driver/device surface without leaking vendor APIs or globals into legacy modules.

### Modified Capabilities

None.

## Risk Tier

- `STRICT`: This change corrects the accepted target baseline for a hardware-facing port after implementation already finished against a different library. It affects build roots, public adapter assumptions, timer semantics, sensor/actuator IO behavior, runtime verification meaning, and the validity of prior evidence, so design and verification must be explicit and dual-gated.

## Impact

- Affected source areas: `new/`, `old/` (reference only), `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/`, and `openspec/changes/port-old-to-new-ls2k0300-library/`.
- Affected interfaces: `zf_common_headfile.h`, `zf_driver_pit.h`, `zf_driver_encoder.h`, `zf_driver_pwm.h`, `zf_driver_gpio.h`, `zf_driver_adc.h`, `zf_device_uvc.h`, `zf_device_imu_core.h`, `zf_device_imu660ra.h`, `zf_device_imu660rb.h`, and `zf_device_imu963ra.h`.
- Affected systems: CMake build wiring, vendor project bootstrap, runtime timer ownership, camera capture and exposure handling, IMU sampling, encoder sampling semantics, actuator output, low-voltage checks, and verification artifact interpretation.
- Participating skills: `openspec-propose`, `openspec-architect`, `openspec-apply-change`, `openspec-verify-change`, and `openspec-repair-change` if post-verification contract gaps remain.
