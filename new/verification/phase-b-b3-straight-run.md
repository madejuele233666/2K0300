# B-3 Straight-Run Baseline

## Command

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

## Expected Markers

- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.drive`
- `control.apply.command`
- `imu.sample.summary`
- `encoder.delta.summary`
- `motion.stop.complete`

## Harness Context

The wrapper header must show the exact `LS2K_AUTO_*` and `LS2K_MAX_FRAMES` values. Pair the log with a video clip or operator note that fixes the path shape, battery pack, and current parameter file.

## Proves

- The accepted runtime entrypoint can produce a lifecycle-complete straight-run trace.
- IMU, encoder, and drive-command markers are time-aligned with motion phases.
- Controlled stop returns the runtime to `DISARMED` after a bounded run.

## Does Not Prove

- Cornering behavior.
- Fault-latch and re-arm correctness.
- Long-duration thermal or battery endurance.
