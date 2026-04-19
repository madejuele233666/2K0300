# Phase B Board Test Checklist

This file is the persistent execution checklist for the remaining Phase B board and real-vehicle evidence work. Use it as the single entrypoint when running `B-1` through `B-5`.

Current status as of `2026-04-19`:

- Runtime lifecycle contract: implemented
- Controlled stop-before-exit contract: implemented
- Source-first review: passed
- Remaining closure work: board / real-vehicle evidence bundle for `B-1` to `B-5`

## 1. Global Rules

Before every Phase B run:

- [ ] Rebuild on the current tree: `cd new/user && ./build.sh`
- [ ] Record board identity, battery pack, surface, and operator
- [ ] Record the active parameter file path and any temporary overrides
- [ ] Keep the wrapper-generated `harness_context_begin ... harness_context_end` block in the saved log
- [ ] Confirm bounded runs stop through wrapper-observed `main.frame.processed` plus `SIGINT`, not by treating `main.exit.forced` as the normal success path
- [ ] For every run, record what the artifact proves and what it does not prove

Common artifact expectations:

- [ ] Save the raw `.log`
- [ ] Update the corresponding `new/verification/phase-b-b*.md` note with result and conclusion
- [ ] Record operator steps: power on, start, test, stop, reset / power off
- [ ] Record the parameter snapshot used for that run

## 2. Exit Targets

Phase B is not complete until the board evidence can jointly show:

- [ ] `START_REQUESTED -> SPINUP -> RUNNING -> STOPPING -> DISARMED`
- [ ] low-speed straight-run is explainable
- [ ] light turn correction works without sustained oscillation
- [ ] fail-safe latch and re-arm are observable under injected faults
- [ ] one repeatable low-speed parameter set is fixed and reusable

## 3. Execution Order

Recommended order:

1. `B-1` static safety and lifecycle boundary
2. `B-2` half-load spinup and stop
3. `B-3` straight-run baseline
4. `B-4` turn-run and recovery
5. `B-5` fault injection and recovery

Do not start `B-3` / `B-4` until `B-1` and `B-2` are acceptable.

## 4. Checklist

### B-1 Static Safety And Lifecycle Boundary

Reference note: `new/verification/phase-b-b1-static-safety.md`

Status:

- Completed on `2026-04-19`
- Accepted evidence: `new/verification/phase-b-b1-static-safety-rerun.log` and `new/verification/phase-b-b1-manual-lifecycle.log`
- Closure scope: static safety boundary and operator signal path only; low-speed motion-quality evidence remains in `B-2+`

Commands:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b1-static-safety.log \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=0 \
  SMOKE_MAX_FRAMES=20 \
  ./run_remote_smoke.sh)

(cd new/user && \
  mkdir -p ../verification && \
  LS2K_ALLOW_DEGRADED_STARTUP=1 \
  LS2K_PROFILE_PATH=../config/hardware_profile.json \
  LS2K_PARAMS_PATH=../config/default_params.json \
  ../out/new > ../verification/phase-b-b1-manual-lifecycle.log 2>&1 & \
  RUNTIME_PID=$! && \
  sleep 1 && kill -USR2 "${RUNTIME_PID}" && \
  sleep 1 && kill -INT "${RUNTIME_PID}" && \
  wait "${RUNTIME_PID}")
```

Artifacts to persist:

- [ ] `new/verification/phase-b-b1-static-safety.md`
- [ ] `new/verification/phase-b-b1-static-safety.log`
- [ ] `new/verification/phase-b-b1-manual-lifecycle.log`

Markers to confirm:

- [ ] `main.harness_context`
- [ ] `main.frame.processed`
- [ ] `startup.complete`
- [ ] `control.start`
- [ ] `motion.start.requested`
- [ ] `motion.phase.transition`
- [ ] `motion.stop.requested`
- [ ] `motion.stop.complete`
- [ ] `main.exit.ready`
- [ ] `shutdown.complete`

Manual observations to record:

- [ ] wheel direction / encoder direction / IMU installation direction check
- [ ] wiring / power / motor / camera fixation check
- [ ] operator stop and emergency interruption path check

Pass intent:

- [ ] repeated power on / stop leaves no dirty state
- [ ] operator can always take over and power down safely

### B-2 Half-Load Spinup And Stop

Reference note: `new/verification/phase-b-b2-half-load.md`

Status:

- Completed on `2026-04-19`
- Accepted evidence: `new/verification/phase-b-b2-half-load-short-pwm3000.log`
- Closure scope: short half-load board pass with `pwm_limit=3000`

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b2-half-load.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Artifacts to persist:

- [ ] `new/verification/phase-b-b2-half-load.md`
- [ ] `new/verification/phase-b-b2-half-load.log`
- [ ] `new/verification/phase-b-half-load.mp4`

Markers to confirm:

- [ ] `main.harness_context`
- [ ] `main.frame.processed`
- [ ] `motion.start.requested`
- [ ] `motion.phase.transition`
- [ ] `motion.spinup.enter`
- [ ] `motion.spinup.complete`
- [ ] `control.apply.drive`
- [ ] `control.apply.command`
- [ ] `motion.stop.requested`
- [ ] `motion.stop.complete`

Manual observations to record:

- [ ] left / right wheel response direction
- [ ] turn correction direction under differential output
- [ ] startup smoothness, overshoot, stop smoothness
- [ ] any extra PWM safety limit or battery restriction used during the run

Pass intent:

- [ ] wheel direction and relative output magnitude are correct
- [ ] differential turn effect is not reversed
- [ ] startup and stop are shaped, not abrupt

### B-3 Straight-Run Baseline

Reference note: `new/verification/phase-b-b3-straight-run.md`

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b3-straight-run.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=180 \
  ./run_remote_smoke.sh)
```

