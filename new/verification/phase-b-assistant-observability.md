# Phase B Assistant And Wheel Observability

## Purpose

This note defines the concrete runbooks for collecting wheel-level `control.snapshot` evidence and the optional Seekfree Assistant sidecar evidence after the dual-wheel PID and assistant refactor.

Current repo status on `2026-04-19`:

- `control.snapshot` export is implemented in source.
- No accepted saved Phase B log under `new/verification/` currently contains `control.snapshot`.
- Assistant waveform and image evidence are still support-only and still need explicit collection runs.

## Primary Board-Owned Evidence

The primary acceptance evidence remains the board-owned `control.snapshot` marker stream. When a run is accepted for wheel-level observability, the saved `.log` must contain:

- `effective_speed_target`
- `left_target`
- `right_target`
- `left_measured`
- `right_measured`
- `turn_pwm`
- `left_pwm`
- `right_pwm`

This evidence must remain available even when the assistant sidecar is disabled.

## Assistant Waveform Channel Mapping

The current waveform payload is fixed by `new/code/runtime/assistant_service.cpp`:

- channel `0`: `effective_speed_target`
- channel `1`: `left_speed_target`
- channel `2`: `right_speed_target`
- channel `3`: `left_measured_speed`
- channel `4`: `right_measured_speed`
- channel `5`: `turn_pwm_command`
- channel `6`: `left_pwm_command`
- channel `7`: `right_pwm_command`

## Shared Artifacts

For every run in this note, save:

- the raw `.log`
- the exact parameter file path
- the operator note saying what the run proves and does not prove

Recommended artifact names:

- `new/verification/phase-b-assistant-disabled.log`
- `new/verification/phase-b-assistant-waveform.log`
- `new/verification/phase-b-assistant-image.log`
- `new/verification/phase-b-wheel-control.log`

If assistant UI evidence is collected, also save:

- `new/verification/phase-b-assistant-waveform.png`
- `new/verification/phase-b-assistant-image.png`

## Run 1: Assistant-Disabled Bench Snapshot

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-assistant-disabled.log \
  LS2K_PARAMS_PATH=../config/default_params.json \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=80 \
  ./run_remote_smoke.sh)
```

Expected runtime markers:

- `main.harness_context`
- `main.frame.processed`
- `startup.complete`
- `control.start`
- `motion.start.requested`
- `control.snapshot`
- `motion.stop.requested`
- `main.exit.ready`

Expected artifacts:

- `new/verification/phase-b-assistant-disabled.log`

Proves:

- `control.snapshot` can be collected without assistant connectivity.
- The accepted runtime entrypoint keeps the board-owned evidence surface available in a bench-safe run.

Does Not Prove:

- Real motor direction or ground-contact behavior.
- Assistant TCP connectivity.
- Image publication.

## Run 2: Assistant-Enabled Waveform

Prerequisite:

- Verify `new/verification/params/assistant-tcp-enabled.json` points at the current PC-side Seekfree Assistant TCP listener before the run.

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-assistant-waveform.log \
  LS2K_PARAMS_PATH=../verification/params/assistant-tcp-enabled.json \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=80 \
  ./run_remote_smoke.sh)
```

Expected runtime markers:

- `assistant.configured`
- `assistant.connecting`
- `assistant.connected`
- `control.snapshot`

Expected external evidence:

- waveform channels `0` through `7` visible in Seekfree Assistant with the mapping listed above
- screenshot saved as `new/verification/phase-b-assistant-waveform.png`

Proves:

- TCP-only assistant transport works without changing the board-owned control surface.
- Seekfree Assistant waveform values can be mapped back to `control.snapshot`.

Does Not Prove:

- Image publication quality.
- Real-wheel motion quality.
- Writable tuning support.

## Run 3: Assistant-Enabled Image

Prerequisite:

- Reuse `new/verification/params/assistant-tcp-enabled.json` after confirming host and port.

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-assistant-image.log \
  LS2K_PARAMS_PATH=../verification/params/assistant-tcp-enabled.json \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=80 \
  ./run_remote_smoke.sh)
```

Expected runtime markers:

- `assistant.configured`
- `assistant.connecting`
- `assistant.connected`

Expected external evidence:

- grayscale image visible in Seekfree Assistant
- screenshot saved as `new/verification/phase-b-assistant-image.png`

Proves:

- The current release publishes TCP-only read-only image data through the assistant sidecar.
- Image publication does not require opening the write/tuning surface.

Does Not Prove:

- Ground-contact behavior.
- Any closed-loop benefit from the image surface by itself.
- Non-TCP transport support.

## Run 4: Low-Speed Wheel-Control Evidence

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-wheel-control.log \
  LS2K_PARAMS_PATH=../config/default_params.json \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Expected runtime markers:

- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.drive`
- `control.apply.command`
- `control.snapshot`
- `motion.stop.complete`

Expected artifacts:

- `new/verification/phase-b-wheel-control.log`
- paired operator note or video that fixes the surface and battery condition

Proves:

- The project-owned wheel control path can be explained from target -> measured speed -> PWM on a bounded low-speed run.
- Wheel-level evidence is available without requiring assistant connectivity.

Does Not Prove:

- Straight-run stability on a longer path.
- Turn quality.
- Fault recovery.

## Non-goals

- No writable tuning overlay in this release
- No UDP or serial assistant transport in this release
