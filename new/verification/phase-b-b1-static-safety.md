# B-1 Static Safety And Lifecycle Boundary

## Command

Diagnostics-only wrapper smoke:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b1-static-safety.log \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=0 \
  SMOKE_MAX_FRAMES=20 \
  ./run_remote_smoke.sh)
```

Baseline product-owned lifecycle path:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  LS2K_ALLOW_DEGRADED_STARTUP=1 \
  LS2K_PROFILE_PATH=../config/hardware_profile.json \
  LS2K_PARAMS_PATH=../config/default_params.json \
  ../out/new > ../verification/phase-b-b1-manual-lifecycle.log 2>&1 & \
  RUNTIME_PID=$! && \
  sleep 1 && kill -USR2 "${RUNTIME_PID}" && \
  sleep 1 && kill -TERM "${RUNTIME_PID}" && \
  wait "${RUNTIME_PID}")
```

## Expected Markers

- `main.harness_context`
- `startup.complete`
- `control.start`
- `motion.start.requested`
- `motion.phase.transition`
- `motion.stop.requested`
- `motion.stop.complete`
- `main.exit.ready`
- `shutdown.complete`

## Harness Context

The wrapper log must contain a dedicated `harness_context_begin ... harness_context_end` block showing the injected `LS2K_AUTO_*`, `LS2K_MAX_FRAMES`, and fault-injection env values. The direct product-owned command above intentionally does not use that harness block; its evidence is the signal-driven lifecycle markers in the runtime log.

## Proves

- Product runtime distinguishes start intent, controlled stop, and process exit.
- Diagnostics-only smoke no longer implies real drive permission.
- The accepted runtime entrypoint exposes a non-test manual lifecycle path.

## Does Not Prove

- Real wheel direction or low-speed ground behavior.
- Sensor-quality or closed-loop tuning quality.
- Fault-latch recovery under injected failures.
