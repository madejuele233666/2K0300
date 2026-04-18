## Why

Board-side smoke testing on April 4, 2026 showed that the migrated runtime still depends on hardware assumptions that are not stable on the real LS2K0300 board. The current direct-match path can fail closed for the wrong reason, fabricate readiness for some devices, and report smoke-script success even when the board process exits with a startup failure.

## What Changes

- Tighten direct-match hardware detection so board-dependent adapters validate the real board resource they intend to use before claiming readiness.
- Replace fixed-path IMU assumptions with runtime-resolved board inventory or explicit project-owned override paths, while keeping raw vendor APIs hidden inside platform-owned bridges and preserving fail-safe startup when no supported runtime-facing IMU resource can be resolved.
- Propagate encoder, motor, and ADC read/write failures through bridge and adapter contracts instead of fabricating valid samples or successful initialization.
- Change board smoke execution so `new/user/run_remote_smoke.sh` still retrieves logs on failure but returns a failing verdict when the remote runtime exits unsuccessfully.
- Expand diagnostics and verification evidence so logs name the resolved hardware resource, startup failure reason, and smoke outcome clearly enough for board regression review.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `tc264-to-true-ls2k0300-adapter-layer`: direct-match adapters now require truthful hardware discovery, error propagation, and fail-safe readiness semantics for IMU, encoder, motor, and ADC paths.
- `true-ls2k0300-port-workspace`: board-side smoke execution and verification evidence now require truthful runtime verdicts and explicit hardware-resolution evidence instead of script-level success alone.

## Risk Tier

- `STANDARD`: this is a cross-cutting runtime and verification fix that touches startup gating, hardware-device discovery, sensor/actuator readiness, and board smoke evidence, but it does not introduce a new product surface or data model.

## Impact

- Affected code: `new/user/run_remote_smoke.sh`, `new/code/platform/*.cpp`, `new/code/platform/true_ls2k0300/*`, and selected true vendor bridge-facing files under `true_LS2K0300_Library/...` if the project-owned bridge must wrap vendor globals more safely.
- Affected systems: board startup on the real LS2K0300 target, runtime smoke verdicts, and diagnostics collected under `openspec/changes/fix-board-runtime-hardware-detection/verification/`.
- Dependencies: existing OpenSpec specs `tc264-to-true-ls2k0300-adapter-layer` and `true-ls2k0300-port-workspace`, plus the shared verifier sequence `openspec/schemas/ai-enforced-workflow/verification-sequence.md`.
- Expected skills: `openspec-propose`, `openspec-align`, `openspec-artifact-verify`, and `openspec-verify-change`.