Artifacts to persist:

- [ ] `new/verification/phase-b-b3-straight-run.md`
- [ ] `new/verification/phase-b-b3-straight-run.log`
- [ ] `new/verification/phase-b-straight-run.mp4`
- [ ] `new/verification/phase-b-straight-tuning.md`

Markers to confirm:

- [ ] `main.frame.processed`
- [ ] `motion.spinup.enter`
- [ ] `motion.spinup.complete`
- [ ] `control.apply.drive`
- [ ] `control.apply.command`
- [ ] `imu.sample.summary`
- [ ] `encoder.delta.summary`
- [ ] `motion.stop.complete`

Manual observations to record:

- [ ] path shape and surface
- [ ] battery pack / supply condition
- [ ] whether the vehicle immediately snakes or loses control
- [ ] startup behavior versus the expected no-overshoot goal
- [ ] parameter adjustments made to IMU / encoder / speed target / PID

Pass intent:

- [ ] low-speed straight motion is explainable
- [ ] controlled stop returns to a safe state
- [ ] one usable straight-run parameter set is captured

### B-4 Turn-Run And Recovery

Reference note: `new/verification/phase-b-b4-turn-run.md`

Command:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b4-turn-run.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=220 \
  ./run_remote_smoke.sh)
```

Artifacts to persist:

- [ ] `new/verification/phase-b-b4-turn-run.md`
- [ ] `new/verification/phase-b-b4-turn-run.log`
- [ ] `new/verification/phase-b-turn-run.mp4`
- [ ] `new/verification/phase-b-turn-analysis.md`

Markers to confirm:

- [ ] `main.frame.processed`
- [ ] `motion.spinup.enter`
- [ ] `motion.spinup.complete`
- [ ] `control.apply.command`
- [ ] `encoder.delta.summary`
- [ ] `motion.stop.requested`
- [ ] `motion.stop.complete`

Manual observations to record:

- [ ] bend direction and surface
- [ ] temporary speed reduction if used
- [ ] whether offset-to-recovery is continuous
- [ ] whether sustained oscillation appears

Pass intent:

- [ ] light turn correction is explainable
- [ ] no sustained large oscillation
- [ ] stop remains controlled after turning

### B-5 Fault Injection And Recovery

Reference note: `new/verification/phase-b-b5-fault-injection.md`

Commands:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-drop-frame.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N=5 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)

(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-imu-invalid.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N=10 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)

(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-encoder-invalid.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N=7 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)

(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-low-voltage.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FORCE_LOW_VOLTAGE=true \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Artifacts to persist:

- [ ] `new/verification/phase-b-b5-fault-injection.md`
- [ ] `new/verification/phase-b-b5-drop-frame.log`
- [ ] `new/verification/phase-b-b5-imu-invalid.log`
- [ ] `new/verification/phase-b-b5-encoder-invalid.log`
- [ ] `new/verification/phase-b-b5-low-voltage.log`

Markers to confirm:

- [ ] `main.harness_context`
- [ ] `main.frame.processed`
- [ ] `perception.inject.drop_frame`
- [ ] `imu.inject.invalid`
- [ ] `encoder.inject.invalid`
- [ ] `power.low_voltage.injected`
- [ ] `control.veto.*`
- [ ] `motion.failsafe.latched`
- [ ] `motion.failsafe.reset_ready`
- [ ] `motion.failsafe.reset_requested`
- [ ] `motion.failsafe.rearmed`

Manual observations to record:

- [ ] whether output clearly returns to fail-safe rather than silent ambiguous zeroing
- [ ] whether re-arm remains explicit and leaves no dirty state
- [ ] whether any one fault mode behaves materially differently from the others

Pass intent:

- [ ] injected faults drive `FAIL_SAFE_LATCHED`
- [ ] reset / re-arm is observable and explicit
- [ ] the system can restart after the injected fault path clears

## 5. Final Bundle To Keep

Do not mark Phase B board evidence complete until the repo contains:

- [ ] all `B-1` to `B-5` logs listed above
- [ ] all five `new/verification/phase-b-b*.md` notes updated with real results
- [ ] `phase-b-half-load.mp4`
- [ ] `phase-b-straight-run.mp4`
- [ ] `phase-b-turn-run.mp4`
- [ ] `phase-b-straight-tuning.md`
- [ ] `phase-b-turn-analysis.md`
- [ ] one explicit parameter snapshot or parameter manifest reference used as the accepted low-speed baseline
