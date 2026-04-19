# B-4 Turn-Run And Recovery

## Command

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

## Expected Markers

- `main.frame.processed`
- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.command`
- `control.snapshot`
- `encoder.delta.summary`
- `motion.stop.requested`
- `motion.stop.complete`

The review focus is that turn corrections stay inside the lifecycle-owned start/run/stop envelope rather than appearing as one unstructured drive burst.

## Harness Context

Keep the wrapper header with the exact automation env block. When using `SMOKE_MAX_FRAMES`, confirm `main.frame.processed` exists before the eventual `motion.stop.requested` / `motion.stop.complete` pair so the stop is demonstrably wrapper-owned. Add a short track note naming the bend direction, surface, and any temporary speed reduction used for the test.

## Proves

- Turn corrections happen while the runtime is in explicit `SPINUP` or `RUNNING`, not in an implicit post-start state.
- Stop behavior remains controlled after a turning segment.
- Turn evidence can be explained from wheel-level `control.snapshot` data instead of inferring everything from one mixed PWM quantity.

## Does Not Prove

- Full race-line quality.
- Higher-speed scenario switching.
- Fault recovery after a turning disturbance.
