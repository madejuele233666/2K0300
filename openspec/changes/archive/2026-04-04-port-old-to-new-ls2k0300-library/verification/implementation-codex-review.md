# Codex Implementation Review (Board Verification Refresh)

- Change: `port-old-to-new-ls2k0300-library`
- Scope: latest repaired runtime plus on-board verification for build/upload, smoke, parameter-failure paths, and hardware-profile paths

## Files reviewed

- `openspec/changes/port-old-to-new-ls2k0300-library/tasks.md`
- `openspec/changes/port-old-to-new-ls2k0300-library/design.md`
- `openspec/changes/port-old-to-new-ls2k0300-library/specs/ls2k0300-new-port-workspace/spec.md`
- `openspec/changes/port-old-to-new-ls2k0300-library/specs/tc264-to-ls2k0300-adapter-layer/spec.md`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/parameter-failure.log`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/hardware-profile.log`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/runtime-smoke.log`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-raw.json`
- `openspec/changes/port-old-to-new-ls2k0300-library/verification/implementation-gemini-report.json`
- `new/user/main.cpp`
- `new/user/CMakeLists.txt`
- `new/user/build.sh`
- `new/user/run_remote_smoke.sh`
- `new/code/port/control_types.hpp`
- `new/code/port/platform_adapter.hpp`
- `new/code/platform/bootstrap.hpp`
- `new/code/platform/bootstrap.cpp`
- `new/code/platform/power_adapter.cpp`
- `new/code/runtime/startup.cpp`
- `new/code/runtime/perception_frontend.hpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/docs/debugging.md`
- `new/docs/hardware-matrix.md`
- `PROJECT_STRUCTURE_REFERENCE.md`
- `PROJECT_STRUCTURE_DETAILED_REFERENCE.md`

## Findings

1. `CRITICAL` `Completeness` `implementation`
   Strict direct-match board verification still does not produce a runnable phase-1 vehicle path because IMU direct-match initialization fails on target hardware, so the primary hardware contract is not yet satisfied end-to-end.
   Evidence:
   - fresh board upload on `2026-04-04`: `cd new/user && ./build.sh` passed and uploaded `/home/root/new`.
   - fresh strict smoke on `2026-04-04`: `verification/runtime-smoke.log` shows `camera.init` and power checks succeed, then `[FAIL_SAFE][imu.init] imu unavailable`, `[FAIL_SAFE][startup.imu.init]`, and `[FAIL_SAFE][main.startup]`.
   - fresh direct-match profile verification on `2026-04-04`: `verification/hardware-profile.log` records the same direct-match IMU failure on board.
   Recommendation:
   - treat the board-side IMU path as the remaining blocking item: either repair the direct-match IMU backend on this board, or formally redefine the target hardware profile / phase-1 contract if this board is not expected to provide `imu660ra-default`.

## Resolved in this pass

- Board transport is no longer the blocker:
  - build upload and SSH execution both succeeded against `root@10.236.192.226`.
- Veto-state progression repair remains present:
  - `new/code/runtime/control_loop.cpp` freezes controller internals while `veto=true`.
- Startup fail-closed and degraded diagnostics split remains present:
  - `new/code/runtime/startup.cpp` enforces direct-match in strict mode and allows explicit degraded startup only under `LS2K_ALLOW_DEGRADED_STARTUP=1`.
- Low-voltage adapter refactor remains present:
  - `new/code/port/platform_adapter.hpp`, `new/code/platform/power_adapter.cpp`, `new/code/runtime/startup.cpp`, and `new/code/runtime/perception_frontend.cpp` consistently use `IPowerMonitorAdapter`.
- Parameter failure handling is now evidenced on board:
  - malformed and missing parameter files both fall back to defaults while still applying `P_Mode` / `exp_light` before adapter bring-up, as captured in `verification/parameter-failure.log`.
- Adaptation-hook diagnostics path is now evidenced on board:
  - with `imu=adaptation-hook` plus degraded startup, startup completes, `imu.hook.read` is emitted, control starts, and runtime stays fail-safe vetoed as intended by the current hook contract, as captured in `verification/hardware-profile.log`.

## Verification notes

- Fresh compile+upload evidence:
  - `cd new/user && ./build.sh` -> pass, upload complete
- Fresh strict smoke evidence:
  - `cd new/user && ./run_remote_smoke.sh` -> remote execution succeeded and wrote `verification/runtime-smoke.log`
- Fresh parameter evidence:
  - board runs with valid, malformed, and missing parameter files written to `verification/parameter-failure.log`
- Fresh hardware-profile evidence:
  - board runs with strict direct-match profile and degraded adaptation-hook IMU profile written to `verification/hardware-profile.log`

## Overall assessment

- Scorecard:
  - Completeness: `warning`
  - Correctness: `blocked`
  - Coherence: `pass`
- Final assessment: `blocked by direct-match IMU failure on target board`
